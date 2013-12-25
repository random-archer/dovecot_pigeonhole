/* Copyright (c) 2002-2013 Pigeonhole authors, see the included COPYING file
 */

#include "lib.h"
#include "lib-signals.h"
#include "env-util.h"
#include "execv-const.h"
#include "array.h"
#include "net.h"
#include "istream.h"
#include "ostream.h"

#include "program-client-private.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>


struct program_client_local {
	struct program_client client;

	pid_t pid;
};

static void exec_child
(const char *bin_path, const char *const *args, const char *const *envs,
	int in_fd, int out_fd)
{
	ARRAY_TYPE(const_string) exec_args;

	if ( in_fd < 0 ) {
		in_fd = open("/dev/null", O_RDONLY);

		if ( in_fd == -1 )
			i_fatal("open(/dev/null) failed: %m");
	}

	if ( out_fd < 0 ) {
		out_fd = open("/dev/null", O_WRONLY);

		if ( out_fd == -1 )
			i_fatal("open(/dev/null) failed: %m");
	}

	if ( dup2(in_fd, STDIN_FILENO) < 0 )
		i_fatal("dup2(stdin) failed: %m");
	if ( dup2(out_fd, STDOUT_FILENO) < 0 )
		i_fatal("dup2(stdout) failed: %m");

	/* Close all fds */
	if ( close(in_fd) < 0 )
		i_error("close(in_fd) failed: %m");
	if ( (out_fd != in_fd) && close(out_fd) < 0 )
		i_error("close(out_fd) failed: %m");

	t_array_init(&exec_args, 16);
	array_append(&exec_args, &bin_path, 1);
	if ( args != NULL ) {
		for (; *args != NULL; args++)
			array_append(&exec_args, args, 1);
	}
	(void)array_append_space(&exec_args);

	env_clean();
	if ( envs != NULL ) {
		for (; *envs != NULL; envs++)
			env_put(*envs);
	}

	args = array_idx(&exec_args, 0);
	execvp_const(args[0], args);
}

static int program_client_local_connect
(struct program_client *pclient)
{
	struct program_client_local *slclient = 
		(struct program_client_local *) pclient;
	int fd[2] = { -1, -1 };

	if ( pclient->input != NULL || pclient->output != NULL ||
		pclient->output_seekable ) {
		if ( socketpair(AF_UNIX, SOCK_STREAM, 0, fd) < 0 ) {
			i_error("socketpair() failed: %m");
			return -1;
		}
	}

	if ( (slclient->pid = fork()) == (pid_t)-1 ) {
		i_error("fork() failed: %m");
		if ( fd[0] >= 0 && close(fd[0]) < 0 ) {
			i_error("close(pipe_fd[0]) failed: %m");
		}
		if ( fd[1] >= 0 && close(fd[1]) < 0 ) {
			i_error("close(pipe_fd[1]) failed: %m");
		}
		return -1;
	}

	if ( slclient->pid == 0 ) {
		unsigned int count;
		const char *const *envs = NULL;

		/* child */
		if ( fd[1] >= 0 && close(fd[1]) < 0 ) {
			i_error("close(pipe_fd[1]) failed: %m");
		}

		if ( array_is_created(&pclient->envs) )
			envs = array_get(&pclient->envs, &count);

		exec_child(pclient->path, pclient->args, envs,
			( pclient->input != NULL ? fd[0] : -1 ),
			( pclient->output != NULL || pclient->output_seekable ? fd[0] : -1 ));
		i_unreached();
	}

	/* parent */
	if ( fd[0] >= 0 && close(fd[0]) < 0 ) {
		i_error("close(pipe_fd[0]) failed: %m");
	}

	if ( fd[1] >= 0 ) {
		net_set_nonblock(fd[1], TRUE);
		pclient->fd_in =
			( pclient->output != NULL || pclient->output_seekable ? fd[1] : -1 );
		pclient->fd_out = ( pclient->input != NULL ? fd[1] : -1 );
	}
	program_client_init_streams(pclient);
	return program_client_connected(pclient);
}

static int program_client_local_close_output(struct program_client *pclient)
{
	/* Shutdown output; program stdin will get EOF */
	if ( pclient->fd_out >= 0 && shutdown(pclient->fd_out, SHUT_WR) < 0 ) {
		i_error("shutdown(%s, SHUT_WR) failed: %m", pclient->path);
		return -1;
	}
	return 1;
}

