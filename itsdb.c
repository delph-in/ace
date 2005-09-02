#include	<sys/fcntl.h>
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<time.h>

#ifdef	TSDB
#include	<itsdb.h>
#else
#define capi_printf(fmt,...)
#endif

#include	"chart.h"
#include	"rule.h"
#include	"lexicon.h"
#include	"mrs.h"
#include	"unpack.h"

extern int inhibit_results;

static int create_test_run(char *data, int run_id, char *comment,
		int interactive, int protocol_version, char *custom)
{
	char platform [1024];

	if(protocol_version > 0 && protocol_version <= 2) {

#if defined(__GNUC__) && defined(__GNUC_MINOR__)
		sprintf(platform, "gcc %d.%d", __GNUC__, __GNUC_MINOR__);
#else
		sprintf(platform, "unknown");
#endif

		capi_printf("(:application . \"answer\") (:platform . \"%s\")",
				platform);
	}
	return 0;
}

extern int exhaustive, inhibit_pack;
extern long top_unifies, top_unifies_wellform, top_copies;

int itsdb_report_derivations;

static int process_item(int i_id, char *i_input, int parse_id, 
		int edges, int nanalyses, int nderivations, int interactive)
{
	extern int total_edges, real_edges, passive_edges;
	int readings, msec;
	struct timeval tv1, tv2;
	clock_t	c0, c1;
	char	*error = 0;

	if(nanalyses == 0)
	{
		exhaustive = 1;
		inhibit_pack = 1;
	}
	else
	{
		exhaustive = 0;
		inhibit_pack = 0;
	}
	inhibit_results = 1;

	itsdb_report_derivations = nderivations;

	top_copies = top_unifies_wellform = top_unifies = 0;

	gettimeofday(&tv1, 0);
	c0 = clock();
	if(g_mode == PARSING)
		readings = parse_line(i_input);
	else
	{
		struct mrs *m = parse_mrs(i_input);
		if(!m)
		{
			readings = -1;
			error = "unable to parse mrs input";
		}
		else
		{
			print_mrs(m);
			readings = generate(m);
			free_mrs(m);
		}
	}
	gettimeofday(&tv2, 0);
	c1 = clock();
	msec = 1000*(tv2.tv_sec - tv1.tv_sec) + 0.001*(tv2.tv_usec - tv1.tv_usec);

	capi_printf(" (:total . %d) (:treal . %d) (:tcpu . %d)", msec, msec, (int)((c1-c0)*0.001));
	if(error)capi_printf(" (:error \"%s\")", error);
}

itsdb_report(int nwords, char **words, int readings)
{
	extern int total_edges, real_edges, passive_edges, slab_usage();

	capi_printf("(:readings . %d) (:pedges . %d) (:aedges . %d) ",
		readings, passive_edges, real_edges - passive_edges);

	capi_printf("(:others . %d) ", slab_usage());

	capi_printf("(:copies . %d) (:unifications . %d) ", top_copies, top_unifies + top_unifies_wellform);

	capi_print_derivations(nwords, words);

	return 0;
}

itsdb_report_gen(int readings)
{
	extern int total_edges, real_edges, passive_edges, slab_usage();

	capi_printf("(:readings . %d) (:pedges . %d) (:aedges . %d) ",
		readings, passive_edges, real_edges - passive_edges);

	capi_printf("(:others . %d) ", slab_usage());

	capi_printf("(:copies . %d) (:unifications . %d) ", top_copies, top_unifies + top_unifies_wellform);
}

itsdb_start_results() { capi_printf("(:results . ("); }
itsdb_end_results() { capi_printf(")) "); }

itsdb_report_genresult(struct hypothesis *hyp, struct mrs *mrs, int id)
{
	char		*esc = malloc(strlen(hyp->string)*2+1), *p, *q;

#ifndef capi_printf
	capi_printf("((:result-id . %d) (:derivation . \"", id++);
	print_hypothesis(hyp, capi_printf);
	capi_printf("\") ");
#endif

	for(q=esc,p=hyp->string;*p;p++)
	{
		if(*p=='"' && *p=='\\')*q++ = '\\';
		*q++ = *p;
	}
	*q=0;
	capi_printf("(:tree . \"%s\") ", esc);
	free(esc);
	capi_printf("(:score . %f)) ", hyp->score);
}

#define DER_PRINTF capi_printf
capi_print_derivations(int nwords, char **words)
{
	int id;

	DER_PRINTF("(:results . (");
	id = 0;
	void capi_print_tree(struct edge *e)
	{
		void capi_print_tree0(struct edge *e)
		{
			int i;
			DER_PRINTF(" (%d %s %f %d %d", e->id, e->rule?e->rule->name:(e->lex?e->lex->word:"no-sign"), (float)e->prio, e->from, e->to);
			if(e->have)
				for(i=0;i<e->have;i++)
					capi_print_tree0(e->daughters[i]);
			else
			{
				int i;
				DER_PRINTF(" (\\\"");
				for(i=e->from;i<e->to;i++)
					DER_PRINTF("%s%s", (i==e->from)?"":" ", words[i]);
				DER_PRINTF("\\\" %d %d)", e->from, e->to);
			}
			DER_PRINTF(")");
		}
		if(itsdb_report_derivations && id>=itsdb_report_derivations)return;
		DER_PRINTF("((:result-id . %d) (:derivation .\"", id++);
		capi_print_tree0(e);
		DER_PRINTF("\"))");
	}
	visit_roots(capi_print_tree);
	DER_PRINTF("))");
}

static int complete_test_run(int run_id, char *custom)
{
	return 0;
}

static int reconstruct_item(char *derivation)
{
	return 0;
}

void itsdb_mode()
{
	int fd;
	close(1);
	dup2(fd = open("/tmp/answer.itsdb", O_WRONLY | O_CREAT, 0600), 1);
#ifdef TSDB
	if(!capi_register(create_test_run, process_item, reconstruct_item, complete_test_run))
		slave();
#endif
	close(fd);
}
