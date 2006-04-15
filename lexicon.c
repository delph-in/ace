#include	<stdlib.h>
#include	<string.h>
#include	<stdio.h>
#include	<assert.h>
#include	<ctype.h>
#include	<sys/time.h>

#include	"tdl.h"
#include	"lexicon.h"
#include	"chart.h"
#include	"morpho.h"
#include	"conf.h"
#include	"freeze.h"

struct lexeme	**lexemes;
int		nlexemes;

extern unsigned int	generation;

extern int trace, gen_qc;
extern unsigned int require_ep_span;

#define		SIMPLE_LEXICON

struct dg	*constrain_lex_rels(struct dg *dg, struct dg *rels);

struct dg	*lexical_dg(struct lexeme *lex, struct dg	*rels)
{
	struct dg	*ldg, *dg;
#ifdef	SIMPLE_LEXICON
	ldg = expand_lexeme(lex);
#else
	ldg = lex->dg;
#endif

	if(rels)ldg = constrain_lex_rels(lex->dg?copy_dg(ldg):ldg, rels);
	dg = unify_dg(ldg, lex->lextype->dg, -1);
	if(!dg)
	{
		if(rels)
		{
#ifdef UNIFY_PATHS
			//printf("specialization of `%s' failed: ", lex->word); unify_backtrace();
#endif
			return 0;
		}
		fprintf(stderr, "NOTE: ERROR! lexeme `%s' failed construction\n", lex->word);
#ifdef UNIFY_PATHS
		unify_backtrace();
#endif
		output_lui_dg(ldg, "lexical dag");
		output_lui_dg(lex->lextype->dg, "type dag");
		while(1)sleep(1);
		exit(-1);
	}
	dg = wellform_dg(dg);
	return dg;
}

struct edge	*lexical_edge(struct lexeme	*lex, int	i, int	len, struct dg	*rels)
{
	struct edge	*edge = slab_alloc(sizeof(struct edge));

//#define RECORD_LEXICAL_ACCESS
#ifdef RECORD_LEXICAL_ACCESS
	static FILE	*lexf = 0;
	if(!lexf)lexf = fopen("/tmp/lexical-access", "w");
	fprintf(lexf, "%s\n", lex->word);
	fflush(lexf);
#endif

	bzero(edge, sizeof(struct edge));
	edge->dg = lexical_dg(lex, rels);
	if(edge->dg == 0)return 0;
	edge->id = next_eid();
	edge->from = i;
	edge->to = i+len;
	edge->lex = lex;
	edge->qc = gen_qc?0:extract_qc(edge->dg);

	if(trace)
	{
		printf("added lexeme `%s' = #%d\n", lex->word, edge->id);
		if(trace>1){print_dg(edge->dg); printf("\n\n");}
	}

	return edge;
}

struct lexeme	*proper_noun_lex(char	*name)
{
	char			*p = name;
	struct lexeme	*lex;
	struct dg		*extra, *stem, *ss, *lk, *kr;
	struct type	*ty = temporary_string_type(name);
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

	// make a skeletal dag for our lexeme
	stem = atomic_dg(g_cons_type);
	stem = add_dg_feature(stem, "FIRST", ty->dg);
	stem = add_dg_feature(stem, "REST", atomic_dg(g_null_type));

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

	return lex;
}

void	record_pred(struct edge	*le, char	*word, char	*tag)
{
	char	*ncopy = malloc(strlen(word)+30);
	struct type	*pred_type;
	struct dg	*pred = calloc(sizeof(struct dg), 1);
	char	tagc = '?';
	switch(tag[0]) {	case 'N': tagc='n'; break;
						case 'V': tagc='v'; break;
						case 'J': tagc='a'; break;
						case 'R': tagc='a'; break;	}
	int		len = strlen(word);
	if(len>2 && word[len-2]=='e' && word[len-1]=='d' && (!strcmp(tag, "VBD") || !strcmp(tag, "VBN")))len-=2;
	else if(len>3 && word[len-3]=='i' && word[len-2]=='n' && word[len-1]=='g' && !strcmp(tag,"VBG"))len-=3;
	else if(len>1 && word[len-1]=='s' && (!strcmp(tag,"VBZ") || !strcmp(tag,"NNS")))len-=1;
	sprintf(ncopy, "\"_%.*s_%c_1_rel\"", len, word, tagc);
	pred_type = temporary_string_type(ncopy);
	pred->xtype = pred->type = pred_type;
	le->dg = dg_path_add(le->dg, lex_pred_path, pred);
}

