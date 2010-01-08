#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<limits.h>
#include	<wchar.h>
#include	<ctype.h>

#include	"conf.h"
#include	"rule.h"
#include	"chart.h"
#include	"morpho.h"
#include	"tdl.h"
#include	"lexicon.h"
#include	"freeze.h"
#include	"lexical-parse.h"
#include	"lattice-mapping.h"

void	add_new_lexical_edge(struct lattice	*lc, struct edge	*e)
{
	struct lattice_edge	*lat_e = slab_alloc(sizeof(*lat_e));
	bzero(lat_e, sizeof(*lat_e));
	lat_e->from = lc->vertices[e->from];
	lat_e->to = lc->vertices[e->to];
	assert(e->from <= lc->nvertices);
	assert(e->to <= lc->nvertices);
	lat_e->token = NULL;
	lat_e->edge = e;
	add_lattice_edge(lc, lat_e);
}

struct lattice	*lexical_lookup_into_chart(struct lattice	*token_chart)
{
	struct lattice	*lc = slab_alloc(sizeof(*lc));
	int i;

	sort_lattice(token_chart);

	// make identical vertices in the lexical chart, with initially no edges
	bzero(lc, sizeof(*lc));
	lc->nvertices = token_chart->nvertices;
	lc->vertices = slab_alloc(sizeof(struct lattice_vertex*)*lc->nvertices);
	for(i=0;i<lc->nvertices;i++)
	{
		lc->vertices[i] = slab_alloc(sizeof(struct lattice_vertex));
		bzero(lc->vertices[i], sizeof(struct lattice_vertex));
		lc->vertices[i]->id = token_chart->vertices[i]->id;
	}

	void	edge_handler(struct edge	*le, struct token	**tokens)
	{
		//printf(" lexical lookup found lexeme '%s'\n", le->lex->word);
		add_new_lexical_edge(lc, le);
	}

	int res = lexical_lookup(token_chart, edge_handler);
	if(res!=0)return NULL;// { fprintf(stderr, "NOTE: lexical coverage gap\n"); return NULL; }
	return lc;
}

int	lexical_parse(struct lattice	*lc, struct lattice_edge	*le, struct edge	**epsila, char	*used)
{
	struct edge	*e = le->edge;
	int	i;

	for(i=0;i<nrules;i++)
	{
		if(!rules[i]->lexical)continue;
		if(check_lexical_rule_constraints(rules[i], le->edge))continue;
		epsila[i]->from = epsila[i]->to = le->edge->from;
		if(filter(epsila[i], le->edge))
		{
			//printf("trying lexrule '%s' on edge #%d ('%s')\n", rules[i]->name, le->edge->id, show_orth_leaves(le->edge));
			struct edge	*e = NULL;
			int	res = unify_and_build1(epsila[i], le->edge, &e, 1);
			if(res == -1)
			{
				fprintf(stderr, "unify_and_build1 got mad\n");
				return -1;
			}
			if(res == 1 && e != NULL)
			{
				//printf(" -- built new edge #%d ('%s')\n", e->id, show_orth_leaves(e));
				add_new_lexical_edge(lc, e);
			}
		}
	}
	return 0;
}

int	lexical_parse_lattice(struct lattice	*lc)
{
	int i, j;
	struct edge	*epsila[nrules];
	char		used[nrules];
	struct lattice_vertex	*epsila_starts[nrules];

	// apply lexical rules as much as possible
	for(i=0;i<nrules;i++)
		if(rules[i]->lexical)
		{
			used[i] = 1;
			epsila_starts[i] = NULL;
		}
	for(i=0;i<lc->nedges;i++)
	{
		struct lattice_edge	*e = lc->edges[i];
		for(j=0;j<nrules;j++)
			if(rules[j]->lexical && used[j] && epsila_starts[j] != e->from)
			{
				// prevent structure sharing between edges that start in different places
				used[j] = 0;
				epsila_starts[j] = NULL;
				epsila[j] = epsilon_edge(rules[j], 0, 0, NULL);
			}
		if(lexical_parse(lc, lc->edges[i], epsila, used) != 0)return -1;
		for(j=0;j<nrules;j++)
			if(rules[j]->lexical && used[j])
				epsila_starts[j] = e->from;
	}

	// delete edges which have not satisfied their orth_routes
	for(i=0;i<lc->nedges;i++)
	{
		struct edge	*e = lc->edges[i]->edge;
		for(j=0;j<e->north_routes;j++)
			if(e->orth_routes[j].len == 0)break;
		if(j==e->north_routes)
		{
			// this edge cannot play in syntax, since it has not completely inflected
			remove_lattice_edge(lc, lc->edges[i]);
			i--;
		}
	}
}
