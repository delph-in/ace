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
#include	"transfer.h"

//#define	DEBUG	printf
#define	DEBUG(fmt, ...)	do {} while(0)

//#define	MAX_UNPACK_CHUNKS	70000
#define	MAX_UNPACK_CHUNKS	7000
extern int noldslabs;

extern int debug_level, inhibit_pack;

unsigned long long	ndecomposed;

int	max_grandparenting = 3;

int	total_hyps;

struct context_unpack_info	*get_edge_cui(struct edge	*e, struct scoring_ancestry	ancestry);
struct hypothesis	*get_cui_hypothesis(struct context_unpack_info	*cui, int	rank);
void	set_cui_hypothesis(struct context_unpack_info	*cui, int	rank, struct hypothesis	*h);
int	cui_has_hypothesis(struct context_unpack_info	*cui, struct hypothesis	*hyp);

static inline char* edge_sign(struct edge *e)
{
	if(!e)return NULL;
	if(!e->have)return e->lex->lextype->name;
	else return e->rule->name;
}

void score_hypothesis(struct hypothesis *h)
{
	int i;
	h->score = 0;
	if(h->dec->edge->have)
	{
		char	*lhs_sign = edge_sign(h->dec->edge);
		char	*rhs0_sign = edge_sign(h->rhs[0]->dec->edge);
		char	*rhs1_sign = h->rhs[1]?edge_sign(h->rhs[1]->dec->edge):NULL;
		int		key_arg = h->dec->edge->rule->rtl;

		float	score = maxent_score(lhs_sign, rhs0_sign, rhs1_sign, key_arg, &h->ancestry);
		h->score += score;

		for(i=0;i<h->dec->edge->have;i++)
		{
			h->score += h->rhs[i]->score;
			/*if(!h->dec->edge->lex &&  h->rhs[i]->dec->edge->lex)
			{
				// syntax edge with lexical daughter.
				// this is the first place in the hypothesis tree where we
				// know for sure what the surface form under us is, so add the maxent score
				char surf[512];
				assert(h->rhs[i]->string);
				sprintf(surf, "\"%s\"", h->rhs[i]->string);
				//printf("should score surface `%s'\n", surf);
				// TODO think carefully about how to score this... what LHS, what ancestry?
				//h->score += maxent_score(h->rhs[i]->dec->edge->lex->lextype->name, surf, NULL, 0, &h->anc);
			}*/
		}
	}
	else
	{
		// 'h' is a leaf node
		// construct the cannonical surface form
		if(g_mode == GENERATING)return;	// FIXME: realization ranking doesn't have access to surface forms because we have no tokens...
		extern int enable_token_avms;
		if(!enable_token_avms)return;
		extern char	*current_sentence;
		char	surface[1024], *sp = surface;
		int	i;
		struct edge	*e = h->dec->edge;
		struct lexeme	*lex = e->lex;
		assert(lex);
		sp += sprintf(sp, "\"");
		for(i=0;i<lex->stemlen;i++)
		{
			struct token	*tok = (struct token*)e->daughters[i];
			if(strlen(tok->string) + (sp-surface) > 1000)return;
			if(tok->cfrom==-1 || tok->cto==-1)return;
			// if we wanted to preserve capitalization, we would do:
			//sp += sprintf(sp, "%s%.*s", i?" ":"", tok->cto-tok->cfrom, current_sentence + tok->cfrom);
			// since we (in general, except names?) don't, we do:
			sp += sprintf(sp, "%s%s", i?" ":"", tok->string);
		}
		sp += sprintf(sp, "\"");
		//printf("should score surface `%s'\n", surface);
		char	*lhs_sign = edge_sign(e);
		h->score = maxent_score(lhs_sign, surface, NULL, 0, &h->ancestry);
	}
}

struct decomposition *decompose_edge(struct edge	*edge, int *Ndecompositions)
{
	int	n = 1, i, j, orign;
	struct decomposition *dec, *d;

	ndecomposed++;
	if(edge->frozen && !edge->frosted)
	{
		// shouldn't get here because we delete frozen edges from the chart
		// and don't build on top of them
		fprintf(stderr, "ERROR: tried to decompose frozen edge #%d\n", edge->id);
		exit(-1);
	}

