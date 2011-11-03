#include	<stdio.h>
#include	<signal.h>
#include	<sys/time.h>
#include	<getopt.h>
#include	<unistd.h>
#include	<string.h>
#include	<ctype.h>
#include	<locale.h>
#include	<sys/fcntl.h>
#include	"chart.h"
#include	"net.h"
#include	"freeze.h"
#include	"conf.h"
#include	"rule.h"
#include	"lexicon.h"
#include	"mrs.h"
#ifdef POST
#include	"post.h"
#endif
#include	"version.h"

#include	"timer.h"

int	debug_level = 0, export_parts = 0, do_itsdb = 0;
char	*save_grammar_path = 0;
char	*load_grammar_path = 0;
char	*sentence_path = 0, *master_addr = 0;
int	question = 0, exhaustive = 1, lui_mode = 0, inhibit_pack = 0, best_only = 0;

int	dont_unspecialize = 0;

int	output_edge_vectors = 0;

int	preprocess_only = 0;
extern char mrs_line_breaks;

int g_mode = PARSING, print_trees = 1;
int	die = 0;

extern int	g_loaded, inhibit_results, trace;
extern long long	accum_ram_usage;
extern char			*use_roots, *disable_signs;
extern int do_forest;
extern int g_profiling;
float	stdin_time = 0;

int	form_to_relation = 0;

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

	while((ch=getopt(argc, argv, "VhvpfEPL:lXg:G:m:qtTRe1n:r:jFOsi"))!=255)switch(ch)
	{
		case	'V':	printf("ACE version %s\n", ACE_VERSION);
						printf("compiled at " __TIME__ " on "  __DATE__ "\n");
						exit(0);
		case	'h':	usage: printf("usage: %s [-h] [-v] [-l] [-p] [-X] [-c] [-m masternode] [-g grammar-to-load] [-G grammar-to-save] [sentences-file]\n", prog); exit(0);
		case	'v':	if(debug_level)trace++; debug_level++;	break;
		case	'g':	load_grammar_path = optarg;	break;
		case	'G':	save_grammar_path = optarg;	break;
		case	'O':	do_forest = 1;	break;
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
		case	'T':	print_trees = 0;		break;
		case	'e':	g_mode = GENERATING;	break;
		case	'L':	disable_signs = optarg;	break;
		case	'j':	output_edge_vectors = 1;	break;
		case	'F':	form_to_relation = 1;	break;
		case	's':	dont_unspecialize = 1;	break;
		case	'i':	g_profiling = 1;		break;
		case	'?':	goto usage;
	}
	if(optind<argc-1)goto usage;
	if(!load_grammar_path)goto usage;
	if(optind<argc)sentence_path = argv[optind];
	else if(!save_grammar_path)sentence_path = "-";
}

struct job_stats
{
	int			success, fail, count;
	long long	ram;
	float		time;
};

show_heirarchy(char	*type)
{
	struct type	*t = lookup_type(type);
	if(!t) { printf("no such type '%s'\n", type); return -1; }

	int i;
	printf("parents:");
	for(i=0;i<t->nparents;i++)printf(" %s", t->parents[i]->name);
	printf("\n");
	printf("daughters:");
	for(i=0;i<t->ndaughters;i++)printf(" %s", t->daughters[i]->name);
	printf("\n");
}

show_glb(char	*input)
{
	struct type	*t = get_top();
	char	*p;
	for(p=strtok(input, " ");p && t;p=strtok(NULL, " "))
	{
		struct type	*t2 = lookup_type(p);
		if(!t2) { printf("no such type '%s'\n", p); return -1; }
		t = glb(t, t2);
	}
	printf("glb = %s\n", t?t->name:"BOTTOM");
}

