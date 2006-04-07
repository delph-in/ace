#include	<stdio.h>
#include	<string.h>
#include	<sys/fcntl.h>
#include	<sys/types.h>
#include	<sys/mman.h>
#include	<unistd.h>
#include	<stdlib.h>

#include	"dag.h"
#include	"rule.h"
#include	"morpho.h"
#include	"lexicon.h"
#include	"maxent.h"
#include	"mrs.h"
#include	"freeze.h"
#ifdef	POST
#include	"post.h"
#endif

extern void		**oldslabs;
extern int		noldslabs, aoldslabs, currslabsize;

extern int debug_level;

void	*freeze_labels();
void	recover_labels(void	*ls);
void	*freeze_rule_filter();
void	recover_rule_filter(void *rf);
void	*freeze_conf();
void	recover_conf(void	*vc);
void	*freeze_fspp();
void	recover_fspp(void	*ptr);
void	*freeze_instances();
void	recover_instances(void	*ptr);

extern char	*grammar_version, *grammar_dir;

// copy all grammar objects to flat blocks
extern int	ntypes, nlexemes, nrules, norules, ninstances, nfnames, nstrings;
extern struct type	**types;
extern struct rule	**rules;
extern struct lexeme	**lexemes;
extern struct orule	**orules;
extern struct type	**strings;
extern struct type	*strtype;
extern char		**fnames;
extern int		*introduce;
extern char		*lgroups[256];

extern unsigned int generation;

struct grammar
{
	char		message[128];
	int		ntypes, nlexemes, nrules, norules, nfnames, nstrings;
	struct type	**types;
	struct rule	**rules;
	struct lexeme	**lexemes;
	struct orule	**orules;
	struct type	**strings;
	char		**fnames;
	char		*lgroups[256];
	struct type	*strtype;
	int		*introduce;
	void	*rule_filter;
	void	*semindex;
	void	*stochmodel;
	void	*typehash;
	void	*vpm;
	void	*labels;
	void	*conf;
	void	*fspp;
	void	*instances;
	void	*post;
};

extern void	*slab_alloc(int	len);

wchar_t	*freeze_wcs(wchar_t	*str)
{
	int	len;
	char	*copy;
	if(str==0)return 0;
	len = (wcslen(str)+1) * sizeof(wchar_t);
	return freeze_block(str, len);
}

char	*freeze_string(char	*str)
{
	int	len;
	char	*copy;
	if(str==0)return 0;
	len = strlen(str)+1;
	if(len%4)len = len - (len%4) + 4;
	copy = slab_alloc(len);
	return strcpy(copy, str);
}

struct type	*freeze_type(struct type	*tin)
{
	struct type	*tout = slab_alloc(sizeof(struct type));

	// XXX hack!
	if(generation > MAX_GEN_RESET)generation = 11;
	// XXX hack!

	*tout = *tin;
	tout->dg = tin->dg?copy_dg(tin->dg):0;
	tout->name = freeze_string(tin->name);
	tout->descendents = freeze_block(tin->descendents, tin->ndescendents * sizeof(short));

	return tout;
}

struct rule	*freeze_rule(struct rule	*rin)
{
	struct rule	*rout = slab_alloc(sizeof(struct rule));

	*rout = *rin;
	rout->dg = copy_dg(rin->dg);
	rout->packdg = copy_dg(rin->packdg);
	rout->gpackdg = copy_dg(rin->gpackdg);
	rout->name = freeze_string(rin->name);
	strcpy(rout->name, rin->name);

	// make sure the semantic index pointers can find the frozen rules
	rin->dg = (struct dg*)rout;
	return rout;
}

struct orule	*freeze_orule(struct orule	*orin)
{
	struct orule	*orout = slab_alloc(sizeof(struct orule));
	int		i;

	*orout = *orin;
	orout->rule->orth = orout;
	orout->sub = slab_alloc(sizeof(struct orule_sub) * orout->nsub);
	for(i=0;i<orout->nsub;i++)
	{
		struct orule_sub *subi = orin->sub+i, *subo = orout->sub+i;
		subo->pat = freeze_wcs(subi->pat);
		subo->rep = freeze_wcs(subi->rep);
		subo->patl = subi->patl;
		subo->repl = subi->repl;
	}

