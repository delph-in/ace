#ifndef	_RULE_H
#define	_RULE_H

struct rule
{
	char		*name;
	struct dg	*dg;		// full form of the dag
	struct dg	*packdg;	// packing-restricted form of the dag
	struct dg	*gpackdg;	// generation-packing-restricted form of the dag
	struct orule	*orth;
	int				rnum:16;
	unsigned int	ndaughters:4;
	int				lexical:4;
	int				rtl:3, hyper:1;
	int				gen_ignore:1, span_only:1, frag_only:1, pad:1;
};

#include	"dag.h"
#include	"chart.h"

int			load_rules_conf();
void		rule_stats();
struct edge	*epsilon_edge(struct rule	*r, int	x, int	rtl, struct dg	*rels);
void		print_rule(char	*name);
void		setup_rule_filter();
void		lex_filter_rules();
void		agenda_rules(int	count);
int			load_rules();
int			load_roots();
int			is_root(struct dg	*dg);

extern int		nrules;
extern struct rule	**rules;

static inline int	check_rf(struct rule	*m, struct rule	*d, int	arg)
{
	char	byte;
	extern char	*rule_filter;

	byte = rule_filter[m->rnum*nrules + d->rnum];
	return byte & (1<<arg);
}

#endif
