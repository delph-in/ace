#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<assert.h>
#include	<sys/time.h>

#include	"dag.h"
#include "mrs.h"
#include "lexicon.h"
#include "chart.h"
#include "unpack.h"

extern int	question, exhaustive, lui_mode, inhibit_pack, trace, debug_level, inhibit_results, do_itsdb, best_only;

		extern int nstrings;
		extern struct type	**strings;

int	require_neps = 0, n_epsbv = 0, n_avsbv = 0;

int			enable_index_accessibility_filtering = 0;

void collect_vars(struct dg	*dg, unsigned long long	*bv)
{
	int	i;
	dg = p_dereference_dg(dg);
	struct dg	**arcs = DARCS(dg);
	static int instloc = -1;
	if(instloc ==-1)instloc = lookup_fname("INSTLOC");

	for(i=0;i<dg->narcs;i++)
	{
		if(dg->label[i] == instloc)
		{
			struct type	*t = arcs[i]->xtype;
			if(t->name[0]=='"')
			{
				int	tnum = t->tnum;
				assert(tnum < n_avsbv*64);
				bitvec_set(bv, tnum);
			}
			return;
		}
		else collect_vars(arcs[i], bv);
	}
}

void accessible_vars(struct edge	*e, unsigned long long	*bv)
{
	// look at SYNSEM.LKEYS
//	static int	save[3] = {-1,-1,-1};
//	struct dg	*lkeys = walk_dg(e->dg, save, "SYNSEM", "LKEYS", NULL);

//	printf("lkeys:\n");
//	print_dg(lkeys);
//	printf("\n");

	//assert(lkeys != NULL);
	//return collect_vars(lkeys);
	collect_vars(e->dg, bv);
}

struct mrs	*current_input_mrs;

int	index_accessibility_filter_one_var(struct edge	*e, struct mrs_var	*v)
{
	return bitvec_test(e->inaccessible_vars_bv, v->vnum);
	//return e->inaccessible_vars & (((unsigned long long)1)<<v->vnum);
}

int index_accessibility_filter_one_ep(struct edge	*e, struct mrs_ep	*ep)
{
	int i;
	if(index_accessibility_filter_one_var(e, ep->lbl))return 1;
	for(i=0;i<ep->nargs;i++)
		if(index_accessibility_filter_one_var(e, ep->args[i].value))return 1;
	return 0;
}

int	index_accessibility_filter(struct edge	*e)
{
	int	i;
	for(i=0;i<current_input_mrs->nrels;i++)
		if(!bitvec_test(e->ep_span_bv, i))
			if(index_accessibility_filter_one_ep(e, current_input_mrs->rels+i))
				return 1;
	return 0;
}

// chart

int	gen_process_edge(struct edge	*e)
{
	struct chart_cell	*c = cells;
	int	i, pack = 0;

	e->accessible_vars_bv = bitvec_slab_alloc(n_avsbv);
	bitvec_clear_all(e->accessible_vars_bv, n_avsbv);
	e->inaccessible_vars_bv = bitvec_slab_alloc(n_avsbv);
	bitvec_clear_all(e->inaccessible_vars_bv, n_avsbv);
	unsigned long long	*temp_bv = bitvec_slab_alloc(n_avsbv);
	bitvec_clear_all(temp_bv, n_avsbv);

	unsigned long long	*previously_accessible = temp_bv;					// use as scratch space
	unsigned long long	*previously_inaccessible = e->inaccessible_vars_bv;	// use as scratch space

	for(i=0;i<e->have;i++)
	{
		if(e->daughters[i]->frozen==1)
		{
			//printf("not processing edge #%d ; daughter #%d is frozen\n", e->id, e->daughters[i]->id);
			return 0;
		}
		bitvec_or(e->daughters[i]->accessible_vars_bv, previously_accessible, previously_accessible, n_avsbv);
		bitvec_or(e->daughters[i]->inaccessible_vars_bv, previously_inaccessible, previously_inaccessible, n_avsbv);
	}

	if(e->have == e->need && enable_index_accessibility_filtering)
	{
		// compute the set of variables actually accessible on 'e'
		accessible_vars(e, e->accessible_vars_bv);
		// inaccessible variables are ones that were once accessible but no longer are
		// newly inaccessible variables are ones that were accessible on a daughter but are not accessible on 'e'
		// variables that were inaccessible on a daughter are still inaccessible on 'e'
		// inaccessible = previously_inaccessible | (previously accessible & ~currently accessible)
		unsigned long long	*newly_inaccessible = temp_bv;
		bitvec_andnot(previously_accessible, e->accessible_vars_bv, newly_inaccessible, n_avsbv);
		bitvec_or(previously_inaccessible, newly_inaccessible, e->inaccessible_vars_bv, n_avsbv);
		if(index_accessibility_filter(e))
		{
			//printf("filtered.\n");
			return 0;
		}
	}

	if(!inhibit_pack && e->have == e->need)
	{
		pack = pack_edge(e);
		if(pack==1)
			return 0;	// big savings! woohoo!
	}
	if(pack==0)add_edge_to_chart(e);

	// try to combine against any other edge with disjoint ep_span
	if(e->have == e->need)
	{
		// passive edge
		for(i=0;i<c->al_nedges;i++)
			if(!bitvec_and(e->ep_span_bv, c->al_edges[i]->ep_span_bv, NULL, n_epsbv))
				if(combine(c->al_edges[i], e) == -1)return -1;
		for(i=0;i<c->ar_nedges;i++)
			if(!bitvec_and(e->ep_span_bv, c->ar_edges[i]->ep_span_bv, NULL, n_epsbv))
				if(combine(c->ar_edges[i], e) == -1)return -1;
	}
	else
	{
		// active edge
		for(i=0;i<c->p_nedges;i++)
			if(!bitvec_and(e->ep_span_bv, c->p_edges[i]->ep_span_bv, NULL, n_epsbv))
				if(combine(e, c->p_edges[i]) == -1)return -1;
	}
	return 0;
}

