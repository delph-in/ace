#ifndef	_MORPHO_H
#define	_MORPHO_H
#include	"lexicon.h"
#include	"dag.h"

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

int	add_lexemes_to_chart(int	nwords, char	**words);

#endif
