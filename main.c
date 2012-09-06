#include	<stdio.h>
#include	<errno.h>
#include	<stdarg.h>
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
#include	"transfer.h"
#ifdef POST
#include	"post.h"
#endif
#include	"version.h"
#include	"tnt.h"

#include	"timer.h"

int	debug_level = 0, export_parts = 0, do_itsdb = 0;
extern int itsdb_dump_chart;
char	*save_grammar_path = 0;
char	*load_grammar_path = 0;
char	*sentence_path = 0, *master_addr = 0;
int	question = 0, exhaustive = 1, lui_mode = 0, inhibit_pack = 0, best_only = 0;
int timeout_seconds = 0;

int	dont_unspecialize = 0;

int	output_edge_vectors = 0;
char	*generation_server_lang = NULL;

int	preprocess_only = 0;
extern char mrs_line_breaks;

int g_mode = PARSING, print_trees = 1;
int	die = 0;

extern int enable_tnt;
char	*tnt_model_path = NULL;
int		tnt_max_tags = 2;

extern int	g_loaded, inhibit_results, trace;
extern int yy_mode;
extern long long	accum_ram_usage;
extern char			*use_roots, *disable_signs;
extern int do_forest;
extern int g_profiling;
float	stdin_time = 0;

int	form_to_relation = 0;
int	show_realization_trees = 0;
int	show_realization_mrses = 0;
int report_labels = 0;
extern int equivalence_packing_only;

int	max_chart_megabytes = 1200, max_unpack_megabytes = 1500;
int preexisting_lui_fd = -1;
int input_from_lui = 0;

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

	struct option long_options[] = {
		{"show-realization-trees", 0, &show_realization_trees, 1},
		{"show-realization-mrses", 0, &show_realization_mrses, 1},
		{"equivalence-packing", 0, &equivalence_packing_only, 1},
		{"itsdb-forest", 0, &itsdb_dump_chart, 1},
		{"report-labels", 0, &report_labels, 1},
		{"input-from-lui", 0, &input_from_lui, 1},
#define	MAX_UNPACK_MEGABYTES_OPTION	1000
		{"max-unpack-megabytes", 1, NULL, MAX_UNPACK_MEGABYTES_OPTION},
#define	MAX_CHART_MEGABYTES_OPTION	1001
		{"max-chart-megabytes", 1, NULL, MAX_CHART_MEGABYTES_OPTION},
#define GENERATION_SERVER_OPTION	1002
		{"generation-server", 1, NULL, GENERATION_SERVER_OPTION},
#define LUI_FD_OPTION			1003
		{"lui-fd", 1, NULL, LUI_FD_OPTION},
#define	TNT_MODEL_OPTION		1004
#define	TNT_MAX_TAGS_OPTION		1005
		{"tnt-model", 1, NULL, TNT_MODEL_OPTION},
		{"tnt-max-tags", 1, NULL, TNT_MAX_TAGS_OPTION},
#define	TIMEOUT_OPTION			1006
		{"timeout", 1, NULL, TIMEOUT_OPTION},
		{NULL,0,NULL,0}
		};

	int	gorv;
	while(1)
	{
		gorv = getopt_long(argc, argv, "VhvpfEPL:lXg:G:m:qtTRe1n:r:jFOsiy", long_options, NULL);
		if(gorv == -1)break;
		switch(gorv) {
		case	'V':	printf("ACE version %s\n", ACE_VERSION);
						printf("compiled at " __TIME__ " on "  __DATE__ "\n");
						exit(0);
		case	'h':	usage: printf("usage: %s [-l | -n COUNT] [-R] [-f] [-e] [-g grammar-to-load] [-G grammar-to-save] [sentences-file]\n", prog); exit(0);
		case	'v':	if(debug_level)trace++; debug_level++;	break;
		case	'y':	yy_mode = 1; break;
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

		case	MAX_UNPACK_MEGABYTES_OPTION:	max_unpack_megabytes = atoi(optarg);	break;
		case	MAX_CHART_MEGABYTES_OPTION:		max_chart_megabytes = atoi(optarg);		break;
		case	GENERATION_SERVER_OPTION:	generation_server_lang = optarg;	break;
		case	LUI_FD_OPTION:	preexisting_lui_fd = atoi(optarg);	break;
		case	TNT_MODEL_OPTION:	enable_tnt = 1; tnt_model_path = optarg; break;
		case	TNT_MAX_TAGS_OPTION: tnt_max_tags = atoi(optarg); break;
		case	TIMEOUT_OPTION: timeout_seconds = atoi(optarg); break;
		case	0:	continue;	// auto-handled long option
	} }
	if(optind<argc-1)goto usage;
	if(!load_grammar_path)goto usage;
	if(optind<argc)sentence_path = argv[optind];
	else if(!save_grammar_path)sentence_path = "-";
	if(generation_server_lang)
		g_mode = GENERATING;
	if(enable_tnt)assert(0 == tnt_init(tnt_model_path, tnt_max_tags));
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

