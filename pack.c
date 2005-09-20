#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

#include	"chart.h"
#include	"lexicon.h"

#define PACKBUG(fmt,...) do {} while(0)
//#define PACKBUG printf

static int	same_cell_daughter(struct edge *par, struct edge *dtr)
{
	int i;

	if(par==dtr)return 1;
	if(g_mode==GENERATING && (par->ep_span != dtr->ep_span))return 0;
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

#ifdef	COPY_COUNTS
static long long packcount = 0, packtry = 0;
extern long long copycount, copytraverse;
#endif

extern int gen_qc;

int	upward_kill(struct edge	*edge)
{
	struct chart_cell	*c = cells + edge->from*chart_size + edge->to;
	int					i, j, k, killed = 1, pass;

	edge->qc = 0;
	more: pass = 0;
	for(i=0;i<c->p_nedges;i++)
	{
		struct edge *E = c->p_edges[i];
		for(k=-1;k<E->npack;k++)
		{
			struct edge *e = (k==-1)?E:E->pack[k];
			if(e->qc==0)continue;
			for(j=0;j<e->have;j++)
				if(e->daughters[j]->qc == 0)
				{
					if(k==-1)
					{
						//printf("UK #%d  since daughter[%d] = #%d\n", e->id, j, e->daughters[j]->id);
						c->p_nedges--;
						memmove(c->p_edges+i, c->p_edges+i+1, sizeof(struct edge*)*(c->p_nedges-i));
						i--;
					}
					else
					{
						//printf("UKP #%d since daughter[%d] = #%d\n", e->id, j, e->daughters[j]->id);
						E->npack--;
						memmove(E->pack+k, E->pack+k+1, sizeof(struct edge*)*(E->npack-k));
						k--;
					}
					e->qc = 0;
					killed++;
					if(k==-1)pass = 1;	// we require that packed edges are not built on
				}
		}
	}
	if(pass)goto more;
	//printf("upward_kill'd %d edges built on #%d\n", killed, edge->id);
	return killed;
}

int	pack_edge(struct edge	*edge)
{
	struct chart_cell	*c = cells + edge->from*chart_size + edge->to;
	struct edge			*h, **candidates = (edge->have==edge->need)?c->p_edges:(edge->rule->rtl?c->ar_edges:c->al_edges);
	int			i, ssd, ncandidates = (edge->have==edge->need)?c->p_nedges:(edge->rule->rtl?c->ar_nedges:c->al_nedges);
	int			already_packed = 0;

#ifdef	COPY_COUNTS
	packcount++;
#endif

	PACKBUG("trying to pack #%d `%s' somewhere\n", edge->id, show_orth_leaves(edge));

	try_more:
	for(i=0;i<ncandidates && (h=candidates[i]);i++)
		if( h->have == edge->have && h->need == edge->need
		&& (g_mode != GENERATING || h->ep_span == edge->ep_span)
		&& (gen_qc || !equiv_quickcheck(h->qc, edge->qc))
			)
		{
#ifdef	COPY_COUNTS
			packtry++;
#endif

			PACKBUG("would like to pack #%d `%s' and #%d `%s'!\n", edge->id, show_orth_leaves(edge), h->id, show_orth_leaves(h));
			if(!identical_orth_routes(h, edge))continue;

			if(!edge->dg)
				if(recall_hyper(edge))return 0;
			if(!h->dg)
				if(recall_hyper(h))continue;

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

			ssd = equivalent_dg(h->dg, edge->dg);
			if(ssd && h->lex && edge->lex)PACKBUG("packing lex `%s' and lex `%s'\n", edge->lex->word, h->lex->word);
			if(ssd == 1)
			{
				if(same_cell_daughter(edge, h))
				{
					//printf("blocked same-cell-daughter packing\n");
					continue;
				}
				PACKBUG("packed #%d `%s' in #%d `%s'!\n", edge->id, show_orth_leaves(edge), h->id, show_orth_leaves(h));
				//printf("host: "); print_dg(h->dg); printf("\n");
				//printf("pack: "); print_dg(edge->dg); printf("\n");
				//printf("EQUIV = %d\n", equivalent_dg(h->dg, edge->dg));
				record_packing(h, edge);
				return 1;
			}
			else if(ssd == -1)
			{
				if(same_cell_daughter(h, edge))
				{
					//printf("blocked same-cell-daughter packing\n");
					continue;
				}
				if(!already_packed)
					c->p_edges[i] = edge;	// replace 'h' by 'edge' in the chart before upward_kill() changes the chart around
				else
				{
					// delete 'h' from the chart
					c->p_nedges--;
					memmove(c->p_edges+i, c->p_edges+i+1, sizeof(struct edge*)*(c->p_nedges-i));
				}
				if(h->built_on)
				{
					//printf("upkilling #%d to pack it in #%d\n", h->id, edge->id);
					upward_kill(h);
				}
				h->built_on = 0;
				if(!already_packed)
				{
					PACKBUG("reverse packed #%d `%s' in #%d `%s'!\n", h->id, show_orth_leaves(h), edge->id, show_orth_leaves(edge));
					//printf("host: "); print_dg(edge->dg); printf("\n");
					//printf("pack: "); print_dg(h->dg); printf("\n");
					edge->npack = h->npack;
					edge->pack = h->pack;
				}
				else
				{
					//printf("claimed #%d also\n", h->id);
					if(h->npack)
					{
						edge->pack = memcpy(slab_alloc(sizeof(struct edge*)*(edge->npack+h->npack)), edge->pack, sizeof(struct edge*)*edge->npack);
						memcpy(edge->pack+edge->npack, h->pack, sizeof(struct edge*)*h->npack);
						edge->npack += h->npack;
					}
				}
				h->npack = 0;
				h->pack = 0;
				record_packing(edge, h);
				already_packed = 1;
				candidates = c->p_edges;
				ncandidates = c->p_nedges;
				//goto try_more;	// XXX for some reason this has a chance of killing edge->qc on the next pass
				return 2;	// tell caller the edge is in the chart but requires further processing
			}
			/*else
			{
				printf(" ... no subsumption relation\n");
				printf("host: "); print_dg(h->dg); printf("\n");
				printf("edge: "); print_dg(edge->dg); printf("\n");
			}*/
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
}
