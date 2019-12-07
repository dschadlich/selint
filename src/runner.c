#include <stdio.h>
#include <string.h>
#include <libgen.h>

#include "runner.h"
#include "fc_checks.h"
#include "if_checks.h"
#include "te_checks.h"
#include "parse_fc.h"
#include "util.h"
#include "startup.h"

extern FILE *yyin;
extern int yyparse();
struct policy_node *ast;        // Must be global so the parser can access it
extern int yylineno;
extern char *parsing_filename;
extern struct policy_node *cur;

#define CHECK_ENABLED(cid) is_check_enabled(cid, config_enabled_checks, config_disabled_checks, cl_enabled_checks, cl_disabled_checks, only_enabled)

struct policy_node *parse_one_file(char *filename, enum node_flavor flavor)
{

	ast = calloc(1, sizeof(struct policy_node));
	ast->flavor = flavor;
	char *mod_name = strdup(basename(filename));
	mod_name[strlen(mod_name) - 3] = '\0'; // Remove suffix
	set_current_module_name(mod_name);
	yylineno = 1;
	free(mod_name);

	yyin = fopen(filename, "r");
	if (!yyin) {
		printf("Error opening %s\n", filename);
		free_policy_node(ast);
		return NULL;
	}
	parsing_filename = filename;
	if (0 != yyparse()) {
		free_policy_node(ast);
		return NULL;
	}
	fclose(yyin);
	cur = NULL;

	// dont run cleanup_parsing until everything is done because it frees the maps
	return ast;
}

int is_check_enabled(const char *check_name,
                     struct string_list *config_enabled_checks,
                     struct string_list *config_disabled_checks,
                     struct string_list *cl_enabled_checks,
                     struct string_list *cl_disabled_checks, int only_enabled)
{

	int is_enabled = 1;     // default to enabled

	if (only_enabled) {
		// if only_enabled is true, we only want to enable checks that are
		// explicitly enabled in the cl_enabled_checks. So change the default
		// enabled state to disabled, and skip all other checks except for the
		// enabled checks.
		is_enabled = 0;
	} else {
		if (str_in_sl(check_name, config_disabled_checks)) {
			is_enabled = 0;
		}

		if (str_in_sl(check_name, config_enabled_checks)) {
			is_enabled = 1;
		}

		if (str_in_sl(check_name, cl_disabled_checks)) {
			is_enabled = 0;
		}
	}

	if (str_in_sl(check_name, cl_enabled_checks)) {
		is_enabled = 1;
	}

	return is_enabled;
}

struct checks *register_checks(char level,
                               struct string_list *config_enabled_checks,
                               struct string_list *config_disabled_checks,
                               struct string_list *cl_enabled_checks,
                               struct string_list *cl_disabled_checks,
                               int only_enabled)
{

	struct checks *ck = malloc(sizeof(struct checks));

	memset(ck, 0, sizeof(struct checks));

	switch (level) {
	case 'C':
		if (CHECK_ENABLED("C-001")) {
			add_check(NODE_TE_FILE, ck, "C-001",
			          check_te_order);
			add_check(NODE_DECL, ck, "C-001",
			          check_te_order);
			add_check(NODE_AV_RULE, ck, "C-001",
			          check_te_order);
			add_check(NODE_IF_CALL, ck, "C-001",
			          check_te_order);
			add_check(NODE_TT_RULE, ck, "C-001",
			          check_te_order);
			add_check(NODE_CLEANUP, ck, "C-001",
			          check_te_order);
		}
		if (CHECK_ENABLED("C-004")) {
			add_check(NODE_INTERFACE_DEF, ck, "C-004",
			          check_interface_definitions_have_comment);
			add_check(NODE_TEMP_DEF, ck, "C-004",
			          check_interface_definitions_have_comment);
		}
	case 'S':
		if (CHECK_ENABLED("S-001")) {
			add_check(NODE_REQUIRE, ck, "S-001", check_require_block);
			add_check(NODE_GEN_REQ, ck, "S-001", check_require_block);
		}
		if (CHECK_ENABLED("S-002")) {
			add_check(NODE_FC_ENTRY, ck, "S-002",
			          check_file_context_types_in_mod);
		}
	case 'W':
		if (CHECK_ENABLED("W-001")) {
			add_check(NODE_AV_RULE, ck, "W-001", check_no_explicit_declaration);
			add_check(NODE_IF_CALL, ck, "W-001", check_no_explicit_declaration);
			add_check(NODE_TT_RULE, ck, "W-001", check_no_explicit_declaration);
		}
		if (CHECK_ENABLED("W-002")) {
			add_check(NODE_AV_RULE, ck, "W-002",
			          check_type_used_but_not_required_in_if);
			add_check(NODE_IF_CALL, ck, "W-002",
			          check_type_used_but_not_required_in_if);
			add_check(NODE_TT_RULE, ck, "W-002",
			          check_type_used_but_not_required_in_if);
		}
		if (CHECK_ENABLED("W-003")) {
			add_check(NODE_DECL, ck, "W-003",
			          check_type_required_but_not_used_in_if);
		}
		if (CHECK_ENABLED("W-004")) {
			add_check(NODE_FC_ENTRY, ck, "W-004", check_file_context_regex);
		}
		if (CHECK_ENABLED("W-005")) {
			add_check(NODE_IF_CALL, ck, "W-005",
			          check_module_if_call_in_optional);
		}
	case 'E':
		if (CHECK_ENABLED("E-002")) {
			add_check(NODE_ERROR, ck, "E-002",
			          check_file_context_error_nodes);
		}
		if (CHECK_ENABLED("E-003")) {
			add_check(NODE_FC_ENTRY, ck, "E-003", check_file_context_users);
		}
		if (CHECK_ENABLED("E-004")) {
			add_check(NODE_FC_ENTRY, ck, "E-004", check_file_context_roles);
		}
		if (CHECK_ENABLED("E-005")) {
			add_check(NODE_FC_ENTRY, ck, "E-005",
			          check_file_context_types_exist);
		}
	case 'F':
		break;
	default:
		free(ck);
		return NULL;
	}

	return ck;
}

