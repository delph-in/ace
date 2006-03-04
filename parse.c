#include	<stdlib.h>
#include	<stdio.h>
#include	<string.h>
#include	<signal.h>
#include	<time.h>

#include	"chart.h"
#include	"rule.h"
#include	"lexicon.h"
#include	"morpho.h"
#include	"mrs.h"
#include	"unpack.h"

extern int	total_edges, real_edges, passive_edges, used_edges, pused_edges, trace, used_total, pused_total;
extern int	question, exhaustive, lui_mode, inhibit_pack, inhibit_results, do_itsdb, best_only;
extern float timer();

#ifdef	LEVEL_TIMER
float	level_times[100];
#endif
int		level = 0;

static int	parse_process_edge(struct edge	*e)
{
	int			i, j, remain, pack = 0, from = e->from, to = e->to, have = e->have, nlist=0;
	struct chart_cell	*c;
	static struct edge	**list = 0;
	static int			alist = 0;

	for(i=0;i<e->have;i++)
		if(e->daughters[i]->frozen==1)
			return 0;

#ifdef	LEVEL_TIMER
	if(!e->lex && (e->to-e->from != level))
	{
		if(e->to > e->from)
			level_times[e->to-e->from - 1] = timer();
		level = e->to - e->from;
	}
#endif

	if(!inhibit_pack && e->have == e->need)
	{
		pack = pack_edge(e);
		if(pack==1)
			return 0;	// big savings! woohoo!
	}
	if(pack==0)add_edge_to_chart(e);

	if(!alist){list=calloc(sizeof(struct edge*), 4); alist=4;}
#define	LADD(e)	do { if(nlist+1 > alist) { \
					alist *= 2; \
					list = realloc(list, sizeof(struct edge*)*alist); \
					} list[nlist++] = (e); } while(0)

	// try to combine with edges on either side
	if(e->have == e->need)
	{
		c = cells + from;
		// passive edge
		for(i=0;i<=from;i++)
		{
			struct edge	**E = c[i*chart_size].al_edges, **F = E+c[i*chart_size].al_nedges;
			for(;E<F;E++) if(to + ((*E)->need - (*E)->have - 1) < chart_size) LADD(*E);
		}
		c = cells + to*chart_size;
		for(i=to;i<chart_size;i++)
		{
			struct edge	**E = c[i].ar_edges, **F = E+c[i].ar_nedges;
			for(;E<F;E++) if(from - ((*E)->need - (*E)->have - 1) >= 0) LADD(*E);
		}
		for(i=0;i<nlist;i++)
			if(combine(list[i], e) == -1)return -1;
	}
	else
	{
		// active edge
		remain = e->need - e->have;
		if(e->rtl) for(i=0,c=cells+from;i<=from;i++)
		{
			struct edge	**E = c[i*chart_size].p_edges, **F = E+c[i*chart_size].p_nedges;
			for(;E<F;E++) if((*E)->from - (remain-1) >= 0)LADD(*E);
		}
		else for(i=to,c=cells+to*chart_size;i<chart_size;i++)
		{
			struct edge **E = c[i].p_edges, **F = E+c[i].p_nedges;
			for(;E<F;E++) if((*E)->to + (remain-1) < chart_size)LADD(*E);
		}
		for(i=0;i<nlist;i++)
			if(combine(e, list[i]) == -1)return -1;
	}
	return 0;
}

int	cancel_parse;

void	alarm_handler(int	sig)
{
	fprintf(stderr, "NOTE: cancelling parse, taking too long\n");
	cancel_parse = 1;
}

