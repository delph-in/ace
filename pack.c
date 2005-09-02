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

int identical_inflr_routes(struct edge *a, struct edge *b)
{
	struct inflr_route *ra, *rb;
	int					i, n = a->ninflr_routes;

	if(a->ninflr_routes != b->ninflr_routes)return 0;
	if(a->ninflr_routes == -1)return 1;
	int inflr_route_cmp(const struct inflr_route *ra, const struct inflr_route *rb)
	{
		int i;
		if(ra->len != rb->len)return ra->len-rb->len;
		return memcmp(ra->rules, rb->rules, sizeof(int)*ra->len);
	}
	// XXX should keep these sorted ahead of time
	qsort(a->inflr_routes, n, sizeof(struct inflr_route), (int(*)(const void*, const void*))inflr_route_cmp);
	qsort(b->inflr_routes, n, sizeof(struct inflr_route), (int(*)(const void*, const void*))inflr_route_cmp);
	for(i=0;i<n;i++)
		if(inflr_route_cmp(a->inflr_routes+i, b->inflr_routes+i))
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

int	pack_edge(struct edge	*edge)
{
	struct chart_cell	*c = cells + edge->from*chart_size + edge->to;
	struct edge			*h, **candidates = (edge->have==edge->need)?c->p_edges:(edge->rule->rtl?c->ar_edges:c->al_edges);
	int			i, ssd, ncandidates = (edge->have==edge->need)?c->p_nedges:(edge->rule->rtl?c->ar_nedges:c->al_nedges);

#ifdef	COPY_COUNTS
	packcount++;
#endif

	PACKBUG("trying to pack #%d `%s' somewhere\n", edge->id, show_orth_leaves(edge));

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
			if(!identical_inflr_routes(h, edge))continue;

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
#ifdef	BIDIR_SS
			else if(ssd == -1 && h->age == edge->age && !h->built_on)	// XXX could kill built_on requirement and do upwards frosting?
			{
				if(same_cell_daughter(h, edge))
				{
					//printf("blocked same-cell-daughter packing\n");
					continue;
				}
				PACKBUG("reverse packed #%d `%s' in #%d `%s'!\n", h->id, show_orth_leaves(h), edge->id, show_orth_leaves(edge));
				//printf("host: "); print_dg(edge->dg); printf("\n");
				//printf("pack: "); print_dg(h->dg); printf("\n");
				edge->npack = h->npack;
				edge->pack = h->pack;
				h->npack = 0;
				h->pack = 0;
				record_packing(edge, h);
				c->p_edges[i] = edge;
				return 2;	// tell caller the edge is in the chart but requires further processing
			}
			/*else if(ssd == -1)printf("would have reverse packed but ages differ\n");
			else
			{
				printf(" ... no subsumption relation\n");
				printf("host: "); print_dg(h->dg); printf("\n");
				printf("edge: "); print_dg(edge->dg); printf("\n");
			}*/
#endif
		}
	return 0;
}

void packing_report()
{
#ifdef	COPY_COUNTS
	fprintf(stderr, "NOTE: %lld packings (%.1f%% success), %lld / %lld = %.1f%% copies\n", packcount, 100*(float)packcount / packtry,
		copycount, copytraverse, 100*(float)copycount / copytraverse);
#endif
}