enum selint_error parse_all_files_in_list(struct policy_file_list *files, enum node_flavor flavor)
{

	struct policy_file_node *cur = files->head;

	while (cur) {
		print_if_verbose("Parsing %s\n", cur->file->filename);
		cur->file->ast = parse_one_file(cur->file->filename, flavor);
		ast = NULL;
		if (!cur->file->ast) {
			return SELINT_PARSE_ERROR;
		}
		cur = cur->next;
	}

	return SELINT_SUCCESS;

}

enum selint_error parse_all_fc_files_in_list(struct policy_file_list *files)
{

	struct policy_file_node *cur = files->head;

	while (cur) {
		print_if_verbose("Parsing fc file %s\n", cur->file->filename);
		cur->file->ast = parse_fc_file(cur->file->filename);
		if (!cur->file->ast) {
			return SELINT_PARSE_ERROR;
		}
		cur = cur->next;
	}

	return SELINT_SUCCESS;
}

enum selint_error run_checks_on_one_file(struct checks *ck,
                                         struct check_data *data,
                                         struct policy_node *head)
{
	struct policy_node *cur = head;

	while (cur) {
		enum selint_error res = call_checks(ck, data, cur);
		if (res != SELINT_SUCCESS) {
			return res;
		}

		cur = dfs_next(cur);
	}

	// Give checks a change to clean up state
	struct policy_node cleanup;
	memset(&cleanup, 0, sizeof(struct policy_node));
	cleanup.flavor = NODE_CLEANUP;

	return call_checks(ck, data, &cleanup);
}

enum selint_error run_all_checks(struct checks *ck, enum file_flavor flavor,
                                 struct policy_file_list *files)
{

	struct policy_file_node *file = files->head;

	struct check_data data;

	data.flavor = flavor;

	while (file) {

		data.filename = strdup(basename(file->file->filename));
		data.mod_name = strdup(data.filename);

		char *suffix_ptr = rindex(data.mod_name, '.');

		*suffix_ptr = '\0';

		enum selint_error res =
			run_checks_on_one_file(ck, &data, file->file->ast);
		if (res != SELINT_SUCCESS) {
			return res;
		}

		free(data.filename);
		free(data.mod_name);

		file = file->next;

	}

	return SELINT_SUCCESS;
}

enum selint_error run_analysis(struct checks *ck,
                               struct policy_file_list *te_files,
                               struct policy_file_list *if_files,
                               struct policy_file_list *fc_files,
                               struct policy_file_list *context_files)
{

	enum selint_error res;

	res = parse_all_files_in_list(if_files, NODE_IF_FILE);
	if (res != SELINT_SUCCESS) {
		goto out;
	}

	res = parse_all_files_in_list(context_files, NODE_IF_FILE); //TODO: This can eventually
	                                                            // include te files too
	if (res != SELINT_SUCCESS) {
		goto out;
	}

	mark_transform_interfaces(if_files);

	res = parse_all_files_in_list(te_files, NODE_TE_FILE);
	if (res != SELINT_SUCCESS) {
		goto out;
	}

	res = parse_all_fc_files_in_list(fc_files);
	if (res != SELINT_SUCCESS) {
		goto out;
	}

	res = run_all_checks(ck, FILE_TE_FILE, te_files);
	if (res != SELINT_SUCCESS) {
		goto out;
	}

	res = run_all_checks(ck, FILE_IF_FILE, if_files);
	if (res != SELINT_SUCCESS) {
		goto out;
	}

	res = run_all_checks(ck, FILE_FC_FILE, fc_files);
	if (res != SELINT_SUCCESS) {
		goto out;
	}

out:
	cleanup_parsing();

	return res;
}
