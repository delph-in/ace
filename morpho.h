#ifndef	_MORPHO_H
#define	_MORPHO_H

#include	<wchar.h>

struct orule
{
	struct rule	*rule;
	enum { or_suffix = 1, or_prefix = 2 } type;
	int		nsub;
	struct orule_sub
	{
		wchar_t		*pat;
		int			patl;
		wchar_t		*rep;
		int			repl;
	}		*sub;

	int		nirregs;
	struct irreg
	{
		wchar_t	*form, *stem;
	}	*irregs_f, *irregs_s;
};

extern int		norules;
extern struct orule	**orules;

struct edge;
struct token;
struct lattice;
int	lexical_lookup(struct lattice	*tc, void	(*edge_handler)(struct edge	*e, struct token	**tokens));
char	*inflect(struct orule	*or, char	*str);
void	setup_morpho(char	*line);
int		load_lrules();
void	add_orule(struct rule	*rule, char	*def);

#endif
