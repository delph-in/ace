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
#include	"token.h"
#include	"lattice-mapping.h"
#include	"lexical-parse.h"
#include	"timer.h"

struct lattice	*last_token_chart;

extern int	total_edges, real_edges, passive_edges, used_edges, pused_edges, trace, used_total, pused_total;
extern int	question, exhaustive, lui_mode, inhibit_pack, inhibit_results, do_itsdb, best_only;
extern float timer();

#ifdef	LEVEL_TIMER
float	level_times[100];
#endif
int		level = 0;
int	do_forest = 0;
extern int debug_level;
int yy_mode = 0;

int	g_profiling = 0;

struct profiler	*start_sign_profiler(struct edge	*e)
{
	static struct profiler	**active_rule_profilers = NULL;
	static struct profiler	**passive_rule_profilers = NULL;
	static struct profiler	*passive_lex_profiler = NULL;
	chart_parsing_profiler->sortable = 10;
	if(!active_rule_profilers)
	{
		active_rule_profilers = calloc(sizeof(struct profiler*), nrules);
		passive_rule_profilers = calloc(sizeof(struct profiler*), nrules);
	}
	if(e->have == e->need)
	{
		if(e->have == 0)
		{
			start_and_alloc_profiler(&passive_lex_profiler, "lexeme", chart_parsing_profiler, NULL);
			return passive_lex_profiler;
		}
		struct rule	*r = e->rule;
		char	name[1024];
		sprintf(name, "passive: %s", r->name);
		start_and_alloc_profiler(&passive_rule_profilers[r->rnum], name, chart_parsing_profiler, NULL);
		return passive_rule_profilers[r->rnum];
	}
	else
	{
		struct rule	*r = e->rule;
		char	name[1024];
		sprintf(name, "active: %s", r->name);
		start_and_alloc_profiler(&active_rule_profilers[r->rnum], name, chart_parsing_profiler, NULL);
		return active_rule_profilers[r->rnum];
	}
}

static int	parse_process_edge(struct edge	*e)
{
	int			i, j, remain, pack = 0, from = e->from, to = e->to, have = e->have, nlist=0;
	struct chart_cell	*c;
	static struct edge	**list = 0;
	static int			alist = 0;

	if(e->daughters)
		for(i=0;i<e->have;i++)
			if(e->daughters[i]->frozen==1)
			{
				//printf("NOT giving edge #%d a chance to try to pack; daughter[%d] = edge #%d was frozen\n", e->id, i, e->daughters[i]->id);
				return 0;
			}

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
		static int packtimer = -1;
		if(packtimer==-1)packtimer = new_timer("packing");
		IFCLOCK( start_timer(packtimer); );
		if(g_profiling)
			start_and_alloc_profiler(&packing_profiler, "packing", chart_parsing_profiler, NULL);
		pack = pack_edge(e);
		if(g_profiling)
			stop_profiler(packing_profiler, 1);
		IFCLOCK( stop_timer(packtimer, 1); );
		if(pack==1)
			return 0;	// big savings! woohoo!
	}
	if(pack==0)add_edge_to_chart(e);

	struct profiler	*prof = NULL;
	if(g_profiling)
		prof = start_sign_profiler(e);

	if(!alist){list=calloc(sizeof(struct edge*), 4); alist=4;}
