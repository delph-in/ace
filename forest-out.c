#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<assert.h>
#include	<stdarg.h>

#include "dag.h"
#include "chart.h"
#include "lexicon.h"
#include "maxent.h"
#include "unpack.h"
#include "mrs.h"
#include	"token.h"

void output_packed_tree(struct edge	*e)
{
	if(e->unpack)return;
	e->unpack = (void*)1;
	//char	*sign = e->rule?e->rule->name:e->lex->word;
	char	*sign = e->rule?e->rule->name:e->lex->lextype->name;
	int	i;
	printf("(%d %s %d %d [", e->id, sign, e->from, e->to);
	for(i=0;i<e->npack;i++)
	{
		if(i)printf(",");
		printf("%d", e->pack[i]->id);
	}
	printf("] (");
	for(i=0;i<e->have;i++)
	{
		if(i)printf(",");
		printf("%d", e->daughters[i]->id);
	}
	printf("))\n");
	for(i=0;i<e->npack;i++)output_packed_tree(e->pack[i]);
	for(i=0;i<e->have;i++)output_packed_tree(e->daughters[i]);
}

output_forest(struct chart_cell	*cell, struct lattice	*tokens)
{
	int	i;
	struct edge	*edge;
	for(i=0;i<cell->p_nedges;i++)
	{
		edge = cell->p_edges[i];
		if( (g_mode!=GENERATING || edge->neps == require_neps) && is_root(edge->dg) && !edge->frozen )
		{
			printf("root %d\n", edge->id);
			output_packed_tree(edge);
		}
	}
	for(i=0;i<tokens->nedges;i++)
	{
		struct lattice_edge	*le = tokens->edges[i];
		struct token	*t = le->token;
		int j;
		for(j=0;j<i;j++)
		{
			struct lattice_edge	*le2 = tokens->edges[j];
			struct token	*t2 = le2->token;
			if(le->from == le2->from && le->to == le2->to)
				break;
		}
		if(j<i)continue;	// already output a representative for this span
		printf("token %d %d %d %d %s\n", le->from->id, le->to->id, t->cfrom, t->cto, t->string);
	}
}
