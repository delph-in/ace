#ifndef	_LEXICON_H
#define	_LEXICON_H

#include	"dag.h"
#include	"rule.h"

struct lexeme
{
	char		*word;
	struct dg	*dg;

	int		stemlen;
	struct type	**stem;
};

int	add_lexemes_to_chart_for_stem(int	x, char	*form, struct rule	*inflr);

#endif
