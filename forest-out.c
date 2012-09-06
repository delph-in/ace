#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<assert.h>
#include	<stdarg.h>
#include	<wctype.h>

#include "dag.h"
#include "chart.h"
#include "lexicon.h"
#include "maxent.h"
#include "unpack.h"
#include "mrs.h"
#include	"token.h"
#include	"unicode.h"

int	find_token_id(struct token	*t, struct lattice	*l);

void output_packed_tree(struct edge	*e, struct lattice	*tokens)
{
	if(e->unpack)return;
	e->unpack = (void*)1;
	char	*sign = e->rule?e->rule->name:e->lex->word;
	//char	*sign = e->rule?e->rule->name:e->lex->lextype->name;
	int	i;
	printf("(%d %s %d %d [", e->id, sign, e->from, e->to);
	for(i=0;i<e->npack;i++)
	{
		if(i)printf(",");
		printf("%d", e->pack[i]->id);
	}
	printf("] (");
	if(!e->rule)
	{
		for(i=0;i<e->lex->stemlen;i++)
		{
			if(i)printf(",");
			printf("%d", find_token_id((struct token*)e->daughters[i], tokens));
		}
	}
	else
	{
		for(i=0;i<e->have;i++)
		{
			if(i)printf(",");
			printf("%d", e->daughters[i]->id);
		}
	}
	printf("))\n");
	for(i=0;i<e->npack;i++)output_packed_tree(e->pack[i], tokens);
	for(i=0;i<e->have;i++)output_packed_tree(e->daughters[i], tokens);
}

set_path_number(struct token	*T, struct path	path, int	n)
{
	char	str[16];
	sprintf(str, "\"%d\"", n);
	// this can in theory break reentrancies, if there's
	// already a node at this path and it's reentrant with somewhere else.
	struct dg	*new_dag_for_unfrozen_string(char	*str);
	T->dg = dg_path_add(T->dg, path, new_dag_for_unfrozen_string(str));
}

update_cfrom(struct token	*T, int	new_cfrom)
{
	if(T->cfrom == new_cfrom)return;
	T->cfrom = new_cfrom;
	// also update the AVM path
	set_path_number(T, token_from_path, new_cfrom);
}

update_cto(struct token	*T, int	new_cto)
{
	if(T->cto == new_cto)return;
	T->cto = new_cto;
	// also update the AVM path
	set_path_number(T, token_to_path, new_cto);
}

int reestimate_token_cto(struct token	*tok)
{
	//printf("ought to reestimate token '%s''s CTO\n", tok->string);
	extern char	*current_sentence;

	wchar_t	*wtok = towide(tok->string);
	wchar_t	*wsent = towide(current_sentence);
	int	slen = wcslen(wsent), tlen = wcslen(wtok);
	assert(tok->cfrom < slen);
	assert(tok->cfrom+tlen <= slen);
	int i;
	for(i=0;i<tlen;i++)
	{
		if(iswpunct(wtok[i]))
		{
			if(iswpunct(wsent[tok->cfrom+i]))continue;
			else break;
		}
		else
		{
			if(wsent[tok->cfrom+i] == wtok[i])continue;
			else break;
		}
	}
	free(wtok);
	free(wsent);
	//printf("first %d wide characters match\n", i);
	if(i == 0)return -1;
	update_cto(tok, tok->cfrom + i);
	return 0;
}