void	record_carg(struct edge	*le, char	*carg_str)
{
	char	*ncopy = malloc(strlen(carg_str)+3);
	struct type	*carg_type;
	struct dg	*carg = calloc(sizeof(struct dg), 1);
	sprintf(ncopy, "\"%s\"", carg_str);
	carg_type = temporary_string_type(ncopy);
	carg->xtype = carg->type = carg_type;
	le->dg = dg_path_add(le->dg, lex_carg_path, carg);
}

#ifdef UNKNOWN
struct lexeme	*unknown_lexeme(char	*name, char	*type)
{
	struct lexeme	*lex;
	struct dg		*extra, *stem, *ss, *lk, *kr;
	char			*ncopy = malloc(strlen(name)+3);
	int				nlen;
	extern struct type	**strings;

	// make a skeletal dag for our lexeme
	extra = atomic_dg(type);
	assert(extra);

	sprintf(ncopy, "\"%s\"", name);

	// define the lexeme
	lex = slab_alloc(sizeof(struct lexeme));
	bzero(lex, sizeof(*lex));
	lex->word = name;
	lex->stemlen = 1;
	lex->stem = slab_alloc(sizeof(struct type*)*1);
	lex->stem[0] = temporary_string_type(ncopy);
	lex->lextype = extra->type;
	lex->dg = extra;

	return lex;
}

struct edge	*unknown_lexical_edge(int	i, char	*word, char *tag, int tagunk)
{
	char	*type = UNKNOWN;
	struct lexeme *l;
	struct edge *e;

	// map 'tag' to unknown lexeme type
		if(!strcmp(tag, "NNP"))type = "n_proper_le";
	else if(!strcmp(tag, "JJ"))type = "adj_intrans_le";
	else if(!strcmp(tag, "JJS"))type = "adj_intrans_superl_nale";
	else if(!strcmp(tag, "JJR"))type = "adj_intrans_compar_nale";
	else if(!strcmp(tag, "RB"))type = "adv_int_vp_le";//"adv_word_nale";
	else if(!strcmp(tag, "NNPS"))type = "n_proper_le";
	else if(!strcmp(tag, "NNS"))type = "n_intr_pl_nale";
	else if(!strcmp(tag, "NN"))type = "n_mass_count_le";
	else if(!strcmp(tag, "VB"))type = "v_np*_trans_bse_nale";
	else if(!strcmp(tag, "VBG"))type = "v_np*_trans_prp_nale";
	else if(!strcmp(tag, "VBD"))type = "v_np*_trans_past_nale";
	else if(!strcmp(tag, "VBN"))type = "v_np*_trans_psp_nale";
	else if(!strcmp(tag, "VBP"))type = "v_np*_trans_presn3sg_nale";
	else if(!strcmp(tag, "VBZ"))type = "v_np*_trans_pres3sg_nale";

	fprintf(stderr, "NOTE: guessing `%s' is a `%s' (POS %s%s)\n", word, type, tag, tagunk?"?":"");

	l = unknown_lexeme(word, type);
	e = lexical_edge(l, i, 1, 0);
	e->north_routes = -1;
	e->orth_routes = 0;

	if(type == "n_proper_le")record_carg(e, word);
	else record_pred(e, word, tag);

	return e;
}
#else
struct edge	*unknown_lexical_edge(int	i, char	*word, char *tag, int tagunk) { return 0; }
#endif

int	wstrcmp(char	*a, char	*b)
{
	while(*a)if(*b++!=*a++)return a[-1]-b[-1];
	if(*b)return -*b;
	return 0;
}

int	visit_lexicon(char	*stem, int (*visitor)(struct lexeme *lex))
{
	int		num=0;
	char		stemcmp[1024];
	int		i, j, k, r = -1;

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
			num += visitor(lexemes[i]);
		for(i=k-1;i>=0 && !wstrcmp(lexemes[i]->stem[0]->name, stemcmp);i--)
			num += visitor(lexemes[i]);
	}
	return num;
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