	for(i=0;i<edge->have;i++)
	{
		int	ngood = 1;
		struct edge	*d = edge->daughters[i];
		if(d->frozen && !d->frosted)
		{
			fprintf(stderr, "ERROR: tried to decompose edge with frozen daughter #%d\n", d->id);
			exit(-1);
		}
		// count non-frozen alternatives for this daughter
		for(j=0;j<d->npack;j++)
			if(!(d->pack[j]->frozen && !d->pack[j]->frosted))
				ngood++;
		n *= ngood + 1;
	}
	// this has been known to exceed the slab segment size:
	int	len = sizeof(struct decomposition) * n;
	extern int currslabsize;
	if(len >= currslabsize)
	{
		printf("NOTE: couldn't allocate %d decompositions, giving up\n",n);
		return NULL;
	}
	d = dec = slab_alloc(len);

	for(i=0;i<n;i++)
	{
		dec[i].edge = edge;
		dec[i].seeni1 = 0;
		dec[i].ni2 = 0;
		dec[i].ai2 = 0;
	}

	orign = n; n = 0;
	if(edge->have == 1)
	{
		for(i=-1;i<edge->daughters[0]->npack;i++)
		{
			struct edge *r1 = (i!=-1)?edge->daughters[0]->pack[i]:edge->daughters[0];
			if(r1->frozen && !r1->frosted)continue;
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
			if(r1->frozen && !r1->frosted)continue;
			// check rule filter to see if this particular local unpacking can be ruled out
			if(r1->rule && !check_rf(edge->rule, r1->rule, 0))continue;
			for(j=-1;j<edge->daughters[1]->npack;j++)
			{
				struct edge *r2 = (j!=-1)?edge->daughters[1]->pack[j]:edge->daughters[1];
				if(r2->frozen && !r2->frosted)continue;
				if(r2->rule && !check_rf(edge->rule, r2->rule, 1))continue;
				d->rhs[0] = r1;
				d->rhs[1] = r2;
				DEBUG("decomp #%d  %d = #%d = #%d		%p\n",
					edge->id, n, r1->id, r2->id, d);
				d++; n++;
			}
		}
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
		if(dec->ni2 >= dec->ai2)
		{
			int	oa = dec->ai2;
			dec->ai2 = (dec->ai2 + 1) * 1.5;
			dec->seeni1 = slab_realloc(dec->seeni1,
				sizeof(int) * oa, sizeof(int) * dec->ai2);
		}
		dec->seeni1[indices[1]] = 0;
	}
	else
	{
		assert(indices[0] <= 1+dec->seeni1[indices[1]]);
		if(dec->seeni1[indices[1]] < indices[0])
			dec->seeni1[indices[1]] = indices[0];
	}
}

print_heap(struct hypagenda_item *h, int indent)
{
	int i;
	if(!h){printf("(null)\n");return 0;}
	for(i=0;i<indent;i++)printf("  ");
	printf("hyp %p [edge #%d] score %f\n", h->h, h->h->dec->edge->id, h->h->score);
	if(h->left)print_heap(h->left, indent+1);
	if(h->right)print_heap(h->right, indent+1);
}

// insert 'h' into 'edge's agenda, a heap structure
void add_uagenda(struct hypagenda_item **A, struct hypagenda_item *newitem)
{
	struct hypagenda_item *w = *A, **W = A;

	while(w && w->h->score >= newitem->h->score)
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

	*W = newitem;
	if(w)add_uagenda(W, w);
}

void	add_hypothesis_to_agenda(struct hypagenda_item	**agenda, struct hypothesis	*h)
{
	struct hypagenda_item	*ha = slab_alloc(sizeof(*ha));
	ha->h = h;
	ha->left = NULL;
	ha->right = NULL;

	add_uagenda(agenda, ha);
}

struct hypothesis *new_hypothesis(struct decomposition	*dec, struct hypagenda_item **agenda, struct hypothesis *rhs[2], int indices[2], struct scoring_ancestry	ancestry)
{
	struct hypothesis	*h = slab_alloc(sizeof(struct hypothesis));
	int					i;