static int program_client_local_disconnect
(struct program_client *pclient, bool force)
{
	struct program_client_local *slclient = 
		(struct program_client_local *) pclient;
	pid_t pid = slclient->pid, ret;
	time_t runtime, timeout = 0;
	int status;
	
	i_assert( pid >= 0 );
	slclient->pid = -1;

	/* Calculate timeout */
	runtime = ioloop_time - pclient->start_time;
	if ( !force && pclient->set->input_idle_timeout_secs > 0 &&
		runtime < (time_t)pclient->set->input_idle_timeout_secs )
		timeout = pclient->set->input_idle_timeout_secs - runtime;

	if ( pclient->debug ) {
		i_debug("waiting for program `%s' to finish after %llu seconds",
			pclient->path, (unsigned long long int)runtime);
	}

	/* Wait for child to exit */
	force = force ||
		(timeout == 0 && pclient->set->input_idle_timeout_secs > 0);
	if ( !force ) {
		alarm(timeout);
		ret = waitpid(pid, &status, 0);
		alarm(0);
	}
	if ( force || ret < 0 ) {
		if ( !force && errno != EINTR ) {
			i_error("waitpid(%s) failed: %m", pclient->path);
			(void)kill(pid, SIGKILL);
			return -1;
		}

		/* Timed out */
		force = TRUE;
		if ( pclient->error == PROGRAM_CLIENT_ERROR_NONE )
			pclient->error = PROGRAM_CLIENT_ERROR_RUN_TIMEOUT;
		if ( pclient->debug ) {
			i_debug("program `%s' execution timed out after %llu seconds: "
				"sending TERM signal", pclient->path,
				(unsigned long long int)pclient->set->input_idle_timeout_secs);
		}

		/* Kill child gently first */
		if ( kill(pid, SIGTERM) < 0 ) {
			i_error("failed to send SIGTERM signal to program `%s'", pclient->path);
			(void)kill(pid, SIGKILL);
			return -1;
		} 
			
		/* Wait for it to die (give it some more time) */
		alarm(5);
		ret = waitpid(pid, &status, 0);
		alarm(0);
		if ( ret < 0 ) {
			if ( errno != EINTR ) {
				i_error("waitpid(%s) failed: %m", pclient->path);
				(void)kill(pid, SIGKILL); 
				return -1;
			}

			/* Timed out again */
			if ( pclient->debug ) {
				i_debug("program `%s' execution timed out: sending KILL signal",
					pclient->path);
			}

			/* Kill it brutally now */
			if ( kill(pid, SIGKILL) < 0 ) {
				i_error("failed to send SIGKILL signal to program `%s'",
					pclient->path);
				return -1;
			}

			/* Now it will die immediately */
			if ( waitpid(pid, &status, 0) < 0 ) {
				i_error("waitpid(%s) failed: %m", pclient->path);
				return -1;
			}
		}
	}
	
	/* Evaluate child exit status */
	pclient->exit_code = -1;
	if ( WIFEXITED(status) ) {
		/* Exited */
		int exit_code = WEXITSTATUS(status);
				
		if ( exit_code != 0 ) {
			i_info("program `%s' terminated with non-zero exit code %d", 
				pclient->path, exit_code);
			pclient->exit_code = 0;
			return 0;
		}

		pclient->exit_code = 1;
		return 1;	

	} else if ( WIFSIGNALED(status) ) {
		/* Killed with a signal */
		
		if ( force ) {
			i_error("program `%s' was forcibly terminated with signal %d",
				pclient->path, WTERMSIG(status));
		} else {
			i_error("program `%s' terminated abnormally, signal %d",
				pclient->path, WTERMSIG(status));
		}
		return -1;

	} else if ( WIFSTOPPED(status) ) {
		/* Stopped */
		i_error("program `%s' stopped, signal %d",
			pclient->path, WSTOPSIG(status));
		return -1;
	} 

	/* Something else */
	i_error("program `%s' terminated abnormally, return status %d",
		pclient->path, status);
	return -1;
}

static void program_client_local_failure
(struct program_client *pclient, enum program_client_error error)
{
	switch ( error ) {
	case PROGRAM_CLIENT_ERROR_RUN_TIMEOUT:
		i_error("program `%s' execution timed out (> %d secs)",
			pclient->path, pclient->set->input_idle_timeout_secs);
		break;
	default:
		break;
	}
}

struct program_client *program_client_local_create
(const char *bin_path, const char *const *args,
	const struct program_client_settings *set)
{
	struct program_client_local *pclient;
	pool_t pool;

	pool = pool_alloconly_create("program client local", 1024);
	pclient = i_new(struct program_client_local, 1);
	program_client_init(&pclient->client, pool, bin_path, args, set);
	pclient->client.connect = program_client_local_connect;
	pclient->client.close_output = program_client_local_close_output;
	pclient->client.disconnect = program_client_local_disconnect;
	pclient->client.failure = program_client_local_failure;
	pclient->pid = -1;

	return &pclient->client;
}

