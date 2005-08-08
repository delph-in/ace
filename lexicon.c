#include	<stdlib.h>
#include	<string.h>
#include	<stdio.h>
#include	<assert.h>
#include	<ctype.h>

#include	"tdl.h"
#include	"lexicon.h"
#include	"chart.h"
#include	"morpho.h"

struct lexeme	**lexemes;
int		nlexemes;

extern unsigned int	generation;

extern int trace;
extern unsigned int require_ep_span;

//#define	FULL_LEXICON
#define		SIMPLE_LEXICON

struct edge	*lexical_edge(struct lexeme	*lex, int	i, int	len)
{
	struct edge	*edge = slab_alloc(sizeof(struct edge));
	struct dg	*ldg;

#define RECORD_LEXICAL_ACCESS
#ifdef RECORD_LEXICAL_ACCESS
	static FILE	*lexf = 0;
	if(!lexf)lexf = fopen("/tmp/lexical-access", "w");
	fprintf(lexf, "%s\n", lex->word);
	fflush(lexf);
#endif

	bzero(edge, sizeof(struct edge));

#ifdef	FULL_LEXICON
	edge->dg = lex->dg;
#else
#ifdef	SIMPLE_LEXICON
	ldg = expand_lexeme(lex);
#else
	ldg = lex->dg;
#endif
	edge->dg = unify_dg(ldg, lex->lextype->dg, -1);
	if(!edge->dg)
	{
		fprintf(stderr, "NOTE: ERROR! lexeme `%s' failed delayed unification\n", lex->word);
		exit(-1);
	}
	wellform(edge->dg, lex->word);
#endif

	//edge->dg = copy_dg(edge->dg);
	edge->id = next_eid();
#ifdef	GENERATION
	edge->from = edge->to = 0;
	edge->ep_span = 1<<i;
#else
	edge->from = i;
	edge->to = i+len;
#endif
	edge->lex = lex;
	edge->qc = extract_qc(edge->dg);
	edge->prio = LEX_PRIO;
#ifdef	GENERATION
	edge->ninflr_routes = 1;
	edge->inflr_routes = slab_alloc(sizeof(struct inflr_route));
	edge->inflr_routes[0].len = 0;
	edge->inflr_routes[0].rules = 0;
#else
	if(len==1)
	{
		edge->ninflr_routes = 0;
		edge->inflr_routes = 0;
	}
	else
	{
		edge->ninflr_routes = 1;
		edge->inflr_routes = slab_alloc(sizeof(struct inflr_route));
		edge->inflr_routes[0].len = 0;
		edge->inflr_routes[0].rules = 0;
	}
#endif
	edge->age = 0;

#ifndef	GENERATION
	if(trace)
	{
		printf("added lexeme `%s' = #%d\n", lex->word, edge->id);
		if(trace>1){print_dg(edge->dg); printf("\n\n");}
	}
#endif

	return edge;
}

struct lexeme	*proper_noun_lex(char	*name)
{
	char			*p = name;
	struct lexeme	*lex;
	struct dg		*extra, *stem, *ss, *lk, *kr;
	struct type	*ty = slab_alloc(sizeof(struct type));
	char			*ncopy;
	int				nlen;

	/* prototype:
	 * 	george_n1 := n_proper_le &
	 * 	 [ STEM < "george" >,
	 * 	    SYNSEM.LKEYS.KEYREL.CARG "george" ].
	 */

	// make a safe copy of 'name'
	nlen = strlen(name);
	nlen += 32;
	nlen -= (nlen % 32);
	ncopy = slab_alloc(nlen);
	strcpy(ncopy, name);
	name = ncopy;

	// make a type representing 'name'
	bzero(ty, sizeof(*ty));
	ty->name = name;
	ty->dg = atomic_dg("*top*");
	ty->dg->type = ty->dg->xtype = ty;
	ty->tnum = 999999;

	// make a skeletal dag for our lexeme

	stem = atomic_dg("*cons*");
	stem = add_dg_feature(stem, "FIRST", ty->dg);
	stem = add_dg_feature(stem, "REST", atomic_dg("*null*"));

