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

#define	MAX_UNPACK_CHUNKS	1000
extern int noldslabs;

extern int debug_level, inhibit_pack;

static inline char* edge_sign(struct edge *e)
{
	if(!e)return 0;
	if(!e->have)return e->lex->lextype->name;
	else return e->rule->name;
}

void score_decomposition(struct decomposition *d)
{
	if(d->edge->have)
		d->score = maxent_score(edge_sign(d->edge), edge_sign(d->rhs[0]),
						edge_sign(d->rhs[1]), d->edge->rule->rtl);
	else d->score = 0; // we have to score leaf nodes later since their surface form changes around
}

struct decomposition *decompose_edge(struct edge	*edge, int *Ndecompositions)
{
	int	n = 1, i, j, orign;
	struct decomposition *dec, *d;

	for(i=0;i<edge->have;i++)n *= edge->daughters[i]->npack + 1;
	d = dec = slab_alloc(sizeof(struct decomposition) * n);

	for(i=0;i<n;i++)
	{
		dec[i].edge = edge;
		dec[i].seeni1 = 0;
		dec[i].ni2 = 0;
	}

	orign = n; n = 0;
	if(edge->have == 1)
	{
		for(i=-1;i<edge->daughters[0]->npack;i++)
		{
			struct edge *r1 = (i!=-1)?edge->daughters[0]->pack[i]:edge->daughters[0];
			// check rule filter to see if this particular local unpacking can be ruled out
			if(r1->rule && !check_rf(edge->rule, r1->rule, 0))continue;
			d->rhs[0] = r1;
			d->rhs[1] = 0;
			d++; n++;
		}
	}
	else
	{
		d = dec;
		for(i=-1;i<edge->daughters[0]->npack;i++)
		{
			struct edge *r1 = (i!=-1)?edge->daughters[0]->pack[i]:edge->daughters[0];
			// check rule filter to see if this particular local unpacking can be ruled out
			if(r1->rule && !check_rf(edge->rule, r1->rule, 0))continue;
			for(j=-1;j<edge->daughters[1]->npack;j++)
			{
				struct edge *r2 = (j!=-1)?edge->daughters[1]->pack[j]:edge->daughters[1];
				if(r2->rule && !check_rf(edge->rule, r2->rule, 1))continue;
				d->rhs[0] = r1;
				d->rhs[1] = r2;
				d++; n++;
			}
		}
	}

	for(i=0;i<n;i++)
	{
		//printf("edge #%d dec %p #%d+#%d\n", edge->id, dec+i, dec[i].rhs[0]->id, dec[i].rhs[1]?dec[i].rhs[1]->id:0);
		score_decomposition(dec+i);
	}
	//if(orign!=n)printf("decompose #%d: xprod %d, rf %d\n", edge->id, orign, n);
	*Ndecompositions = n;
	return dec;
}

