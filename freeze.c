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
#include	"freeze.h"

void		**oldslabs;
int		noldslabs, aoldslabs, currslabsize;

extern int debug_level;

// copy all grammar objects to flat blocks
extern int	ntypes, nlexemes, nrules, nirules, nroots, nfnames, nstrings;
extern struct type	**types;
extern struct rule	**rules;
extern struct lexeme	**lexemes;
extern struct irule	**irules;
extern struct dg	**roots;
extern struct type	**strings;
extern struct type	*strtype;
extern char		**fnames;
extern char		*rule_filter;
extern char		*lgroups[256];

struct grammar
{
	char		message[128];
	int		ntypes, nlexemes, nrules, nirules, nroots, nfnames, nstrings;
	struct type	**types;
	struct rule	**rules;
	struct lexeme	**lexemes;
	struct irule	**irules;
	struct dg	**roots;
	struct type	**strings;
	char		**fnames;
	char		*rule_filter;
	char		*lgroups[256];
	struct type	*strtype;
};

extern void	*slab_alloc(int	len);

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

	*tout = *tin;
	tout->dg = tin->dg?copy_dg(tin->dg):0;
	tout->name = freeze_string(tin->name);
	//if(tin->par)tout->par = types[tin->par->tnum];
	//if(tin->parb)tout->parb= types[tin->parb->tnum];
	tout->descendents = slab_alloc(tin->ndescendents * sizeof(int));
	memcpy(tout->descendents, tin->descendents, tin->ndescendents * sizeof(int));

	return tout;
}

struct rule	*freeze_rule(struct rule	*rin)
{
	struct rule	*rout = slab_alloc(sizeof(struct rule));

	*rout = *rin;
	rout->dg = copy_dg(rin->dg);
	rout->name = freeze_string(rin->name);
	strcpy(rout->name, rin->name);

	return rout;
}

struct irule	*freeze_irule(struct irule	*irin)
{
	struct irule	*irout = slab_alloc(sizeof(struct irule));
	int		i;

	*irout = *irin;
	irout->rule->inflr = irout;
	irout->subpat = slab_alloc(sizeof(char*) * irout->nsub);
	irout->subrep = slab_alloc(sizeof(char*) * irout->nsub);
	for(i=0;i<irout->nsub;i++)
	{
		irout->subpat[i] = freeze_string(irin->subpat[i]);
		irout->subrep[i] = freeze_string(irin->subrep[i]);
	}

	irout->irregs = slab_alloc(sizeof(struct irreg) * irout->nirregs);
	for(i=0;i<irout->nirregs;i++)
	{
		irout->irregs[i].stem = freeze_string(irin->irregs[i].stem);
		irout->irregs[i].form = freeze_string(irin->irregs[i].form);
	}

	return irout;
}

struct lexeme	*freeze_lexeme(struct lexeme	*lin)
{
	struct lexeme	*lout = slab_alloc(sizeof(struct lexeme));

	lout->dg = lin->dg;	// NOTE: this is frozen *later*, for file layout reasons
	lout->word = freeze_string(lin->word);

	lout->stemlen = lin->stemlen;
	lout->stem = slab_alloc(sizeof(struct type*) * lout->stemlen);
	lout->lextype = types[lin->lextype->tnum];
	memcpy(lout->stem, lin->stem, sizeof(struct type*) * lout->stemlen);

	return lout;
}

void	patch_dg(struct dg	*dg)
{
	int	i;

	if(!dg)return;
	dg = dereference_dg(dg);
	if(!dg)return;

	//printf("type %s -> ", dg->xtype->name);
	if(dg->xtype->name[0]=='"')dg->xtype = strings[dg->xtype->tnum];
	else dg->xtype = types[dg->xtype->tnum];

	dg->ncarcs = 0;
	dg->carcs = 0;
	dg->gen = 0;
	dg->forward = 0;
	dg->copy = 0;
	dg->type = dg->xtype;

	//printf("%s\n", dg->type->name);
	for(i=0;i<dg->narcs;i++)
		patch_dg(dg->arcs[i].dest);
}

void	*freeze_block(void	*src, int	len)
{
	void	*dst;
	int	alen = len;

	if(len%4)alen += 4-len%4;
	dst = slab_alloc(alen);
	memcpy(dst, src, len);
	return dst;
}