	kr = add_dg_feature(atomic_dg("*top*"), "CARG", ty->dg);
	lk = add_dg_feature(atomic_dg("*top*"), "KEYREL", kr);
	ss = add_dg_feature(atomic_dg("*top*"), "LKEYS", lk);

	extra = atomic_dg("n_proper_le");
	assert(extra);
	extra = add_dg_feature(extra, "STEM", stem);
	extra = add_dg_feature(extra, "SYNSEM", ss);

	// define the lexeme
	lex = slab_alloc(sizeof(struct lexeme));
	bzero(lex, sizeof(*lex));
	lex->word = name;
	lex->stemlen = 1;
	lex->stem = slab_alloc(sizeof(struct type*)*1);
	lex->stem[0] = ty;
	lex->lextype = extra->type;
	lex->dg = extra;

#ifdef	FULL_LEXICON
#error	fix proper_noun_lex() to support FULL_LEXICON, please
#endif

	return lex;
}

int	wstrcmp(char	*a, char	*b)
{
	while(*a)if(*b++!=*a++)return a[-1]-b[-1];
	if(*b)return -*b;
	return 0;
}

int	add_multilexemes_to_chart(int	x, int	nwords, char	**words)
{
	int		num=0;
	char	stemcmp[1024], extracmp[1024];
	int		i, j, k, r = -1;
	int		max = 0;

	if(nwords<2)return 0;
	sprintf(stemcmp, "\"%s\"", words[0]);

	for(i=0,j=nlexemes-1;i<=j;)
	{
		k = (i+j)/2;
		r = wstrcmp(lexemes[k]->stem[0]->name, stemcmp);
		if(r==0)break;
		if(r<0)i = k+1;
		else j = k-1;
	}
	if(r==0)
	{
		for(i=k;i<nlexemes && !wstrcmp(lexemes[i]->stem[0]->name, stemcmp);i++)
		{
			//printf("lex %s len %d\n", lexemes[i]->word, lexemes[i]->stemlen);
			if(lexemes[i]->stemlen<=1 || lexemes[i]->stemlen > nwords)continue;
			for(j=1;j<lexemes[i]->stemlen;j++)
			{
				sprintf(extracmp, "\"%s\"", words[j]);
				if(wstrcmp(lexemes[i]->stem[j]->name, extracmp))break;
			}
			if(j<lexemes[i]->stemlen)continue;
			if(!lexeme_in_agenda(lexemes[i], x, x+lexemes[i]->stemlen))
				add_agenda(lexical_edge(lexemes[i], x, lexemes[i]->stemlen));
			if(lexemes[i]->stemlen > max)max = lexemes[i]->stemlen;
			num++;
		}
		for(i=k-1;i>=0 && !wstrcmp(lexemes[i]->stem[0]->name, stemcmp);i--)
		{
			if(lexemes[i]->stemlen<=1 || lexemes[i]->stemlen > nwords)continue;
			//printf("lex %s len %d\n", lexemes[i]->word, lexemes[i]->stemlen);
			for(j=1;j<lexemes[i]->stemlen;j++)
			{
				sprintf(extracmp, "\"%s\"", words[j]);
				if(wstrcmp(lexemes[i]->stem[j]->name, extracmp))break;
			}
			if(j<lexemes[i]->stemlen)continue;
			if(!lexeme_in_agenda(lexemes[i], x, x+lexemes[i]->stemlen))
				add_agenda(lexical_edge(lexemes[i], x, lexemes[i]->stemlen));
			if(lexemes[i]->stemlen > max)max = lexemes[i]->stemlen;
			num++;
		}
		return max;
	}
	else return 0;
}

