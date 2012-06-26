#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

#include	"chart.h"
#include	"lexicon.h"

#define PACKBUG(fmt,...) do {} while(0)
//#define PACKBUG printf

extern int debug_level;
extern unsigned int	generation;

extern int *nptries;

int equivalence_packing_only = 0;

// to determine whether the active edge component of a task is built out of frozen goods
int	contains_frozen(struct edge	*e)
{
	int	i;
	if(e->frozen)return 1;
	for(i=0;i<e->have;i++)
		if(e->daughters[i]->frozen)return 1;
	return 0;
}

static int	same_cell_daughter(struct edge *par, struct edge *dtr)
{
	int i;

	if(par==dtr)return 1;
	if(g_mode==GENERATING && !bitvec_equal(par->ep_span_bv, dtr->ep_span_bv, n_epsbv))return 0;
	if(g_mode==PARSING && (par->from != dtr->from || par->to != dtr->to))return 0;
	for(i=0;i<par->have;i++)
		if(same_cell_daughter(par->daughters[i], dtr))return 1;
	return 0;
}

struct dg	*restrict_copy_dg(struct dg	*dg, int for_generation)
{
	struct dg *copy;

	enable_packing(for_generation?2:1);
	copy = copy_dg(dg);
	enable_packing(0);

	return copy;
}

int identical_orth_routes(struct edge *a, struct edge *b)
{
	struct orth_route *ra, *rb;
	int					i, n = a->north_routes;

	if(a->north_routes != b->north_routes)return 0;
	if(a->north_routes == -1)return 1;
	int orth_route_cmp(const struct orth_route *ra, const struct orth_route *rb)
	{
		int i;
		if(ra->len != rb->len)return ra->len-rb->len;
		return memcmp(ra->rules, rb->rules, sizeof(int)*ra->len);
	}
	// XXX should keep these sorted ahead of time
	qsort(a->orth_routes, n, sizeof(struct orth_route), (int(*)(const void*, const void*))orth_route_cmp);
	qsort(b->orth_routes, n, sizeof(struct orth_route), (int(*)(const void*, const void*))orth_route_cmp);
	for(i=0;i<n;i++)
		if(orth_route_cmp(a->orth_routes+i, b->orth_routes+i))
			return 0;
	return 1;
}

static void record_packing(struct edge *host, struct edge *e)
{
	host->npack++;
	host->pack = slab_realloc(host->pack, (host->npack-1)*sizeof(struct edge*), host->npack*sizeof(struct edge*));
	host->pack[host->npack-1] = e;
}

static long long packcount = 0, packtry = 0, packqc = 0, packss = 0;
#ifdef	COPY_COUNTS
extern long long copycount, copytraverse;
#endif

extern int gen_qc;