#define	LADD(e)	do { if(nlist+1 > alist) { \
					alist *= 2; \
					list = realloc(list, sizeof(struct edge*)*alist); \
					} list[nlist++] = (e); } while(0)

	// try to combine with edges on either side
	if(e->have == e->need)
	{
//		char	fil[nrules*2];
//		if(constraint_rule_filter(e->lex, e->id, e->dg, fil) != 0)
//			return 0;

		c = cells + from;
		// passive edge
		for(i=0;i<=from;i++)
		{
			struct edge	**E = c[i*chart_size].al_edges, **F = E+c[i*chart_size].al_nedges;
			for(;E<F;E++) if(to + ((*E)->need - (*E)->have - 1) < chart_size)
//				if(!fil[2*(*E)->rule->rnum + 0] || !fil[2*(*E)->rule->rnum + 1])
					LADD(*E);
		}
		c = cells + to*chart_size;
		for(i=to;i<chart_size;i++)
		{
			struct edge	**E = c[i].ar_edges, **F = E+c[i].ar_nedges;
			for(;E<F;E++) if(from - ((*E)->need - (*E)->have - 1) >= 0)
//				if(!fil[2*(*E)->rule->rnum + 0] || !fil[2*(*E)->rule->rnum + 1])
					LADD(*E);
		}
	}
	else
	{
		// active edge
		remain = e->need - e->have;
		if(e->rtl)
		{
			for(i=0,c=cells+from;i<=from;i++)
			{
				struct edge	**E = c[i*chart_size].p_edges, **F = E+c[i*chart_size].p_nedges;
				for(;E<F;E++) if((*E)->from - (remain-1) >= 0)LADD(*E);
			}
		}
		else
		{
			for(i=to,c=cells+to*chart_size;i<chart_size;i++)
			{
				struct edge **E = c[i].p_edges, **F = E+c[i].p_nedges;
				for(;E<F;E++) if((*E)->to + (remain-1) < chart_size)LADD(*E);
			}
		}
	}
	//printf(" edge #%d found %d potential neighbors\n", e->id, nlist);

	if(!nlist)
	{
		if(prof)stop_profiler(prof, 1);
		return 0;
	}

	if(e->have == e->need)
	{
		__builtin_prefetch(e->rule);
		for(i=j=0;i<nlist;i++)
		{
			if(i+2<nlist)__builtin_prefetch(list[i+2]);
			if(i+1<nlist)__builtin_prefetch(list[i+1]->rule);
			if(filter(list[i], e))
				list[j++] = list[i];
		}
	}
	else
	{
		__builtin_prefetch(e->rule);
		for(i=j=0;i<nlist;i++)
		{
			if(i+2<nlist)__builtin_prefetch(list[i+2]);
			if(i+1<nlist)__builtin_prefetch(list[i+1]->rule);
			if(filter(e, list[i]))
				list[j++] = list[i];
		}
	}
	nlist = j;

	if(!nlist)
	{
		if(prof)stop_profiler(prof, 1);
		return 0;
	}

/*
int	cmp(const void	*a, const void	*b)
{
	struct dg	*da = (*(const struct edge**)a)->dg;
	struct dg	*db = (*(const struct edge**)b)->dg;
	if(da && db)return db->gen - da->gen;
	else if(da)return -1;
	else if(db)return 1;
	else return 0;
}
qsort(list, nlist, sizeof(void*), cmp);
*/

	if(e->have == e->need)
	{
		for(i=0;i<nlist;i++)
			if(unify_and_build(list[i], e) == -1)return -1;
	}
	else
	{
		for(i=0;i<nlist;i++)
			if(unify_and_build(e, list[i]) == -1)return -1;
	}
	if(prof)stop_profiler(prof, 1);
	return 0;
}

int	cancel_parse;

void	alarm_handler(int	sig)
{
	fprintf(stderr, "NOTE: cancelling parse, taking too long\n");
	cancel_parse = 1;
}

int	fits(struct rule	*r, int	d, struct edge	*e)
{
	if(e->rule)return check_rf(r, e->rule, d);
	if(d==r->ndaughters-1)return check_trl(r, e->lex->lextype);
	return 1;
}

int	quickcheck_rule(int	rnum, int	d, struct qc	*q)
{
	static struct qc	**rqc = 0;
	if(!rqc)
	{
		int	i;
		rqc = malloc(sizeof(struct qc*)*nrules*2);
		for(i=0;i<nrules;i++)
		{
			rqc[2*i+0] = extract_qc_arg(rules[i]->packdg, 0);
			if(rules[i]->ndaughters>1)
				rqc[2*i+1] = extract_qc_arg(rules[i]->packdg, 1);
		}
	}
	return quickcheck(rqc[2*rnum+d], q);
}