	DEBUG("hypothesize %p at dec %p [%d %d] for edge #%d\n", h, dec, indices[0], indices[1], dec->edge->id);
//	DEBUG(" ... ancestry: ");
//	for(i=0;i<ancestry.nparent_types;i++){ DEBUG(" %s", ancestry.parent_types[i]); }
//	DEBUG("\n");

	h->dec = dec;
	h->rhs[0] = rhs[0];
	h->rhs[1] = rhs[1];
	h->dg = NULL; h->string = NULL;
	h->checkup = 0;
	h->toklen = rhs[0]->toklen;
	if(rhs[1])h->toklen += rhs[1]->toklen;

	if(dec->edge->lex)
	{
		assert(dec->edge->have==1);	// lexical rules are unary
		if(dec->edge->rule->orth)
		{
			h->string = inflect(dec->edge->rule->orth, h->rhs[0]->string);
			if(!h->string)h->string = "(failed to inflect)";	// packing might make us try something silly here
		}
		else h->string = h->rhs[0]->string;
	}

	h->indices[0] = indices[0];
	h->indices[1] = indices[1];
	h->ancestry = ancestry;

	score_hypothesis(h);

	add_hypothesis_to_agenda(agenda, h);
	total_hyps++;
	return h;
}

struct hypothesis *leaf_hypothesis(struct edge *e, struct scoring_ancestry	ancestry)
{
	struct decomposition *dec = slab_alloc(sizeof(struct decomposition));
	struct hypothesis *hyp = slab_alloc(sizeof(struct hypothesis));
	int	i, slen = 0;
	assert(!e->have);
	dec->edge = e;
	dec->rhs[0] = dec->rhs[1] = NULL;
	dec->ni2 = dec->ai2 = 0;
	dec->seeni1 = NULL;
	hyp->dec = dec;
	hyp->toklen = e->lex->stemlen;

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
	hyp->dg = NULL;
	hyp->checkup = -1;
	hyp->rhs[0] = hyp->rhs[1] = NULL;
	hyp->ancestry = ancestry;
	hyp->indices[0] = hyp->indices[1] = 0;

	score_hypothesis(hyp);
	return hyp;
}

static int indent = 0;