static int	stemfeat = -1, rfeat = 2, ffeat = 1, synsemfeat = -1, lkeysfeat, keyrelfeat,
			predfeat, compkeyfeat, ocompkeyfeat, phonfeat, onsetfeat, listfeat;
static struct type	*predsort, *con, *voc;

int	nsimpleform = 0;

void	init_fnums()
{
#ifdef	ERG
	stemfeat = lookup_fname("STEM");
#endif
	synsemfeat = lookup_fname("SYNSEM");
	lkeysfeat = lookup_fname("LKEYS");
	keyrelfeat = lookup_fname("KEYREL");
	predfeat = lookup_fname("PRED");
	compkeyfeat = lookup_fname("--+COMPKEY");
	ocompkeyfeat = lookup_fname("--+OCOMPKEY");
	phonfeat = lookup_fname("PHON");
	onsetfeat = lookup_fname("ONSET");
	listfeat = lookup_fname("LIST");
	predsort = lookup_type("predsort");
	con = lookup_type("con");
	voc = lookup_type("voc");
}

struct dg	*constrain_lex_rels(struct dg *dg, struct dg *rels)
{
	return dg_path_add(dg, lex_rels_path, rels);
}

struct type *predict_onset(char *form)
{
	if(*form=='"')form++;
	if(strchr("aeiouAEIOU", *form))return voc;
	else return con;
}

void	simplify_lexeme(struct lexeme	*l)
{
#ifdef	ERG
	struct dg	*dg = l->dg, *ss, *lk, *kr, *pr, *ph, *on;

	// check for most common structures and just store parameters for them
	if(l->stemlen != 1)return;
	if(dg->narcs != 2)return;

	// we want just 'STEM' and 'SYNSEM' features
	if(dg->label[0] != stemfeat && dg->label[1] != stemfeat)return;
	if(dg->label[0] != synsemfeat && dg->label[1] != synsemfeat)return;

	ss = dg_hop(dg, synsemfeat);
	// we want just 'LKEYS' and 'ONSET' or else just LKEYS
	if(ss->narcs == 2)
	{
		if(ss->label[0] != lkeysfeat && ss->label[1] != lkeysfeat)return;
		if(ss->label[0] != phonfeat && ss->label[1] != phonfeat)return;
	}
	else if(ss->narcs==1)
	{
		if(ss->label[0] != lkeysfeat)return;
	}
	else return;

	// check LKEYS branch
	lk = dg_hop(ss, lkeysfeat);
	// we want just 'KEYREL'
	if(lk->narcs != 1 || lk->label[0] != keyrelfeat)return;
	kr = dg_hop(lk, keyrelfeat);
	// we want just 'PRED'
	if(kr->narcs != 1 || kr->label[0] != predfeat)return;
	pr = dg_hop(kr, predfeat);
	// we want l->pred
	if(pr->narcs != 0 || pr->type != l->pred)return;

	// check PHON branch
	ph = dg_hop(ss, phonfeat);
	if(!ph)l->onset = 0;
	else
	{
		// we want just 'ONSET'
		if(ph->narcs != 1 || ph->label[0] != onsetfeat)return;
		on = dg_hop(ph, onsetfeat);
		// we want predictable onset
		if(on->narcs != 0 || on->type != predict_onset(l->stem[0]->name))return;
		l->onset = 1;
	}

	//printf("`%s' has simple form; ph = %p, onset = %d\n", l->word, ph, (int)l->onset);
	nsimpleform++;
	l->dg = 0;
#else
	return;
#endif
}

struct dg	*expand_lexeme(struct lexeme	*l)
{
	struct dg	*dg, *ss, *lk, *kr, *pr, *st, *f, *ph, *on;

	if(l->dg)return l->dg;
#ifdef	ERG
	if(stemfeat==-1)init_fnums();

	pr = atomic_dg("*top*");
	pr->xtype = pr->type = l->pred;
	kr = add_dg_hop(atomic_dg("*top*"), predfeat, pr);
	lk = add_dg_hop(atomic_dg("*top*"), keyrelfeat, kr);
	ss = add_dg_hop(atomic_dg("*top*"), lkeysfeat, lk);