int		narbiterinputs;
char	**arbiterinputs;
char	**arbiter_job_ids;

char	*arbiter_partial_line;
int		arbiter_partial_line_length, arbiter_partial_line_length_a = 0;
int		arbiter_fd = 0;

void	make_blocking(int	fd)
{
	int	opt = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, opt & ~O_NONBLOCK);
}

void	make_nonblocking(int	fd)
{
	int	opt = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, opt | O_NONBLOCK);
}

void	arbiter_status(char	*format, ...)
{
	va_list	v;
	va_start(v, format);
	printf(":s ");
	vprintf(format, v);
	printf("\n");
	fflush(stdout);
	va_end(v);
}

void	arbiter_enqueue(char	*id, char	*input)
{
	narbiterinputs++;
	arbiterinputs = realloc(arbiterinputs, sizeof(char*)*narbiterinputs);
	arbiterinputs[narbiterinputs-1] = input;
	arbiter_job_ids = realloc(arbiter_job_ids, sizeof(char*)*narbiterinputs);
	arbiter_job_ids[narbiterinputs-1] = id;
	arbiter_status("queuelen=%d", narbiterinputs);
}

void	arbiter_ram(char	*mem)
{
	int	forest, unpack;
	if(2 == sscanf(mem, ":r %d %d", &forest, &unpack))
	{
		max_chart_megabytes = forest;
		max_unpack_megabytes = unpack;
	}
	arbiter_status("chartlimit=%d", max_chart_megabytes);
	arbiter_status("unpackinglimit=%d", max_unpack_megabytes);
}

void	arbiter_time(char	*res)
{
	int	max;
	if(1 == sscanf(res, ":t %d", &max))
		timeout_seconds = max;
	arbiter_status("timelimit=%d", timeout_seconds);
}

void	arbiter_count(char	*res)
{
	int	max;
	if(1 == sscanf(res, ":n %d", &max))
		best_only = max;
	arbiter_status("resultlimit=%d", best_only);
}

char	*current_arbiter_job_id = NULL;

void	arbiter_cancel()
{
	if(current_arbiter_job_id)
	{
		extern int cancel_parse;
		cancel_parse = 1;
		arbiter_status("cancelling %s", current_arbiter_job_id);
	}
	else arbiter_status("no job to cancel");
}

