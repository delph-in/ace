#ifndef	_LEXICON_H
#define	_LEXICON_H

#include	"dag.h"
#include	"rule.h"
#include	"morpho.h"

struct lexeme
{
	char		*word;
	struct dg	*dg;

	int		stemlen;
	struct type	**stem;
	struct type	*lextype;

	struct type	*select;	// this lexeme selects a required complement
	struct type	*pred;		// this lexeme provides relation 'pred'
};

int	add_lexemes_to_chart_for_stem(int	x, char	*stem, struct irule	*ir);
int	add_multilexemes_to_chart(int	x, int	nwords, char	**words);
void	dump_parts_of_speech();
int		load_lexicon();
struct dg	*expand_lexeme(struct lexeme	*l);

#endif