int	add_lexemes_to_chart_for_stem(int	x, char	*stem, int nrules, int *rules)
{
	int		num=0;
	char		stemcmp[1024];
	int		i, j, k, r = -1;

	sprintf(stemcmp, "\"%s\"", stem);
	//printf("adding lexemes for stem `%s'\n", stem);

	for(i=0,j=nlexemes-1;i<=j;)
	{
		k = (i+j)/2;
		r = wstrcmp(lexemes[k]->stem[0]->name, stemcmp);
		if(r==0)break;
		if(r<0)i = k+1;
		else j = k-1;
	}
	if(r==0)
	{
		i=k;
		for(i=k;i<nlexemes && !wstrcmp(lexemes[i]->stem[0]->name, stemcmp);i++)
		{
			if(lexemes[i]->stemlen>1)continue;
			if(!lexeme_in_agenda(lexemes[i], x, x+1))
				add_agenda(lexical_edge(lexemes[i], x, 1));
			allow_inflr(x, lexemes[i], nrules, rules);
			num++;
		}
		for(i=k-1;i>=0 && !wstrcmp(lexemes[i]->stem[0]->name, stemcmp);i--)
		{
			if(lexemes[i]->stemlen>1)continue;
			if(!lexeme_in_agenda(lexemes[i], x, x+1))
				add_agenda(lexical_edge(lexemes[i], x, 1));
			allow_inflr(x, lexemes[i], nrules, rules);
			num++;
		}
		return num;
	}
	else return 0;
}

void	skolemize_arg(struct dg	*dg, char	*arg, struct dg	*inst)
{
	dg = dg_hop(dg, lookup_fname(arg));
	if(dg)add_dg_feature(dg, "INSTLOC", inst);
}

void	skolemize_rel_as(struct dg	*rel, struct dg	*a0, struct dg	*a1, struct dg	*a2, struct dg	*a3)
{
	if(a0)skolemize_arg(rel, "ARG0", a0);
	if(a1)skolemize_arg(rel, "ARG1", a1);
	if(a2)skolemize_arg(rel, "ARG2", a2);
	if(a3)skolemize_arg(rel, "ARG3", a3);
}

void	skolemize_as(struct dg	*dg, struct dg	*a0, struct dg	*a1, struct dg	*a2, struct dg	*a3)
{
	struct dg	*rel=0;

	if(dg)dg = dg_hop(dg, lookup_fname("SYNSEM"));
	/*if(dg)dg = dg_hop(dg, lookup_fname("LOCAL"));
	if(dg)dg = dg_hop(dg, lookup_fname("CONT"));
	if(dg)dg = dg_hop(dg, lookup_fname("RELS"));
	if(dg)dg = dg_hop(dg, lookup_fname("LIST"));
	if(dg)rel = dg_hop(dg, lookup_fname("FIRST"));*/
	if(dg)dg = dg_hop(dg, lookup_fname("LKEYS"));
	if(dg)rel = dg_hop(dg, lookup_fname("KEYREL"));
	if(rel)skolemize_rel_as(rel, a0, a1, a2, a3);
}


int	add_generative_lexeme(char	*stem, struct dg	*a0, struct dg	*a1, struct dg	*a2, struct dg	*a3)
{
	int		num=0;
	char		stemcmp[1024];
	int		i, j, k, r = -1, x;
	struct edge	*ed;
	struct dg	*dg;

	for(x=0;require_ep_span & 1<<x;x++){}
	require_ep_span += 1<<x;

	sprintf(stemcmp, "\"%s\"", stem);

	for(i=0,j=nlexemes-1;i<=j;)
	{
		k = (i+j)/2;
		r = wstrcmp(lexemes[k]->stem[0]->name, stemcmp);
		if(r==0)break;
		if(r<0)i = k+1;
		else j = k-1;
	}
	if(r==0)
	{
		i=k;
		for(i=k;i<nlexemes && !wstrcmp(lexemes[i]->stem[0]->name, stemcmp);i++)
		{
			if(lexemes[i]->stemlen>1)continue;
			add_agenda(ed = lexical_edge(lexemes[i], x, 1));
			dg = ed->dg;
			skolemize_as(dg, a0, a1, a2, a3);
			printf(" generative lex %s\n", lexemes[i]->word);
			num++;
		}
		for(i=k-1;i>=0 && !wstrcmp(lexemes[i]->stem[0]->name, stemcmp);i--)
		{
			if(lexemes[i]->stemlen>1)continue;
			add_agenda(ed = lexical_edge(lexemes[i], x, 1));
			dg = ed->dg;
			skolemize_as(dg, a0, a1, a2, a3);
			printf(" generative lex %s\n", lexemes[i]->word);
			num++;
		}
		return num;
	}
	else return 0;
}

