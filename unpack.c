#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<assert.h>

#include "dag.h"
#include "chart.h"
#include "lexicon.h"
#include "maxent.h"
#include "unpack.h"
#include "mrs.h"

extern int debug_level;

static inline char* edge_sign(struct edge *e)
{
	if(!e)return 0;
	if(!e->have)return e->lex->lextype->name;
	else return e->rule->name;
}

void score_decomposition(struct decomposition *d)
{
	if(!d->edge->have)
	{
		// leaf node
		// XXX make this work for MWEs
		d->score = maxent_score(edge_sign(d->edge), d->edge->lex->stem[0]->name, 0, 0);
	}
	else d->score = maxent_score(edge_sign(d->edge), edge_sign(d->rhs[0]), edge_sign(d->rhs[1]), d->edge->rule->rtl);
}

struct decomposition *decompose_edge(struct edge	*edge, int *Ndecompositions)
{
	int	n = 1, i, j;
	struct decomposition *dec, *d;

	for(i=0;i<edge->have;i++)n *= edge->daughters[i]->npack + 1;
	d = dec = slab_alloc(sizeof(struct decomposition) * n);

	for(i=0;i<n;i++)
	{
		dec[i].edge = edge;
		dec[i].seeni1 = 0;
		dec[i].ni2 = 0;
	}

	if(edge->have == 1)
	{
		for(i=-1;i<edge->daughters[0]->npack;i++)
		{
			d->rhs[0] = (i!=-1)?edge->daughters[0]->pack[i]:edge->daughters[0];
			d->rhs[1] = 0;
			d++;
		}
	}
	else
	{
		d = dec;
		for(i=-1;i<edge->daughters[0]->npack;i++)
			for(j=-1;j<edge->daughters[1]->npack;j++)
			{
				d->rhs[0] = (i!=-1)?edge->daughters[0]->pack[i]:edge->daughters[0];
				d->rhs[1] = (j!=-1)?edge->daughters[1]->pack[j]:edge->daughters[1];
				d++;
			}
	}

	for(i=0;i<n;i++)
	{
		//printf("edge #%d dec %p #%d+#%d\n", edge->id, dec+i, dec[i].rhs[0]->id, dec[i].rhs[1]?dec[i].rhs[1]->id:0);
		score_decomposition(dec+i);
	}
	*Ndecompositions = n;
	return dec;
}

int dec_check_indices(struct decomposition	*dec, int indices[2])
{
	int i;

	assert(indices[1]>=0 && indices[0]>=0);
	if(indices[1] > dec->ni2)return 0;
	if(indices[1] == dec->ni2)
	{
		if(indices[0] == 0)return 0;	// haven't seen but should consider
		else return 2;	// haven't seen but shouldn't add to agenda
	}

	if(indices[0] > dec->seeni1[indices[1]]+1)
		return 2;	// haven't seen but shouldn't add to agenda
	else if(indices[0] == dec->seeni1[indices[1]]+1)
		return 0;	// haven't seen but should consider
	else return 1;	// have already seen
}

int dec_add_indices(struct decomposition	*dec, int indices[2])
{
	assert(indices[1] <= dec->ni2);
	if(indices[1] == dec->ni2)
	{
		assert(indices[0] == 0);
		dec->ni2++;
		dec->seeni1 = slab_realloc(dec->seeni1,
			sizeof(int) * (dec->ni2-1),
			sizeof(int) * dec->ni2);
		dec->seeni1[indices[1]] = 0;
	}
	else
	{
		assert(indices[0] <= 1+dec->seeni1[indices[1]]);
		if(dec->seeni1[indices[1]] < indices[0])
			dec->seeni1[indices[1]] = indices[0];
	}
}

// insert 'h' into 'edge's agenda, in sorted order
void add_uagenda(struct hypothesis **A, struct hypothesis *h)
{
	struct hypothesis *w = *A, **W = A;

	while(w && w->score > h->score)
	{
		W = &w->next;
		w = w->next;
	}

	h->next = w;
	*W = h;
}

struct hypothesis *new_hypothesis(struct decomposition	*dec, struct hypothesis **agenda, struct hypothesis *rhs[2], int indices[2])
{
	struct hypothesis	*h = slab_alloc(sizeof(struct hypothesis));
	int					i;

	//printf("hypothesize at dec %p [%d %d]\n", dec, indices[0], indices[1]);
	h->dec = dec;
	h->rhs[0] = rhs[0];
	h->rhs[1] = rhs[1];
	h->indices[0] = indices[0];
	h->indices[1] = indices[1];
	h->score = dec->score + rhs[0]->score;
	h->dg = 0; h->string = 0;
	h->checkup = 0;
	if(dec->edge && dec->edge->have>1)h->score += rhs[1]->score;

	h->next = 0;

	add_uagenda(agenda, h);
}

