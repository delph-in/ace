#include	<stdlib.h>
#include	<string.h>
#include	<stdio.h>

#include	"tdl.h"
#include	"lexicon.h"
#include	"chart.h"
#include	"morpho.h"

struct lexeme	**lexemes;
int		nlexemes;

extern int trace;

#define	FULL_LEXICON

struct edge	*lexical_edge(struct lexeme	*lex, int	i, int	len)
{
	struct edge	*edge = slab_alloc(sizeof(struct edge));

	bzero(edge, sizeof(struct edge));

#ifdef	FULL_LEXICON
	edge->dg = lex->dg;
#else
	edge->dg = unify_dg(lex->dg, lex->lextype->dg, -1);
	if(!edge->dg)
	{
		fprintf(stderr, "NOTE: ERROR! lexeme `%s' failed delayed unification\n", lex->word);
		exit(-1);
	}
	wellform(edge->dg, lex->word);
#endif

	//edge->dg = copy_dg(edge->dg);
	edge->id = next_eid();
	edge->from = i;
	edge->to = i+len;
	edge->lex = lex;
	edge->qc = extract_qc(edge->dg);
	edge->prio = LEX_PRIO;

	if(trace)
	{
		printf("added lexeme `%s' = #%d\n", lex->word, edge->id);
		if(trace>1){print_dg(edge->dg); printf("\n\n");}
	}

	return edge;
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

int	add_lexemes_to_chart_for_stem(int	x, char	*stem)
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
		{
			if(lexemes[i]->stemlen>1)continue;
			if(!lexeme_in_agenda(lexemes[i], x, x+1))
				add_agenda(lexical_edge(lexemes[i], x, 1));
			num++;
		}
		for(i=k-1;i>=0 && !wstrcmp(lexemes[i]->stem[0]->name, stemcmp);i--)
		{
			if(lexemes[i]->stemlen>1)continue;
			if(!lexeme_in_agenda(lexemes[i], x, x+1))
				add_agenda(lexical_edge(lexemes[i], x, 1));
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

void	add_lexeme(char	*word, struct dg	*dg, struct type	*type, struct dg	*fulldg)
{
	struct lexeme	*l = malloc(sizeof(struct lexeme));
	struct dg	*stem, *str, *synsem, *lkeys, *keyrel, *pred, *compkey, *ocompkey;
	static int	sfeat = -1, rfeat = 2, ffeat = 1, synsemfeat, lkeysfeat, keyrelfeat, predfeat, compkeyfeat, ocompkeyfeat;
	static struct type	*predsort;

	//wellform(dg, word);

	if(sfeat==-1)
	{
		sfeat = lookup_fname("STEM");
		synsemfeat = lookup_fname("SYNSEM");
		lkeysfeat = lookup_fname("LKEYS");
		keyrelfeat = lookup_fname("KEYREL");
		predfeat = lookup_fname("PRED");
		compkeyfeat = lookup_fname("--+COMPKEY");
		ocompkeyfeat = lookup_fname("--+OCOMPKEY");
		predsort = lookup_type("predsort");
	}
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

	for(stem = dg_hop(fulldg, sfeat);stem;stem = dg_hop(stem, rfeat))
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
		//printf("%s[%d] = %s\n", word, l->stemlen, str->type->name);
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
#ifdef	ERG
	if(iterate_tdl_lex("../erg/lexicon.tdl"))
#else
	if(iterate_tdl_dgs("../smallgrammars/esslli2k-grammar9/lexicon.tdl", study_lexicon))
#endif
	{
		fprintf(stderr, "lexicon: unable to load, bailing out.\n");
		exit(-1);
	}
#ifdef	ERG
	if(iterate_tdl_lex("special-lex.tdl"))
	{
		fprintf(stderr, "lexicon: unable to load special-lex, bailing out.\n");
		exit(-1);
	}
#endif
	qsort(lexemes, nlexemes, sizeof(struct lexeme*), lexcomp);
	return 0;
}
