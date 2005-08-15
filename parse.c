#include	<stdlib.h>
#include	<stdio.h>
#include	<string.h>
#include	<signal.h>

#include	"chart.h"
#include	"rule.h"
#include	"lexicon.h"
#include	"morpho.h"
#include	"mrs.h"
#include	"unpack.h"

extern int	total_edges, real_edges, passive_edges, used_edges, pused_edges, trace, used_total, pused_total;
extern int	question, exhaustive, lui_chart, inhibit_pack, inhibit_results;

static int	parse_process_edge(struct edge	*e)
{
	int			i, j, remain, pack = 0;
	struct chart_cell	*c;

	//printf("processing [%d-%d] edge;    ", e->from, e->to);
	//printf("  status: %d/%d\n", e->have, e->need);

	/*if(e->have==e->need)
		e->dg->xtype = lookup_type("*top*");*/
	if(!inhibit_pack && e->have == e->need)
	{
		pack = pack_edge(e);
		if(pack==1)
			return 0;	// big savings! woohoo!
	}
	if(pack==0)add_edge_to_chart(e);

	// try to combine with edges on either side
	if(e->have == e->need)
	{
		// passive edge
		for(i=0;i<=e->from;i++)
		{
			c = cells + e->from + i*chart_size;
			for(j=0;j<c->al_nedges;j++)
			{
				remain = c->al_edges[j]->need - c->al_edges[j]->have;
				if(e->to + (remain-1) < chart_size)
					if(combine(c->al_edges[j], e) == -1)return -1;
			}
		}
		for(i=e->to;i<chart_size;i++)
		{
			c = cells + i + e->to*chart_size;
			for(j=0;j<c->ar_nedges;j++)
			{
				remain = c->ar_edges[j]->need - c->ar_edges[j]->have;
				if(e->from - (remain-1) >= 0)
					if(combine(c->ar_edges[j], e) == -1)return -1;
			}
		}
	}
	else
	{
		// active edge
		remain = e->need - e->have;
		if(e->rtl)
		{
			for(i=0;i<=e->from;i++)
			{
				c = cells + e->from + i*chart_size;
				for(j=0;j<c->p_nedges;j++)
					if(c->p_edges[j]->from - (remain-1) >= 0)
						if(combine(e, c->p_edges[j]) == -1)return -1;
			}
		}
		else
		{
			for(i=e->to;i<chart_size;i++)
			{
				c = cells + i + e->to*chart_size;
				for(j=0;j<c->p_nedges;j++)
					if(c->p_edges[j]->to + (remain-1) < chart_size)
						if(combine(e, c->p_edges[j]) == -1)return -1;
			}
		}
	}
	return 0;
}

int	cancel_parse;

void	alarm_handler(int	sig)
{
	fprintf(stderr, "NOTE: cancelling parse, too long\n");
	cancel_parse = 1;
}

int	parse(int	nwords, char	*words[], char	*sentence)
{
	int		i, count;
	int		num_parses = 0, num_entries;
	struct dg	*root;
	struct dg	*sem;
	struct edge	*edge;

	// setup the chart
	clear_chart(nwords+1);
	if(lexical_lookup(nwords, words))return -1;
	if(reduce_chart(nwords))return -1;

	//if(rtried)free(rtried);
	//rtried = calloc(sizeof(int),nrules);

	agenda_rules(nwords+1);

	cancel_parse = 0;
	signal(SIGALRM, alarm_handler);

	//alarm(10);
	//ualarm(100000);

	// do the actual parsing
	while( !cancel_parse && (edge = next_agenda(0)) )
		if(parse_process_edge(edge))break;

	alarm(0);

	/*num_entries = 0;
	for(i=0;i<nrules;i++)if(rtried[i])
	{
		num_entries++;
		printf("rule %32s: %8d tries\n", rules[i]->name, rtried[i]);
	}
	printf(" -- %d / %d were tried\n", num_entries, nrules);*/

	//capi_print_derivations();

	// show analyses
	void show_result(struct hypothesis	*hyp, struct mrs	*mrs)
	{
		struct dg	*root = hyp->dg, *sem;
		if(trace>0)printf("edge #%d has a sentential unpacking\n", hyp->dec->edge->id);
		if(trace>1){print_dg(hyp->dg); printf("\n");}
		if(!inhibit_results)
		{
			if(sentence && !question)printf("SENT: %s\n", sentence);
			print_mrs(mrs);
			fflush(stdout);
		}
		free_mrs(mrs);
	}
	num_parses = iterate_cell_root_hypotheses(&cells[chart_size-1], show_result, 0);

	/*if(!exhaustive && sentential) { i=0;num_entries=0;edge = sentential; root = edge->dg; goto show; }
	num_entries = cells[chart_size-1].p_nedges;
	for(i=0;i<num_entries;i++)
	{
		edge = cells[chart_size-1].p_edges[i];
		root = edge->dg;
		if(!is_root(root))continue;

		//printf("edge #%d is root\n", edge->id);

		show:
		count = 1;//unfold_edge(edge);
		num_parses+=count;

		if(!inhibit_results)
		{
			//print_dg(root);
			//printf("\n\n");

			sem = dg_hop(root, lookup_fname("SYNSEM"));
			if(sem)sem = dg_hop(sem, lookup_fname("LOCAL"));
			if(sem)sem = dg_hop(sem, lookup_fname("CONT"));
			if(!sem)
			{
				//fprintf(stderr, "parse had no semantics!\n");
				continue;
			}
			if(sentence && !question)printf("SENT: %s\n", sentence);

			print_sem(sem);
			if(!question)printf("\n\n");
			fflush(stdout);
		}
		if(!exhaustive)break;
	}*/

	if(!inhibit_results)
	{
		fprintf(stderr, "NOTE: %d readings. added %d / %d edges to chart (%d fully instantiated, %d actives used, %d passives used)\t",
					num_parses, total_edges, real_edges, passive_edges, used_edges, pused_edges);
		print_slab_stats();
		if(lui_chart)output_lui(words);
	}
	used_total += used_edges;
	pused_total += pused_edges;

	return num_parses;
}