void	dump_parts_of_speech()
{
	FILE	*f = popen("uniq", "w");
	struct dg	*synsem, *lkeys, *keyrel;
	struct dg	*pred, *carg;
	int		synsemf = lookup_fname("SYNSEM");
	int		lkeysf = lookup_fname("LKEYS");
	int		keyrelf = lookup_fname("KEYREL");
	int		predf = lookup_fname("PRED");
	int		cargf = lookup_fname("CARG");
	int	i;
	char	rel[128], *rptr, *letype, *pos;

	for(i=0;i<nlexemes;i++)
	{
		synsem = dg_hop(lexemes[i]->dg, synsemf);
		lkeys = keyrel = 0;
		if(synsem)lkeys = dg_hop(synsem, lkeysf);
		if(synsem && lkeys)keyrel = dg_hop(lkeys, keyrelf);
		if(synsem && keyrel)
		{
			pred = dg_hop(keyrel, predf);
			carg = dg_hop(keyrel, cargf);
			if(pred)rptr = pred->xtype->name;
			else if(carg)rptr = carg->xtype->name;
			else rptr = 0;
			if(rptr)
			{
				if(*rptr == '"')
				{
					strcpy(rel, rptr+1);
					if(*rel)rel[strlen(rel)-1] = 0;
				}
				else strcpy(rel, rptr);
				letype = lexemes[i]->dg->xtype->name;
				if(!strncmp(letype, "n_", 2))pos = "noun";
				else if(!strncmp(letype, "v_", 2))pos = "verb";
				else if(!strncmp(letype, "det_", 4))pos = "det";
				else if(!strncmp(letype, "adj_", 4))pos = "adj";
				else if(!strncmp(letype, "adv_", 4))pos = "adv";
				else if(!strncmp(letype, "p_", 2))pos = "prep";
				else if(!strncmp(letype, "pp_", 3))pos = "prep";
				else if(!strncmp(letype, "va_", 3))pos = "auxverb";
				else if(!strncmp(letype, "vc_", 3))pos = "copverb";
				else if(!strncmp(letype, "conj_", 5))pos = "conj";
				else if(!strncmp(letype, "comp_", 5))pos = "comp";
				else pos = 0;
				if(pos)fprintf(f, "%s	%s\n", pos, rel);
			}
		}
	}

	pclose(f);
}

static int	stemfeat = -1, rfeat = 2, ffeat = 1, synsemfeat, lkeysfeat, keyrelfeat, predfeat, compkeyfeat, ocompkeyfeat;
static struct type	*predsort;

int	nsimpleform = 0;

void	init_fnums()
{
	stemfeat = lookup_fname("STEM");
	synsemfeat = lookup_fname("SYNSEM");
	lkeysfeat = lookup_fname("LKEYS");
	keyrelfeat = lookup_fname("KEYREL");
	predfeat = lookup_fname("PRED");
	compkeyfeat = lookup_fname("--+COMPKEY");
	ocompkeyfeat = lookup_fname("--+OCOMPKEY");
	predsort = lookup_type("predsort");
}