void	arbiter_process(int	len)
{
	arbiter_partial_line[len] = 0;
	if(arbiter_partial_line[0] != ':')
	{
		arbiter_status("missing colon");
		return;
	}

	//arbiter_status("got '%s'", arbiter_partial_line);

	// starts with a ':' -- a special command
	switch(arbiter_partial_line[1])
	{
		case	'j':
			{ assert(arbiter_partial_line[2]==' '); char	*sp = strchr(arbiter_partial_line+3, ' ');
				if(!sp) { arbiter_status("no job id"); return; }
				*sp = 0;
				arbiter_enqueue(strdup(arbiter_partial_line+3), strdup(sp+1));
				break; }
		case	'r':	arbiter_ram(arbiter_partial_line);		break;
		case	't':	arbiter_time(arbiter_partial_line);		break;
		case	'n':	arbiter_count(arbiter_partial_line);	break;
		case	'c':	arbiter_cancel(arbiter_partial_line);	break;
		default:	arbiter_status("unknown command '%c'", arbiter_partial_line[1]);	break;
	}
}

void	read_from_arbiter()
{
	int	expect_more = 1;
	while(1)
	{
		char	buffer[1024];
		if(!narbiterinputs)make_blocking(arbiter_fd);
		else make_nonblocking(arbiter_fd);
		int		r = read(arbiter_fd, buffer, 1023);
		if(r == 0) { die = 1; break; }
		if(r < 0)
		{
			if(errno == EAGAIN)break;	// would have blocked
			restore_stderr();
			perror("read");
			die = 1;
			break;
		}
		if(arbiter_partial_line_length + r > arbiter_partial_line_length_a)
		{
			arbiter_partial_line_length_a = 1 + 2 * (arbiter_partial_line_length + r);
			arbiter_partial_line = realloc(arbiter_partial_line, arbiter_partial_line_length_a);
		}
		memcpy(arbiter_partial_line + arbiter_partial_line_length, buffer, r);
		arbiter_partial_line_length += r;
		int i;
		for(i=arbiter_partial_line_length-r;i<arbiter_partial_line_length;i++)
			if(arbiter_partial_line[i]=='\n')
			{
				arbiter_process(i);
				arbiter_partial_line_length -= i+1;
				memmove(arbiter_partial_line, arbiter_partial_line+i+1, arbiter_partial_line_length);
				i -= i+1;
			}
	}
}

char	*arbiter_next_input()
{
	if(!narbiterinputs)make_blocking(arbiter_fd);
	read_from_arbiter();
	if(narbiterinputs)
	{
		char	*input = arbiterinputs[0];
		current_arbiter_job_id = arbiter_job_ids[0];
		--narbiterinputs;
		memmove(arbiterinputs, arbiterinputs+1, sizeof(char*)*narbiterinputs);
		memmove(arbiter_job_ids, arbiter_job_ids+1, sizeof(char*)*narbiterinputs);
		return input;
	}
	return NULL;
}

struct job_stats	arbiter_input_loop(FILE	*fin)
{
	struct job_stats	st = {.success = 0, .fail = 0, .count = 0, .time = 0};
	struct timeval		tv1, tv2;
	struct timeval		jtv1, jtv2;
	float				stdin_time = 0;

	arbiter_fd = 0;
	gettimeofday(&tv1, 0);
	while(!die && !feof(fin))
	{
		char	*next_input = arbiter_next_input();
		if(!next_input)break;
		arbiter_status("start work job=%s", current_arbiter_job_id);
		gettimeofday(&jtv1, 0);
		int num_results;
		clear_slab();
		if(g_mode == PARSING)
			num_results = parse_line(next_input);
		else
		{
			clear_mrs();
			clear_slab();
			struct mrs	*mrs = parse_mrs(next_input);
			if(g_mode == GENERATING)
				num_results = generate(mrs);
			else num_results = transfer(mrs);
			free_mrs(mrs);
		}
		free(next_input);
		gettimeofday(&jtv2, 0);
		jtv2.tv_sec -= jtv1.tv_sec;
		jtv2.tv_usec -= jtv1.tv_usec;
		double	dt = jtv2.tv_sec + 0.000001 * jtv2.tv_usec;
		int slab_size_in_bytes();
		arbiter_status("stop work job=%s time=%f ram=%d results=%d", current_arbiter_job_id, dt, slab_size_in_bytes(), num_results);
		free(current_arbiter_job_id);
		current_arbiter_job_id = NULL;
		if(num_results>0)st.success++;
		else if(num_results==0)st.fail++;
		if(num_results>=0)st.count++;
		fflush(stderr);
	}
	restore_stderr();
	//fprintf(stderr, "die = %d ; feof(fin) = %d\n", die, feof(fin));
	if(fin==stdin)stdin_time += timer();
	gettimeofday(&tv2, 0);

