#ifndef	_MORPHO_H
#define	_MORPHO_H

struct irule
{
	struct rule	*rule;
	int		nsub;
	char		**subpat;
	char		**subrep;

	int		nirregs;
	struct irreg
	{
		char	*form, *stem;
	}	*irregs;
};

int		add_lexemes_to_chart(int	nwords, char	**words);
int		add_lexemes_to_chart_for_form(int	x, char	*form, int nrules, int *rules);
char	*inflect(struct irule	*ir, char	*str);
void	setup_morpho(char	*line);
int		load_inflr();
void	add_inflr(struct rule	*rule, char	*def);

#endif
