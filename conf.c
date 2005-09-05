#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<stdarg.h>

#include	"dag.h"
#include	"conf.h"

struct path	lex_stem_path, lex_rels_path, rule_rels_path, extract_mrs_path;
char		*semarg_type;
char		*g_cons_type;
char		*g_list_type;
char		*g_null_type;
char		*g_diff_list_type;

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
	p.fnums = malloc(sizeof(short)*count);
	for(i=0;i<count;i++)
		p.fnums[i] = lookup_fname(va_arg(va, char*));
	va_end(va);

	return p;
}

int	load_conf()
{
#ifdef	ERG
	semarg_type = "basic_semarg";
	g_cons_type = "*cons*";
	g_list_type = "*list*";
	g_null_type = "*null*";
	g_diff_list_type = "*diff-list*";
#else
	semarg_type = "semarg";
	g_cons_type = "cons";
	g_list_type = "list";
	g_null_type = "null";
	g_diff_list_type = "diff-list";
#endif

#ifdef	JACY
	lex_stem_path = make_path(2, "ORTH", "LIST");
#else
	lex_stem_path = make_path(1, "STEM");
#endif

	lex_rels_path = make_path(4, "SYNSEM", "LOCAL", "CONT", "RELS");
	rule_rels_path = make_path(2, "C-CONT", "RELS");
	extract_mrs_path = make_path(3, "SYNSEM", "LOCAL", "CONT");

	//printf("lex_stem_path: "); print_path(lex_stem_path); printf("\n");
	//printf("lex_rels_path: "); print_path(lex_rels_path); printf("\n");
	//printf("rule_rels_path: "); print_path(rule_rels_path); printf("\n");
}