struct hypothesis *leaf_hypothesis(struct edge *e)
{
	struct decomposition *dec = slab_alloc(sizeof(struct decomposition));
	struct hypothesis *hyp = slab_alloc(sizeof(struct hypothesis));
	dec->edge = e;
	dec->rhs[0] = dec->rhs[1] = 0;
	dec->ni2 = 0;
	dec->seeni1 = 0;
	score_decomposition(dec);
	hyp->dec = dec;
	hyp->string = 0;
	hyp->dg = 0;
	hyp->checkup = -1;
	hyp->score = dec->score;
	hyp->indices[0] = hyp->indices[1] = 0;
	hyp->rhs[0] = hyp->rhs[1] = 0;
	return hyp;
}

static int indent = 0;

static int g_checkup;
static inline void checkup(struct hypothesis *h)
{
	if(h->dg==(void*)1)return;
	if(h->checkup>=0 && h->checkup < g_checkup)
	{
		int i, arity = h->dec->edge?h->dec->edge->have:1;
		for(i=0;i<arity;i++)
		{
			checkup(h->rhs[i]);
			if(h->rhs[i]->dg == (void*)1)
			{
				h->dg = (void*)1;
				return;
			}
		}
		h->checkup = g_checkup;
	}
}

struct hypothesis *process_hypagenda(struct hypothesis **A)
{
	struct hypothesis	*hyp;
	int					arity, i, j;

again:
	if(!*A)return 0;
	hyp = *A; *A = hyp->next;
	if(hyp->dec->edge)arity = hyp->dec->edge->have;
	else arity = 1;

	// popped an item off the agenda, now tell the corresponding
	// decomposition that the relevant index tuple has been popped
	// so it will let us extend around it
	dec_add_indices(hyp->dec, hyp->indices);
	// do the expansion around the popped hyp
	for(i=0;i<arity;i++)
	{
		int idx[2] = {hyp->indices[0], hyp->indices[1]};
		idx[i]++;
		if(!dec_check_indices(hyp->dec, idx))
		{
			struct hypothesis *rhs[2];
			// try to add a new agenda hypothesis for {dec,idx}
			//for(j=1;j<indent;j++)printf("  ");
			//printf("trying further hypothesize [%d %d] on dec %p of #%d\n", idx[0], idx[1], hyp->dec, hyp->dec->edge?hyp->dec->edge->id:-1);

			for(j=0;j<arity;j++)
			{
				rhs[j] = hypothesize_edge(hyp->dec->rhs[j], idx[j]);
				if(!rhs[j])break;
			}
			if(j==arity)new_hypothesis(hyp->dec, A, rhs, idx);
			else
			{
				//for(j=0;j<indent;j++)printf("  ");
				//printf(" further hypothesize [%d %d] failed on dec %p\n", idx[0], idx[1], hyp->dec);
			}
		}
		else
		{
			//for(j=1;j<indent;j++)printf("  ");
			//printf("%p of #%d doesn't want [%d %d]\n", hyp->dec, hyp->dec->edge?hyp->dec->edge->id:-1, idx[0], idx[1]);
		}
	}

	checkup(hyp);
	if(hyp->dg == (void*)1)goto again;
	return hyp;
}

struct hypothesis *hypothesize_edge(struct edge *e, int rank)
{
	struct hypothesis	*hyp;
	int					i, j;

	//for(i=0;i<indent;i++)printf("  ");
	//printf("asked to hyp %d on edge #%d [%d-%d] (had seen %d)\n", rank, e->id, e->from, e->to, e->unpack?e->unpack->nhypotheses:0);

	if(!e->have)
	{
		if(rank)return 0;
		if(!e->unpack)
		{
			e->unpack = slab_alloc(sizeof(struct unpack_info));
			e->unpack->agenda = 0;
			e->unpack->nhypotheses = e->unpack->ahypotheses = 1;
			e->unpack->hypotheses = slab_alloc(sizeof(struct hypothesis*)*1);
			e->unpack->hypotheses[0] = leaf_hypothesis(e);
		}
		return e->unpack->hypotheses[0];
	}

	if(!e->unpack)
	{
		int i, j, idx[2] = {0,0}, ndecs;
		struct decomposition *decs;
		// initialize agenda with (0,...,0) index vector for each decomposition
		decs = decompose_edge(e, &ndecs);
		e->unpack = slab_alloc(sizeof(struct unpack_info));
		e->unpack->agenda = 0;
		e->unpack->hypotheses = 0;
		e->unpack->nhypotheses = e->unpack->ahypotheses = 0;
		indent++;
		for(i=0;i<ndecs;i++)
		{
			struct decomposition *dec = decs+i;
			struct hypothesis *rhs[2];
			for(j=0;j<e->have;j++)
				rhs[j] = hypothesize_edge(dec->rhs[j], 0);
			new_hypothesis(dec, &e->unpack->agenda, rhs, idx);
		}
		indent--;
	}
	else if(rank < e->unpack->nhypotheses)return e->unpack->hypotheses[rank];

	indent++;
	hyp = process_hypagenda(&e->unpack->agenda);
	indent--;
	if(!hyp)return 0;

