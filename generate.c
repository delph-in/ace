#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<assert.h>

#include "mrs.h"
#include "lexicon.h"
#include "chart.h"

extern int	question, exhaustive, lui_chart, inhibit_pack, trace;

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

// instantiation

static struct dg	**skunk;
struct mrs_vlist	*vlist;

struct dg *constrain_role(struct dg *dg, struct mrs_var *var)
{
	struct dg	*skolem, *newrole;
	static int instlocf = -1;
	int i;

	for(i=0;i<vlist->nvars;i++)
		if(vlist->vars[i] == var)break;
	if(i==vlist->nvars)
	{
		fprintf(stderr, "ERROR: constrain_role unable to find an MRS variable\n");
		return 0;
	}
	else skolem = skunk[i];

	//printf("place of constraint: "); print_dg(dg); printf("\n");

	if(instlocf == -1)instlocf = lookup_fname("INSTLOC");
	newrole = add_dg_hop(dg, instlocf, skolem);

	// XXX need to specialize the value's properties in as well
	return newrole;
}

int	constrain_rel(struct dg	*rel, struct mrs_ep	*p)
{
	int	i;

	for(i=0;i<p->nargs;i++)
	{
		struct dg *role = dg_hop(rel, lookup_fname(p->args[i].name));
		struct dg *newrole;
		if(!role)
		{
			fprintf(stderr, "ERROR: constraint site lacked role `%s'\n", p->args[i].name);
			return -1;
		}
		newrole = constrain_role(role, p->args[i].value);
		if(newrole == 0)return -1;
		if(newrole != role)
		{
			//printf(" ... had to change REL's pointer to role `%s'\n", p->args[i].name);
			add_dg_feature(rel, p->args[i].name, newrole);
		}
	}
	return 0;
}

int constrain_gen_edge(struct edge *e, struct dg	*cont, int	neps, struct mrs_ep	*eps[])
{
	int 		i;
	static int	savef[3] = {-1,-1,-1};
	struct dg	*rels, *rel;

	//printf("`%s' aligns as", e->lex?e->lex->word:e->rule->name);
	//for(i=0;i<neps;i++)printf(" /%d %s", eps[i]->epnum, eps[i]->pred);
	//printf("\n");

	// visit each relation in the edge's dag
	rels = walk_dg(cont, savef, "RELS", "LIST", 0);
	for(i=0;rels && i<neps;i++,rels=dg_hop(rels, 2 /* REST */))
	{
		if(! (rel = dg_hop(rels, 1 /* FIRST */)) )break;
		//printf("should skolemize REL: "); print_dg(rel);
		//printf(" using ep %d = %s\n", eps[i]->epnum, eps[i]->pred);
		if(constrain_rel(rel, eps[i]) != 0)break;
		// mark ep coverage provided by this edge
		e->ep_span |= (1<<eps[i]->epnum);
	}

	if(i!=neps)
	{
		printf("edge skolemization or specialization failed\n");
		return 0;
	}
	return 1;
}

// driver

int	generate(struct mrs	*mrs)
{
	extern struct edge *sentential;
	struct edge	*edge;
	char		*str;
	int			i, count = 0;

	clear_chart(1);

	printf("trying to generate from MRS:\n");
	print_mrs(mrs); printf("\n");

	if(mrs->nrels > 32)
	{
		fprintf(stderr, "ERROR: generator cannot currently function with more than 32 EPs\n");
		return 0;
	}
	require_ep_span = (1<<mrs->nrels)-1;

	// set up the skolem constants we'll need.
	skunk = malloc(sizeof(struct dg*) * mrs->vlist.nvars);
	for(i=0;i<mrs->vlist.nvars;i++)
	{
		extern struct type	**strings;
		skunk[i] = atomic_dg("*top*");
		skunk[i]->type = skunk[i]->xtype = strings[i];
	}
	vlist = &mrs->vlist;

	// set up some lexemes and rules for the generator to play with
	semantic_lookup(mrs);

	sentential = 0;
	while( (edge = next_agenda(0)) )
	{
		//printf("... process %p = #%d\n", edge, edge->id);
		if(gen_process_edge(edge)){/*printf("terminated!\n");break;*/}
	}

	/*if(sentential)
	{
		i = cells->p_nedges+1;
		edge = sentential;
		goto show;
	}
	else*/
	{
		for(i=0;i<cells->p_nedges;i++)
		{
			edge = cells->p_edges[i];
			if(edge->ep_span == require_ep_span && is_root(edge->dg))
			{
				show:
				if(trace>0)printf("edge #%d is sentential\n", edge->id);
				if(trace>1){print_dg(edge->dg); printf("\n");}
				printf("%s\n", str = show_orth_leaves(edge));
				free(str);
				count++;
			}
		}
	}
	printf("NOTE: %d passive, %d active edges in final generation chart\n",
		cells->p_nedges, cells->al_nedges + cells->ar_nedges);
	if(lui_chart)
	{
		char	*words[] = {"generate"};
		output_lui(words);
	}

	return count;
}
