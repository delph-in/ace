#ifndef	_MORPHO_H
#define	_MORPHO_H

struct irule
{
	struct rule	*rule;
	enum { ir_suffix = 1, ir_prefix = 2 } type;
	int		nsub;
	struct irule_sub
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

extern int		nirules;
extern struct irule	**irules;

int		lexical_lookup(int	nwords, char	**words);
char	*inflect(struct irule	*ir, char	*str);
void	setup_morpho(char	*line);
int		load_inflr();
void	add_inflr(struct rule	*rule, char	*def);

#endif
