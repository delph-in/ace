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

unsigned long long	require_ep_span = 0;

unsigned long long collect_vars(struct dg	*dg)
{
	int	i;
	dg = p_dereference_dg(dg);
	struct dg	**arcs = DARCS(dg);
	static int instloc = -1;
	if(instloc ==-1)instloc = lookup_fname("INSTLOC");

	unsigned long long v = 0;
	for(i=0;i<dg->narcs;i++)
	{
		if(dg->label[i] == instloc)
		{
			struct type	*t = arcs[i]->xtype;
			if(t->name[0]!='"')return 0;;
			int	tnum = t->tnum;
			//int k;
			//for(k=0;k<100;k++)
			//	if(strings[k]==t)tnum = k;
			//printf("instloc has tnum = %d = %s\n", tnum, t->name);
			return ((unsigned long long)1)<<tnum;
		}
		else v |= collect_vars(arcs[i]);
	}
	return v;
}

unsigned long long accessible_vars(struct edge	*e)
{
	// look at SYNSEM.LKEYS
	static int	save[3] = {-1,-1,-1};
	struct dg	*lkeys = walk_dg(e->dg, save, "SYNSEM", "LKEYS", NULL);

//	printf("lkeys:\n");
//	print_dg(lkeys);
//	printf("\n");

	//assert(lkeys != NULL);
	//return collect_vars(lkeys);
	return collect_vars(e->dg);
}

struct mrs	*current_input_mrs;

int	index_accessibility_filter_one_var(struct edge	*e, struct mrs_var	*v)
{
	return e->inaccessible_vars & (((unsigned long long)1)<<v->vnum);
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
		if(!(e->ep_span & (((unsigned long long)1)<<i)))
			if(index_accessibility_filter_one_ep(e, current_input_mrs->rels+i))
				return 1;
	return 0;
}

// chart

#define	INDEX_ACCESSIBILITY_FILTERING

int	gen_process_edge(struct edge	*e)
{
	struct chart_cell	*c = cells;
	int	i, pack = 0;

	e->accessible_vars = 0;
	e->inaccessible_vars = 0;
	for(i=0;i<e->have;i++)
	{
		if(e->daughters[i]->frozen==1)return 0;
		e->accessible_vars |= e->daughters[i]->accessible_vars;
		e->inaccessible_vars |= e->daughters[i]->inaccessible_vars;
	}

#ifdef	INDEX_ACCESSIBILITY_FILTERING
	if(e->have == e->need)
	{
		unsigned long long	really_accessible = accessible_vars(e);
		e->inaccessible_vars |= e->accessible_vars & ~really_accessible;
		e->accessible_vars = really_accessible;
		if(index_accessibility_filter(e))
		{
			//printf("filtered.\n");
			return 0;
		}
		//if(e->accessible_vars)printf("#%d has accessibility: %16.16llX\n", e->id, e->accessible_vars);
		//if(e->inaccessible_vars)printf("#%d has inaccessibility: %16.16llX\n", e->id, e->inaccessible_vars);
	}
#endif

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
			if(! (e->ep_span & c->al_edges[i]->ep_span))
				if(combine(c->al_edges[i], e) == -1)return -1;
		for(i=0;i<c->ar_nedges;i++)
			if(! (e->ep_span & c->ar_edges[i]->ep_span))
				if(combine(c->ar_edges[i], e) == -1)return -1;
	}
	else
	{
		// active edge
		for(i=0;i<c->p_nedges;i++)
			if(! (e->ep_span & c->p_edges[i]->ep_span))
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

	if(mrs->nrels > 64 || mrs->vlist.nvars > 64)
	{
		fprintf(stderr, "ERROR: generator cannot currently function with more than 64 EPs and 64 vars\n");
		return 0;
	}
	require_ep_span = (((unsigned long long)1)<<mrs->nrels)-1;

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

	sentential = 0;
	while( (edge = next_agenda(0)) )
	{
		//printf("... process %p = #%d\n", edge, edge->id);
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
		if(trace>0)printf("edge #%d has a correct unpacking\n", hyp->dec->edge->id);
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

	fprintf(stderr, "NOTE: %d passive, %d active edges in final generation chart [%d results]\n",
		cells->p_nedges, cells->al_nedges + cells->ar_nedges, count);

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