void	halftime_analysis()
{
	struct chart_cell	*right = cells+(chart_size-1)+(chart_size-2)*chart_size;
	char				parent[nrules], uleftsib[nrules], leftsib[nrules];
	int					i, j;

	bzero(parent, sizeof(parent));
	bzero(uleftsib, sizeof(parent));
	bzero(leftsib, sizeof(parent));
	//printf("rightmost passive edges: %d\n", right->p_nedges);
	for(i=0;i<right->p_nedges;i++)
	{
		struct edge	*e = right->p_edges[i];
		for(j=0;j<nrules;j++) if(!rules[j]->rtl && rules[j]->ndaughters>1)
			if(fits(rules[j], rules[j]->ndaughters-1, e) &&
				!quickcheck_rule(j, rules[j]->ndaughters-1, e->qc))
				parent[j] = 1;
	}
	//printf("rightmost active RL edges: %d\n", right->ar_nedges);
	for(i=0;i<right->ar_nedges;i++)
	{
		struct edge	*e = right->ar_edges[i];
		if(!e->have)continue;
		parent[e->rule->rnum] = 1;
	}

	for(i=0;i<nrules;i++) if(parent[i])
		printf("	right daughter of %s\n", rules[i]->name);

	// mark all unaries which fit the left slot of the parent, recursively

	int again;
	do { again = 0;
	for(i=0;i<nrules;i++) if(parent[j])
		for(j=0;j<nrules;j++) if(rules[j]->ndaughters==1
			&& check_rf(rules[i], rules[j], 0) && !parent[j])parent[j] = 1, again=1; } while(again);

	// find all binaries that fit into the parents or the unaries
	for(i=0;i<nrules;i++) if(parent[i])
		for(j=0;j<nrules;j++)
			if(check_rf(rules[i], rules[j], 0))leftsib[j] = 1;

	for(i=0;i<nrules;i++) if(leftsib[i])
		printf("	left sibling could be %s\n", rules[i]->name);
}

int	prepare_parse_chart(struct lattice	*lexical_chart)
{
	sort_lattice(lexical_chart);

	int	nwords = lexical_chart->lattice_vertex_id_gen - 1;

	// setup the chart
	clear_chart(nwords+1);

	// put lexical edges into agenda
	int i;
	for(i=0;i<lexical_chart->nedges;i++)
	{
		struct lattice_edge	*le = lexical_chart->edges[i];
		assert(le->edge != NULL);
		if(! edge_is_ready_for_syntax(le->edge)) continue;
		le->edge->from = le->from->id;
		le->edge->to = le->to->id;
		add_agenda(le->edge);
	}

	// put syntax rules into agenda
	//if(rtried)free(rtried);
	//rtried = calloc(sizeof(int),nrules);
	agenda_rules(nwords);

	return nwords;
}

//int		wcap[1024];
char	*current_sentence = "(not initialized)";
int didsent;

void	(*parse_callback)(struct hypothesis	*hyp, struct mrs	*mrs) = NULL;

// show analyses
int parse_show_result(struct hypothesis	*hyp, struct mrs	*mrs)
{
	struct dg	*root = hyp->dg, *sem;
	extern char	*mrs_end;
	extern int print_trees;
	if(trace>0)printf("edge #%d has a sentential unpacking\n", hyp->edge->id);
	if(trace>1){print_dg(hyp->dg); printf("\n");}
	if(!inhibit_results)
	{
		char	*sentence = current_sentence;
		if(sentence && !question && !didsent){printf("SENT: %s\n", sentence);didsent=1;}
		if(print_trees)
		{
			mrs_end = " ; ";
			print_mrs(mrs);
			char	*dstr = hypothesis_to_dstring(hyp);
			puts(dstr);
			free(dstr);
			mrs_end = "\n";
			//print_hypothesis(hyp, printf);
			//printf((mrs_end = "\n"));
		}
		else print_mrs(mrs);
	}
	if(parse_callback)
		parse_callback(hyp, mrs);
	free_mrs(mrs);
	//printf("\n");
	//printf("dstring = `%s'\n", slab_escape(hypothesis_to_dstring(hyp)));
	return 1;
}

static int chart_setup_timer = -1;

