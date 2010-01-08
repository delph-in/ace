#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<stdarg.h>

#include	"dag.h"
#include	"conf.h"
#include	"rule.h"
#include	"freeze.h"

struct path	lex_stem_path, lex_rels_path, rule_rels_path, extract_mrs_path, lex_carg_path, lex_pred_path;
char		*semarg_type;
char		*g_cons_type;
char		*g_list_type;
char		*g_null_type;
char		*g_diff_list_type;

struct path	*chart_dependencies_from, *chart_dependencies_to;
int			nchart_dependencies;

struct path	token_form_path, token_from_path, token_to_path, token_id_path, token_postags_path, token_posprobs_path;
struct path	lex_tokens_list_path, lex_tokens_last_path;
struct path	lattice_mapping_input_path, lattice_mapping_output_path, lattice_mapping_context_path, lattice_mapping_position_path;

char		*token_type;

struct dg	*dg_path(struct dg	*dg, struct path	p)
{
	int i;

	for(i=0;i<p.count;i++)
		if(dg)dg = dg_hop(dg, p.fnums[i]);
	return dg;
}

struct dg	*dg_path_add(struct dg	*dg, struct path	p, struct dg	*add)
{
	return add_dg_path(dg, p.fnums, p.count, add);
}

void print_path(struct path	p)
{
	extern char	**fnames;
	int 		i;

	for(i=0;i<p.count;i++)
		printf("%s%s", (i==0)?"":" ", fnames[p.fnums[i]]);
}

int			pathcmp(const struct path	*p1, const struct path	*p2)
{
	if(p1->count != p2->count)return p1->count - p2->count;
	return memcmp(p1->fnums, p2->fnums, sizeof(int)*p1->count);
}

struct path	pathdup(struct path	q)
{
	struct path p;
	p.count = q.count;
	p.fnums = malloc(sizeof(int)*p.count);
	memcpy(p.fnums, q.fnums, sizeof(int)*p.count);
	return p;
}

struct path	make_path(int count, ...)
{
	struct path	p;
	va_list	va;
	int		i;

	va_start(va, count);
	p.count = count;
	p.fnums = malloc(sizeof(int)*count);
	for(i=0;i<count;i++)
		p.fnums[i] = lookup_fname(va_arg(va, char*));
	va_end(va);

	return p;
}

extern char	*get_conf(char	*lhs);

struct path slab_string_to_path(char	*str)
{
	struct path	p;

	p.count = 0;
	p.fnums = NULL;

	for(str = strtok(freeze_string(str), " \t");str;str = strtok(NULL, " \t"))
	{
		p.count++;
		p.fnums = slab_realloc(p.fnums, sizeof(int)*(p.count-1), sizeof(int) * p.count);
		p.fnums[p.count-1] = lookup_fname(str);
	}
	return p;
}

struct path	string_to_path(char	*str)
{
	struct path	p;

	p.count = 0;
	p.fnums = NULL;

	str = strdup(str);
	for(str = strtok(str, " \t");str;str = strtok(NULL, " \t"))
	{
		p.count++;
		p.fnums = realloc(p.fnums, sizeof(int) * p.count);
		p.fnums[p.count-1] = lookup_fname(str);
	}
	free(str);
	return p;
}

static struct path get_conf_path(char	*lhs)
{
	char		*str = get_conf(lhs);
	if(!str)
	{
		fprintf(stderr, "avm path `%s' not specified in configuration!\n", lhs);
		exit(-1);
	}
	return string_to_path(str);
}

struct conf
{
	char		*semarg_type;
	char		*cons_type;
	char		*list_type;
	char		*null_type;
	char		*diff_list_type;
	struct path	lex_stem_path;
	struct path	lex_rels_path;
	struct path	lex_carg_path;
	struct path	lex_pred_path;
	struct path	rule_rels_path;
	struct path	extract_mrs_path;

	struct path	*chart_dependencies_to, *chart_dependencies_from;
	int			nchart_dependencies;

	char	*token_type;
	struct path	token_form_path, token_from_path, token_to_path,
		token_id_path, token_postags_path, token_posprobs_path;
	struct path	lex_tokens_list_path, lex_tokens_last_path;
	struct path	lattice_mapping_input_path, lattice_mapping_output_path, lattice_mapping_context_path, lattice_mapping_position_path;

	char	*deleted_daughters;
	char	*parsing_packing;
	char	*generating_packing;
	char	*ep_drop;
	int		restrictor_size;
};

