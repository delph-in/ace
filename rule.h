#ifndef	_RULE_H
#define	_RULE_H

#include	"dag.h"
#include	"chart.h"

struct rule
{
	char		*name;
	struct dg	*dg;
	int		ndaughters;
	struct irule	*inflr;
	int		lexical;
	int		rnum;
	int		rtl;
};

int			load_rules_conf();
void		rule_stats();
struct edge	*epsilon_edge(struct rule	*r, int	x, int	rtl);
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