float timer()
{
	float ret;
	static struct timeval tvstart = {0,0}, tv;
	if(!tvstart.tv_sec)gettimeofday(&tvstart, 0);
	gettimeofday(&tv, 0);
	ret = tv.tv_sec - tvstart.tv_sec + 0.000001 * (tv.tv_usec - tvstart.tv_usec);
	tvstart = tv;
	return ret;
}

// driver

int	generate(struct mrs	*mrs)
{
	extern struct edge *sentential;
	struct edge	*edge;
	char		*str;
	int			i, count = 0;

	current_input_mrs = mrs;

	timer();
	reset_edges();
	clear_chart(1);

	if(debug_level)
	{
		printf("trying to generate from MRS:\n");
		print_mrs(mrs); printf("\n");
	}

	fprintf(stderr, "NOTE: input semantics had %d EPs and %d variables\n", mrs->nrels, mrs->vlist.nvars);

	require_neps = mrs->nrels;
	n_epsbv = bitvec_n(mrs->nrels);
	n_avsbv = bitvec_n(mrs->vlist.nvars);

	// set up the skolemization and specialization dags we'll need.
	struct dg	*skar[mrs->vlist.nvars];
	for(i=0;i<mrs->vlist.nvars;i++)
	{
		struct dg			*skolem;
		skolem = atomic_dg("*top*");
		assert(i<nstrings);
		skolem->type = skolem->xtype = strings[i];
		skar[i] = skolem;
	}
	for(i=0;i<mrs->nhcons;i++)
		skar[mrs->hcons[i].hi->vnum] = skar[mrs->hcons[i].lo->vnum];
	for(i=0;i<mrs->vlist.nvars;i++)
	{
		dagify_mrs_var(mrs->vlist.vars[i], skar[i]);
		//printf("dag for %s: \n", mrs->vlist.vars[i]->name);
		//print_dg(mrs->vlist.vars[i]->dg);
		//printf("\n");
	}
	for(i=0;i<mrs->nrels;i++)
		dagify_mrs_ep(mrs->rels+i);
	if(debug_level)printf("mrs-to-dg %.4f\n", timer());

	// set up some lexemes and rules for the generator to play with
	semantic_lookup(mrs);
	if(debug_level)printf("semlook %.4f\n", timer());

	int	npassive_edges = 0;
	sentential = 0;
	while( (edge = next_agenda(0)) )
	{
		if(edge->have == edge->need)npassive_edges++;
		//printf("... process %p = #%d  ; %d EPs\n", edge, edge->id, edge->neps);
		if(gen_process_edge(edge)){/*printf("terminated!\n");break;*/}
	}
	if(debug_level)printf("packed %.4f\n", timer());

	int show_result(struct hypothesis	*hyp, struct mrs	*mrs_res)
	{
		if(!mrs_subsumes(mrs, mrs_res))
		{
			if(trace){ printf("NICE TRY! `%s' has incompatible semantics:\n", hyp->string);
			print_mrs(mrs_res); printf("\n"); }
			free_mrs(mrs_res);
			return 0;
		}
		if(trace>0)printf("edge #%d has a correct unpacking\n", hyp->edge->id);
		if(trace>1){print_dg(hyp->dg); printf("\n");}
		if(!inhibit_results)printf("%s\n", hyp->string);
		if(do_itsdb)itsdb_report_genresult(hyp, mrs_res, count);
		count++;
		if(trace>0){ print_mrs(mrs_res); printf("\n"); }
		free_mrs(mrs_res);
		return 1;
	}
	if(do_itsdb)itsdb_start_results();
	iterate_cell_root_hypotheses(cells, show_result, best_only);
	if(do_itsdb)itsdb_end_results();
	if(do_itsdb)itsdb_report_gen(count);
	if(debug_level)printf("unpacked %.4f\n", timer());

	fprintf(stderr, "NOTE: %d passive, %d active edges in final generation chart; built %d passives total. [%d results]\n",
		cells->p_nedges, cells->al_nedges + cells->ar_nedges, npassive_edges, count);

	/*if(lui_chart)
	{
		char	*words[] = {"generate"};
		output_lui_chart(words);
	}*/
	if(lui_mode)
		output_lui_trees(cells, "Generation Results", best_only, mrs);

	clear_mrs();
	clear_slab();

	return count;
}
