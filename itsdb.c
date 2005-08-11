#include	<sys/fcntl.h>
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<time.h>

#include	<itsdb.h>

#include	"chart.h"
#include	"rule.h"
#include	"lexicon.h"

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
	extern int last_mem_bytes;
	int readings, msec;
	struct timeval tv1, tv2;
	clock_t	c0, c1;

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
	readings = parse_line(i_input);
	gettimeofday(&tv2, 0);
	c1 = clock();
	msec = 1000*(tv2.tv_sec - tv1.tv_sec) + 0.001*(tv2.tv_usec - tv1.tv_usec);

	capi_printf(" (:total . %d) (:treal . %d) (:tcpu . %d)", msec, msec, (int)((c1-c0)*0.001));
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
				// XXX fix for MWEs
				DER_PRINTF(" (\\\"%s\\\" %d %d)", words[e->from], e->from, e->to);
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
	if(!capi_register(create_test_run, process_item, reconstruct_item, complete_test_run))
		slave();
	close(fd);
}