	orout->irregs = slab_alloc(sizeof(struct irreg) * orout->nirregs);
	for(i=0;i<orout->nirregs;i++)
	{
		orout->irregs[i].stem = freeze_wcs(orin->irregs[i].stem);
		orout->irregs[i].form = freeze_wcs(orin->irregs[i].form);
	}

	return orout;
}

static struct type	*FT(struct type	*t)
{
	if(!t)return 0;
	else if(t->name[0]=='"')return strings[t->tnum];
	else return types[t->tnum];
}

struct lexeme	*freeze_lexeme(struct lexeme	*lin)
{
	struct lexeme	*lout = slab_alloc(sizeof(struct lexeme));

	// XXX hack!
	if(generation > MAX_GEN_RESET)generation = 11;
	// XXX hack!

	lout->dg = lin->dg;	// NOTE: this is frozen *later*, for file layout reasons
	lout->word = freeze_string(lin->word);

	lout->stemlen = lin->stemlen;
	lout->stem = slab_alloc(sizeof(struct type*) * lout->stemlen);
	lout->lextype = FT(lin->lextype);
	lout->select = FT(lin->select);
	lout->pred = FT(lin->pred);
	lout->onset = lin->onset;
	memcpy(lout->stem, lin->stem, sizeof(struct type*) * lout->stemlen);

	lin->dg = (struct dg*)lout;	// NOTE: keep pointer to new copy of lexeme, so semantic index can be updated

	return lout;
}

void	patch_dg(struct dg	*dg)
{
	int	i;
	struct dg	**arcs;

	if(!dg)return;
	dg = dereference_dg(dg);
	if(!dg)return;

	//printf("type %s -> ", dg->xtype->name);
	dg->xtype = FT(dg->xtype);

	dg->ncarcs = 0;
	dg->carcs = 0;
	dg->gen = 0;
	dg->forward = 0;
	dg->copy = 0;
	dg->type = dg->xtype;

	arcs = DARCS(dg);

	//printf("%s\n", dg->type->name);
	for(i=0;i<dg->narcs;i++)
		patch_dg(arcs[i]);
}

struct dg	*freeze_dg(struct dg	*dg)
{
	patch_dg(dg);
	return copy_dg(dg);
}

void	*freeze_block(void	*src, int	len)
{
	void	*dst;
	int	alen = len;

	if(!src && !len)return 0;
	if(len%4)alen += 4-len%4;
	dst = slab_alloc(alen);
	memcpy(dst, src, len);
	return dst;
}

static void freezer_note(char *designation)
{
	extern int slab_usage();
	static int last_bytes = 0;
	int			size = slab_usage() - last_bytes;
	last_bytes = slab_usage();
	if(size<8192)printf(" %s: %d", designation, size);
	else if(size<1024*1024)printf(" %s: %dK", designation, (size+512)/1024);
	else if(size<1024*1024*1024)printf(" %s: %.1fM", designation, ((float)size+512*1024)/(1024*1024));
	else printf(" %s: %.1fG", designation, ((float)size+512*1024*1024)/(1024*1024*1024));
}