void	simplify_lexeme(struct lexeme	*l)
{
	struct dg	*dg = l->dg, *ss, *lk, *kr, *pr;

	// check for most common structures and just store parameters for them
	if(l->stemlen != 1)return;
	if(dg->narcs != 2)return;

	// we want just 'STEM' and 'SYNSEM' features
	if(DARCS(dg)[0].label != stemfeat && DARCS(dg)[1].label != stemfeat)return;
	if(DARCS(dg)[0].label != synsemfeat && DARCS(dg)[1].label != synsemfeat)return;

	ss = dg_hop(dg, synsemfeat);
	// we want just 'LKEYS'
	if(ss->narcs != 1 || DARCS(ss)[0].label != lkeysfeat)return;
	lk = dg_hop(ss, lkeysfeat);
	// we want just 'KEYREL'
	if(lk->narcs != 1 || DARCS(lk)[0].label != keyrelfeat)return;
	kr = dg_hop(lk, keyrelfeat);
	// we want just 'PRED'
	if(kr->narcs != 1 || DARCS(kr)[0].label != predfeat)return;
	pr = dg_hop(kr, predfeat);
	// we want l->pred
	if(pr->narcs != 0 || pr->type != l->pred)return;

	//printf("`%s' has simple form\n", l->word);
	nsimpleform++;
	l->dg = 0;
}

struct dg	*expand_lexeme(struct lexeme	*l)
{
	struct dg	*dg, *ss, *lk, *kr, *pr, *st, *f;

	if(stemfeat==-1)init_fnums();
	if(l->dg)return l->dg;

	pr = atomic_dg("*top*");
	pr->xtype = pr->type = l->pred;
	kr = add_dg_hop(atomic_dg("*top*"), predfeat, pr);
	lk = add_dg_hop(atomic_dg("*top*"), keyrelfeat, kr);
	ss = add_dg_hop(atomic_dg("*top*"), lkeysfeat, lk);

	f = atomic_dg("*top*");
	f->xtype = f->type = l->stem[0];
	st = add_dg_hop(atomic_dg("*top*"), ffeat, f);
	dg = atomic_dg("*top*");
	dg = add_dg_hop(dg, stemfeat, st);
	dg = add_dg_hop(dg, synsemfeat, ss);

	//printf("expanded `%s':\n", l->word);
	//print_dg(dg);printf("\n");

	return dg;
}

void print_lexeme(char *name)
{
	int	i;

	for(i=0;i<nlexemes;i++)
		if(!strcmp(lexemes[i]->word, name))
		{
			print_dg(lexemes[i]->dg); printf("\n");
			printf(" lex type %s\n", lexemes[i]->lextype->name);
			print_dg(lexemes[i]->lextype->dg); printf("\n");
		}
}

