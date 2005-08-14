#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<assert.h>

#include "mrs.h"
#include "lexicon.h"
#include "chart.h"
#include "unpack.h"

extern int	question, exhaustive, lui_chart, inhibit_pack, trace, debug_level;

unsigned int	require_ep_span = 0;

// chart

int	gen_process_edge(struct edge	*e)
{
	struct chart_cell	*c = cells;
	int	i, pack = 0;

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

// ep, var, and rels-list dg construction

void	dagify_mrs_var(struct mrs_var	*var, struct dg	*skolem)
{
	struct dg	*dg;
	char		*type = "individual";
	int			i, path[16], len;

	if(var->is_const)
	{
		fprintf(stderr, "ERROR: generator doesn't handle CARG's yet\n");
		exit(-1);
	}

	switch(var->type[0]) {
		case	'e': type = "event";			break;
		case	'x': type = "event_or_index";	break;
		case	'u': type = "event_or_index";	break;
		case	'h': type = "handle";			break;
	}

	dg = atomic_dg(type);
	for(i=0;i<var->nprops;i++)
	{
		char	*vtype = var->props[i].value, *str = strdup(var->props[i].name), *p = str, *p2;
		len = 0; do
		{
			p2 = strchr(p, '.');
			if(p2)*p2 = 0;
			path[len++] = lookup_fname(p);
			p = p2?(p2+1):0;
		} while(p);
		dg = (struct dg*)add_dg_path(dg, path, len, atomic_dg(vtype));
	}
	if(var->type[0]=='e' || var->type[0]=='u' || var->type[0]=='x')dg = add_dg_feature(dg, "INSTLOC", skolem);
	var->dg = wellform_dg(dg);
}

void dagify_mrs_ep(struct mrs_ep *ep)
{
	struct dg	*dg = atomic_dg("relation");
	int			i;

	dg = add_dg_feature(dg, "PRED", atomic_dg(ep->pred));
	//if(ep->lbl)dg = add_dg_feature(dg, "LBL", ep->lbl->dg);	// XXX something funny about the grammar.  you *can't* skolemize LBL.
	for(i=0;i<ep->nargs;i++)
		dg = add_dg_feature(dg, ep->args[i].name, ep->args[i].value->dg);
	ep->dg = wellform_dg(dg);
	//printf("ep dag: "); print_dg(ep->dg); printf("\n\n");
}

struct dg	*dagify_rels_from_eps(int neps, struct mrs_ep	*eps[])
{
	struct dg	*last = atomic_dg("*top*"), *list = last;
	struct dg	*rels = add_dg_feature(atomic_dg("*top*"), "LAST", last);
	int			i;

	for(i=0;i<neps;i++)
	{
		struct dg *cons = atomic_dg("*top*");
		cons = add_dg_hop(cons, 2, list);
		list = cons = add_dg_hop(cons, 1, eps[i]->dg);
	}
	return add_dg_feature(rels, "LIST", list);
}

// driver

int	generate(struct mrs	*mrs)
{
	extern struct edge *sentential;
	struct edge	*edge;
	char		*str;
	int			i, count = 0;

	clear_chart(1);

	if(debug_level)
	{
		printf("trying to generate from MRS:\n");
		print_mrs(mrs); printf("\n");
	}

	if(mrs->nrels > 32)
	{
		fprintf(stderr, "ERROR: generator cannot currently function with more than 32 EPs\n");
		return 0;
	}
	require_ep_span = (1<<mrs->nrels)-1;

	// set up the skolemization and specialization dags we'll need.
	for(i=0;i<mrs->vlist.nvars;i++)
	{
		extern struct type	**strings;
		struct dg			*skolem;
		skolem = atomic_dg("*top*");
		skolem->type = skolem->xtype = strings[i];
		dagify_mrs_var(mrs->vlist.vars[i], skolem);
	}
	for(i=0;i<mrs->nrels;i++)
		dagify_mrs_ep(mrs->rels+i);

	// set up some lexemes and rules for the generator to play with
	semantic_lookup(mrs);

	sentential = 0;
	while( (edge = next_agenda(0)) )
	{
		//printf("... process %p = #%d\n", edge, edge->id);
		if(gen_process_edge(edge)){/*printf("terminated!\n");break;*/}
	}

	void show_result(struct hypothesis	*hyp, struct mrs	*mrs_res)
	{
		// XXX check that `mrs' subsumes `mrs_res'.
		if(trace>0)printf("edge #%d has a correct unpacking\n", hyp->dec->edge->id);
		if(trace>1){print_dg(hyp->dg); printf("\n");}
		printf("%s\n", hyp->string);
		free_mrs(mrs_res);
	}
	iterate_cell_root_hypotheses(cells, show_result, 0);

	printf("NOTE: %d passive, %d active edges in final generation chart\n",
		cells->p_nedges, cells->al_nedges + cells->ar_nedges);
	if(lui_chart)
	{
		char	*words[] = {"generate"};
		output_lui(words);
	}

	return count;
}