int	parse(int	nwords, char	**words, int	*cfrom, int	*cto, char	*sentence)
{
	clock_t	start = clock();

	if(chart_setup_timer == -1)chart_setup_timer = new_timer("chart setup");
	start_timer(chart_setup_timer);

	// a starting place for main-chart edge IDs
	reset_edges();

	if(g_profiling)start_and_alloc_profiler(&token_mapping_profiler, "token mapping", parse_profiler, preprocess_profiler);

	// setup a token lattice and do token mapping
	struct lattice	*token_chart = build_token_chart(nwords, words, cfrom, cto);
	if(!token_chart) { stop_timer(chart_setup_timer, 1); return -1; }

	return parse_with_token_chart(token_chart, start);
}

int parse_with_token_chart(struct lattice	*token_chart, clock_t	start)
{
	int		i, count, cforest;
	int		num_parses = 0, num_entries;
	float	t_setup, t_forest, t_unpack;
	struct dg	*root;
	struct dg	*sem;
	struct edge	*edge;

	apply_token_mapping(token_chart);
	sort_lattice(token_chart);

	if(trace)print_token_chart(token_chart);

	if(trace)printf("finished token mapping\n");

	if(g_profiling)start_and_alloc_profiler(&lexical_lookup_profiler, "lexical lookup", parse_profiler, token_mapping_profiler);

	// do lexical lookup
	struct lattice	*lexical_chart = lexical_lookup_into_chart(token_chart);
	if(!lexical_chart)
	{
		fprintf(stderr, "NOTE: failed to build lexical chart\n");
		stop_timer(chart_setup_timer, 1);
		return -1;
	}
	if(trace)printf("finished lexical lookup\n");

	if(g_profiling)start_and_alloc_profiler(&lexical_parsing_profiler, "lexical parsing", parse_profiler, lexical_lookup_profiler);

	extern int reduce_chart_before_lexical_parsing;

	if(!reduce_chart_before_lexical_parsing)
	{
		// do lexical parsing
		int rv = lexical_parse_lattice(lexical_chart);
		if(rv == -1)return -1;	// ran out of memory in lexical parsing!
	}

	if(reduce_lexical_lattice(lexical_chart))
	{
		fprintf(stderr, "NOTE: post reduction gap\n");
		stop_timer(chart_setup_timer, 1);
		return -1;
	}

	if(reduce_chart_before_lexical_parsing)
		lexical_parse_lattice(lexical_chart);

	if(trace)print_lexical_chart(lexical_chart);
	if(trace)printf("finished lexical parsing\n");

	if(g_profiling)start_and_alloc_profiler(&lexical_filtering_profiler, "lexical filtering", parse_profiler, lexical_parsing_profiler);

	// do lexical filtering
	apply_lexical_filtering(lexical_chart);
	if(trace)printf("finished lexical filtering\n");

	if(g_profiling)start_and_alloc_profiler(&chart_parsing_profiler, "chart parsing", parse_profiler, lexical_filtering_profiler);

	// setup the main parse chart
	int nwords = prepare_parse_chart(lexical_chart);
	if(nwords < 0)
	{
		fprintf(stderr, "NOTE: negative lexical chart length\n");
		stop_timer(chart_setup_timer, 1);
		return -1;
	}
	assert(nwords >= 0);
	if(!nwords)
	{
		fprintf(stderr, "NOTE: nothing to parse\n");
		stop_timer(chart_setup_timer, 1);
		return -1;
	}

	cancel_parse = 0;
	signal(SIGALRM, alarm_handler);

	//alarm(10);	// XXX make this commandline configurable, and apply it to unpacking too

	stop_timer(chart_setup_timer, 1);
	static int chart_parsing_timer = -1;
	if(chart_parsing_timer == -1)chart_parsing_timer = new_timer("chart parsing");
	start_timer(chart_parsing_timer);

	if(trace)printf("finished packing\n");

	t_setup = timer();

	// do the actual parsing
	int	half = nwords/2;
	double t_half;
	while( !cancel_parse && (edge = next_agenda(0)) )
	{
		if(edge->to-edge->from >= half)
		{
			half = nwords+2;
			t_half = timer();
			//halftime_analysis();
		}
		if(parse_process_edge(edge))break;
	}

	stop_timer(chart_parsing_timer, 1);

	if(g_profiling)start_and_alloc_profiler(&unpacking_profiler, "unpacking", parse_profiler, chart_parsing_profiler);

	t_forest = t_half + timer();
	cforest = clock() - start;

	alarm(0);

	if(do_forest)
	{
		output_forest(&cells[chart_size-1]);
		num_parses = 1;
	}
	else if(!do_itsdb && !lui_mode)
	{
		didsent = 0;
		if(chart_size>0)
			num_parses = iterate_cell_root_hypotheses(&cells[chart_size-1], parse_show_result, best_only);
		else num_parses = 0;
		if(num_parses && !inhibit_results)printf("\n\n");
		fflush(stdout);

		fprintf(stderr, "NOTE: %d readings, added %d / %d edges to chart (%d fully instantiated, %d actives used, %d passives used)\t",
			num_parses, total_edges, real_edges, passive_edges, used_edges, pused_edges);
		print_slab_stats();
	}
	else if(do_itsdb)
	{
		num_parses = itsdb_report(0, (int)((long long)cforest*1000 / CLOCKS_PER_SEC));
	}
	else if(lui_mode)
	{
		num_parses = output_lui_trees(cells+chart_size-1, current_sentence, best_only, 0);
		if(!num_parses){}//output_lui_chart(words);
		last_token_chart = token_chart;
	}

//	extern int	output_edge_vectors;
//	if(output_edge_vectors)
//		do_output_edge_vectors();

	t_unpack = timer();

	used_total += used_edges;
	pused_total += pused_edges;

	if(0)
	{
		static FILE	*f = 0;
		//extern long long ndecomposed;
		//static long long ond = 0;
		if(!f)f = fopen("/tmp/times", "w");
		fprintf(f, "%d	%d	%f	%f	%f	%s\n", nwords, num_parses, t_setup, t_forest, t_unpack, current_sentence);
		//ond = ndecomposed;
		fflush(f);
	}

	return num_parses;
}

