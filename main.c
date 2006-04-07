#include	<stdio.h>
#include	<signal.h>
#include	<sys/time.h>
#include	<getopt.h>
#include	<unistd.h>
#include	<string.h>
#include	<ctype.h>
#include	<locale.h>
#include	"chart.h"
#include	"net.h"
#include	"freeze.h"
#include	"rule.h"
#include	"lexicon.h"
#include	"mrs.h"
#ifdef POST
#include	"post.h"
#endif

int	debug_level = 0, export_parts = 0, do_itsdb = 0;
char	*save_grammar_path = 0;
char	*load_grammar_path = 0;
char	*sentence_path = 0, *master_addr = 0;
int	question = 0, exhaustive = 1, lui_mode = 0, inhibit_pack = 0, best_only = 0;

int	preprocess_only = 0;
extern char mrs_line_breaks;

int g_mode = PARSING;
int	die = 0;

extern int	g_loaded, inhibit_results, trace;
extern long long	accum_ram_usage;
extern char			*use_roots;
float	stdin_time = 0;

extern float timer();

int	parse_line(char	*line);

FILE	*open_sentence_source()
{
	FILE	*fin;
	assert(sentence_path != 0);
	if(!strcmp(sentence_path, "-"))
	{
		if(master_addr)
			connect_to_master(master_addr);
		fin = stdin;
	}
	else
	{
		assert(master_addr == 0 || !sentence_path);
		fin = fopen(sentence_path, "r");
	}
	if(!fin) { perror(sentence_path); exit(-1); }
	return fin;
}

void broken_pipe_handler(int sig) { die = 1; }

void	setup_signal_handlers()
{
	signal(SIGSEGV, print_current_sentence);
	signal(SIGBUS, print_current_sentence);
	signal(SIGPIPE, broken_pipe_handler);
}

void	parse_args(int	argc, char	*argv[])
{
	char	*prog = argv[0], *tla;
	extern int nofragments;
	unsigned char ch;

	tla = prog + strlen(prog) - 3;
	if(tla >= prog)
	{
		if(!strcmp(tla, "ape"))g_mode = PARSING;
		if(!strcmp(tla, "age"))g_mode = GENERATING;
	}

	while((ch=getopt(argc, argv, "hvpfEPlXg:G:m:qtRe1n:r:"))!=255)switch(ch)
	{
		case	'h':	usage: printf("usage: %s [-h] [-v] [-l] [-p] [-X] [-c] [-m masternode] [-g grammar-to-load] [-G grammar-to-save] [sentences-file]\n", prog); exit(0);
		case	'v':	if(debug_level)trace++; debug_level++;	break;
		case	'g':	load_grammar_path = optarg;	break;
		case	'G':	save_grammar_path = optarg;	break;
		case	'E':	preprocess_only = 1;		break;
		case	'P':	export_parts = 1;		break;
		case	'm':	master_addr = optarg;	break;
		case	'q':	question = 1;			break;
		case	'X':	exhaustive = 0;			break;
		case	'1':	best_only = 1;			break;
		case	'n':	best_only = atoi(optarg);	break;
		case	'r':	use_roots = optarg;	if(strstr(use_roots, "root_frag"))nofragments = 0;	break;
		case	'l':	lui_mode = 1;			break;
		case	'f':	mrs_line_breaks = '\n';		break;
		case	'p':	inhibit_pack = 1;		break;
		case	'R':	inhibit_results = 1;		break;
		case	't':	do_itsdb = 1;			break;
		case	'e':	g_mode = GENERATING;	break;
		case	'?':	goto usage;
	}
	if(optind<argc-1)goto usage;
	if(optind<argc)sentence_path = argv[optind];
	else if(!save_grammar_path)sentence_path = "-";
}

struct job_stats
{
	int			success, fail, count;
	long long	ram;
	float		time;
};

struct job_stats	main_input_loop(FILE	*fin)
{
	struct job_stats	st = {.success = 0, .fail = 0, .count = 0, .time = 0};
	struct timeval		tv1, tv2;
	float				stdin_time = 0;

	gettimeofday(&tv1, 0);
	while(!die && !feof(fin))
	{
		int num_results;
		clear_slab();
		if(g_mode == PARSING)
		{
			char	line[10240];
			if(fin==stdin)timer();
			if(!fgets(line, 10240, fin))break;
			if(fin==stdin)stdin_time += timer();
			num_results = parse_line(line);
		}
		else
		{
			if(fin==stdin)timer();
			struct mrs	*mrs = read_mrs(fin);
			if(fin==stdin)stdin_time += timer();
			if(!mrs && feof(fin))break;
			num_results = generate(mrs);
			free_mrs(mrs);
		}
		if(num_results==-1)fprintf(stderr, "NOTE: ignore\n");
		else if(num_results>0)st.success++;
		else if(num_results==0)st.fail++;
		if(num_results>=0)st.count++;
		fflush(stderr);
	}
	if(fin==stdin)stdin_time += timer();
	gettimeofday(&tv2, 0);

	tv2.tv_sec -= tv1.tv_sec;
	tv2.tv_usec -= tv1.tv_usec;
	clear_slab();
	st.ram = accum_ram_usage;
	st.time = (float)tv2.tv_sec + (float)tv2.tv_usec/1000000 - stdin_time;
	return st;
}

void	initialize_engine()
{
	setup_carcs_stack();
	init_glb_cache();

	if(!save_grammar_path)
		load_frozen_grammar(load_grammar_path);
	else
	{
		struct timeval	tv1, tv2;
		if(!load_grammar_path)
			load_grammar_path = "../erg/ace/config.tdl";
		setup_save_frozen_grammar(save_grammar_path);

		gettimeofday(&tv1, 0);
		load_grammar(load_grammar_path);
#ifdef	POST
		post_train_from("../post/brown.train.txt");
#endif
		gettimeofday(&tv2, 0);
		tv2.tv_sec -= tv1.tv_sec;
		tv2.tv_usec -= tv1.tv_usec;
		fprintf(stderr, "loaded grammar in %.5fs\n", (float)tv2.tv_sec + (float)tv2.tv_usec/1000000);

		freeze_grammar();
	}

	if(save_grammar_path)
		exit(0);

	setup_extra_type_dgs();
	g_loaded = 1;

	install_rf(inhibit_pack?0:((g_mode == PARSING)?1:2));

	if(export_parts)
	{
		dump_parts_of_speech();
		exit(0);
	}

	//print_rule("hspec");
	//lui_type("j-head");
	//print_rule("adjh_i_ques");
	//print_type("adj_head_int_ques_phrase");
	//print_type("basic_adj_head_phrase");
	//print_type("phrasal");
	//print_lexeme("delete_v2");

	//check_unary_chains();
}

void	report_stats(struct job_stats	stats)
{
	if(stats.count)
		fprintf(stderr, "NOTE: %s %d / %d sentences, avg %dk, time %.5fs\n", (g_mode==PARSING)?"parsed":"generated",
			stats.success, stats.count, (int)(stats.ram / stats.count / 1024), (float)stats.time);

	rule_stats();
	print_glb_stats();
	final_chart_stats();
}

int	main(int	argc, char	*argv[])
{
	struct job_stats	stats;

	setlocale(LC_ALL, "");
	parse_args(argc, argv);
	initialize_engine();

	if(do_itsdb) itsdb_mode();
	setup_signal_handlers();

	stats = main_input_loop(open_sentence_source());

	shutdown_net();
	report_stats(stats);
	wait_for_lui();

	if(stats.success<=0 && stats.count>0)return -1;
	else return 0;
}
