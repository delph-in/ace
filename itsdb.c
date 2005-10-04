#include	<sys/fcntl.h>
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<time.h>
#include	<sys/time.h>

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

#ifdef	TSDB
	DER_PRINTF("(:results . (");
	id = 0;
	void capi_print_tree(struct hypothesis	*hyp, struct mrs	*mrs)
	{
		if(itsdb_report_derivations && id>=itsdb_report_derivations)return;
		DER_PRINTF("((:result-id . %d) (:derivation .\"", id++);
		print_hypothesis(hyp, capi_printf);
		DER_PRINTF("\"))");
	}
	iterate_cell_root_hypotheses(&cells[chart_size-1], capi_print_tree, best_only);
	DER_PRINTF("))");
#endif
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