improve_token_lattice_characterization(struct lattice	*tokens)
{
	int	vtxcfrom[tokens->nvertices];
	int	vtxcfromn[tokens->nvertices];
	int	vtxcto[tokens->nvertices];
	int	vtxcton[tokens->nvertices];
	int i;
	for(i=0;i<tokens->nvertices;i++)vtxcfrom[i] = vtxcfromn[i] = vtxcto[i] = vtxcton[i] = 0;
	for(i=0;i<tokens->nedges;i++)
	{
		struct lattice_edge	*e = tokens->edges[i];
		int	from = e->from->id, to = e->to->id;
		struct token	*tok = e->token;
		int cfrom = tok->cfrom, cto = tok->cto;
		assert(from >= 0 && from < tokens->nvertices);
		assert(to >= 0 && to < tokens->nvertices);

		if(!vtxcfromn[from]) { vtxcfrom[from] = cfrom; vtxcfromn[from] = 1; }
		else if(vtxcfrom[from] != cfrom)vtxcfromn[from]++;	// count distinct values

		if(!vtxcton[to]) { vtxcto[to] = cto; vtxcton[to] = 1; }
		else if(vtxcto[to] != cto)vtxcton[to]++;	// count distinct values
	}
	char	suspicious[tokens->nvertices];
	for(i=0;i<tokens->nvertices;i++)
	{
		assert(vtxcfromn[i] <= 1);	// we would be surprised to find that two tokens starting at the same vertex had different CFROMs
		assert(vtxcton[i] <= 1);	// likewise CTOs

		suspicious[i] = 0;
		if(vtxcfromn[i] == 0 || vtxcton[i] == 0)continue;
		if(vtxcfrom[i] >= vtxcto[i])continue;	// normal; adjacent tokens are either immediately adjacent or have some whitespace
		//printf("vertex %d is suspect; as CTO=%d, as CFROM=%d\n", i, vtxcto[i], vtxcfrom[i]);
		// not normal: when used as CTO, this vertex is farther to the right than when used as CFROM
		// this means adjacent tokens overlap according to CFROM/CTO. this is an error in characterization from one or more of the tokens abutting this vtx.
		// look at each token abutting this vtx, and use hackery to estimate a new CFROM/CTO for whichever side of that token this vtx is on
		suspicious[i] = 1;
	}
	int progress, retry = 10;
do_retry:
	progress = 0;
	for(i=0;i<tokens->nedges;i++)
	{
		int from_susp = suspicious[tokens->edges[i]->from->id];
		int to_susp = suspicious[tokens->edges[i]->to->id];
		if(to_susp && !from_susp)
		{
			if(reestimate_token_cto(tokens->edges[i]->token) == -1)continue;
			int new_cto = tokens->edges[i]->token->cto;
			//printf("reestimated vertex %d at %d\n", tokens->edges[i]->to->id, new_cto);
			// go find everyone talking aobut the suspicious vertex and update them.
			// make both CFROM and CTO = this new value; this situation usually arises internally to a word, so we needn't allow for space between tokens.
			int j;
			for(j=0;j<tokens->nedges;j++)
			{
				struct token	*T = tokens->edges[j]->token;
				if(tokens->edges[j]->from == tokens->edges[i]->to)
					update_cfrom(T, new_cto);
				if(tokens->edges[j]->to == tokens->edges[i]->to)
					update_cto(T, new_cto);
			}
			suspicious[tokens->edges[i]->to->id] = 0;
			progress = 1;
		}
	}
	if(progress && retry-- > 0)goto do_retry;
}

output_forest(struct chart_cell	*cell, struct lattice	*tokens)
{
	int	i;
	int	found_root = 0;
	struct edge	*edge;
	for(i=0;i<cell->p_nedges;i++)
	{
		edge = cell->p_edges[i];
		if( (g_mode!=GENERATING || edge->neps == require_neps) && is_root(edge->dg) && !edge->frozen )
		{
			printf("root %d\n", edge->id);
			output_packed_tree(edge, tokens);
			found_root = 1;
		}
	}
	improve_token_lattice_characterization(tokens);
	for(i=0;i<tokens->nedges;i++)
	{
		struct lattice_edge	*le = tokens->edges[i];
		struct token	*t = le->token;
		/*int j;
		for(j=0;j<i;j++)
		{
			struct lattice_edge	*le2 = tokens->edges[j];
			struct token	*t2 = le2->token;
			if(le->from == le2->from && le->to == le2->to)
				break;
		}
		if(j<i)continue;	// already output a representative for this span*/
		//printf("token %d %d %d %d %s\n", le->from->id, le->to->id, t->cfrom, t->cto, t->string);
		char	token_str[10240];
		if(enable_token_avms)
		{
			extern int dag_print_style;
			dag_print_style = 1;
			snprint_dg(token_str, 10239, t->dg);
			dag_print_style = 0;
		}
		else
		{
			//sprintf(token_str, "%s", t->string);
			char	*heap_escape(char	*str);
			char	*formesc = heap_escape(t->string);
			sprintf(token_str, "token [ +FROM \"%d\" +TO \"%d\" +FORM \"%s\" ]", t->cfrom, t->cto, formesc);
			free(formesc);
		}
		printf("token %d %d %d %s\n", find_token_id(t, tokens), le->from->id, le->to->id, token_str);
	}
	return found_root;
}