void	preprocess_line(char	*string, int	*Nwords, char	***Words, int	**CFrom, int	**CTo);

int	parse_line(char	*line)
{
	char	copy[10240];
	int	nparse = 0;
	int	nwords = 0, i, j;
	int		*cfrom, *cto;
	char	**words, *p, *q;
	extern int preprocess_only;

	last_token_chart = NULL;

	if(yy_mode)
	{
		current_sentence = "(yy mode)";
		return parse_yy(line);
	}

	timer();
	if(g_profiling)
	{
		if(parse_profiler)stop_profilers_recursively(parse_profiler);
		start_and_alloc_profiler(&parse_profiler, "parsing", NULL, NULL);
		start_and_alloc_profiler(&preprocess_profiler, "REPP", parse_profiler, NULL);
	}

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

	preprocess_line(copy, &nwords, &words, &cfrom, &cto);
	if(preprocess_only)
	{
		if(!inhibit_results)
		{
			for(i=0;i<nwords;i++)
				printf("%s%s", i?" ":"", words[i]);
			printf("\n");
		}
		goto finished;
	}

	if(nwords>70)
	{
		fprintf(stderr, "NOTE: 0 readings, too many words (70+)\n");
		if(!inhibit_results){printf("SKIP: %s\n", line);fflush(stdout);}
		goto finished;
	}

	current_sentence = line;
	level = 0;
	timer();
	nparse = parse(nwords, words, cfrom, cto, current_sentence);
	if(nparse<=0 && !inhibit_results){printf("SKIP: %s\n", line);fflush(stdout);}

#ifdef	LEVEL_TIMER
	level_times[nwords] = timer();
	for(i=1;i<=nwords;i++)printf(" level %d: %f\n", i, level_times[i]);
#endif

	if(nparse==-1)fprintf(stderr, "NOTE: ignoring `%s'\n", current_sentence);
	if(do_itsdb && nparse==-1)itsdb_report(-1, 0);

finished:
	if(g_profiling)stop_profilers_recursively(parse_profiler);
	clear_mrs();
	clear_slab();
	for(i=0;i<nwords;i++)free(words[i]);
	free(words);
	free(cfrom);
	free(cto);
	return nparse;
}

show_last_parse_chart()
{
	if(!last_token_chart)
		fprintf(stderr, "no chart to display.\n");
	else output_lui_chart(last_token_chart);
}
