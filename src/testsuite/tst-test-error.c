/* Copyright (c) 2002-2010 Dovecot Sieve authors, see the included COPYING file
 */

#include "sieve-common.h"
#include "sieve-error.h"
#include "sieve-script.h"
#include "sieve-commands.h"
#include "sieve-comparators.h"
#include "sieve-match-types.h"
#include "sieve-validator.h"
#include "sieve-generator.h"
#include "sieve-interpreter.h"
#include "sieve-code.h"
#include "sieve-binary.h"
#include "sieve-dump.h"
#include "sieve-match.h"

#include "testsuite-common.h"
#include "testsuite-log.h"

/*
 * Test_error command
 *
 * Syntax:   
 *   test [MATCH-TYPE] [COMPARATOR] [:index number] <key-list: string-list>
 */

static bool tst_test_error_registered
	(struct sieve_validator *valdtr, const struct sieve_extension *ext,
		struct sieve_command_registration *cmd_reg);
static bool tst_test_error_validate
	(struct sieve_validator *valdtr, struct sieve_command *cmd);
static bool tst_test_error_generate
	(const struct sieve_codegen_env *cgenv, struct sieve_command *ctx);

const struct sieve_command_def tst_test_error = { 
	"test_error", 
	SCT_TEST, 
	1, 0, FALSE, FALSE,
	tst_test_error_registered, 
	NULL,
	tst_test_error_validate, 
	tst_test_error_generate, 
	NULL 
};

/* 
 * Operation 
 */

static bool tst_test_error_operation_dump
	(const struct sieve_dumptime_env *denv, sieve_size_t *address);
static int tst_test_error_operation_execute
	(const struct sieve_runtime_env *renv, sieve_size_t *address);

const struct sieve_operation_def test_error_operation = { 
	"TEST_ERROR",
	&testsuite_extension, 
	TESTSUITE_OPERATION_TEST_ERROR,
	tst_test_error_operation_dump, 
	tst_test_error_operation_execute 
};

/*
 * Tagged arguments
 */ 

/* NOTE: This will be merged with the date-index extension when it is 
 * implemented.
 */

static bool tst_test_error_validate_index_tag
	(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
		struct sieve_command *cmd);

static const struct sieve_argument_def test_error_index_tag = {
	"index",
	NULL, 
	tst_test_error_validate_index_tag,
	NULL, NULL, NULL
};

enum tst_test_error_optional {
	OPT_INDEX = SIEVE_MATCH_OPT_LAST,
};


/*
 * Argument implementation
 */

static bool tst_test_error_validate_index_tag
(struct sieve_validator *valdtr, struct sieve_ast_argument **arg,
	struct sieve_command *cmd)
{
	struct sieve_ast_argument *tag = *arg;

	/* Detach the tag itself */
	*arg = sieve_ast_arguments_detach(*arg,1);

	/* Check syntax:
	 *   :index number
	 */
	if ( !sieve_validate_tag_parameter
		(valdtr, cmd, tag, *arg, NULL, 0, SAAT_NUMBER, FALSE) ) {
		return FALSE;
	}

	/* Skip parameter */
	*arg = sieve_ast_argument_next(*arg);
	return TRUE;
}


/*
 * Command registration
 */

static bool tst_test_error_registered
(struct sieve_validator *valdtr, const struct sieve_extension *ext,
	struct sieve_command_registration *cmd_reg)
{
	/* The order of these is not significant */
	sieve_comparators_link_tag(valdtr, cmd_reg, SIEVE_MATCH_OPT_COMPARATOR);
	sieve_match_types_link_tags(valdtr, cmd_reg, SIEVE_MATCH_OPT_MATCH_TYPE);

	sieve_validator_register_tag
		(valdtr, cmd_reg, ext, &test_error_index_tag, OPT_INDEX);

	return TRUE;
}

/* 
 * Validation 
 */

static bool tst_test_error_validate
(struct sieve_validator *valdtr ATTR_UNUSED, struct sieve_command *tst) 
{
	struct sieve_ast_argument *arg = tst->first_positional;
	struct sieve_comparator cmp_default = 
		SIEVE_COMPARATOR_DEFAULT(i_octet_comparator);
	struct sieve_match_type mcht_default = 
		SIEVE_COMPARATOR_DEFAULT(is_match_type);

	if ( !sieve_validate_positional_argument
		(valdtr, tst, arg, "key list", 2, SAAT_STRING_LIST) ) {
		return FALSE;
	}

	if ( !sieve_validator_argument_activate(valdtr, tst, arg, FALSE) )
		return FALSE;

	/* Validate the key argument to a specified match type */
	return sieve_match_type_validate
		(valdtr, tst, arg, &mcht_default, &cmp_default);
}