	if(++e->unpack->nhypotheses > e->unpack->ahypotheses)
	{
		int onum = e->unpack->ahypotheses;
		e->unpack->ahypotheses++;
		e->unpack->ahypotheses *= 2;
		e->unpack->hypotheses = slab_realloc(e->unpack->hypotheses,
			sizeof(struct hypothesis*) * onum,
			sizeof(struct hypothesis*) * e->unpack->ahypotheses);
	}
	assert(e->unpack->nhypotheses == rank+1);
	e->unpack->hypotheses[rank] = hyp;
	return hyp;
}

char	*show_h_orth_leaves(struct hypothesis	*h)
{
	int		i;
	char	*sub, *ret;

	if(!h->dec->edge->have)
	{
		if(h->dec->edge->lex)
		{
			// XXX fix me for MWEs
			ret = strdup(h->dec->edge->lex->stem[0]->name+1);
			ret[strlen(ret)-1] = 0;
			return ret;
		}
		else return strdup("[null]");
	}
	if(h->dec->edge->have==1)
	{
		sub = show_h_orth_leaves(h->rhs[0]);
		if(h->dec->edge->rule->inflr)
		{
			//printf("applying inflr `%s' to `%s'\n", e->rule->name, sub);
			ret = inflect(h->dec->edge->rule->inflr, sub);
			free(sub);
			return ret;
		}
		else return sub;
	}
	else
	{
		for(ret=calloc(1,1),i=0;i<2;i++)
		{
			sub = show_h_orth_leaves(h->rhs[i]);
			ret = realloc(ret, strlen(ret) + strlen(sub) + 2);
			if(i)strcat(ret, " ");
			strcat(ret, sub);
			free(sub);
		}
		return ret;
	}
}

int instantiate_hypothesis(struct hypothesis *h)
{
	int arity = h->dec->edge?h->dec->edge->have:1, i;

	// don't try to re-instantiate when we've already been here!
	if(h->dg)
	{
		if(h->dg == (void*)1)
		{
			assert(!"Not reached");
			return -1;
		}
		else return 0;
	}

	//printf("inst edge #%d [%d %d] rule %s\n", h->dec->edge->id, h->indices[0], h->indices[1], h->dec->edge->rule?h->dec->edge->rule->name:0);
	for(i=0;i<arity;i++)
		if(0 != instantiate_hypothesis(h->rhs[i]))
		{
			h->dg = (void*)1;
			return -1;
		}
	h->string = show_h_orth_leaves(h); // XXX not quite; want inflected forms even before rules happen, and this should have already been computed by this point... to score it

	if(!arity)
	{
		// lexeme.  don't put the specialized RELS list in.
		h->dg = lexical_dg(h->dec->edge->lex, 0);
	}
	else if(h->dec->edge)
	{
		h->dg = copy_dg(h->dec->edge->rule->dg);
		for(i=0;i<arity;i++)
		{
			h->dg = unify_dg(h->rhs[i]->dg, h->dg, i);
			if(!h->dg)
			{
				//printf("when combining edges #%d and #%d\n", h->dec->edge->id, h->rhs[i]->dec->edge->id);
				//unify_backtrace();
				h->dg = (void*)1;
				return -1;
			}
		}
	}
	// else this is the fake super-root node, and will be ignored

	return 0;
}

int iterate_cell_root_hypotheses(struct chart_cell *cell, void (*process)(struct hypothesis *hyp, struct mrs *mrs), int nbest)
{
	int	i, j, count = 0, nhyp = 0;
	struct hypothesis	*agenda = 0, *hyp;
	struct edge			*edge;

	for(i=0;i<cell->p_nedges;i++)
	{
		edge = cell->p_edges[i];
		if( (g_mode!=GENERATING || edge->ep_span == require_ep_span) && is_root(edge->dg) )
		{
			for(j=-1;j<edge->npack;j++)
			{
				// for each potentially root packed edge...
				struct edge *e = (j==-1)?edge:edge->pack[j];
				if(!is_root(e->dg))continue;
				struct decomposition *rootdec = slab_alloc(sizeof(struct decomposition));
				rootdec->edge = 0;
				rootdec->rhs[0] = e;
				rootdec->rhs[1] = 0;
				rootdec->score = 0;
				rootdec->seeni1 = 0;
				rootdec->ni2 = 0;

				int idx[2] = {0,0};
				struct hypothesis *rhs[2] = { hypothesize_edge(e, 0), 0 };
				new_hypothesis(rootdec, &agenda, rhs, idx);
			}
		}
	}

	while( (hyp = process_hypagenda(&agenda)) )
	{
		struct hypothesis	*rhyp;
		rhyp = hyp->rhs[0];
		nhyp++;
		//printf("\ntry a hyp score %f ind %d on edge #%d\n", rhyp->score, hyp->indices[0], rhyp->dec->edge->id);
		if(0 == instantiate_hypothesis(rhyp))
		{
			if(is_root(rhyp->dg))
			{
				struct mrs	*m = extract_mrs(rhyp->dg);
				process(rhyp, m);
				count++;
				if(nbest && count>=nbest)break;
			}
			//else printf("... wasn't root after unpacking\n");
		}
		else g_checkup++;	// mark that we need to look for subfailures before hypothesizing
	}
	if(debug_level)printf("%d hyps / %d readings\n", nhyp, count);
	return count;
}
