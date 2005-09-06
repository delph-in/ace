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
extern int	question, exhaustive, lui_mode, inhibit_pack, inhibit_results, do_itsdb, best_only;
extern float timer();

static int	parse_process_edge(struct edge	*e)
{
	int			i, j, remain, pack = 0, from = e->from, to = e->to, have = e->have;
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
		for(i=0;i<=from;i++)
		{
			c = cells + from + i*chart_size;
			for(j=0;j<c->al_nedges;j++)
			{
				remain = c->al_edges[j]->need - c->al_edges[j]->have;
				if(to + (remain-1) < chart_size)
					if(combine(c->al_edges[j], e) == -1)return -1;
			}
		}
		for(i=to;i<chart_size;i++)
		{
			c = cells + i + to*chart_size;
			for(j=0;j<c->ar_nedges;j++)
			{
				remain = c->ar_edges[j]->need - c->ar_edges[j]->have;
				if(from - (remain-1) >= 0)
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
			for(i=0;i<=from;i++)
			{
				c = cells + from + i*chart_size;
				for(j=0;j<c->p_nedges;j++)
					if(c->p_edges[j]->from - (remain-1) >= 0)
						if(combine(e, c->p_edges[j]) == -1)return -1;
			}
		}
		else
		{
			for(i=to;i<chart_size;i++)
			{
				c = cells + i + to*chart_size;
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
	fprintf(stderr, "NOTE: cancelling parse, taking too long\n");
	cancel_parse = 1;
}

int	parse(int	nwords, char	*words[], char	*sentence)
{
	int		i, count;
	int		num_parses = 0, num_entries;
	float	t_setup, t_forest, t_unpack;
	struct dg	*root;
	struct dg	*sem;
	struct edge	*edge;

	// setup the chart
	clear_chart(nwords+1);
	if(lexical_lookup(nwords, words))return -1;
	if(reduce_chart(nwords, words))return -1;

	//if(rtried)free(rtried);
	//rtried = calloc(sizeof(int),nrules);

	agenda_rules(nwords+1);

	cancel_parse = 0;
	signal(SIGALRM, alarm_handler);

	alarm(10);	// XXX make this commandline configurable, and apply it to unpacking too

	t_setup = timer();

	// do the actual parsing
	while( !cancel_parse && (edge = next_agenda(0)) )
		if(parse_process_edge(edge))break;

	t_forest = timer();

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
	int show_result(struct hypothesis	*hyp, struct mrs	*mrs)
	{
		struct dg	*root = hyp->dg, *sem;
		if(trace>0)printf("edge #%d has a sentential unpacking\n", hyp->dec->edge->id);
		if(trace>1){print_dg(hyp->dg); printf("\n");}
		if(!inhibit_results)
		{
			if(sentence && !question)printf("SENT: %s\n", sentence);
			print_mrs(mrs);
		}
		free_mrs(mrs);
		return 1;
	}
	num_parses = iterate_cell_root_hypotheses(&cells[chart_size-1], show_result, best_only);
	if(num_parses && !inhibit_results)printf("\n");
	fflush(stdout);

	t_unpack = timer();

	fprintf(stderr, "NOTE: %d readings, added %d / %d edges to chart (%d fully instantiated, %d actives used, %d passives used)\t",
		num_parses, total_edges, real_edges, passive_edges, used_edges, pused_edges);
	print_slab_stats();
	if(lui_mode)
	{
		if(num_parses)
			output_lui_trees(cells+chart_size-1, sentence, best_only);
		else output_lui_chart(words);
	}

	used_total += used_edges;
	pused_total += pused_edges;

	{
		static FILE	*f = 0;
		if(!f)f = fopen("/tmp/times", "w");
		fprintf(f, "%d	%d	%f	%f	%f	%s\n", nwords, num_parses, t_setup, t_forest, t_unpack, sentence);
		fflush(f);
	}

	return num_parses;
}

int		wcap[1024];
char	*current_sentence = "(not initialized)";

int	parse_line(char	*line)
{
	char	copy[10240];
	int	nparse = 0;
	/*char	*p, *w;
	char	*words[100];
	int	nwords = 0;*/
	int	nwords = 0, i, j;
	char	**words, *p, *q;

	timer();

	current_sentence = "(preparing)";
	while(line[0] && line[strlen(line)-1] == '\n')line[strlen(line)-1] = 0;
	if(!*line)return -1;
	if(!strncmp(line, "<DOC", 4))
	{
		printf("%s\n", line);
		return -1;
	}
	//if(!strcmp(line, "All rights reserved."))return -1;	// not interesting.
	if(!strcmp(line, "@reloadrules@"))
	{
		load_rules_conf();
		return -1;
	}

	strcpy(copy, line);

	// break up into words, normalize capitalization
	/*w = p = copy;
	while(*p)
	{
		if(*p>='A' && *p<='Z')(*p) += 32;
		switch(*p)
		{
			case	' ': case '\t': case '.': case '\n':
			case	',': case '?': case '!':
				if(nwords>=100) { fprintf(stderr, "NOTE: 0 readings. Too many words (100+)\n"); return 0; }
				*p = 0;
				if(w != p)words[nwords++] = w;
				w = p+1;
				break;
		}
		p++;
	}*/

	if(preprocess_line(copy, &nwords, &words))
	{
		fprintf(stderr, "NOTE: preprocessor failed!\n");
		return -1;
	}
	//if(nwords<=1)return -1;	// experience shows short "sentences" to not usually be interesting
	if(nwords>60)
	{
		fprintf(stderr, "NOTE: 0 readings, too many words (60+)\n");
		return 0;	// trying to parse long sentences is a waste of time. we have lots of short ones.
	}

	//printf("%d words\n", nwords);
	for(j=0;j<nwords;j++)
	{
		wcap[j] = isupper(words[j][0]);
		for(i=0;words[j][i];i++)
		{
			words[j][i] = tolower(words[j][i]);
		}
	}
	current_sentence = line;//(char*)trim_string(line);
	nparse = parse(nwords, words, current_sentence);
	if(do_itsdb)itsdb_report(nwords, words, nparse);
	clear_slab();
	for(i=0;i<nwords;i++)free(words[i]);
	free(words);
	return nparse;
}