	tv2.tv_sec -= tv1.tv_sec;
	tv2.tv_usec -= tv1.tv_usec;
	clear_slab();
	st.ram = accum_ram_usage;
	st.time = (float)tv2.tv_sec + (float)tv2.tv_usec/1000000 - stdin_time;
	return st;
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
				if(line[1]=='c')
					show_last_parse_chart();
				num_results = -2;
			}
			else num_results = parse_line(line);
			//double et = stop_timer(tid, 1);
			//printf("%f seconds\n", et);
		}
		else
		{
			clear_mrs();
			clear_slab();
			if(fin==stdin)timer();
			struct mrs	*mrs = read_mrs(fin);
			if(fin==stdin)stdin_time += timer();
			if(!mrs && feof(fin))break;
			if(g_mode == GENERATING)
				num_results = generate(mrs);
			else num_results = transfer(mrs);
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

		if(strstr(save_grammar_path, ".tdl"))
		{
			fprintf(stderr, "CAUTION: refusing to write grammar file to a pathname containing `.tdl'.\nDid you swap the -G and -g options?\n");
			exit(-1);
		}
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

	if(g_transfer)g_mode = TRANSFER;

	if(g_mode != TRANSFER)
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

void	logon_generation_server(char	*langcode)
{
	int	done = 0;
	void quitme(int sig) { done = 1; }
	char	path[10240];
	// LKB pretty much explicitly uses ~/tmp/
	sprintf(path, "%s/tmp/.transfer.%s.%s", getenv("HOME"), getlogin(), langcode);
	fprintf(stderr, "NOTE: generation server watching path: %s\n", path);
	signal(SIGINT, quitme);
	while(!done)
	{
		// this "protocol" is pretty hokey...
		FILE	*f = fopen(path, "r");
		if(!f) { usleep(100000); continue; }
		clear_mrs();
		clear_slab();
		struct mrs	*m = read_mrs(f);
		if(m)
		{
			int	nresults = generate(m);
		}
		else
		{
			fprintf(stderr, "NOTE: ignoring unreadable MRS\n");
		}
		fclose(f);
		unlink(path);
	}
	exit(0);
}

int	main(int	argc, char	*argv[])
{
	struct job_stats	stats;

	setlocale(LC_ALL, "");
	setlocale(LC_NUMERIC, "POSIX");

	parse_args(argc, argv);
	initialize_engine();

	if(do_itsdb) itsdb_mode();
	setup_signal_handlers();
	if(preexisting_lui_fd != -1)
	{
		lui_initialize();
		if(input_from_lui)
		{
			input_from_lui_mode();
			return;
		}
	}

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

	if(master_addr)
		stats = arbiter_input_loop(open_sentence_source());
	else if(!generation_server_lang)
		stats = main_input_loop(open_sentence_source());
	else logon_generation_server(generation_server_lang);

	shutdown_net();
	report_stats(stats);
	if(g_profiling)
	{
		if(g_mode == PARSING)
			report_profiler(parse_profiler);
		if(g_mode == GENERATING)
		{
			report_profiler(generation_profiler);
			report_profiler(transfer_profiler);
		}
	}
	if(debug_level)freport_timers(stderr);
	if(lui_mode)
	{
		quit_lui();
		wait_for_lui();
	}
	if(enable_tnt)tnt_kill();

	if(stats.success<=0 && stats.count>0)return -1;
	else return 0;
}