struct job_stats	main_input_loop(FILE	*fin)
{
	struct job_stats	st = {.success = 0, .fail = 0, .count = 0, .time = 0};
	struct timeval		tv1, tv2;
	float				stdin_time = 0;
//	int	tid = new_timer((g_mode==PARSING)?"parsing":"generating");

//	if(g_mode == GENERATING)	// Stephan suggested this, but it's actually slower.
//		configure_rules_head_driven();

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
			//start_timer(tid);
			if(lui_mode && line[0]==':')
			{
				int	l = strlen(line);
				if(l>3)line[l-1] = 0;
				if(line[1]=='r' && line[2]==' ')
					lui_rule(line+3);
				if(line[1]=='l' && line[2]==' ')
					lui_lex(line+3);
				if(line[1]=='t' && line[2]==' ')
					lui_type(line+3);
				if(line[1]=='h' && line[2]==' ')
					show_heirarchy(line+3);
				if(line[1]=='g' && line[2]==' ')
					show_glb(line+3);
				num_results = -2;
			}
			else num_results = parse_line(line);
			//double et = stop_timer(tid, 1);
			//printf("%f seconds\n", et);
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
		if(num_results==-1 || num_results==-2)
			{ if(num_results==-1)fprintf(stderr, "NOTE: ignore\n"); }
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

		int fd = open(save_grammar_path, O_CREAT | O_TRUNC | O_RDWR, 0600);
		if(fd == -1)
		{
			perror(save_grammar_path);
			exit(-1);
		}

		gettimeofday(&tv1, 0);
		load_grammar(load_grammar_path);
#ifdef	POST
		if(enable_post)
		{
		//	post_train_from("../post/brown.train.txt");
			post_load_from_text_dump("post/english-postagger.hmm");
		}
#endif
		gettimeofday(&tv2, 0);
		tv2.tv_sec -= tv1.tv_sec;
		tv2.tv_usec -= tv1.tv_usec;
		fprintf(stderr, "loaded grammar in %.5fs\n", (float)tv2.tv_sec + (float)tv2.tv_usec/1000000);

		close(fd);
		setup_save_frozen_grammar(save_grammar_path);
		freeze_grammar();
		exit(0);
	}

	setup_extra_type_dgs();
	g_loaded = 1;

	install_rf(inhibit_pack?0:((g_mode == PARSING)?1:2));
	init_rstats();

	if(export_parts)
	{
		dump_parts_of_speech();
		exit(0);
	}

	//print_rule("hspec");
	//lui_type("j-head");
	//print_rule("adjh_i_ques");
	//print_type("adj_head_int_ques_phrase");
	//print_type("n_mass_count_le");
	//print_type("n_intr_le");
	//print_lexeme("delete_v2");

	//check_unary_chains();
	//decompose_rules();
}

void	report_stats(struct job_stats	stats)
{
	if(stats.count)
		fprintf(stderr, "NOTE: %s %d / %d sentences, avg %dk, time %.5fs\n", (g_mode==PARSING)?"parsed":"generated",
			stats.success, stats.count, (int)(stats.ram / stats.count / 1024), (float)stats.time);

	rule_stats();
	report_unpacking();
	if(debug_level)print_glb_stats();
	final_chart_stats();
	//show_me();
}

int	main(int	argc, char	*argv[])
{
	struct job_stats	stats;

	setlocale(LC_ALL, "");
	parse_args(argc, argv);
	initialize_engine();

	if(do_itsdb) itsdb_mode();
	setup_signal_handlers();

	//generalization_experiments();
	/*extern int nstrings;
	extern struct type	**strings;
	printf("%d strings\n", nstrings);
	int i;
	for(i=0;i<nstrings;i++)printf("%d	= %s\n", i, strings[i]->name);*/

	/*int i;
	extern int nfnames;
	extern char **fnames;
	for(i=0;i<nfnames;i++)printf("%s\n", fnames[i]);*/

	//treebank_control(); return 0;

	stats = main_input_loop(open_sentence_source());

	shutdown_net();
	report_stats(stats);
	if(g_profiling)
	{
		if(g_mode == PARSING)
		{
			extern struct profiler	*parse_profiler;
			report_profiler(parse_profiler);
		}
	}
	if(debug_level)freport_timers(stderr);
	wait_for_lui();

	if(stats.success<=0 && stats.count>0)return -1;
	else return 0;
}