/* 
 * Code generation 
 */

static bool tst_test_error_generate
(const struct sieve_codegen_env *cgenv, struct sieve_command *tst)
{
	sieve_operation_emit(cgenv->sblock, tst->ext, &test_error_operation);

	/* Generate arguments */
	return sieve_generate_arguments(cgenv, tst, NULL);
}

/* 
 * Code dump
 */
 
static bool tst_test_error_operation_dump
(const struct sieve_dumptime_env *denv, sieve_size_t *address)
{
	int opt_code = 0;

	sieve_code_dumpf(denv, "TEST_ERROR:");
	sieve_code_descend(denv);

	/* Handle any optional arguments */
	for (;;) {
		int ret;

		if ( (ret=sieve_match_opr_optional_dump(denv, address, &opt_code)) 
			< 0 )
			return FALSE;

		if ( ret == 0 ) break;

		if ( opt_code == OPT_INDEX ) {
			if ( !sieve_opr_number_dump(denv, address, "index") )
				return FALSE;
		} else {
			return FALSE;
		}
	} 

	return sieve_opr_stringlist_dump(denv, address, "key list");
}

/*
 * Intepretation
 */

static int tst_test_error_operation_execute
(const struct sieve_runtime_env *renv, sieve_size_t *address)
{	
	int opt_code = 0;
	bool result = TRUE;
	struct sieve_comparator cmp = SIEVE_COMPARATOR_DEFAULT(i_octet_comparator);
	struct sieve_match_type mcht = SIEVE_COMPARATOR_DEFAULT(is_match_type);
	struct sieve_match_context *mctx;
	struct sieve_coded_stringlist *key_list;
	bool matched;
	const char *error;
	int cur_index = 0, index = 0;
	int ret;

	/*
	 * Read operands
	 */

	/* Read optional operands */
	for (;;) {
		sieve_number_t number; 
		int ret;

		if ( (ret=sieve_match_opr_optional_read
			(renv, address, &opt_code, &cmp, &mcht)) < 0 )
			return SIEVE_EXEC_BIN_CORRUPT;

		if ( ret == 0 ) break;
	
		if ( opt_code == OPT_INDEX ) {
			if ( !sieve_opr_number_read(renv, address, "index", &number) )
				return SIEVE_EXEC_BIN_CORRUPT;
			index = (int) number;
		} else {
			sieve_runtime_trace_error(renv, "invalid optional operand");
			return SIEVE_EXEC_BIN_CORRUPT;
		}	
	}

	/* Read key-list */
	if ( (key_list=sieve_opr_stringlist_read(renv, address, "key_list")) == NULL )
		return SIEVE_EXEC_BIN_CORRUPT;

	/*
	 * Perform operation
	 */
	
	sieve_runtime_trace(renv, SIEVE_TRLVL_TESTS,
		"TEST_ERROR test (index: %d)", index);

	testsuite_log_get_error_init();

	/* Initialize match */
	mctx = sieve_match_begin(renv->interp, &mcht, &cmp, NULL, key_list);

	/* Iterate through all errors to match */
	error = NULL;
	matched = FALSE;
	cur_index = 1;
	ret = 0;
	while ( result && !matched &&
		(error=testsuite_log_get_error_next(FALSE)) != NULL ) {
		
		if ( index == 0 || index == cur_index ) {
			if ( (ret=sieve_match_value(mctx, error, strlen(error))) < 0 ) {
				result = FALSE;
				break;
			}
		}

		matched = ret > 0;
		cur_index++;
	}

	/* Finish match */
	if ( (ret=sieve_match_end(&mctx)) < 0 )
		result = FALSE;
	else
		matched = ( ret > 0 || matched );

	/* Set test result for subsequent conditional jump */
	if ( result ) {
		sieve_interpreter_set_test_result(renv->interp, matched);
		return SIEVE_EXEC_OK;
	}

	sieve_runtime_trace_error(renv, "invalid string-list item");
	return SIEVE_EXEC_BIN_CORRUPT;
}