/*
int	upward_kill(struct edge	*edge, struct edge	*save)
{
	struct chart_cell	*c = cells + edge->from*chart_size + edge->to;
	int					i, j, k, killed = 1, pass;

	// what (I think) this is supposed to do:
	// FROST `edge', which means mark it as frozen but leave it in the chart
	// FREEZE all edges (packed or not) that are parents of `edge'
	//	which means remove it from the chart.
	//   since we are parsing each string length exhaustively before moving to longer constituents,
	//		only need to search in the same cell
	//   *ALSO*, when we freeze a parent, we must see if there are any X packed *in* that edge,
	//      and if there are, we must return those X to the agenda 
	// BUT, 'save' was the new host, so DON'T freeze it;
	//	this is non-NULL when we retroactively pack an edge into its parent
	edge->frozen = 1;
	PACKBUG("frost #%d\n", edge->id);
	// algorithm: anything with a frozen daughter gets frozen, repeat till convergence
	more: pass = 0;
	for(i=0;i<c->p_nedges;i++)
	{
		struct edge *E = c->p_edges[i];
		for(k=-1;k<E->npack;k++)
		{
			struct edge *e = (k==-1)?E:E->pack[k];
			if(e == save)continue;
			// for each edge, packed or not
			for(j=0;j<e->have;j++)
				if(e->daughters[j]->frozen == 1)
					break;
			if(j==e->have)continue;	// no frozen daughters
			// we have a frozen daughter
			// FREEZE
			PACKBUG("freeze #%d\n", e->id);
			e->frozen = 1;	// mark so future scans see this edge as frozen
			killed++;
			// remove `e' from the chart
			if(k==-1)
			{
				c->p_nedges--;
				memmove(c->p_edges+i, c->p_edges+i+1, sizeof(struct edge*) * (c->p_nedges - i));
				// new code august-2-2011;
				// when we kill an edge e because its descendent has been packed,
				// we are guaranteed that that edge will be regenerated (in more general form) off of the edge that the descendent packed into.
				// however, other edges packed on e will not be regenerated, so we have to be careful not to lose them.
				int l;
				for(l=0;l<e->npack;l++)
				{
					struct edge	*p = e->pack[l];
					if(!edge_descendent(edge, p))
					{
						// make sure everything built on `p' from the first time it entered the chart dies
						// since p is packed, there is nothing *in* the chart built on p,
						// but there may be stuff on the agenda built on p.
						// since we are putting p back on the agenda,
						// we can't leave a marker for all future edges built on p to be killed.
						// we have to do it now.
						kill_agenda(p);
						//printf("returning packed edge #%d to agenda on upward_kill\n", e->pack[l]->id);
						p->frozen = 0;
						p->frosted = 0;
						add_agenda(p);
					}
					else {} // 'p' would have been killed by a different pass of this function
				}
				goto more;
			}
			else
			{
				E->npack--;
				memmove(E->pack+k, E->pack+k+1, sizeof(struct edge*) * (E->npack - k));
				k--;
				continue;
			}
		}
	}
	if(pass)goto more;
	//printf("upward_kill'd %d edges built on #%d\n", killed, edge->id);
	return killed;
}*/

/*int	generalize_pack_edge(struct edge	*edge)
{
	struct chart_cell	*c = cells + edge->from*chart_size + edge->to;
	printf("thinking about how to pack #%d [%d passives in cell]\n", edge->id, c->p_nedges);
	if(c->p_nedges == 0)return 0;

	struct edge	*existing = c->p_edges[0];

	int	ssd, fw=1, bk=1;
	ssd = equivalent_dg(existing->dg, edge->dg, fw, bk);
	if(ssd == 1 || ssd == 3)
	{
		// existing susumes new edge
		record_packing(existing, edge);
		printf("#%d packs INTO existing #%d\n", edge->id, existing->id);
		return 1;
	}
	if(ssd == 2)
	{
		// new edge subsumes existing edge
		assert(!existing->frozen);
		record_packing(edge, existing);
		c->p_nedges = 0;
		printf("#%d SUBSUMES existing #%d (so replaces it in the chart)\n", edge->id, existing->id);
		return 0;
	}

	// no subsumption relation; invent a new more general edge!
	struct edge	*invent = slab_alloc(sizeof(struct edge));
	bzero(invent, sizeof(*invent));

	enable_packing(1);
	enable_trim(1);
	invent->dg = generalize(existing->dg, edge->dg);
	enable_packing(0);
	enable_trim(0);

	invent->from = edge->from;
	invent->to = edge->to;
	invent->id = next_eid();
	invent->have = edge->have;
	invent->need = edge->need;
	invent->qc = extract_qc(invent->dg);

	printf("invented a new generalized edge #%d [ subsumes #%d and #%d ]\n", invent->id, existing->id, edge->id);

	record_packing(invent, edge);
	record_packing(invent, existing);
	c->p_nedges = 0;
	add_agenda(invent);
	return 1;
}*/

