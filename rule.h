#ifndef	_RULE_H
#define	_RULE_H

#include	"dag.h"

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

static inline int	check_rf(struct rule	*m, struct rule	*d, int	arg)
{
	char	byte;
	extern int	nrules;
	extern char	*rule_filter;

	byte = rule_filter[m->rnum*nrules + d->rnum];
	return byte & (1<<arg);
}

#endif
