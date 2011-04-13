#include	<stdio.h>
#include	<locale.h>
#include	<assert.h>

#include	"dag.h"
#include	"rule.h"
#include	"freeze.h"

int	form_to_relation = 0;
int	debug_level = 0, export_parts = 0, do_itsdb = 0;
char	*save_grammar_path = 0;
char	*load_grammar_path = 0;
char	*sentence_path = 0, *master_addr = 0;
int	question = 0, exhaustive = 1, lui_mode = 0, inhibit_pack = 0, best_only = 0;

int	preprocess_only = 0;
extern char mrs_line_breaks;

int g_mode = PARSING, print_trees = 1;
int	die = 0;

extern int	g_loaded, inhibit_results, trace;

void	ace_load_grammar(char	*path)
{
	setlocale(LC_ALL, "");
	setup_carcs_stack();
	init_glb_cache();
	load_frozen_grammar(path);
	setup_extra_type_dgs();
	g_loaded = 1;
}
