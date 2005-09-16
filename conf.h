#ifndef	CONF_H
#define	CONF_H

struct path
{
	int		count;
	int		*fnums;
};

extern struct path	lex_stem_path, lex_rels_path, rule_rels_path, extract_mrs_path, lex_carg_path;
extern char			*semarg_type;
extern char			*g_cons_type;
extern char			*g_list_type;
extern char			*g_null_type;
extern char			*g_diff_list_type;

struct dg	*dg_path(struct dg	*dg, struct path	p);
struct dg	*dg_path_add(struct dg	*dg, struct path	p, struct dg	*add);
void		print_path(struct path	p);
struct path	make_path(int count, ...);

#endif
