#ifndef	_MORPHO_H
#define	_MORPHO_H

struct orule
{
	struct rule	*rule;
	enum { or_suffix = 1, or_prefix = 2 } type;
	int		nsub;
	struct orule_sub
	{
		char		*pat;
		int			patl;
		char		*rep;
		int			repl;
	}		*sub;

	int		nirregs;
	struct irreg
	{
		char	*form, *stem;
	}	*irregs;
};

extern int		norules;
extern struct orule	**orules;

int		lexical_lookup(int	nwords, char	**words);
char	*inflect(struct orule	*or, char	*str);
void	setup_morpho(char	*line);
int		load_lrules();
void	add_orule(struct rule	*rule, char	*def);

#endif