struct path	freeze_path(struct path	p)
{
	p.fnums = freeze_block(p.fnums, sizeof(int)*p.count);
	return p;
}

extern int reload_restrictors, restrictor_size, nfnames;
extern char	*deleted_daughters, *parsing_packing, *generating_packing, *ep_drop;

void	*freeze_conf()
{
	struct conf	*c = slab_alloc(sizeof(*c));
	c->semarg_type = freeze_string(semarg_type);
	c->cons_type = freeze_string(g_cons_type);
	c->list_type = freeze_string(g_list_type);
	c->null_type = freeze_string(g_null_type);
	c->diff_list_type = freeze_string(g_diff_list_type);
	c->lex_stem_path = freeze_path(lex_stem_path);
	c->lex_rels_path = freeze_path(lex_rels_path);
	c->lex_carg_path = freeze_path(lex_carg_path);
	c->lex_pred_path = freeze_path(lex_pred_path);
	c->rule_rels_path = freeze_path(rule_rels_path);
	c->extract_mrs_path = freeze_path(extract_mrs_path);
	c->lex_tokens_list_path = freeze_path(lex_tokens_list_path);
	c->lex_tokens_last_path = freeze_path(lex_tokens_last_path);
	c->token_form_path = freeze_path(token_form_path);
	c->token_from_path = freeze_path(token_from_path);
	c->token_to_path = freeze_path(token_to_path);
	c->token_id_path = freeze_path(token_id_path);
	c->token_postags_path = freeze_path(token_postags_path);
	c->token_posprobs_path = freeze_path(token_posprobs_path);
	c->token_type = freeze_string(token_type);

	c->lattice_mapping_input_path = freeze_path(lattice_mapping_input_path);
	c->lattice_mapping_output_path = freeze_path(lattice_mapping_output_path);
	c->lattice_mapping_context_path = freeze_path(lattice_mapping_context_path);
	c->lattice_mapping_position_path = freeze_path(lattice_mapping_position_path);

	int i;
	for(i=0;i<nchart_dependencies;i++)
	{
		chart_dependencies_from[i] = freeze_path(chart_dependencies_from[i]);
		chart_dependencies_to[i] = freeze_path(chart_dependencies_to[i]);
	}
	c->chart_dependencies_from = freeze_block(chart_dependencies_from, sizeof(struct path)*nchart_dependencies);
	c->chart_dependencies_to = freeze_block(chart_dependencies_to, sizeof(struct path)*nchart_dependencies);
	c->nchart_dependencies = nchart_dependencies;

	c->deleted_daughters = freeze_block(deleted_daughters, restrictor_size);
	c->parsing_packing = freeze_block(parsing_packing, restrictor_size);
	c->generating_packing = freeze_block(generating_packing, restrictor_size);
	c->restrictor_size = restrictor_size;
	c->ep_drop = freeze_block(ep_drop, restrictor_size);

	return c;
}

void	recover_conf(void	*vc)
{
	struct conf	*c = vc;
	semarg_type = c->semarg_type;
	g_cons_type = c->cons_type;
	g_list_type = c->list_type;
	g_null_type = c->null_type;
	g_diff_list_type = c->diff_list_type;
	lex_stem_path = c->lex_stem_path;
	lex_rels_path = c->lex_rels_path;
	lex_carg_path = c->lex_carg_path;
	lex_pred_path = c->lex_pred_path;
	rule_rels_path = c->rule_rels_path;
	extract_mrs_path = c->extract_mrs_path;

	nchart_dependencies = c->nchart_dependencies;
	chart_dependencies_from = c->chart_dependencies_from;
	chart_dependencies_to = c->chart_dependencies_to;

	lex_tokens_list_path = c->lex_tokens_list_path;
	lex_tokens_last_path = c->lex_tokens_last_path;
	token_form_path = c->token_form_path;
	token_from_path = c->token_from_path;
	token_to_path = c->token_to_path;
	token_id_path = c->token_id_path;
	token_postags_path = c->token_postags_path;
	token_posprobs_path = c->token_posprobs_path;
	token_type = c->token_type;

	lattice_mapping_position_path = c->lattice_mapping_position_path;
	lattice_mapping_input_path = c->lattice_mapping_input_path;
	lattice_mapping_output_path = c->lattice_mapping_output_path;
	lattice_mapping_context_path = c->lattice_mapping_context_path;

	deleted_daughters = c->deleted_daughters;
	parsing_packing = c->parsing_packing;
	generating_packing = c->generating_packing;
	restrictor_size = c->restrictor_size;
	ep_drop = c->ep_drop;
	reload_restrictors = (restrictor_size != nfnames);

	//printf("types cons `%s' list `%s' null `%s' dlist `%s'\n", g_cons_type, g_list_type, g_null_type, g_diff_list_type);
}