	if(l->onset)
	{
		on = atomic_dg("*top*");
		on->xtype = on->type = predict_onset(l->stem[0]->name);
		ph = add_dg_hop(atomic_dg("*top*"), onsetfeat, on);
		ss = add_dg_hop(ss, phonfeat, ph);
	}

	f = atomic_dg("*top*");
	f->xtype = f->type = l->stem[0];
	st = add_dg_hop(atomic_dg("*top*"), ffeat, f);
	dg = atomic_dg("*top*");
	dg = add_dg_hop(dg, stemfeat, st);
	dg = add_dg_hop(dg, synsemfeat, ss);

	//printf("expanded `%s':\n", l->word);
	//print_dg(dg);printf("\n");

	return dg;
#else
	assert(!"Not reached (simple lexeme for non-ERG)");
#endif
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

	if(synsemfeat==-1)init_fnums();
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

	stem = dg_path(fulldg, lex_stem_path);

	for(;stem;stem = dg_hop(stem, rfeat))
	{
		str = dg_hop(stem, ffeat);
		if(!str)break;	// end of list.
		if(str->type->name[0] != '"')
		{
			fprintf(stderr, "lexicon: stem[%d] in word `%s' is not a string\n", l->stemlen, word);
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
		fprintf(stderr, "lexeme `%s' has open ended stem list!\n", word);
		exit(-1);
	}

#ifdef	SIMPLE_LEXICON
	simplify_lexeme(l);
#endif

	if(!temporary_expedient(l->word))
		add_semantic_index(l, fulldg, 0);

	//printf("lexeme %s has %d-long stem starting %s\n", word, l->stemlen, l->stem[0]->name);
	if(nlexemes%100 == 0)printf("lexeme %s             \r", word); fflush(stdout);
	/*printf("lexeme %s  --> ", word);
	print_dg(dg);
	printf("\n");*/
}

int		nexpedients = 0;
char	**expedients = 0;

int	load_expedients(char	*path)
{
	FILE	*f = 0;
	if(!path)return 0;
	f = fopen(path, "r");
	if(f)
	{
		char	exp[1024];
		while(fgets(exp, 1023, f) != NULL)
		{
			if(*exp && exp[strlen(exp)-1]=='\n')
				exp[strlen(exp)-1] = 0;
			nexpedients++;
			expedients = realloc(expedients, sizeof(char*)*(nexpedients+1));
			expedients[nexpedients-1] = strdup(exp);
			expedients[nexpedients] = 0;
		}
		fclose(f);
		return 0;
	}
	else
	{
		fprintf(stderr, "unable to load temporary expedients from `%s'\n", path);
		exit(-1);
	}
}

int temporary_expedient(char *sign)
{
	int i;

	for(i=0;i<nexpedients;i++)
		if(!strcmp(sign, expedients[i]))return 1;
	return 0;
}

int	study_lexeme(char	*name, struct dg	*dg, struct type	*t)
{
	struct dg	*fulldg;

	fulldg = unify_dg_noss(dg, t->dg, -1);
	if(!fulldg)
	{
		fprintf(stderr, "study_lexeme `%s': dag incompatible with type `%s'\n", name, t->name);
		return -1;
	}

	wellform(fulldg, name);

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
	struct timeval tv1, tv2;

	gettimeofday(&tv1, 0);
	if(process_tdl_status("lex-entry", lexify_tdl_line, 0))
	{
		fprintf(stderr, "lexicon: unable to load, bailing out.\n");
		exit(-1);
	}
	gettimeofday(&tv2, 0);
	show_task_name("loading lexicon...");
	printf("%.1f secs\n",
		(tv2.tv_sec - tv1.tv_sec) + 0.000001*(tv2.tv_usec-tv1.tv_usec));

	qsort(lexemes, nlexemes, sizeof(struct lexeme*), lexcomp);
	show_task_name("simple lexemes");
	printf("%d / %d = %.2f%%\n", nsimpleform, nlexemes, 100*(float)nsimpleform / nlexemes);
	return 0;
}
