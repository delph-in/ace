#include	<stdio.h>
#include	<signal.h>
#include	<sys/time.h>
#include	<unistd.h>
#include	<string.h>
#include	<ctype.h>
#include	"chart.h"
#include	"net.h"
#include	"freeze.h"
#include	"rule.h"
#include	"lexicon.h"
#include	"preprocessor.h"

int	debug_level = 1, export_parts = 0;
char	*save_grammar_path = 0;
char	*load_grammar_path = 0;
char	*sentence_path = 0;

extern int	g_loaded;
extern long long	accum_ram_usage;

char	*current_sentence = "(not initialized)";

int	parse_line(char	*line);

void	parse_args(int	argc, char	*argv[])
{
	char	*prog = argv[0], ch;

	while((ch=getopt(argc, argv, "hvlg:G:m:"))!=255)switch(ch)
	{
		case	'h':	usage: printf("usage: %s [-h] [-v] [-l] [-m masternode] [-g grammar-to-load] [-G grammar-to-save] [sentences-file]\n", prog); exit(0);
		case	'v':	debug_level++;	break;
		case	'g':	load_grammar_path = optarg;	break;
		case	'G':	save_grammar_path = optarg;	break;
		case	'l':	export_parts = 1;		break;
		case	'm':	connect_to_master(optarg);	break;
		case	'?':	goto usage;
	}
	if(optind<argc-1)goto usage;
	if(optind<argc)sentence_path = argv[optind];
	else if(!save_grammar_path)sentence_path = "-";

	if(load_grammar_path && save_grammar_path)
	{
		fprintf(stderr, "use cp(1) for duplicating grammars.\n");
		exit(-1);
	}
}

int	main(int	argc, char	*argv[])
{
	int	num_parses, success = 0, fail = 0, count = 0;
	struct timeval	tv1, tv2;
	char	line[10240];
	FILE	*fin;

	parse_args(argc, argv);

	gettimeofday(&tv1, 0);
	if(load_grammar_path)load_frozen_grammar(load_grammar_path);
	else load_grammar();
	gettimeofday(&tv2, 0);
	tv2.tv_sec -= tv1.tv_sec;
	tv2.tv_usec -= tv1.tv_usec;
	fprintf(stderr, "NOTE: loaded grammar in %.5fs\n", (float)tv2.tv_sec + (float)tv2.tv_usec/1000000);

	if(save_grammar_path)
		setup_save_frozen_grammar(save_grammar_path);

	if(!load_grammar_path)
	{
		fprintf(stderr, "freezing grammar...\n");
		freeze_grammar();
	}
	//else fprintf(stderr, "(not freezing)\n");
	load_rules_conf();

	if(save_grammar_path)
		exit(0);

	//dot_rf("/tmp/rules.dot");
	//lex_filter_rules();
	//dump_grammar();

	g_loaded = 1;
	if(export_parts)
	{
		dump_parts_of_speech();
		return 0;
	}

	//print_rule("nadj_rr_nt");
	//print_type("v_np*_trans_le");
	//print_type("np_trans_subst");

	if(!sentence_path)return 0;
	if(!strcmp(sentence_path, "-"))fin = stdin;
	else
	{
		fin = fopen(sentence_path, "r");
		if(!fin)
		{
			perror(sentence_path);
			return -1;
		}
	}

	signal(SIGSEGV, print_current_sentence);
	signal(SIGBUS, print_current_sentence);
	gettimeofday(&tv1, 0);
	while(!feof(fin) && fgets(line, 10240, fin))
	{
		num_parses = parse_line(line);
		if(num_parses==-1)fprintf(stderr, "NOTE: ignore\n");
		else if(num_parses>0)success++;
		else if(num_parses==0)fail++;
		if(num_parses>=0)count++;
	}
	gettimeofday(&tv2, 0);
	restore_stderr();

	tv2.tv_sec -= tv1.tv_sec;
	tv2.tv_usec -= tv1.tv_usec;
	if(count)
	{
		fprintf(stderr, "NOTE: parsed %d / %d sentences, avg %dk, time %.5fs\n",
			success, count, (int)(accum_ram_usage / count / 1024), (float)tv2.tv_sec + (float)tv2.tv_usec/1000000);
		rule_stats();
	}

	if(success<=0 && count>0)return -1;
	else return 0;
}

int	parse_line(char	*line)
{
	char	copy[10240];
	int	nparse = 0;
	/*char	*p, *w;
	char	*words[100];
	int	nwords = 0;*/
	int	nwords = 0, i, j;
	char	**words;

	current_sentence = "(preparing)";
	if(!*line)return -1;
	if(!strncmp(line, "<DOC", 4))
	{
		printf("%s", line);
		return -1;
	}
	if(!strcmp(line, "All rights reserved.\n"))return -1;	// not interesting.
	if(!strcmp(line, "@reloadrules@\n"))
	{
		load_rules_conf();
		return -1;
	}

	strcpy(copy, line);
	// break up into words, normalize capitalization
	/*w = p = copy;
	while(*p)
	{
		if(*p>='A' && *p<='Z')(*p) += 32;
		switch(*p)
		{
			case	' ': case '\t': case '.': case '\n':
			case	',': case '?': case '!':
				if(nwords>=100) { fprintf(stderr, "NOTE: 0 readings. Too many words (100+)\n"); return 0; }
				*p = 0;
				if(w != p)words[nwords++] = w;
				w = p+1;
				break;
		}
		p++;
	}*/

	if(preprocess_line(copy, &nwords, &words))
	{
		fprintf(stderr, "NOTE: preprocessor failed!\n");
		return -1;
	}
	if(nwords<=2)return -1;	// experience shows short "sentences" to not usually be interesting
	if(nwords>50)
	{
		fprintf(stderr, "NOTE: 0 readings. Too many words (50+)\n");
		return 0;	// trying to parse long sentences is a waste of time. we have lots of short ones.
	}

	//printf("%d words\n", nwords);
	for(j=0;j<nwords;j++)
		for(i=0;words[j][i];i++)
			words[j][i] = tolower(words[j][i]);
	current_sentence = (char*)trim_string(line);
	nparse = parse(nwords, words, current_sentence);
	clear_slab();
	for(i=0;i<nwords;i++)free(words[i]);
	free(words);
	return nparse;
}