static inline int dec_check_indices(struct decomposition	*dec, int indices[2])
{
	int ret;

#define DEC_SEEN(x, y)	(x<0 || y<0 || (y<dec->ni2 && x<=dec->seeni1[y]))
	ret = !(DEC_SEEN(indices[0]-1, indices[1]) && DEC_SEEN(indices[0], indices[1]-1));
	//printf("dec_check on %p [%d %d] = %d;   ni2 = %d\n", dec, indices[0], indices[1], ret, dec->ni2);
	return ret;
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

print_heap(struct hypothesis *h, int indent)
{
	int i;
	if(!h){printf("(null)\n");return 0;}
	for(i=0;i<indent;i++)printf("  ");
	printf("hyp %p score %f\n", h, h->score);
	if(h->left)print_heap(h->left, indent+1);
	if(h->right)print_heap(h->right, indent+1);
}

// insert 'h' into 'edge's agenda, a heap structure
void add_uagenda(struct hypothesis **A, struct hypothesis *h)
{
	struct hypothesis *w = *A, **W = A;

	while(w && w->score >= h->score)
	{
		if(!w->left || (w->right && rand()%2))
		{
			W = &w->left;
			w = w->left;
		}
		else
		{
			W = &w->right;
			w = w->right;
		}
	}

	*W = h;
	if(w)add_uagenda(W, w);
}

struct hypothesis *new_hypothesis(struct decomposition	*dec, struct hypothesis **agenda, struct hypothesis *rhs[2], int indices[2])
{
	struct hypothesis	*h = slab_alloc(sizeof(struct hypothesis));
	int					i;

	//printf("hypothesize %p at dec %p [%d %d]\n", h, dec, indices[0], indices[1]);
	h->dec = dec;
	h->rhs[0] = rhs[0];
	h->rhs[1] = rhs[1];
	h->indices[0] = indices[0];
	h->indices[1] = indices[1];
	h->score = dec->score + rhs[0]->score;
	h->dg = 0; h->string = 0;
	h->checkup = 0;
	if(dec->edge)
	{
		if(dec->edge->have>1)h->score += rhs[1]->score;
		if(dec->edge->lex)
		{
			assert(dec->edge->have==1);	// lexical rules are unary
			if(dec->edge->rule->orth)
			{
				h->string = inflect(dec->edge->rule->orth, rhs[0]->string);
				if(!h->string)h->string = "(failed to inflect)";	// packing might make us try something silly here
			}
			else h->string = rhs[0]->string;
		}
		else for(i=0;i<dec->edge->have;i++) if(rhs[i]->dec->edge->lex)
		{
			// syntax edge with lexical daughter.
			// this is the first place in the hypothesis tree where we
			// know for sure what the surface form under us is, so add the maxent score
			char surf[512];
			assert(rhs[i]->string);
			sprintf(surf, "\"%s\"", rhs[i]->string);
			h->score += maxent_score(rhs[i]->dec->edge->lex->lextype->name, surf, 0, 0);
		}
	}

	h->left = 0;
	h->right = 0;

	add_uagenda(agenda, h);
}

struct hypothesis *leaf_hypothesis(struct edge *e)
{
	struct decomposition *dec = slab_alloc(sizeof(struct decomposition));
	struct hypothesis *hyp = slab_alloc(sizeof(struct hypothesis));
	int	i, slen = 0;
	dec->edge = e;
	dec->rhs[0] = dec->rhs[1] = 0;
	dec->ni2 = 0;
	dec->seeni1 = 0;
	score_decomposition(dec);
	hyp->dec = dec;

	for(i=0;i<e->lex->stemlen;i++)slen += strlen(e->lex->stem[i]->name) + 1 - 2;
	if(slen%4)slen = (slen&~3)+4;
	hyp->string = slab_alloc(slen); hyp->string[0]=0;
	for(i=0;i<e->lex->stemlen;i++)
	{
		char	single[256];
		if(i)strcat(hyp->string, " ");
		strcpy(single, e->lex->stem[i]->name+1);
		if(*single)single[strlen(single)-1] = 0;
		strcat(hyp->string, single);
	}

	hyp->left = 0;
	hyp->right = 0;

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
	if(h->dg)return; // if we already know failure/success status, don't recurse
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
	struct hypothesis	*hyp, **W, *l, *r;
	int					arity, i, j;

again:
	if(!*A)return 0;

	// pop max from the agenda heap
	hyp = *A;

	for(l=hyp->left,r=hyp->right,W=A;l && r;)
	{
		struct hypothesis *tmp;
		if(l->score > r->score)
		{
			// promote left
			*W = l;
			tmp = l->right;
			l->right = r;
			W = &l->left;
			l = l->left;
			r = tmp;
		}
		else
		{
			// promote right
			*W = r;
			tmp = r->left;
			r->left = l;
			W = &r->right;
			r = r->right;
			l = tmp;
		}
	}
	if(l)*W = l;
	else *W = r;

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
			{
				rhs[j] = hypothesize_edge(dec->rhs[j], 0);
				if(!rhs[j])break;
			}
			if(j<e->have)continue;	// failed to hypothesize even the 0'th unpacking of this decomposition
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
	if(e->unpack->nhypotheses != rank+1)
	{
		fprintf(stderr, "unpack(): asked for rank %d, but next is actually rank %d\n",
					rank, e->unpack->nhypotheses-1);
		assert(0);
	}
	e->unpack->hypotheses[rank] = hyp;
	return hyp;
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
	if(!h->string)
	{
		// combine the strings of daughters
		int slen = 0, i;
		for(i=0;i<arity;i++)slen += strlen(h->rhs[i]->string) + 1;
		if(slen%4)slen = (slen&~3)+4;
		h->string = slab_alloc(slen+1); h->string[0] = 0;
		for(i=0;i<arity;i++)
		{
			if(i)strcat(h->string, " ");
			strcat(h->string, h->rhs[i]->string);
		}
	}

	if(g_mode == GENERATING || !inhibit_pack || 1)	// XXX fixme, this should work...
	{
		if(!arity)
		{
			// lexeme.  don't put the specialized RELS list in.
			h->dg = lexical_dg(h->dec->edge->lex, 0);
		}
		else if(h->dec->edge)
		{
			struct dg *rdg = h->dec->edge->rule->dg;
			for(i=0;i<arity;i++)
				if(unify_dg_tmp(h->rhs[i]->dg, rdg, i))
				{
					//printf("when combining edges #%d and #%d under rule %s\n",
					//	h->dec->edge->id, h->rhs[i]->dec->edge->id, h->dec->edge->rule->name);
					//unify_backtrace();
					forget_tmp();
					h->dg = (void*)1;
					return -1;
				}
			h->dg = finalize_tmp(rdg, 1);
			if(!h->dg)	// cycle failure can occur
			{
				h->dg = (void*)1;
				return -1;
			}
		}
		// else this is the fake super-root node, and will be ignored
	}
	else
	{
		// no need to unspecialize or reconstruct; we already have a full dag.
		h->dg = h->dec->edge?h->dec->edge->dg:0;
	}

	return 0;
}

void print_hypothesis(struct hypothesis *hyp, int (*print)(const char *fmt,...))
{
	int print_tree0(struct hypothesis *hyp, char *surf, int from, int quiet)
	{
		int i, len = 0;
		struct edge *e = hyp->dec->edge;
		if(e && e->lex && !surf)
			surf = hyp->string;	// grab tree-top string and pass it down till we get to the lexeme

		// first silently traverse subtree and compute its word length
		if(e->have)
			for(i=0;i<e->have;i++)
				len += print_tree0(hyp->rhs[i], surf, from+len, 1);
		else if(e->lex) len = e->lex->stemlen;

		// now possibly actually print out the edge, with span information
		if(!quiet)
		{
			print(" (%d %p %s %f %d %d", e->id, hyp, e->rule?e->rule->name:(e->lex?e->lex->word:"no-sign"), hyp->score, from, from+len);
			if(e->have)
				for(len=i=0;i<e->have;i++)
					len += print_tree0(hyp->rhs[i], surf, from+len, 0);
			else if(e->lex)
				print(" (\\\"%s\\\" %d %d)", surf, from, from+len);
			print(")");
		}
		return len;
	}
	print_tree0(hyp, 0, 0, 0);
}

int iterate_cell_root_hypotheses(struct chart_cell *cell, int (*process)(struct hypothesis *hyp, struct mrs *mrs), int nbest)
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
				if(rhs[0])new_hypothesis(rootdec, &agenda, rhs, idx);
			}
		}
	}

	while( (hyp = process_hypagenda(&agenda)) )
	{
		struct hypothesis	*rhyp;
		rhyp = hyp->rhs[0];
		nhyp++;
		//printf("try a hyp %d / %d score %f ind %d on edge #%d\n", nhyp, count, rhyp->score, hyp->indices[0], rhyp->dec->edge->id);
		if(0 == instantiate_hypothesis(rhyp))
		{
			if(is_root(rhyp->dg))
			{
				struct mrs	*m = extract_mrs(rhyp->dg);
				//printf("\n"); print_hypothesis(rhyp, printf); printf("\n");
				count += process(rhyp, m);
				if(nbest && count>=nbest)break;
			}
			//else printf("... wasn't root after unpacking\n");
		}
		else g_checkup++;	// mark that we need to look for subfailures before hypothesizing
		if(noldslabs >= MAX_UNPACK_CHUNKS)break;
	}
	if(debug_level)printf("%d hyps / %d readings\n", nhyp, count);
	return count;
}