void	add_lexeme(char	*word, struct dg	*dg, struct type	*type, struct dg	*fulldg)
{
	struct lexeme	*l = malloc(sizeof(struct lexeme));
	struct dg	*stem, *str, *synsem, *lkeys, *keyrel, *pred, *compkey, *ocompkey;
	int			print = 0;

	// XXX hack!
	if(generation > MAX_GEN_RESET)generation = 11;
	// XXX hack!

	//wellform(dg, word);

	if(stemfeat==-1)init_fnums();
	nlexemes++;
	lexemes = realloc(lexemes, sizeof(struct lexeme)*nlexemes);
	lexemes[nlexemes-1] = l;
	l->word = strdup(word);
	l->dg = dg;
	l->stemlen = 0;
	l->stem = 0;
	l->lextype = type;
	l->pred = 0;
	l->select = 0;

	synsem = dg_hop(fulldg, synsemfeat);
	if(synsem)
	{
		lkeys = dg_hop(synsem, lkeysfeat);
		if(lkeys)
		{
			compkey = dg_hop(lkeys, compkeyfeat);
			if(compkey && compkey->xtype != predsort)
			{
				l->select = compkey->xtype;
				//printf("lex `%s' selects for `%s'\n", word, l->select->name);
			}
			else
			{
				ocompkey = dg_hop(lkeys, ocompkeyfeat);
				if(ocompkey && ocompkey->xtype != predsort)
				{
					l->select = ocompkey->xtype;
					//printf("lex `%s' oselects for `%s'\n", word, l->select->name);
				}
			}
			keyrel = dg_hop(lkeys, keyrelfeat);
			if(keyrel)
			{
				pred = dg_hop(keyrel, predfeat);
				if(pred && pred->xtype != predsort)
				{
					l->pred = pred->xtype;
					//printf("lex `%s' provides `%s'\n", word, l->pred->name);
				}
			}
		}
	}

	//if(!strcmp(word, "hand_v1")) { printf("hand_v1...\n"); print = 1; }

	for(stem = dg_hop(fulldg, stemfeat);stem;stem = dg_hop(stem, rfeat))
	{
		if(!strcasecmp(stem->type->name, "*null*"))break;
		str = dg_hop(stem, ffeat);
		if(!str)
		{
			fprintf(stderr, "lexicon: lexeme `%s' has no FIRST in STEM[%d]\n", word, l->stemlen);
			exit(-1);
		}
		if(str->type->name[0] != '"')
		{
			fprintf(stderr, "lexicon: STEM[%d] in word `%s' is not a string\n", l->stemlen, word);
			exit(-1);
		}
		l->stemlen++;
		l->stem = realloc(l->stem, sizeof(struct type*)*l->stemlen);
		l->stem[l->stemlen-1] = str->type;
		if(print)printf("%s[%d] = %s\n", word, l->stemlen, str->type->name);
	}
	if(l->stemlen == 0)
	{
		fprintf(stderr, "lexicon: lexeme `%s' has no stem!\n", word);
		exit(-1);
	}
	if(!stem)
	{
		fprintf(stderr, "lexeme `%s' has open ended STEM list!\n", word);
		exit(-1);
	}

#ifdef	SIMPLE_LEXICON
	simplify_lexeme(l);
#endif

	add_semantic_index(l, fulldg, 0);

	//printf("lexeme %s has %d-long stem starting %s\n", word, l->stemlen, l->stem[0]->name);
	if(nlexemes%100 == 0)printf("lexeme %s             \r", word); fflush(stdout);
	/*printf("lexeme %s  --> ", word);
	print_dg(dg);
	printf("\n");*/
}

int	study_lexeme(char	*name, struct dg	*dg, struct type	*t)
{
	struct dg	*fulldg;

	fulldg = unify_dg(dg, t->dg, -1);
	if(!fulldg)
	{
		fprintf(stderr, "study_lexeme `%s': dag incompatible with type `%s'\n", name, t->name);
		return -1;
	}
	wellform(fulldg, name);
#ifdef	FULL_LEXICON
	dg = fulldg;
#endif

	add_lexeme(name, dg, t, fulldg);

	return 0;
}

int	study_lexicon(char	*lhs, struct dg	*dg)
{
	add_lexeme(lhs, dg, dg->xtype, dg);
	return 0;
}

int	lexcomp(const void	*a, const void	*b)
{
	const struct lexeme	*la = *(const struct lexeme**)a, *lb = *(const struct lexeme**)b;
	int	res;

	if(la->stemlen==0)return -1;
	if(lb->stemlen==0)return 1;
	res = strcmp(la->stem[0]->name, lb->stem[0]->name);
	if(res)return res;
	else return strcmp(la->word, lb->word);
}

int	load_lexicon()
{
	extern int	lexify_tdl_line(char	*lhs, char	*rhs, void	*ref);

#ifdef	ERG
	//if(iterate_tdl_lex("../erg/lexicon.tdl"))
	if(process_tdl_status("lex-entry", lexify_tdl_line, 0))
#else
	if(iterate_tdl_dgs("../smallgrammars/esslli2k-grammar9/lexicon.tdl", study_lexicon))
#endif
	{
		fprintf(stderr, "lexicon: unable to load, bailing out.\n");
		exit(-1);
	}
/*#ifdef	ERG
	if(iterate_tdl_lex("special-lex.tdl"))
	{
		fprintf(stderr, "lexicon: unable to load special-lex, bailing out.\n");
		exit(-1);
	}
#endif*/
	qsort(lexemes, nlexemes, sizeof(struct lexeme*), lexcomp);
	printf("%d / %d = %.2f%% lexemes were simple\n",
		nsimpleform, nlexemes, 100*(float)nsimpleform / nlexemes);
	return 0;
}
