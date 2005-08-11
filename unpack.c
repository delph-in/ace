#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

#include "dag.h"
#include "chart.h"
#include "lexicon.h"
#include "maxent.h"

struct decomposition
{
	struct edge *edge;
	struct edge *rhs[2];
	float		score;

	int	**indices;
	int	nindices;
};

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
		dec[i].indices = 0;
		dec[i].nindices = 0;
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

	for(i=0;i<n;i++)score_decomposition(dec+i);
	*Ndecompositions = n;
	return dec;
}

int dec_saw_indices(struct decomposition	*dec, int indices[2])
{
	int i;

	for(i=0;i<dec->nindices;i++)
		if(indices[0] == dec->indices[i][0] && indices[1] == dec->indices[i][1])
			return 1;
	return 0;
}

struct hypothesis
{
	struct decomposition *dec;
	struct hypothesis	*rhs[2];
	int					indices[2];
	float				score;

	struct hypothesis *next;
};

struct unpack_info
{
	struct hypothesis *agenda;
	struct hypothesis **hypotheses;
	int	nhypotheses;
};

// insert 'h' into 'edge's agenda, in sorted order
void add_uagenda(struct edge *edge, struct hypothesis *h)
{
	struct hypothesis *w = edge->unpack->agenda, **W = &edge->unpack->agenda;

	while(w && w->score > h->score)
	{
		W = &w->next;
		w = w->next;
	}

	h->next = w;
	*W = h;
}

struct hypothesis *new_hypothesis(struct decomposition	*dec, struct hypothesis *rhs[2], int indices[2])
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
	if(dec->edge->have>1)h->score += rhs[1]->score;

	h->next = 0;

	if(!dec_saw_indices(dec, indices))
	{
		dec->nindices++;
		dec->indices = slab_realloc(dec->indices,
			sizeof(int*) * (dec->nindices-1),
			sizeof(int*) * dec->nindices);
		dec->indices[dec->nindices-1] = slab_alloc(sizeof(int)*2);
		dec->indices[dec->nindices-1][0] = indices[0];
		dec->indices[dec->nindices-1][1] = indices[1];
	}

	add_uagenda(dec->edge, h);
}

struct hypothesis *leaf_hypothesis(struct edge *e)
{
	struct decomposition *dec = slab_alloc(sizeof(struct decomposition));
	struct hypothesis *hyp = slab_alloc(sizeof(struct hypothesis));
	dec->edge = e;
	dec->rhs[0] = dec->rhs[1] = 0;
	dec->nindices = 0;
	dec->indices = 0;
	score_decomposition(dec);
	hyp->dec = dec;
	hyp->score = dec->score;
	hyp->indices[0] = hyp->indices[1] = 0;
	hyp->rhs[0] = hyp->rhs[1] = 0;
	return hyp;
}

static int indent = 0;
struct hypothesis *hypothesize_edge(struct edge *e, int rank)
{
	struct hypothesis	*hyp;
	int					i, j;

	//for(i=0;i<indent;i++)printf("  ");
	//printf("asked to hyp %d on edge #%d [%d-%d]\n", rank, e->id, e->from, e->to);
	if(!e->have)
	{
		if(rank)return 0;
		else return leaf_hypothesis(e);
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
		e->unpack->nhypotheses = 0;
		indent++;
		for(i=0;i<ndecs;i++)
		{
			struct decomposition *dec = decs+i;
			struct hypothesis *rhs[2];
			for(j=0;j<e->have;j++)
				rhs[j] = hypothesize_edge(dec->rhs[j], 0);
			new_hypothesis(dec, rhs, idx);
		}
		indent--;
	}
	else if(rank < e->unpack->nhypotheses)return e->unpack->hypotheses[rank];

	if(!e->unpack->agenda)return 0;

	indent++;
	hyp = e->unpack->agenda; e->unpack->agenda = hyp->next;
	for(i=0;i<e->have;i++)
	{
		int idx[2] = {hyp->indices[0], hyp->indices[1]};
		idx[i]++;
		if(!dec_saw_indices(hyp->dec, idx))
		{
			struct hypothesis *rhs[2];
			// try to add a new agenda hypothesis for {dec,idx}
			//for(j=0;j<indent;j++)printf("  ");
			//printf(" trying further hypothesize [%d %d] on dec %p\n", idx[0], idx[1], hyp->dec);
			for(j=0;j<e->have;j++)
			{
				rhs[j] = hypothesize_edge(hyp->dec->rhs[j], idx[j]);
				if(!rhs[j])break;
			}
			if(j==e->have)new_hypothesis(hyp->dec, rhs, idx);
			else
			{
				//for(j=0;j<indent;j++)printf("  ");
				//printf(" further hypothesize [%d %d] failed on dec %p\n", idx[0], idx[1], hyp->dec);
			}
		}
		//else printf("%p has already seen [%d %d]\n", hyp->dec, idx[0], idx[1]);
	}
	indent--;

	e->unpack->nhypotheses = rank+1;
	e->unpack->hypotheses = slab_realloc(e->unpack->hypotheses,
		sizeof(int) * (e->unpack->nhypotheses-1),
		sizeof(int) * e->unpack->nhypotheses);
	e->unpack->hypotheses[rank] = hyp;
	return hyp;
}

struct edge *instantiate_hypothesis(struct hypothesis *h)
{
	return h->dec->edge;
}

struct forest *selectively_unpack(struct edge *e, int n)
{
	struct forest	*f = slab_alloc(sizeof(struct forest));
	int				i;

	printf("note -- packed %d in top node\n", e->npack);
	f->ntrees = 0;
	f->trees = slab_alloc(sizeof(struct forest*) * n);
	f->scores = slab_alloc(sizeof(float) * n);
	for(i=0;f->ntrees<n;i++)
	{
		struct hypothesis *h = hypothesize_edge(e, i);
		if(!h)
		{
			printf("hypothesize_edge gave up on %d\n", i);
			break;
		}
		struct edge *t = instantiate_hypothesis(h);
		if(t)
		{
			f->scores[f->ntrees] = h->score;
			f->trees[f->ntrees] = t;
			f->ntrees++;
			printf(" ... got a reading\n");
		}
		else break;
	}

	return f;
}
