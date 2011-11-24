#include	<sys/fcntl.h>
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<time.h>
#include	<sys/time.h>
#include	<stdarg.h>

/* notes:

There are 4 top-level entry points where the ITSDB communication loop calls back to ACE:

create_test_run()
	reports the name of the engine back to ITSDB

process_item()
	either parses or generates from text input, for one item.
		parse_line(blah) ... calls back to:
			itsdb_report() ... which reports various stats and calls:
				capi_print_derivations() .. which calls the unpacker to call:
					capi_print_tree() .. which reports individual results
		generate(blah) ... calls back to:
			itsdb_start_results()
			itsdb_report_genresult()
			itsdb_end_results()
			itsdb_report_gen()
	measures and reports :total = :tcpu (clock()) and :treal (wallclock), and possibly :error

reconstruct_item()
	we don't support this at the moment

complete_test_run()
	uncertain what this is supposed to do.


questions:
	- what is complete_test_run() supposed to do?
	- what is reconstruct_item() supposed to do?
	- how is ITSDB supposed to tell us whether to parse or to generate (or to transfer)?
		currently, we get that info from the commandline invocation, but it would be nice to be able to present a single CPU definition and have ITSDB tell us which operation it is interested in.
	- are we reporting the right memory usage?
	- what is the correct interpretation of each of the time reports?
	- how do we send back MRSes?
	- how do we send back a tree for generation?

*/

#ifdef	TSDB
#include	<itsdb.h>
#else
#define capi_printf	printf
//#define capi_printf(fmt,...)
#endif

#include	"chart.h"
#include	"rule.h"
#include	"lexicon.h"
#include	"mrs.h"
#include	"unpack.h"

//#define	capi_printf(fmt...)	fprintf(stderr, fmt)

void tsdb_report_derivation(struct hypothesis	*hyp);
void tsdb_report_mrs(struct mrs	*mrs);

extern int inhibit_results;

const int itsdb_wants_mrs = 1;

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

int tsdb_nanalyses, tsdb_maxanalyses, tsdb_nderivations, tsdb_maxderivations=1000;

static int process_item(int i_id, const char *i_input, int parse_id, 
		int edges, int nanalyses, int nderivations, int interactive, const char	*who_knows_what_this_argument_is)
{
	extern int total_edges, real_edges, passive_edges;
	int readings;
	struct timeval tv1, tv2;
	clock_t	c0, c1;
	char	*error = 0;

	//fprintf(stderr, "process_item asked for: %d = nanalyses, %d = nderivations\n", nanalyses, nderivations);
	tsdb_maxanalyses = nanalyses;
	tsdb_maxderivations = nderivations;
	tsdb_nanalyses = 0;
	tsdb_nderivations = 0;

	inhibit_results = 1;
	best_only = nanalyses;//nderivations;
	tsdb_i_id = i_id;

	top_copies = top_unifies_wellform = top_unifies = 0;

	extern int want_mrs;
	want_mrs = itsdb_wants_mrs;

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

	int	wallclock_msec = 1000*(tv2.tv_sec - tv1.tv_sec) + 0.001*(tv2.tv_usec - tv1.tv_usec);
	int	cpuclock_msec = (int)((long long)(c1-c0)*1000 / CLOCKS_PER_SEC);
	capi_printf(" (:total . %d) (:treal . %d)", cpuclock_msec, wallclock_msec);
	capi_printf(" (:tcpu . %d)", cpuclock_msec);

	if(error)capi_printf(" (:error \"%s\")", error);
}

itsdb_report(int	status, int	tforest)
{
	extern int total_edges, real_edges, passive_edges, slab_usage();

	if(status == 0)
	{
		capi_printf("(:copies . %d) (:unifications . %d) ", top_copies, top_unifies + top_unifies_wellform);
		//capi_printf("(:tcpu . %d) ", tforest);

		int	readings = capi_print_derivations();

		capi_printf("(:readings . %d) (:pedges . %d) (:aedges . %d) ",
//			readings,
			tsdb_nanalyses,
			passive_edges, real_edges - passive_edges);

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
//		readings,
		tsdb_nanalyses,
		passive_edges, real_edges - passive_edges);

	capi_printf("(:others . %d) ", slab_usage());

	capi_printf("(:copies . %d) (:unifications . %d) ", top_copies, top_unifies + top_unifies_wellform);
}

itsdb_start_results() { capi_printf("(:results . ("); }
itsdb_end_results() { capi_printf(")) "); }

itsdb_report_genresult(struct hypothesis *hyp, struct mrs *mrs, int id)
{
	char		*esc = malloc(strlen(hyp->string)*2+1), *p, *q;

	tsdb_nanalyses++;

	if(tsdb_nderivations >= tsdb_maxderivations)return;
	tsdb_nderivations++;

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
	//fprintf(stderr, "ITSDB output '%s'\n", esc);

	// report the derivation and the MRS as well
	tsdb_report_derivation(hyp);
	if(itsdb_wants_mrs)
		tsdb_report_mrs(mrs);

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

void capi_escape_and_print_long_string(char	*unesc)
{
	char	*hesc = heap_escape(unesc), *esc = hesc;
	// capi_printf can only handle 1kb chunks at a time... :blink:
	// if we blindly split text up at 1kb boundaries,
	// we stand a chance of splitting in the middle of a multibyte character.
	// capi_printf() apparently has a problem with that...
	// soooo... be more careful?
	while(strlen(esc) > 1000)
	{
		// figure out how many characters less than 1000
		// we have to send to avoid stopping in the middle of a multibyte sequence.
		// this code assumes characters in the middle of a multibyte sequence
		// are >= 128. (true for UTF-8)
		int	snark = 0;
		while(0x80 & esc[1000-snark])
			snark--;
		capi_printf("%.*s", 1000-snark, esc);
		esc += 1000-snark;
	}
	capi_printf("%s", esc);
	free(hesc);
}

void tsdb_report_derivation(struct hypothesis	*hyp)
{
	capi_printf(" (:derivation .\"");
	char	*derivation = hypothesis_to_dstring(hyp);
	capi_escape_and_print_long_string(derivation);
	free(derivation);
	capi_printf("\")");
}

void tsdb_report_mrs(struct mrs	*mrs)
{
	capi_printf(" (:mrs .\"");
	static char	*buffer = NULL;
	#define	MRS_BUFFER_LEN	(1024*32)
	if(!buffer)buffer = malloc(MRS_BUFFER_LEN);
	int l = snprint_mrs(buffer, MRS_BUFFER_LEN-1, mrs);
	if(l < MRS_BUFFER_LEN-1)capi_escape_and_print_long_string(buffer);
	else
	{
		char	*tmp = malloc(l+10);
		snprint_mrs(tmp, l+10, mrs);
		capi_escape_and_print_long_string(tmp);
		free(tmp);
	}
	capi_printf("\")");
}

int	capi_print_derivations()
{
	int id;

	id = 0;
	int capi_print_tree(struct hypothesis	*hyp, struct mrs	*mrs)
	{
		tsdb_nanalyses++;
		if(tsdb_nderivations >= tsdb_maxderivations)return 1;
		tsdb_nderivations++;

		capi_printf("((:result-id . %d)", id++);

		tsdb_report_derivation(hyp);

		if(itsdb_wants_mrs)
			tsdb_report_mrs(mrs);

		capi_printf(" (:score .%f))", hyp->score);
#ifndef TSDB
		capi_printf("\n");
#endif
		return 1;
	}
	itsdb_start_results();
	int	n = iterate_cell_root_hypotheses(&cells[chart_size-1], capi_print_tree, best_only);
	itsdb_end_results();
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
