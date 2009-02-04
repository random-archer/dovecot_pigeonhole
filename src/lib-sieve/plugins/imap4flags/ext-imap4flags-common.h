/* Copyright (c) 2002-2009 Dovecot Sieve authors, see the included COPYING file
 */

#ifndef __EXT_IMAP4FLAGS_COMMON_H
#define __EXT_IMAP4FLAGS_COMMON_H

#include "lib.h"

#include "sieve-common.h"
#include "sieve-ext-variables.h"

/*
 * Extension
 */
 
extern const struct sieve_extension imapflags_extension;
extern const struct sieve_interpreter_extension 
	imapflags_interpreter_extension;

/*
 * Side effect
 */

extern const struct sieve_side_effect flags_side_effect;

/*
 * Operands
 */

extern const struct sieve_operand flags_side_effect_operand;

/*
 * Operations
 */
 
enum ext_imap4flags_opcode {
	ext_imap4flags_OPERATION_SETFLAG,
	ext_imap4flags_OPERATION_ADDFLAG,
	ext_imap4flags_OPERATION_REMOVEFLAG,
	ext_imap4flags_OPERATION_HASFLAG
};

extern const struct sieve_operation setflag_operation;
extern const struct sieve_operation addflag_operation;
extern const struct sieve_operation removeflag_operation;
extern const struct sieve_operation hasflag_operation;

/* 
 * Commands 
 */

extern const struct sieve_command cmd_setflag;
extern const struct sieve_command cmd_addflag;
extern const struct sieve_command cmd_removeflag;

extern const struct sieve_command tst_hasflag;

/*
 * Common command functions
 */

bool ext_imap4flags_command_validate
	(struct sieve_validator *validator, struct sieve_command_context *cmd);

/*
 * Flags tagged argument
 */	
	
void ext_imap4flags_attach_flags_tag
	(struct sieve_validator *valdtr, const char *command);

/* 
 * Flag management 
 */

struct ext_imap4flags_iter {
	string_t *flags_list;
	unsigned int offset;
	unsigned int last;
};

void ext_imap4flags_iter_init
	(struct ext_imap4flags_iter *iter, string_t *flags_list);
	
const char *ext_imap4flags_iter_get_flag
	(struct ext_imap4flags_iter *iter);

typedef int (*ext_imapflag_flag_operation_t)
	(const struct sieve_runtime_env *renv, struct sieve_variable_storage *storage,
		unsigned int var_index, string_t *flags);

int ext_imap4flags_set_flags
	(const struct sieve_runtime_env *renv, struct sieve_variable_storage *storage,
		unsigned int var_index, string_t *flags);
int ext_imap4flags_add_flags
	(const struct sieve_runtime_env *renv, struct sieve_variable_storage *storage,
		unsigned int var_index, string_t *flags);
int ext_imap4flags_remove_flags
	(const struct sieve_runtime_env *renv, struct sieve_variable_storage *storage,
		unsigned int var_index, string_t *flags);

/*
 * Flags access
 */

int ext_imap4flags_get_flags_string
(const struct sieve_runtime_env *renv, struct sieve_variable_storage *storage, 
	unsigned int var_index, const char **flags);

void ext_imap4flags_get_flags_init
	(struct ext_imap4flags_iter *iter, const struct sieve_runtime_env *renv,
		string_t *flags_list);
void ext_imap4flags_get_implicit_flags_init
	(struct ext_imap4flags_iter *iter, struct sieve_result *result);


#endif /* __EXT_IMAP4FLAGS_COMMON_H */