int	freeze_grammar()
{
	int		i, j;
	struct grammar	*G;

	use_freezer();
	//print_rule("sing_noun_infl_rule");

	inval_thash();
	for(i=0;i<ntypes;i++)
		if(types[i]->tnum != i)
		{
			fprintf(stderr, "type %s tnum %d is wrong.\n", types[i]->name, types[i]->tnum);
			exit(-1);
		}

	clear_slab();

	G = slab_alloc(sizeof(struct grammar));

	null_generation();

	for(i=0;i<ntypes;i++)types[i] = freeze_type(types[i]);
	freezer_note("types");

	for(i=0;i<nstrings;i++)strings[i] = freeze_type(strings[i]);
	strtype = lookup_type("string");
	freezer_note("strings");

	for(i=0;i<ntypes;i++)patch_dg(types[i]->dg);
	for(i=0;i<nrules;i++)patch_dg(rules[i]->dg);
	for(i=0;i<nrules;i++)patch_dg(rules[i]->packdg);
	for(i=0;i<nrules;i++)patch_dg(rules[i]->gpackdg);

	for(i=0;i<nlexemes;i++)
	{
		if(lexemes[i]->dg)patch_dg(lexemes[i]->dg);
		for(j=0;j<lexemes[i]->stemlen;j++)
			lexemes[i]->stem[j] = strings[lexemes[i]->stem[j]->tnum];
	}

	//printf("\n"); print_rule("sing_noun_infl_rule");

	for(i=0;i<nrules;i++)rules[i] = freeze_rule(rules[i]);
	for(i=0;i<norules;i++)
		orules[i]->rule = rules[orules[i]->rule->rnum];

	for(i=0;i<norules;i++)orules[i] = freeze_orule(orules[i]);
	freezer_note("rules");

	//printf("\n"); print_rule("sing_noun_infl_rule");

	for(i=0;i<nlexemes;i++)lexemes[i] = freeze_lexeme(lexemes[i]);
	freezer_note("lex-info");

	for(i=0;i<nfnames;i++)fnames[i] = freeze_string(fnames[i]);
	introduce = freeze_block(introduce, sizeof(int) * nfnames);

	for(i=0;i<256;i++)
		if(lgroups[i])
			lgroups[i] = freeze_block(lgroups[i], 256);

	G->rule_filter = freeze_rule_filter();
	freezer_note("\n miscellaneous");

	// freeze lexicon dgs -- this isn't done earlier so that the
	// 	'struct lexemes' will all be quickly accessible without
	// 	having to page in all their dgs
	for(i=0;i<nlexemes;i++)
	{
		// XXX hack!
		if(generation > MAX_GEN_RESET)generation = 11;
		// XXX hack!

		if(lexemes[i]->dg)lexemes[i]->dg = copy_dg(lexemes[i]->dg);
	}
	freezer_note("lex-dgs");

	G->ntypes = ntypes;
	G->nrules = nrules;
	G->norules = norules;
	G->nlexemes = nlexemes;
	G->nfnames = nfnames;
	G->nstrings = nstrings;
	G->types = slab_alloc(sizeof(void*) * G->ntypes);	memcpy(G->types, types, sizeof(void*) * G->ntypes);
	G->rules = slab_alloc(sizeof(void*) * G->nrules);	memcpy(G->rules, rules, sizeof(void*) * G->nrules);
	G->orules = slab_alloc(sizeof(void*) * G->norules);	memcpy(G->orules, orules, sizeof(void*) * G->norules);
	G->lexemes = slab_alloc(sizeof(void*) * G->nlexemes);	memcpy(G->lexemes, lexemes, sizeof(void*) * G->nlexemes);
	G->fnames = slab_alloc(sizeof(void*) * G->nfnames);	memcpy(G->fnames, fnames, sizeof(char*) * G->nfnames);
	G->strings = slab_alloc(sizeof(void*) * G->nstrings);	memcpy(G->strings, strings, sizeof(void*) * G->nstrings);
	G->typehash = freeze_typehash();
	G->vpm = freeze_vpm();
	G->labels = freeze_labels();
	G->conf = freeze_conf();
	G->fspp = freeze_fspp();
	G->instances = freeze_instances();
	G->introduce = introduce;
	for(i=0;i<256;i++)G->lgroups[i] = lgroups[i];
	G->strtype = strtype;
	G->semindex = freeze_semantic_index();
	freezer_note("sem-index");
	G->stochmodel = freeze_stochastic_model();
	freezer_note("stochastic-model");
	strcpy(G->message, "frozen grammar file\n");
	strcat(G->message, grammar_dir); strcat(G->message, "\n");
	strcat(G->message, grammar_version); strcat(G->message, "\n");

#ifdef	POST
	G->post = slab_alloc(post_image_size());
	post_freeze(G->post);
#endif

	//printf("\n"); print_rule("sing_noun_infl_rule");

	printf("\n ... "); commit_slab();
	return 0;
}

int	loaded_freeze_fd = -1;
long	loaded_freeze_size;