int	load_conf()
{
	semarg_type = get_conf("semarg-type")?:"semarg";
	g_cons_type = get_conf("cons-type")?:"cons";
	g_list_type = get_conf("list-type")?:"list";
	g_null_type = get_conf("null-type")?:"null";
	g_diff_list_type = get_conf("diff-list-type")?:"diff-list";

	lex_stem_path = get_conf_path("orth-path");
	lex_rels_path = get_conf_path("lex-rels-path");
	lex_carg_path = get_conf_path("lex-carg-path");
	lex_pred_path = get_conf_path("lex-pred-path");
	rule_rels_path = get_conf_path("rule-rels-path");
	extract_mrs_path = get_conf_path("semantics-path");

	lex_tokens_list_path = get_conf_path("lexicon-tokens-path");
	lex_tokens_last_path = get_conf_path("lexicon-last-token-path");
	token_form_path = get_conf_path("token-form-path");
	token_from_path = get_conf_path("token-from-path");
	token_to_path = get_conf_path("token-to-path");
	token_id_path = get_conf_path("token-id-path");
	token_postags_path = get_conf_path("token-postags-path");
	token_posprobs_path = get_conf_path("token-posprobs-path");

	lattice_mapping_input_path = get_conf_path("lattice-mapping-input-path");
	lattice_mapping_output_path = get_conf_path("lattice-mapping-output-path");
	lattice_mapping_context_path = get_conf_path("lattice-mapping-context-path");
	lattice_mapping_position_path = get_conf_path("lattice-mapping-position-path");

	token_type = get_conf("token-type")?:"token";

	load_chart_dependencies(get_conf("chart-dependencies"));

	recover_conf(freeze_conf());
}

int check_conf_list(char	*key, char	*word)
{
	char	*list = get_conf(key), *p;
	if(!list)return 0;
	list = strdup(list);
	for(p=strtok(list, " \t\n");p;p=strtok(0, " \t\n"))
		if(!strcasecmp(word,p))break;
	free(list);
	return p?1:0;
}

void iterate_conf_list(char	*key, int	(*callback)(char	*word))
{
	int	hits = 0;
	char	*list = get_conf(key), *p;
	if(!list)return;
	list = strdup(list);
	for(p=strtok(list, " \t\n");p;p=strtok(0, " \t\n"))
		if(callback(p))break;
	free(list);
}

void	configure_rule(struct rule	*r)
{
	r->gen_ignore = 0;
	if(temporary_expedient(r->name))r->gen_ignore = 1;
	else add_semantic_index(r, r->dg, 1);

	r->span_only = check_conf_list("spanning-only-rules", r->name);
	r->frag_only = check_conf_list("fragment-only-rules", r->name);
	r->hyper = check_conf_list("hyper-active-rules", r->name);
	r->pad = 0;
}

void	load_chart_dependencies(char	*depstring)
{
	char	*strings[1024];
	int		nstrings = 0;

	depstring = trim_string(depstring);
	while(depstring && *depstring)
	{
		char	*str = depstring;
		if(*str != '"')
		{
			fprintf(stderr, "chart-dependencies parameter must be a list of path-strings\n");
			exit(-1);
		}
		depstring = strchr(str+1, '"');
		if(!depstring)
		{
			fprintf(stderr, "chart-dependencies parameter had mismatched quotes\n");
			exit(-1);
		}
		*depstring = 0;
		depstring = trim_string(depstring+1);
		strings[nstrings++] = trim_string(str+1);
	}

	if(nstrings % 2 != 0)
	{
		fprintf(stderr, "chart-dependencies parameter paths must be in pairs\n");
		exit(-1);
	}

	nchart_dependencies = nstrings/2;
	chart_dependencies_from = malloc(sizeof(struct path)*nchart_dependencies);
	chart_dependencies_to = malloc(sizeof(struct path)*nchart_dependencies);
	int	i;
	for(i=0;i<nchart_dependencies;i++)
	{
		chart_dependencies_from[i] = string_to_path(strings[2*i + 0]);
		chart_dependencies_to[i] = string_to_path(strings[2*i + 1]);
		/*printf("chart dep:  ");
		print_path(chart_dependencies_from[i]);
		printf(" => ");
		print_path(chart_dependencies_to[i]);
		printf("\n");*/
	}
}
