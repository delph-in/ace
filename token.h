#ifndef	TOKEN_H
#define	TOKEN_H

#include	"lattice-mapping.h"

struct token
{
	char	*string;
	int	npostags;
	struct postag
	{
		char	*tag;
		char	*prob;
	}	*postags;
	int		cfrom, cto;
	struct dg	*dg;

	struct ochart	*oc;
};

struct lattice	*build_token_chart(int	nwords, char	**words, int	*cfrom, int	*cto);
void	build_token_dag(struct token	*t);
void	print_token_chart(struct lattice	*tc);
char	*get_token_carg(struct token	*t);

struct edge;
int		install_tokens_in_le(struct edge	*le, struct token	**tokens);

#endif