int	parse(int	nwords, char	**lemmas, char	**forms, char	*sentence)
{
	int		i, count, didsent = 0, cforest;
	int		num_parses = 0, num_entries;
	float	t_setup, t_forest, t_unpack;
	struct dg	*root;
	struct dg	*sem;
	struct edge	*edge;
	clock_t	start = clock();

	// setup the chart
	clear_chart(nwords+1);
	if(lexical_lookup(nwords, lemmas, forms)) { fprintf(stderr, "NOTE: lexical coverage gap\n");  return -1; }
	if(reduce_chart(nwords, lemmas)) { fprintf(stderr, "NOTE: post reduction gap\n"); return -1; }

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
	cforest = clock() - start;

	alarm(0);

	// show analyses
	int show_result(struct hypothesis	*hyp, struct mrs	*mrs)
	{
		struct dg	*root = hyp->dg, *sem;
		extern char	*mrs_end;
		if(trace>0)printf("edge #%d has a sentential unpacking\n", hyp->dec->edge->id);
		if(trace>1){print_dg(hyp->dg); printf("\n");}
		if(!inhibit_results)
		{
			if(sentence && !question && !didsent){printf("SENT: %s\n", sentence);didsent=1;}
			mrs_end = " ; ";
			print_mrs(mrs);
			print_hypothesis(hyp, printf);
			printf((mrs_end = "\n"));
		}
		free_mrs(mrs);
		return 1;
	}
	num_parses = iterate_cell_root_hypotheses(&cells[chart_size-1], show_result, best_only);
	if(num_parses && !inhibit_results)printf("\n\n");
	fflush(stdout);

	t_unpack = timer();

	fprintf(stderr, "NOTE: %d readings, added %d / %d edges to chart (%d fully instantiated, %d actives used, %d passives used)\t",
		num_parses, total_edges, real_edges, passive_edges, used_edges, pused_edges);
	print_slab_stats();
	if(lui_mode)
	{
		if(num_parses)
			output_lui_trees(cells+chart_size-1, sentence, best_only, 0);
		else output_lui_chart(lemmas);
	}

	used_total += used_edges;
	pused_total += pused_edges;

	if(1)
	{
		static FILE	*f = 0;
		if(!f)f = fopen("/tmp/times", "w");
		fprintf(f, "%d	%d	%f	%f	%f	%s\n", nwords, num_parses, t_setup, t_forest, t_unpack, sentence);
		fflush(f);
	}

	if(do_itsdb)itsdb_report(nwords, lemmas, num_parses, (int)((long long)cforest*1000 / CLOCKS_PER_SEC));

	return num_parses;
}

int		wcap[1024];
char	*current_sentence = "(not initialized)";

void	preprocess_line(char	*string, int	*Nwords, char	***Lemmas, char	***Forms);

int	parse_line(char	*line)
{
	char	copy[10240];
	int	nparse = 0;
	int	nwords = 0, i, j;
	char	**forms, **lemmas, *p, *q;
	extern int preprocess_only;

	timer();

	current_sentence = "(preparing)";
	while(line[0] && line[strlen(line)-1] == '\n')line[strlen(line)-1] = 0;
int	check_mbs(char	*mbs);
	if(!*line || check_mbs(line)==-1)
	{
		if(!inhibit_results){printf("SKIP: %s\n", line);fflush(stdout);}
		return -1;
	}
	if(!strncmp(line, "<DOC", 4))
	{
		printf("%s\n", line);
		return -1;
	}

	strcpy(copy, line);

	preprocess_line(copy, &nwords, &lemmas, &forms);
	if(preprocess_only)
	{
		for(i=0;i<nwords;i++)
			printf("%s%s", i?" ":"", forms[i]);
		printf("\n");
		goto finished;
	}

	if(nwords>60)
	{
		fprintf(stderr, "NOTE: 0 readings, too many words (60+)\n");
		if(!inhibit_results){printf("SKIP: %s\n", line);fflush(stdout);}
		return 0;
	}

	for(j=0;j<nwords;j++)
	{
		wcap[j] = isupper(forms[j][0]);
		for(i=0;forms[j][i];i++)
		{
			forms[j][i] = tolower(forms[j][i]);
		}
		//if(j<=nwords-3)printf("FORM %s %s %s\n", forms[j], forms[j+1], forms[j+2]);
	}
	current_sentence = line;
	level = 0;
	timer();
	nparse = parse(nwords, lemmas, forms, current_sentence);
	if(nparse<=0 && !inhibit_results){printf("SKIP: %s\n", line);fflush(stdout);}

#ifdef	LEVEL_TIMER
	level_times[nwords] = timer();
	for(i=1;i<=nwords;i++)printf(" level %d: %f\n", i, level_times[i]);
#endif

	if(nparse==-1)fprintf(stderr, "NOTE: ignoring `%s'\n", current_sentence);
	if(do_itsdb && nparse==-1)itsdb_report(nwords, lemmas, -1, 0);

finished:
	clear_mrs();
	clear_slab();
	for(i=0;i<nwords;i++)free(forms[i]);
	free(forms);
	for(i=0;i<nwords;i++)free(lemmas[i]);
	free(lemmas);
	return nparse;
}
