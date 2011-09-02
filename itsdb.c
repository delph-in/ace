#include	<sys/fcntl.h>
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<time.h>
#include	<sys/time.h>
#include	<stdarg.h>

#ifdef	TSDB
#include	<itsdb.h>
#else
//#define capi_printf	printf
#define capi_printf(fmt,...)
#endif

#include	"chart.h"
#include	"rule.h"
#include	"lexicon.h"
#include	"mrs.h"
#include	"unpack.h"

extern int inhibit_results;

static int create_test_run(const char *data, int run_id, const char *comment,
		int interactive, int protocol_version, const char *custom)
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

extern int best_only;

int	tsdb_i_id = -1;

static int process_item(int i_id, const char *i_input, int parse_id, 
		int edges, int nanalyses, int nderivations, int interactive, const char	*who_knows_what_this_argument_is)
{
	extern int total_edges, real_edges, passive_edges;
	int readings, msec;
	struct timeval tv1, tv2;
	clock_t	c0, c1;
	char	*error = 0;

	inhibit_results = 1;
	best_only = nderivations;
	tsdb_i_id = i_id;

	top_copies = top_unifies_wellform = top_unifies = 0;

	gettimeofday(&tv1, 0);
	c0 = clock();
	clear_slab();
	if(g_mode == PARSING)
		readings = parse_line(i_input);
	else
	{
		//fprintf(stderr, "mrs input: `%s'\n", i_input);
		char	*mutable_input = strdup(i_input);
		struct mrs *m = parse_mrs(mutable_input);
		if(!m)
		{
			readings = -1;
			error = "unable to parse mrs input";
		}
		else
		{
			//print_mrs(m);
			readings = generate(m);
			free_mrs(m);
		}
		free(mutable_input);
	}
	gettimeofday(&tv2, 0);
	c1 = clock();
	msec = 1000*(tv2.tv_sec - tv1.tv_sec) + 0.001*(tv2.tv_usec - tv1.tv_usec);

	capi_printf(" (:total . %d) (:treal . %d)", (int)((long long)(c1-c0)*1000 / CLOCKS_PER_SEC), msec);
	if(error)capi_printf(" (:error \"%s\")", error);
}

itsdb_report(int nwords, char **words, int	status, int	tforest)
{
	extern int total_edges, real_edges, passive_edges, slab_usage();

	if(status == 0)
	{
		capi_printf("(:copies . %d) (:unifications . %d) ", top_copies, top_unifies + top_unifies_wellform);
		capi_printf("(:tcpu . %d) ", tforest);

		int	readings = capi_print_derivations(nwords, words);

		capi_printf("(:readings . %d) (:pedges . %d) (:aedges . %d) ",
			readings, passive_edges, real_edges - passive_edges);

		capi_printf("(:others . %d) ", slab_usage());
		return readings;
	}
	else
	{
		capi_printf("(:copies . 0) (:unifications . 0) (:tcpu . 0) (:readings . 0) (:pedges . 0) (:aedges . 0) (:others . 0) ");
		return -1;
	}
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

	capi_printf("((:result-id . %d)", id++);
	
//#ifndef capi_printf
/*	capi_printf("(:derivation . \"");
	print_hypothesis(hyp, capi_printf);
	capi_printf("\") ");*/
//#endif


	for(q=esc,p=hyp->string;*p;p++)
	{
		if(*p=='"' && *p=='\\')*q++ = '\\';
		*q++ = *p;
	}
	*q=0;
	capi_printf("(:surface . \"%s\") ", esc);
	capi_printf("(:tree . \"%s\") ", esc);
	fprintf(stderr, "ITSDB output '%s'\n", esc);
	free(esc);
	capi_printf("(:score . %f)) ", hyp->score);
}

char	*heap_escape(char	*str)
{
	int	len = strlen(str) * 2 + 1;
	char	*esc = malloc(len), *ep = esc;
	while(*str)
	{
		if(*str=='\\' || *str=='"')
			*ep++ = '\\';
		*ep++ = *str++;
	}
	*ep = 0;
	return esc;
}

int	capi_print_derivations(int nwords, char **words)
{
	int id;

	capi_printf("(:results . (");
	id = 0;
	int capi_print_tree(struct hypothesis	*hyp, struct mrs	*mrs)
	{
		char	*derivation = hypothesis_to_dstring(hyp);
		char	*hesc = heap_escape(derivation), *esc = hesc;
		free(derivation);
		capi_printf("((:result-id . %d) (:derivation .\"", id++);
		// capi_printf can only handle 1kb chunks at a time... :blink:
		while(strlen(esc) > 1000)
		{
			capi_printf("%.*s", 1000, esc);
			esc += 1000;
		}
		capi_printf("%s", esc);
		free(hesc);
		capi_printf("\") (:score .%f))", hyp->score);
#ifndef TSDB
		capi_printf("\n");
#endif
		return 1;
	}
	extern int want_mrs;
	want_mrs = 0;
	int	n = iterate_cell_root_hypotheses(&cells[chart_size-1], capi_print_tree, best_only);
	capi_printf("))");
	return n;
}

static int complete_test_run(int run_id, const char *custom)
{
	return 0;
}

static int reconstruct_item(const char *derivation)
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