static int g_checkup;
static inline void checkup(struct hypothesis *h)
{
	if(h->dg)return; // if we already know failure/success status, don't recurse
	if(h->checkup>=0 && h->checkup != g_checkup)
	{
		int i, arity = h->dec->edge->have;
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

int	compare_ancestry(struct scoring_ancestry	a, struct scoring_ancestry	b)
{
	if(a.nparent_types != b.nparent_types)return a.nparent_types - b.nparent_types;
	if(a.rooted != b.rooted)return a.rooted - b.rooted;
	int i, r;
	for(i=0;i<a.nparent_types;i++)
		if( (r=strcmp(a.parent_types[i], b.parent_types[i])) != 0)return r;
	return 0;
}

struct scoring_ancestry	compute_sub_ancestry(struct scoring_ancestry	panc, struct edge	*edge)
{
	struct scoring_ancestry	anc;
	anc.nparent_types = panc.nparent_types + 1;
	anc.rooted = panc.rooted;
	if(anc.nparent_types > max_grandparenting)
	{
		anc.nparent_types = max_grandparenting;
		anc.rooted = 0;
	}
	anc.parent_types = slab_alloc(sizeof(char*) * anc.nparent_types);
	int i;
	for(i=1;i<anc.nparent_types;i++)
		anc.parent_types[i] = panc.parent_types[i-1];
	assert(edge->rule != NULL);
	if(anc.nparent_types)
		anc.parent_types[0] = edge_sign(edge);
	return anc;
}

void	put_more_on_agenda(struct hypagenda_item	**A, struct decomposition	*dec, int	from_indices[], struct scoring_ancestry	ancestry)
{
	int i, j;
	int	arity = dec->edge->have;
	struct scoring_ancestry	sub_ancestry;
	sub_ancestry = compute_sub_ancestry(ancestry, dec->edge);

	// popped an item off the agenda, now tell the corresponding
	// decomposition that the relevant index tuple has been popped
	// so it will let us extend around it
	dec_add_indices(dec, from_indices);
	for(i=0;i<arity;i++)
	{
		int idx[2] = {from_indices[0], from_indices[1]};
		idx[i]++;
		if(!dec_check_indices(dec, idx))
		{
			struct hypothesis *rhs[2] = {NULL,NULL};
			// try to add a new agenda hypothesis for {dec,idx}
			for(j=1;j<indent;j++)DEBUG("  ");
			DEBUG("trying further hypothesize [%d %d] on dec %p of #%d\n", idx[0], idx[1], dec, dec->edge->id);

			int	status = 0;
			for(j=0;j<arity;j++)
			{
				rhs[j] = hypothesize_edge(dec->rhs[j], idx[j], sub_ancestry, NULL);
				if(!rhs[j]) { status = -1; break; }
				/*if(rhs[j]->dg==(void*)0x1)	// TODO find a way to make this actually work.
				{
					// XXX this solution doesn't really work.
					// we miss readings on CSLI item 629 if the "if" is there
					// we crash on the very next item if it isn't there.
					// simple solution: pretend this hypothesis succeeded,
					// and pretend we've already popped it; get yet more indices!
					if(i==j)
						put_more_on_agenda(A, dec, idx, ancestry);
					//put_more_on_agenda(A, dec, idx, ancestry);
					status = -2;
					break;
				}*/
			}
			if(status == 0)new_hypothesis(dec, A, rhs, idx, ancestry);
			else
			{
				for(j=0;j<indent;j++)DEBUG("  ");
				if(status == -1)
					DEBUG(" further hypothesize [%d %d] returned nothing on dec %p\n", idx[0], idx[1], dec);
				else
					DEBUG(" further hypothesize [%d %d] returned a failed instantiation record on dec %p\n", idx[0], idx[1], dec);
			}
		}
		else
		{
			for(j=1;j<indent;j++)DEBUG("  ");
			DEBUG("%p of #%d doesn't want [%d %d]\n", dec, dec->edge->id, idx[0], idx[1]);
		}
	}
}

struct hypothesis *process_hypagenda(struct hypagenda_item **A)
{
	struct hypothesis	*hyp;
	struct hypagenda_item	**W, *l, *r;
	int					arity, i, j;

	//printf("agenda:\n");
	//print_heap(*A, 1);

again:
	if(!*A)return NULL;

	// pop max from the agenda heap
	hyp = (*A)->h;

	for(l=(*A)->left,r=(*A)->right,W=A;l && r;)
	{
		struct hypagenda_item *tmp;
		if(l->h->score > r->h->score)
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

	// do the expansion around the popped hyp
	put_more_on_agenda(A, hyp->dec, hyp->indices, hyp->ancestry);

	checkup(hyp);
	if(hyp->dg == (void*)1)goto again;

	// 'hyp' is the next ranking item in the relevant edge's cache for this ancestry; record it.
	struct context_unpack_info	*cui = get_edge_cui(hyp->dec->edge, hyp->ancestry);
	if(!cui_has_hypothesis(cui, hyp))
		set_cui_hypothesis(cui, cui->nhypotheses, hyp);

	return hyp;
}

struct context_unpack_info	*get_edge_cui(struct edge	*e, struct scoring_ancestry	ancestry)
{
	if(!e->unpack)
	{
		e->unpack = slab_alloc(sizeof(struct unpack_info));
		bzero(e->unpack, sizeof(struct unpack_info));
	}

	int i;
	for(i=0;i<e->unpack->ncuis;i++)
		if(!compare_ancestry(ancestry, e->unpack->cuis[i]->ancestry))
			return e->unpack->cuis[i];

	e->unpack->cuis = slab_realloc(e->unpack->cuis, sizeof(struct context_unpack_info*)*e->unpack->ncuis, sizeof(struct context_unpack_info*)*(e->unpack->ncuis+1));
	struct context_unpack_info	*cui = slab_alloc(sizeof(*cui));
	cui->agenda = NULL;
	cui->nhypotheses = 0;
	cui->ahypotheses = 0;
	cui->nhb = 0;
	cui->hypotheses = NULL;
	cui->ancestry = ancestry;
	e->unpack->cuis[e->unpack->ncuis++] = cui;
	return cui;
}

//#define	HYPS_PER_BLOCK	1024

void	set_cui_hypothesis(struct context_unpack_info	*cui, int	rank, struct hypothesis	*h)
{
	if(cui->nhypotheses <= rank)
	{
		assert(cui->nhypotheses == rank);
		int	oah = cui->ahypotheses;
		cui->nhypotheses = rank+1;
		cui->ahypotheses = 2*(rank+1) - 1;
		//cui->nhb = cui->ahypotheses / HYPS_PER_BLOCK;
		cui->hypotheses = slab_realloc(cui->hypotheses, sizeof(struct hypothesis*)*(oah), sizeof(struct hypothesis*)*cui->ahypotheses);
	}
	cui->hypotheses[rank] = h;
}

struct hypothesis	*get_cui_hypothesis(struct context_unpack_info	*cui, int	rank)
{
	if(cui->nhypotheses <= rank)return NULL;
	return cui->hypotheses[rank];
}

int	cui_has_hypothesis(struct context_unpack_info	*cui, struct hypothesis	*hyp)
{
	int i;
	for(i=0;i<cui->nhypotheses;i++)
		if(hyp == cui->hypotheses[i])return 1;
	return 0;
}

struct hypothesis *hypothesize_edge(struct edge *e, int rank, struct scoring_ancestry	ancestry, struct hypagenda_item	**customAgenda)
{
	int					i, j;

	struct context_unpack_info	*cui = get_edge_cui(e, ancestry);

	for(i=0;i<indent;i++)DEBUG("  ");
	DEBUG("asked to hyp %d on edge #%d [%d-%d] (had seen %d)\n", rank, e->id, e->from, e->to, cui->nhypotheses);
	DEBUG(" ... cui %p ancestry: ", cui);
	for(i=0;i<ancestry.nparent_types;i++){ DEBUG(" %s", ancestry.parent_types[i]); }
	DEBUG("\n");

	struct hypothesis	*hyp = get_cui_hypothesis(cui, rank);
	if(hyp)
	{
		if(customAgenda)
		{
			// if customAgenda is set, we are supposed to put all the hypotheses that have been created at time of return onto it before returning
			DEBUG(" ... returning %d already-known hypotheses in customAgenda\n", cui->nhypotheses);
			int i;
			for(i=0;i<cui->nhypotheses;i++)
				add_hypothesis_to_agenda(customAgenda, cui->hypotheses[i]);
		}
		return hyp;
	}

	if(cui->nhypotheses != rank)
	{
		fprintf(stderr, "unpack(): asked for rank %d, but next is actually rank %d\n",
					rank, cui->nhypotheses);
	/*DEBUG("context of error was:\n");
	DEBUG("  ... asking hyp %d on edge #%d [%d-%d] (had seen %d)\n", rank, e->id, e->from, e->to, cui->nhypotheses);
	DEBUG(" ... ancestry: ");
	for(i=0;i<ancestry.nparent_types;i++){ DEBUG(" %s", ancestry.parent_types[i]); }
	DEBUG("\n");*/
		assert(0);
	}

	//DEBUG("asked to hyp %d on edge #%d\n", rank, e->id);

	if(!e->have)
	{
		if(rank != 0)return NULL;
		hyp = leaf_hypothesis(e, ancestry);
		set_cui_hypothesis(cui, 0, hyp);
		return hyp;
	}

	if(!cui->nhypotheses)
	{
		// nothing in the cache means we haven't decomposed this edge and put the initial hypotheses on the agenda yet
		int i, j, idx[2] = {0,0}, ndecs;
		struct decomposition *decs;
		// initialize agenda with (0,...,0) index vector for each decomposition
		decs = decompose_edge(e, &ndecs);
		if(!decs)return NULL;
		indent++;

		struct scoring_ancestry	sub_ancestry;
		sub_ancestry = compute_sub_ancestry(ancestry, e);

		for(i=0;i<ndecs;i++)
		{
			struct decomposition *dec = decs+i;
			struct hypothesis *rhs[2] = {NULL,NULL};
			for(j=0;j<e->have;j++)
			{
				rhs[j] = hypothesize_edge(dec->rhs[j], 0, sub_ancestry, NULL);
				if(!rhs[j])break;
			}
			if(j<e->have)continue;	// failed to hypothesize even the 0'th unpacking of this decomposition
			struct hypothesis	*h = new_hypothesis(dec, &cui->agenda, rhs, idx, ancestry);
			if(customAgenda)add_hypothesis_to_agenda(customAgenda, h);
		}
		indent--;
	}

	if(customAgenda)return NULL;

	indent++;
	hyp = process_hypagenda(&cui->agenda);
	indent--;
	if(!hyp)return NULL;

	//set_cui_hypothesis(cui, rank, hyp);
	return hyp;
}

unsigned long long total_reconstructed_nodes;

int instantiate_hypothesis(struct hypothesis *h)
{
	int arity = h->dec->edge->have, i;

	// don't try to re-instantiate when we've already been here!
	if(h->dg)
	{
		if(h->dg == (void*)1)
		{
		return -1;
			assert(!"Not reached");
			return -1;
		}
		else return 0;
	}

	DEBUG("inst edge #%d rule %s     = dec %p\n", h->dec->edge->id, h->dec->edge->rule?h->dec->edge->rule->name:NULL, h->dec);
	for(i=0;i<arity;i++)
		if(0 != instantiate_hypothesis(h->rhs[i]))
		{
			h->dg = (void*)1;
			DEBUG("edge #%d fail on daughter %d\n", h->dec->edge->id, i);
			return -1;
		}
	if(!h->string)
	{
		// combine the strings of daughters
		int slen = 0, i;
		for(i=0;i<arity;i++)slen += strlen(h->rhs[i]->string) + 1;
		if(slen%4)slen = (slen&~3)+4;
		h->string = slab_alloc(slen); h->string[0] = 0;
		for(i=0;i<arity;i++)
		{
			if(i)strcat(h->string, " ");
			strcat(h->string, h->rhs[i]->string);
		}
	}

	if(g_mode == GENERATING || !inhibit_pack)
	{
		if(!arity)
		{
			if(g_mode == GENERATING)
			{
				// generator lexeme.  don't put the specialized RELS list in.
				h->dg = lexical_dg(h->dec->edge->lex, 0);
			}
			else h->dg = h->dec->edge->dg;
		}
		else
		{
			total_reconstructed_nodes++;
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
				//printf("cycle\n");
				return -1;
			}
		}
	}
	else
	{
		// no need to unspecialize or reconstruct; we already have a full dag.
		h->dg = h->dec->edge->dg;
	}

	return 0;
}

char	*slab_escape(char	*str)
{
	int	len = strlen(str) * 2 + 1;
	len -= len%4; len += 4;
	char	*esc = slab_alloc(len), *ep = esc;
	while(*str)
	{
		if(*str=='\\' || *str=='"')
			*ep++ = '\\';
		*ep++ = *str++;
	}
	*ep = 0;
	return esc;
}

struct dstr_cache
{
	char	*str;
	struct hypothesis	*h;
	int					o;
	int		gen;
};

#define	DSTR_CACHE_SIZE	102397
static struct dstr_cache	dstr_cache[DSTR_CACHE_SIZE];
int		dstr_cache_gen = 1;

#define	DSTR_HASH(h,o)	(int)((((unsigned long long)h)^o) % (DSTR_CACHE_SIZE))

char	*dstr_lookup(struct hypothesis	*h, int	o)
{
	int	hc = DSTR_HASH(h,o);
	if(dstr_cache[hc].h == h && dstr_cache[hc].o == o && dstr_cache[hc].gen == dstr_cache_gen)
		return dstr_cache[hc].str;
	return NULL;
}

void	dstr_save(struct hypothesis	*h, int	o, char	*str)
{
	int	hc = DSTR_HASH(h,o);
	dstr_cache[hc].h = h;
	dstr_cache[hc].o = o;
	dstr_cache[hc].str = str;
	dstr_cache[hc].gen = dstr_cache_gen;
}

void print_hypothesis(struct hypothesis *hyp, int (*print)(const char *fmt,...))
{
	void	print_tree0(struct hypothesis *hyp, int from)
	{
		int i;
		struct edge *e = hyp->dec->edge;

		// print out the edge, with span information
		char	*sign = "no-sign";
		if(e->rule)sign = e->rule->name;
		else if(e->lex)
		{
			sign = e->lex->word;
			//sign = e->dg->type->name;
		}
		print(" (%d %s %f %d %d", e->id, sign, hyp->score, from, from+hyp->toklen);
		if(e->have)
		{
			for(i=0;i<e->have;i++)
			{
				print_tree0(hyp->rhs[i], from);
				from += hyp->rhs[i]->toklen;
			}
		}
		else if(e->lex)
		{
			if(g_mode == PARSING)
			{
				extern int enable_token_avms;
				if(enable_token_avms)
				{
					int	ntokens = e->lex->stemlen;
					print(" (\"");
					for(i=0;i<ntokens;i++)
					{
						struct token	*tok = (struct token*)e->daughters[i];
						char	*form = tok->string;
						if(i>0)print(" ");
						print("%s", slab_escape(form));
					}
					print("\"");

					for(i=0;i<ntokens;i++)
					{
						struct token	*tok = (struct token*)e->daughters[i];
						int	id = ((unsigned long long)tok)%713;	// XXX ok, it's a hack, but who really cares about the token ID anyway?
						char	*tokstr = dstr_lookup((void*)tok, from);
						if(!tokstr)
						{
							char	token_str[10240];
							extern int dag_print_style;
							dag_print_style = 1;
							snprint_dg(token_str, 10239, tok->dg);
							// *token_str = 0;
							dag_print_style = 0;
							tokstr = slab_escape(token_str);
							dstr_save((void*)tok, from, tokstr);
						}
						print(" %d \"%s\"", id, tokstr);
					}
					print(")");
				}
				else
				{
					print(" (\"?\")");
				}
			}
			else if(g_mode == GENERATING)
			{
				print(" (\"-\" 0 \"-\")");
			}
		}
		print(")");
	}
	static int timer = -1;
	if(timer==-1)timer = new_timer("derivation printing");
	start_timer(timer);
	//struct slab_state	st;
	//get_slabstate(&st);	// the slab_escape() nonsense for escaping token structures shouldn't stay with us forever!
	// actually, it should since we're caching them now
	print_tree0(hyp, 0);
	//set_slabstate(&st);
	stop_timer(timer, 1);
}

char	*hypothesis_to_dstring(struct hypothesis	*h)
{
	char	*derivation = malloc(10240);
	int		dalen = 10240, dlen = 0;
	*derivation = 0;
	int	dprintf(const char	*fmt, ...)
	{
		va_list	v;
		va_start(v, fmt);
		int	l = vsnprintf(derivation+dlen, dalen-dlen, fmt, v);
		va_end(v);
		if(l >= dalen-dlen)
		{
			va_list v2;
			va_start(v2, fmt);
			dalen += l + 10240 ;
			derivation = realloc(derivation, dalen);
			l = vsnprintf(derivation+dlen, dalen-dlen, fmt, v2);
			va_end(v2);
		}
		dlen += l;
		return l;
	}
	print_hypothesis(h, dprintf);
	return derivation;
}

void	fixup_generator_string(struct hypothesis	*h)
{
	char	*p = h->string;
	while(p && *p && !isalpha(*p))p++;
	if(p && *p)
	{
		*p = toupper(*p);
	}
}

int want_mrs = 1;

int iterate_cell_root_hypotheses(struct chart_cell *cell, int (*process)(struct hypothesis *hyp, struct mrs *mrs), int nbest)
{
	int	i, j, count = 0, nhyp = 0;
	struct hypagenda_item	*agenda = NULL;
	struct hypothesis		*hyp;
	struct edge			*edge;

	//if(!nbest)return exhaustive_unpack(cell, process);

	static struct scoring_ancestry	root_ancestry = {.nparent_types=0,.parent_types=NULL, .rooted = 1};

	dstr_cache_gen++;

	for(i=0;i<cell->p_nedges;i++)
	{
		edge = cell->p_edges[i];
		if(edge->frozen)
		{
			fprintf(stderr, "ERROR: found a frozen edge #%d (not packed) in the root cell!\n", edge->id);
			exit(-1);
		}
		if( (g_mode!=GENERATING || edge->ep_span == require_ep_span) && is_root(edge->dg) )// && !edge->frozen )
		{
			DEBUG("edge #%d can be root...\n", edge->id);
			for(j=-1;j<edge->npack;j++)
			{
				// for each potentially root packed edge...
				struct edge *e = (j==-1)?edge:edge->pack[j];
				if(e->frozen && !e->frosted)continue;
				if(!is_root(e->dg))continue;
				hypothesize_edge(e, 0, root_ancestry, &agenda);
			}
		}
	}

	static int	hyp_timer = -1, inst_timer = -1, mrs_timer = -1;
	int	ninsthyps = 0;
	if(hyp_timer==-1)hyp_timer = new_timer("hypothesize");
	if(inst_timer==-1)inst_timer = new_timer("instantiate");
	if(mrs_timer==-1)mrs_timer = new_timer("mrs");
	while( !(nbest && count>=nbest) )
	{
		start_timer(hyp_timer);
		hyp = process_hypagenda(&agenda);
		stop_timer(hyp_timer, 1);
		if(!hyp)break;
		nhyp++;
		if(hyp->dec->edge->have == 2)
			DEBUG("try a hyp %p score %f ind %d %d on decomp %p [#%d,#%d] of edge #%d\n", hyp, hyp->score, hyp->indices[0], hyp->indices[1], hyp->dec, hyp->dec->rhs[0]->id, hyp->dec->rhs[1]->id, hyp->dec->edge->id);
		else DEBUG("try a hyp %p score %f ind %d on decomp %p [#%d] of edge #%d\n", hyp, hyp->score, hyp->indices[0], hyp->dec, hyp->dec->rhs[0]->id, hyp->dec->edge->id);
		start_timer(inst_timer);
		int	ires = instantiate_hypothesis(hyp);
		stop_timer(inst_timer, 1);
		DEBUG("instantiation result = %d\n", ires);
		//if(hyp->dec->edge->id == 11329)
		//	DEBUG("instantiation of hypothesis from edge 11329: res = %d\n", ires);
		if(0 == ires)
		{
			ninsthyps++;
		//	if(hyp->dec->edge->id == 11329)
		//		DEBUG("... testing an instantiated hypothesis from edge 11329\n");
			if(is_root(hyp->dg))
			{
				struct mrs	*m = NULL;
				if(want_mrs)
				{
					start_timer(mrs_timer);
					m = extract_mrs(hyp->dg);
					stop_timer(mrs_timer, 1);
				}
				if(check_idioms(hyp->dg, m))
				{
					//printf("hyp = %p\n", hyp);
					//printf("\n"); print_hypothesis(h, printf); printf("\n");
					if(g_mode == GENERATING)
						fixup_generator_string(hyp);
					count += process(hyp, m);
				}
			}
			else DEBUG("... wasn't root after unpacking\n");
		}
		else g_checkup++;	// mark that we need to look for subfailures before hypothesizing
		if(noldslabs >= MAX_UNPACK_CHUNKS)
		{
			printf("NOTE: hit RAM limit while unpacking\n");
			break;
		}
	}
	if(debug_level)
	{
		printf("%d hyps / %d reconstructed / %d readings\n", nhyp, ninsthyps, count);
	}
	/*for(i=0;i<chart_size;i++)
	{
		for(j=i+2;j<chart_size;j++)
		{
			struct chart_cell	*c = cells + i*chart_size + j;
			int k;
			for(k=0;k<c->p_nedges;k++)
			{
				struct edge	*E = c->p_edges[k];
				int l;
				for(l=-1;l<E->npack;l++)
				{
					struct edge	*e = (l==-1)?E:E->pack[l];
					struct unpack_info	*u = e->unpack;
					if(u)
					{
						int m;
						for(m=0;m<u->ncuis;m++)
						{
							int o, ns = 0;
							for(o=0;o<u->cuis[m]->nhypotheses;o++)
							{
								int	ires = instantiate_hypothesis(u->cuis[m]->hypotheses[o]);
								if(ires == 0)ns++;
							}
							printf("edge #%d cui %d had %d hypotheses ; %d instantiate\n", e->id, m, u->cuis[m]->nhypotheses, ns);
						}
					}
				}
			}
		}
	}*/
	return count;
}

report_unpacking()
{
	if(!debug_level)return;
	fprintf(stderr, "%d total hypotheses generated\n", total_hyps);
	fprintf(stderr, "%lld total nodes reconstructed\n", total_reconstructed_nodes);
}