int	freeze_grammar()
{
	int		i, j;
	struct grammar	*G;

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
	for(i=0;i<nstrings;i++)strings[i] = freeze_type(strings[i]);
	strtype = lookup_type("string");

	for(i=0;i<ntypes;i++)patch_dg(types[i]->dg);
	for(i=0;i<nrules;i++)patch_dg(rules[i]->dg);
	for(i=0;i<nlexemes;i++)
	{
		patch_dg(lexemes[i]->dg);
		for(j=0;j<lexemes[i]->stemlen;j++)
			lexemes[i]->stem[j] = strings[lexemes[i]->stem[j]->tnum];
	}
	for(i=0;i<nroots;i++)patch_dg(roots[i]);

	for(i=0;i<nrules;i++)rules[i] = freeze_rule(rules[i]);
	for(i=0;i<nirules;i++)
		irules[i]->rule = rules[irules[i]->rule->rnum];

	for(i=0;i<nirules;i++)irules[i] = freeze_irule(irules[i]);

	for(i=0;i<nlexemes;i++)lexemes[i] = freeze_lexeme(lexemes[i]);

	for(i=0;i<nroots;i++)roots[i] = copy_dg(roots[i]);

	for(i=0;i<nfnames;i++)fnames[i] = freeze_string(fnames[i]);
	for(i=0;i<256;i++)
		if(lgroups[i])
			lgroups[i] = freeze_block(lgroups[i], 256);

	rule_filter = freeze_block(rule_filter, sizeof(char)*nrules*nrules);

	// freeze lexicon dgs -- this isn't done earlier so that the
	// 	'struct lexemes' will all be quickly accessible without
	// 	having to page in all their dgs
	for(i=0;i<nlexemes;i++)lexemes[i]->dg = copy_dg(lexemes[i]->dg);

	G->rule_filter = rule_filter;
	G->ntypes = ntypes;
	G->nrules = nrules;
	G->nirules = nirules;
	G->nlexemes = nlexemes;
	G->nroots = nroots;
	G->nfnames = nfnames;
	G->nstrings = nstrings;
	G->types = slab_alloc(sizeof(void*) * G->ntypes);	memcpy(G->types, types, sizeof(void*) * G->ntypes);
	G->rules = slab_alloc(sizeof(void*) * G->nrules);	memcpy(G->rules, rules, sizeof(void*) * G->nrules);
	G->irules = slab_alloc(sizeof(void*) * G->nirules);	memcpy(G->irules, irules, sizeof(void*) * G->nirules);
	G->lexemes = slab_alloc(sizeof(void*) * G->nlexemes);	memcpy(G->lexemes, lexemes, sizeof(void*) * G->nlexemes);
	G->roots = slab_alloc(sizeof(void*) * G->nroots);	memcpy(G->roots, roots, sizeof(void*) * G->nroots);
	G->fnames = slab_alloc(sizeof(void*) * G->nfnames);	memcpy(G->fnames, fnames, sizeof(char*) * G->nfnames);
	G->strings = slab_alloc(sizeof(void*) * G->nstrings);	memcpy(G->strings, strings, sizeof(void*) * G->nstrings);
	for(i=0;i<256;i++)G->lgroups[i] = lgroups[i];
	G->strtype = strtype;
	strcpy(G->message, "frozen grammar file\n");
	strcat(G->message, "english resource grammar (erg)\n");

	commit_slab();
	return 0;
}

int	loaded_freeze_fd = -1, loaded_freeze_size;

static int	load_freeze()
{
	struct grammar	*G;
	int		i;

	G = (struct grammar*)mmap((void*)0x60000000, loaded_freeze_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, loaded_freeze_fd, 0);
	if(!G || G==(struct grammar*)0xffffffff)
	{
		perror("mmap fixed 0x60000000");
		exit(-1);
	}

	ntypes = G->ntypes;
	nlexemes = G->nlexemes;
	nrules = G->nrules;
	nirules = G->nirules;
	nroots = G->nroots;
	nfnames = G->nfnames;
	nstrings = G->nstrings;

	types = G->types;
	lexemes = G->lexemes;
	rules = G->rules;
	irules = G->irules;
	roots = G->roots;
	strings = G->strings;
	strtype = G->strtype;

	rule_filter = G->rule_filter;

	// in case we accidentally add more during parsing
	fnames = malloc(sizeof(char*) * nfnames);
	memcpy(fnames, G->fnames, sizeof(char*) * nfnames);
	for(i=0;i<256;i++)
	{
		lgroups[i] = G->lgroups[i];
		//if(lgroups[i])printf("lgroup[%c] = %s\n", i, lgroups[i]);
	}

	setup_glb_lut();

	if(debug_level)
		fprintf(stderr, "NOTE: %d types, %d lexemes, %d rules, %d irules, %d roots, %d strings, %d features\n",
				ntypes, nlexemes, nrules, nirules, nroots, nstrings, nfnames);

	//printf("root symbol:\n"); print_dg(roots[0]); printf("\n\n");
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
	munmap((void*)0x60000000, loaded_freeze_size);
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

	if(ftruncate(freeze_fd, 0x10000000)) { perror("ftruncate 0x10000000"); exit(-1); }
	freeze_point = mmap((void*)0x60000000, 0x10000000, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, freeze_fd, 0);
	if(!freeze_point || freeze_point==(void*)0xffffffff)
	{
		perror("mmap fixed 0x60000000");
		exit(-1);
	}
	//fprintf(stderr, "we got %p\n", freeze_point);
	freeze_next = freeze_point;
	currslabsize = 0x10000000;
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
