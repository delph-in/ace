#ifndef	CONF_H
#define	CONF_H

struct path
{
	int		count;
	int		*fnums;
};

extern struct path	lex_stem_path, lex_rels_path, rule_rels_path, extract_mrs_path, lex_carg_path, lex_pred_path;
extern char			*semarg_type;
extern char			*g_cons_type;
extern char			*g_list_type;
extern char			*g_null_type;
extern char			*g_diff_list_type;

struct dg	*dg_path(struct dg	*dg, struct path	p);
struct dg	*dg_path_add(struct dg	*dg, struct path	p, struct dg	*add);
int			pathcmp(const struct path	*p1, const struct path	*p2);
void		print_path(struct path	p);
struct path	make_path(int count, ...);
struct path	pathdup(struct path	p);

#endif