static int	load_freeze()
{
	struct grammar	*G;
	int		i;

	G = (struct grammar*)mmap((void*)FREEZER_MMAP_BASE, loaded_freeze_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, loaded_freeze_fd, 0);
	if(!G || G==(struct grammar*)0xffffffff)
	{
		perror("mmap fixed");
		exit(-1);
	}

	ntypes = G->ntypes;
	nlexemes = G->nlexemes;
	nrules = G->nrules;
	norules = G->norules;
	nfnames = G->nfnames;
	nstrings = G->nstrings;

	types = G->types;
	lexemes = G->lexemes;
	rules = G->rules;
	orules = G->orules;
	strings = G->strings;
	strtype = G->strtype;

	// in case we accidentally add more during parsing
	fnames = malloc(sizeof(char*) * nfnames);
	introduce = malloc(sizeof(int) * nfnames);
	memcpy(fnames, G->fnames, sizeof(char*) * nfnames);
	memcpy(introduce, G->introduce, sizeof(int) * nfnames);

	recover_rule_filter(G->rule_filter);
	recover_typehash(G->typehash);
	recover_fspp(G->fspp);
	recover_conf(G->conf);
	recover_instances(G->instances);
	recover_labels(G->labels);
	recover_vpm(G->vpm);
	recover_semantic_index(G->semindex);
	recover_stochastic_model(G->stochmodel);

#ifdef	POST
	post_recover(G->post);
#endif

	for(i=0;i<256;i++)
	{
		lgroups[i] = G->lgroups[i];
		//if(lgroups[i])printf("lgroup[%c] = %s\n", i, lgroups[i]);
	}

	if(debug_level)
		fprintf(stderr, "NOTE: %d types, %d lexemes, %d rules, %d orules, %d instances, %d strings, %d features\n",
				ntypes, nlexemes, nrules, norules, ninstances, nstrings, nfnames);

	/*if(mlock(strings[0], sizeof(struct type)*nstrings))
		perror("unable to mlock strings");*/

	return 0;
}

void	reload_frozen_grammar()
{
	if(loaded_freeze_fd==-1)
	{
		fprintf(stderr, "can't reload_frozen_grammar(), since no freeze is currently loaded!\n");
		exit(-1);
	}
	fprintf(stderr, "NOTE: generation reset\n");
	munmap((void*)FREEZER_MMAP_BASE, loaded_freeze_size);
	null_generation();
	load_freeze();
}

// load flat blocks grammar file
int	load_frozen_grammar(char	*path)
{
	int	freeze_fd = open(path, O_RDONLY);
	int	len;

	if(freeze_fd<0) { perror(path); exit(-1); }

	len = lseek(freeze_fd, 0, SEEK_END);
	lseek(freeze_fd, 0, SEEK_SET);
	loaded_freeze_fd = freeze_fd;
	loaded_freeze_size = len;
	return load_freeze();
}

void	*freeze_point = 0, *freeze_next;
int	freeze_fd;

// prepare mmap to save flat blocks grammar file
int	setup_save_frozen_grammar(char	*path)
{
	freeze_fd = open(path, O_RDWR | O_CREAT, 0664);

	if(freeze_fd<0) { perror(path); exit(-1); }

	if(ftruncate(freeze_fd, FREEZER_MMAP_SIZE)) { perror("ftruncate FREEZER_MMAP_SIZE"); exit(-1); }
	freeze_point = mmap((void*)FREEZER_MMAP_BASE, FREEZER_MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, freeze_fd, 0);
	if(!freeze_point || freeze_point==(void*)0xffffffff)
	{
		perror("mmap fixed");
		exit(-1);
	}
}

int use_freezer()
{
	//fprintf(stderr, "we got %p\n", freeze_point);
	freeze_next = freeze_point;
	currslabsize = FREEZER_MMAP_SIZE;
	return 0;
}

void	dump_grammar()
{
	int	i;

	for(i=0;i<ntypes;i++)
	{
		printf("type `%s': ", types[i]->name);
		print_dg(types[i]->dg);printf("\n");
	}

	for(i=0;i<nrules;i++)
	{
		printf("rule `%s': ", rules[i]->name);
		print_dg(rules[i]->dg);printf("\n");
	}

	for(i=0;i<nlexemes;i++)
	{
		printf("lex `%s': ", lexemes[i]->word);
		print_dg(lexemes[i]->dg);printf("\n");
	}
}
