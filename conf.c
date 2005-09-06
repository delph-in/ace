#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<stdarg.h>

#include	"dag.h"
#include	"conf.h"
#include	"freeze.h"

struct path	lex_stem_path, lex_rels_path, rule_rels_path, extract_mrs_path;
char		*semarg_type;
char		*g_cons_type;
char		*g_list_type;
char		*g_null_type;
char		*g_diff_list_type;

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

static struct path get_conf_path(char	*lhs)
{
	char		*str = get_conf(lhs);
	struct path	p;
	if(!str)
	{
		fprintf(stderr, "avm path `%s' not specified in configuration!\n", lhs);
		exit(-1);
	}

	p.count = 0;
	p.fnums = 0;

	for(str = strtok(strdup(str), " \t");str;str = strtok(NULL, " \t"))
	{
		p.count++;
		p.fnums = realloc(p.fnums, sizeof(int) * p.count);
		p.fnums[p.count-1] = lookup_fname(str);
	}
	return p;
}

char	*trims[] = {
#ifdef	ERG
						"HD-DTR", "NH-DTR",
#else	// MATRIX
						"HEAD-DTR", "NON-HEAD-DTR",
#endif
						"ARGS",	"LCONJ-DTR", "RCONJ-DTR",
						"DTR", "DTR1", "DTR2", "DTRA", "DTRB", 0};

#ifdef	JACY
#define	STEM	"ORTH"
#else
#define	STEM	"STEM"
#endif
char	*ppack[] = {STEM, "RELS", "HCONS", "RNAME", 0};
char	*gpack[] = {STEM, "RELS", "HCONS", "RNAME", "PUNCT", 0};

struct conf
{
	char		*semarg_type;
	char		*cons_type;
	char		*list_type;
	char		*null_type;
	char		*diff_list_type;
	struct path	lex_stem_path;
	struct path	lex_rels_path;
	struct path	rule_rels_path;
	struct path	extract_mrs_path;
};

struct path	freeze_path(struct path	p)
{
	p.fnums = freeze_block(p.fnums, sizeof(int)*p.count);
	return p;
}

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
	c->rule_rels_path = freeze_path(rule_rels_path);
	c->extract_mrs_path = freeze_path(extract_mrs_path);
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
	rule_rels_path = c->rule_rels_path;
	extract_mrs_path = c->extract_mrs_path;

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
	rule_rels_path = get_conf_path("rule-rels-path");
	extract_mrs_path = get_conf_path("semantics-path");
	recover_conf(freeze_conf());
}