block(struct edge	*e, int	freeze)
{
	if(!e->frozen || freeze)
	{
		if(freeze && !(e->frozen && !e->frosted))
			PACKBUG("FREEZING edge #%d; expect to pick it up later.\n", e->id);

		if(freeze) { e->frozen = 1; e->frosted = 0; }
		else { e->frozen = 1; e->frosted = 1; }
	}
	// for each of `e's parents:
	struct chart_cell	*c = cells + e->from*chart_size + e->to;
	int i, j, l;
	for(i=0;i<c->p_nedges;i++)
	{
		struct edge	*P = c->p_edges[i];
		for(l=-1;l<P->npack;l++)
		{
			struct edge	*p = (l==-1)?P:P->pack[l];
			for(j=0;j<p->have;j++)
				if(p->daughters[j] == e)
				{
					block(p, 1);
					break;
				}
		}
	}
}

int	pack_edge(struct edge	*edge)
{
	struct chart_cell	*c = cells + edge->from*chart_size + edge->to;
	struct edge			*h, **candidates;
	int			i, ssd, ncandidates;
	int			already_packed = 0;

	int	would_pack_as_ssd = 0;

#ifdef	COPY_COUNTS
	packcount++;
#endif

	PACKBUG("trying to pack #%d `%s' somewhere\n", edge->id, show_orth_leaves(edge));

	try_more:
	candidates = (edge->have==edge->need)?c->p_edges:(edge->rule->rtl?c->ar_edges:c->al_edges);
	ncandidates = (edge->have==edge->need)?c->p_nedges:(edge->rule->rtl?c->ar_nedges:c->al_nedges);
	for(i=0;i<ncandidates && (h=candidates[i]);i++)
		if( h->have == edge->have && h->need == edge->need
		&& (g_mode != GENERATING || bitvec_equal(h->ep_span_bv, edge->ep_span_bv, n_epsbv))
			)
		{
			int	fw = 1, bk = 1;
			if(edge == h)continue;	// if we retroactively packed an existing passive into 'edge', then 'edge' is now in the chart, so be careful
			// if nothing is subsumed by both rule schemata,
			// then refinements of them cannot be in subsumption relations
			if(edge->rule && h->rule)
				if(!check_srf(edge->rule, h->rule))continue;

			packtry++;
			if(!gen_qc && equiv_quickcheck(h->qc, edge->qc, &fw, &bk))continue;
			packqc++;

			PACKBUG("would like to pack #%d `%s' and #%d `%s'!\n", edge->id, show_orth_leaves(edge), h->id, show_orth_leaves(h));
			if(!identical_orth_routes(h, edge))continue;

			if(!edge->dg)
				if(recall_hyper(edge))
				{
					PACKBUG("whoops! edge was hyperpassive and vanished\n");
					return 0;
				}
			if(!h->dg)
				if(recall_hyper(h))
				{
					PACKBUG("whoops! host was hyperpassive and vanished\n");
					continue;
				}

#ifdef	RULE_STATS
			if(edge->ntries==0)
			{
				if(edge->rule)nptries[edge->rule->rnum]++;
				else nptries[nrules]++;
			}
			edge->ntries++;
			if(h->ntries==0)
			{
				if(h->rule)nptries[h->rule->rnum]++;
				else nptries[nrules]++;
			}
			h->ntries++;
#endif

			ssd = equivalent_dg(h->dg, edge->dg, fw, bk);
			if(equivalence_packing_only && ssd != 2)ssd = 0;
			if(ssd)packss++;
			if(ssd && h->lex && edge->lex)PACKBUG("packing lex `%s' and lex `%s'\n", edge->lex->word, h->lex->word);
			PACKBUG("ssd = %d ; h->frozen = %d\n", ssd, h->frozen);
			//if(!ssd)subsume_backtrace();
			//if(ssd == 1 || (ssd == 2 && !h->frozen))
			if((ssd == 1 || ssd == 2) && !h->frozen)
			{
				// forward packing
				// h subsumes edge
				if(same_cell_daughter(edge, h))
				{
					// h is a descendent of edge
					// packing would cause a loop in unpacking
					// could happen if a rule produces a result
					// that is compatible with but more specific than
					// a descendent with the same span.
					// sounds like poor grammar engineering, but conceivable.
					// with ERG-1004 on 'CSLI' at least, this never happens.

					// BUT it does happen with ERG-1010 on the following sentence:
					// A bunkhouse is a hostel or barracks-like building that historically was used to house working cowboys on ranches in North America.
					// hd_optcomp_c makes an infinite loop on [hostel or barracks- like]
					// yuck.
					if(ssd == 2)
					{
						// mother is completely equivalent to daughter.
						// that is a loop in the grammar.
						fprintf(stderr, "WARNING: grammar rule `%s' is loopy\n", edge->rule->name);
						fprintf(stderr, "WARNING: edge #%d [%s] == edge #%d [%s]\n", edge->id, edge->rule->name, h->id, h->rule->name);
						// pretend we packed it, but actually just discard it.
						return 1;
					}
					PACKBUG("blocked same-cell-daughter packing\n");
					continue;
				}
				PACKBUG("packed #%d `%s' in #%d `%s'!\n", edge->id, show_orth_leaves(edge), h->id, show_orth_leaves(h));
				//printf("host: "); print_dg(h->dg); printf("\n");
				//printf("pack: "); print_dg(edge->dg); printf("\n");
				//printf("EQUIV = %d\n", equivalent_dg(h->dg, edge->dg));
				record_packing(h, edge);
				return 1;
			}
			else if((ssd == -1 || ssd == 2) && 1)
			{
				int was_frozen = h->frozen;
				// retroactive packing
				// edge subsumes h
				if(same_cell_daughter(h, edge))
				{
					// edge is a descendent of h
					// packing would cause a loop in unpacking
					// how could this happen?
					// to build 'h', 'edge' would already have to be in the chart... 
					// this should never be reached.
					assert(!"not reached");
					//printf("blocked same-cell-daughter packing\n");
					continue;
				}
				if(!already_packed)
				{
					// replace 'h' by 'edge' in the chart before upward_kill() changes the chart around
					c->p_edges[i] = edge;
					PACKBUG("reverse packed #%d `%s' in #%d `%s'!\n", h->id, show_orth_leaves(h), edge->id, show_orth_leaves(edge));
					//printf("host: "); print_dg(edge->dg); printf("\n");
					//printf("pack: "); print_dg(h->dg); printf("\n");
					// promote what's packed on 'h' to be packed on 'edge'
					edge->npack = h->npack;
					edge->pack = h->pack;
				}
				else
				{
					// delete 'h' from the chart
					c->p_nedges--;
					ncandidates--;
					memmove(c->p_edges+i, c->p_edges+i+1, sizeof(struct edge*)*(c->p_nedges-i));
					i--;
					PACKBUG("claimed #%d also\n", h->id);
					if(h->npack)
					{
						// promote what's packed on 'h' to be packed on 'edge'
						edge->pack = memcpy(slab_alloc(sizeof(struct edge*)*(edge->npack+h->npack)), edge->pack, sizeof(struct edge*)*edge->npack);
						memcpy(edge->pack+edge->npack, h->pack, sizeof(struct edge*)*h->npack);
						edge->npack += h->npack;
					}
				}
				h->npack = 0;
				h->pack = 0;
				if(!h->frozen)
				{
					//printf(" ... packing it too\n");
					//h->frosted = 1;
					record_packing(edge, h);
				}
				already_packed = 1;
				if(h->built_on)
				{
					//printf("upkilling #%d to pack it in #%d\n", h->id, edge->id);
					block(h, 0);
				}
				h->built_on = 0;
				// keep going!
			}
		}
	if(already_packed)return 2;
	return 0;
}

void packing_report()
{
#ifdef	COPY_COUNTS
	fprintf(stderr, "NOTE: %lld packings (%.1f%% success), %lld / %lld = %.1f%% copies\n", packcount, 100*(float)packcount / packtry,
		copycount, copytraverse, 100*(float)copycount / copytraverse);
#endif
	if(debug_level)
	{
		fprintf(stderr, "NOTE: %lld subsumption tests; qc filters %.1f%% leaving %lld, of which ss passes %.1f%% = %lld\n",
			packtry, 100 - 100*(float)packqc/packtry, packqc, 100*(float)packss/packqc, packss);
	}
}
