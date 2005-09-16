#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<ctype.h>

int	invert_ipat(char	*pat, int patl, char	*rep, int repl, char	*form, int forml, char	*stem, int type);
struct lexeme	*proper_noun_lex(char	*name);

extern int	wcap[1024];

#include	"rule.h"
#include	"chart.h"
#include	"morpho.h"
#include	"tdl.h"
#include	"lexicon.h"
#include	"freeze.h"
#ifdef POST
#include "post.h"
#endif

#define	MAX_ORTH_RULES	5

int				norules;
struct orule	**orules;

struct ochart
{
	int	nedges;
	struct ochart_edge
	{
		char				*form;
		int					north_routes;
		struct orth_route	*orth_routes;
		int					nparents;
		struct ochart_parent
		{
			struct ochart_edge	*edge;
			struct orule		*rule;
		}					*parents;
	}	**edges;
};

void	add_ochart_edge(struct ochart_edge	*parent, struct ochart *chart, char *form, struct orule *rule);
void	unfold_oroutes(struct ochart_edge *target, struct ochart_edge *visit, int nrules, int *rnums);

void add_orules(struct edge *le)
{
	int i, j;

	for(i=0;i<le->north_routes;i++)
	{
		struct orth_route *r = le->orth_routes+i;
		for(j=0;j<r->len;j++)
			add_agenda_lrule(le->from, rules[r->rules[j]]);
	}
}

static inline is_type_route(struct orth_route *r, int type)
{
	int j;

	for(j=0;j<r->len;j++)
		if(rules[r->rules[j]]->orth->type != type)break;
	// found a route to this form by rule only of type `type'
	if(j==r->len)return 1;
	else return 0;
}

int is_type_only_oedge(struct ochart_edge *e, int type)
{
	int i;

	for(i=0;i<e->north_routes;i++)
		if(is_type_route(e->orth_routes+i, type))return 1;
	return 0;
}

void add_mwe_routes(struct edge *e, struct ochart_edge *pre, struct ochart_edge *suf)
{
	int i, j;

	for(i=0;i<suf->north_routes;i++) if(is_type_route(suf->orth_routes+i, or_suffix))
		for(j=0;j<pre->north_routes;j++) if(is_type_route(pre->orth_routes+j, or_prefix))
			{
				struct orth_route *a = suf->orth_routes+i, *b = pre->orth_routes+j;
				int len, *route;
				len = a->len + b->len;
				route = slab_alloc(sizeof(int)*len);
				memcpy(route, a->rules, sizeof(int)*a->len);
				memcpy(route+a->len, b->rules, sizeof(int)*b->len);
				allow_edge_orth_route(e, len, route);
			}
}

int	lexical_lookup(int	nwords, char	**lemmas, char	**forms)
{
	struct ochart	*charts = slab_alloc(sizeof(struct ochart)*nwords);
	int				i, j, k, cover[nwords];
	char	**tags;
	int		*unk;

	tags = malloc(sizeof(char*) * nwords);
	unk = malloc(sizeof(int) * nwords);
#ifdef	POST
	post_tag_sequence(nwords, lemmas, tags, unk);
#else
	for(i=0;i<nwords;i++)
	{
		tags[i] = "NNP";
		unk[i] = 1;
	}
#endif
//	for(i=0;i<nwords;i++)printf("%s%s ", tags[i], unk[i]?"?":"");
//	printf("\n");

	// compute all the orthography variations
	bzero(charts, sizeof(struct ochart)*nwords);
	bzero(cover, sizeof(int)*nwords);
	for(i=0;i<nwords;i++)
	{
		add_ochart_edge(0, charts+i, forms[i], 0);
		for(j=0;j<charts[i].nedges;j++)
			unfold_oroutes(charts[i].edges[j], charts[i].edges[j], 0, 0);
		/*for(j=0;j<charts[i].nedges;j++)
		{
			printf("%s -> %s\n", forms[i], charts[i].edges[j]->form);
			for(k=0;k<charts[i].edges[j]->north_routes;k++)
			{
				struct orth_route *r = charts[i].edges[j]->orth_routes+k;
				int					l;
				printf("	");
				for(l=0;l<r->len;l++)printf(" %s", rules[r->rules[l]]->name);
				printf("\n");
			}
		}*/
	}

	// do lexical lookup on each variation
	for(i=0;i<nwords;i++)
	{
		for(j=0;j<charts[i].nedges;j++)
		{
			struct ochart_edge *e = charts[i].edges[j];

			int lex_adder(struct lexeme *lex)
			{
				int k;
				struct edge			*le = 0;
				if(lex->stemlen!=1)
				{
					char *last = lex->stem[lex->stemlen-1]->name;
					char stemcmp[1024];
					if(!is_type_only_oedge(e, or_prefix))return 0;
					if(lex->stemlen + i > nwords)return 0;
					for(k=1;k<lex->stemlen-1;k++)
					{
						sprintf(stemcmp, "\"%s\"", forms[i+k]);
						// no inflection allowed on inner elements of MWEs
						if(strcasecmp(lex->stem[k]->name, stemcmp))
							return 0;
					}
					for(k=0;k<charts[i+lex->stemlen-1].nedges;k++)
					{
						struct ochart_edge *f = charts[i+lex->stemlen-1].edges[k];
						sprintf(stemcmp, "\"%s\"", f->form);
						if(!strcasecmp(stemcmp, last) && is_type_only_oedge(f, or_suffix))
						{
							le = lexical_edge(lex, i, lex->stemlen, 0);
							add_mwe_routes(le, e, f);
							break;
						}
					}
				}
				else
				{
					le = lexical_edge(lex, i, lex->stemlen, 0);
					if(strcasecmp(forms[i], lemmas[i]))
						record_carg(le, lemmas[i]);
					le->north_routes = e->north_routes;
					le->orth_routes = e->orth_routes;
				}
				if(le)
				{
					for(k=0;k<lex->stemlen;k++)
						cover[i+k]++;
					add_agenda(le);
					add_orules(le);
					//printf("added lexeme `%s' at %d\n", lex->word, i);
					return 1;
				}
				else return 0;
			}
			visit_lexicon(e->form, lex_adder);
		}
	}

#ifdef	UNKNOWN
	for(i=0;i<nwords;i++)
	{
		if(cover[i] == 0)
		{
			char	*stem = freeze_string(forms[i]);
			int		l;
			struct edge *le;
			while(stem[0]=='"' || stem[0]=='\'' || stem[0]=='[' || stem[0]=='(')stem++;
			l = strlen(stem)-1;
			while(l>=0 && (stem[l]=='"' || stem[l]=='\'' || stem[l]==']' || stem[l]==')' || stem[l]=='?' || stem[l]=='!' || stem[l]==',' || stem[l]=='.'))
				l--;
			stem[l+1] = 0;
			//printf("NOTE: suffix removal from `%s' produced `%s'\n", forms[i], stem);
			//for(j=0;j<charts[i].nedges;j++) printf(" choice `%s'\n", charts[i].edges[j]->form);
			if(stem[0])
			{
				for(j=0;j<charts[i].nedges;j++)
					if(!strcasecmp(charts[i].edges[j]->form, stem))
						break;
				if(j<charts[i].nedges)
				{
					struct ochart_edge	*e = charts[i].edges[j];
					le = unknown_lexical_edge(i, stem, tags[i], unk[i]);
					le->north_routes = e->north_routes;
					le->orth_routes = e->orth_routes;
					add_agenda(le);
					add_orules(le);
				}
			}
		}
	}
#endif
	free(tags);
	free(unk);

	return 0;
}

int	*longer_rule_list(int nr, int *ru, struct orule	*or)
{
	int *r = slab_alloc(sizeof(int)*(nr+1));
	memcpy(r+1,ru,sizeof(int)*nr);
	r[0] = or->rule->rnum;
	return r;
}

int	*longer_rule_list_rev(int nr, int *ru, struct orule	*or)
{
	int *r = slab_alloc(sizeof(int)*(nr+1));
	memcpy(r,ru,sizeof(int)*nr);
	r[nr] = or->rule->rnum;
	return r;
}

int record_oroute(struct ochart_edge *e, int nrules, int *rnums)
{
	int i;

	// see if this route is already packed on the edge
	for(i=0;i<e->north_routes;i++)
	{
		struct orth_route *r = e->orth_routes+i;
		if(r->len == nrules && !memcmp(r->rules, rnums, sizeof(int)*nrules))
			return 0;
	}

	// otherwise, add it
	e->north_routes++;
	e->orth_routes = slab_realloc(e->orth_routes,
		sizeof(struct orth_route) * (e->north_routes-1),
		sizeof(struct orth_route) * e->north_routes);
	e->orth_routes[e->north_routes-1].len = nrules;
	e->orth_routes[e->north_routes-1].rules = rnums;
	return 1;
}

void unfold_oroutes(struct ochart_edge *target, struct ochart_edge *visit, int nrules, int *rnums)
{
	int i;

	// don't go down garden paths
	if(nrules >= MAX_ORTH_RULES)return;

	for(i=0;i<visit->nparents;i++)
	{
		struct ochart_parent	*d = visit->parents+i;
		int						*mnums = d->rule?longer_rule_list_rev(nrules, rnums, d->rule):0;
		if(d->edge)unfold_oroutes(target, d->edge, nrules+1, mnums);
		else record_oroute(target, nrules, rnums);
	}
}

void add_oparent(struct ochart_edge *daughter, struct ochart_edge *parent, struct orule *rule)
{
	//if(parent)printf("add %p [`%s' %s] parent of %p `%s'\n", parent, parent->form, rule->rule->name, daughter, daughter->form);
	daughter->nparents++;
	daughter->parents = slab_realloc(daughter->parents,
				sizeof(struct ochart_parent) * (daughter->nparents-1),
				sizeof(struct ochart_parent) * daughter->nparents);
	daughter->parents[daughter->nparents-1].edge = parent;
	daughter->parents[daughter->nparents-1].rule = rule;
}

void	add_ochart_edge(struct ochart_edge	*parent, struct ochart *chart, char *form, struct orule *rule)
{
	int	i, forml = strlen(form);
	struct ochart_edge *edge = 0;

	// check for a compatible edge already in the chart
	for(i=0;i<chart->nedges;i++)
		if(!strcmp(chart->edges[i]->form, form))
		{
			// pack into this edge; the forms possible from here have already been explored.
			add_oparent(chart->edges[i], parent, rule);
			return;
		}

	//printf(".. `%s' is new\n", form);
	// completely new edge; add it to the chart
	chart->nedges++;
	chart->edges = slab_realloc(chart->edges, sizeof(struct ochart_edge*)*(chart->nedges-1), sizeof(struct ochart_edge)*chart->nedges);
	edge = chart->edges[chart->nedges-1] = slab_alloc(sizeof(struct ochart_edge));
	edge->form = freeze_string(form);
	edge->north_routes = 0;
	edge->orth_routes = 0;
	edge->nparents = 0;
	edge->parents = 0;

	add_oparent(edge, parent, rule);

	// this edge is at least partly new; try to combine it with all the orthographemic rules
	for(i=0;i<norules;i++)
	{
		int				j;//, *mrules = 0;
		char			stem[1024];
		struct orule	*or = orules[i];
		struct irreg	*irr;

		for(j=0;j<or->nirregs;j++)
		{
			irr = or->irregs+j;
			if(!strcmp(form, irr->form))
			{
				//fprintf(stderr, "used irreg `%s -> %s' for rule %s\n",
				//		irr->stem, irr->form, or->rule->name);
				add_ochart_edge(edge, chart, irr->stem, or);
				break;	// TODO support irregs.tab defining whether to cut the search here or continue it
			}
		}
		if(j<or->nirregs)continue;	// if we got it with an irreg, don't try patterns
		for(j=or->nsub-1;j>=0;j--)
		{
			struct orule_sub *sub = or->sub+j;
			if(!invert_ipat(sub->pat, sub->patl, sub->rep, sub->repl, form, forml, stem, or->type))
			{
				// good guess; now check whether 'stem' actually inflects to 'form'
				/*char *check = inflect(or, stem);
				if(!check || strcasecmp(check, form))
				{
					fprintf(stderr, "hah! we avoided being tricked; `%s' doesn't disinflect to `%s' under `%s', since that would go to `%s'\n",
						form, stem, or->rule->name, check?check:"(null)");
					if(check)free(check);
					continue;
				}
				free(check);*/

				//fprintf(stderr, "form `%s' = stem `%s' under rule `%s'[%d], i.e. `%s' replaced by `%s'\n",
				//		form, stem, or->rule->name, j, sub->pat, sub->rep);
				add_ochart_edge(edge, chart, stem, or);
			}
		}
	}
	return;
}


char	*lgroups[256];

void	set_lgroup(int	group, char	*letters)
{
	//printf("letter group %c = %s\n", group, letters);
	if(lgroups[group])
	{
		fprintf(stderr, "morpho: redefining letter group `%c' not allowed\n", group);
		exit(-1);
	}
	lgroups[group] = calloc(256, sizeof(char));
	while(*letters)
		lgroups[group][(int)*letters++] = 1;
}

void	setup_morpho(char	*line)
{
	char	*end;
	int	letter;

	line = trim_string(line);
	end = line + strlen(line)-1;
	if(*end != ')')
	{
		fprintf(stderr, "morpho: setup command `%s' malformed\n", line);
		exit(-1);
	}
	*end = 0;

	end = line;
	while(*end && *end!=' ' && *end!='\t')end++;
	if(*end)*end=0;
	if(!strcmp(line, "letter-set"))
	{
		fprintf(stderr, "morpho: setup command `%s' not supported\n", line);
		exit(-1);
	}

	line = trim_string(end+1);
	if(*line != '(' || line[1] != '!')
	{
		synt:
		fprintf(stderr, "morpho: letter-set syntax is (!x abcd...)\n");
		exit(-1);
	}
	end = line + strlen(line)-1;
	if(*end != ')')goto synt;
	*end = 0;
	line = trim_string(line+2);

	letter = *line;
	if(line[1] != ' ' && line[1] != '\t')goto synt;
	set_lgroup(letter, trim_string(line+2));
}

int	appropriate_letter(int	let, int	group)
{
	if(!lgroups[group])
	{
		fprintf(stderr, "morpho: no such letter group as `%c'\n", group);
		exit(-1);
	}
	else if(lgroups[group][let])return 1;
	else return 0;
}

char	*inflect(struct orule	*or, char	*str)
{
	char	*res=0;
	int		i, strl = strlen(str);

	for(i=0;i<or->nirregs;i++)
		if(!strcmp(str, or->irregs[i].stem))
			{res = freeze_string(or->irregs[i].form);break;}
	if(!res)
	{
		int slen = strlen(str)+10;
		if(slen%4)slen = (slen&~3)+4;
		res = slab_alloc(slen);
		strcpy(res, str);
		for(i=or->nsub-1;i>=0;i--)
		{
			struct orule_sub *sub = or->sub+i;
			if(!invert_ipat(sub->rep, sub->repl, sub->pat, sub->patl, str, strl, res, or->type))
			{
				//printf("applied (%s %s)\n", sub->pat, sub->rep);
				break;
			}
		}
		if(i<0)res=0;
		if(!or->nsub)res = freeze_string(str);
	}
	//printf("inflected[%s] `%s' to `%s'\n", or->rule->name, str, res);
	return res;
}

int	invert_ipat_prefix(char	*pat, int patl, char	*rep, int repl, char	*form, int forml, char	*stem)
{
	int	let, lclear = 0;
	int	ri = 0, fi = 0;
	char	*rstem = stem, *rpat = pat, *rrep = rep, letters[256];

	if(repl==1 && rep[0]=='*')ri++;	// trivially match
	else do
	{
		let = form[fi++];
		//printf("now to match `%c'; [%d %d]\n", let, len, len2);
		if(ri+1<repl && rep[ri]=='!')
		{
			if(!lclear){bzero(letters, 256);lclear=1;}
			if(appropriate_letter(let, rep[++ri]))
			{
				if(letters[(int)rep[ri]] == 0)
					letters[(int)rep[ri]] = let;
				else if(let != letters[(int)rep[ri]])
					break;
				ri++;
			}
			else break;
		}
		else if(rep[ri] != let)break;
		else ri++; // matched.
	} while(ri<repl && fi<forml);

	if(ri==repl)
	{
		match:
		// success!
		if(*pat!='*') while(*pat)
		{
			if(*pat != '!')
				*stem++ = *pat++;
			else
			{
				*stem++ = letters[(int)pat[1]];
				pat+=2;
			}
		}
		memcpy(stem, form+fi, forml-fi+1);
		//printf(" form `%s' -> stem `%s' under pat `%s' rep `%s' [len %d len2 %d]\n", form, rstem, rpat, rrep, len, len2);
		return 0;
	}
	else return -1;
}

int	invert_ipat_suffix(char	*pat, int patl, char	*rep, int repl, char	*form, int forml, char	*stem)
{
	int	let, lclear = 0;
	int	len = forml-1, len2 = repl-1;
	char	*rstem = stem, *rpat = pat, *rrep = rep, letters[256];

	if(repl==1 && rep[0]=='*')len2--;	// trivially match
	else do
	{
		let = form[len--];
		//printf("now to match `%c'; [%d %d]\n", let, len, len2);
		if(len2>0 && rep[len2-1]=='!')
		{
			if(!lclear){bzero(letters, 256);lclear=1;}
			if(appropriate_letter(let, rep[len2]))
			{
				//printf("`%c' is appropriate for `%c'\n", let, rep[len2]);
				if(letters[(int)rep[len2]] == 0)
					letters[(int)rep[len2]] = let;
				else if(let != letters[(int)rep[len2]])
					break;
				len2-=2;
			}
			else break;
		}
		else if(rep[len2] != let)break;
		else len2--; // matched.
	} while(len2>=0 && len>=0);

	if(len2<0)
	{
		match:
		// success!
		strncpy(stem, form, len+1);
		stem += len+1;
		if(*pat!='*') while(*pat)
		{
			if(*pat != '!' || !pat[1])
				*stem++ = *pat++;
			else
			{
				*stem++ = letters[(int)pat[1]];
				pat+=2;
			}
		}
		*stem++ = 0;
		//printf(" form `%s' -> stem `%s' under pat `%s' rep `%s' [len %d len2 %d]\n", form, rstem, rpat, rrep, len, len2);
		return 0;
	}
	else return -1;
}

int	invert_ipat(char	*pat, int patl, char	*rep, int repl, char	*form, int forml, char	*stem, int type)
{
	if(type == or_suffix)
		return invert_ipat_suffix(pat, patl, rep, repl, form, forml, stem);
	else return invert_ipat_prefix(pat, patl, rep, repl, form, forml, stem);
}

void	setup_orule(struct orule	*or, char	*def)
{
	char	*sp, *type, *next;

	or->nsub = 0;
	or->sub = 0;

	if(!def)return;
	for(sp=def;*sp && *sp!=' ' && *sp!='\t';sp++) {}
	if(!*sp)
	{
		fprintf(stderr, "morpho: malformed orthographemic pattern `%s'\n", def);
		exit(-1);
	}

	*sp = 0;
	type = trim_string(def);
	def = trim_string(sp+1);
	if(strcmp(type, "suffix") && strcmp(type, "prefix"))
	{
		fprintf(stderr, "morpho: only `suffix' and `prefix' orthographemic patterns are supported (not `%s')\n", type);
		exit(-1);
	}
	if(!strcmp(type, "suffix"))or->type = or_suffix;
	else or->type = or_prefix;

	while(*def == '(')
	{
		struct orule_sub *sub;
		def++;
		sp = strchr(def, ')');
		if(!sp)
		{
			fprintf(stderr, "morpho: subrule `%s' is not closed\n", def);
			exit(-1);
		}
		*sp = 0;
		next = sp+1;
		for(sp=def;*sp && *sp!=' ' && *sp!='\t';sp++) {}
		if(!*sp)
		{
			fprintf(stderr, "morpho: subrule `%s' has no RHS\n", def);
			exit(-1);
		}
		*sp = 0;
		sp = trim_string(sp+1);
		//printf("subrule lhs `%s', rhs `%s'\n", def, sp);
		or->nsub++;
		or->sub = realloc(or->sub, sizeof(struct orule_sub)*or->nsub);
		sub = or->sub + or->nsub-1;
		sub->pat = strdup(def);
		sub->patl = strlen(def);
		sub->rep = strdup(sp);
		sub->repl = strlen(sp);

		def = trim_string(next);
	}
	if(*def)
	{
		fprintf(stderr, "morpho: syntax for orthographemic patterns is %%type (pat rep) (pat rep) ...\n");
		fprintf(stderr, "   trailing garbage `%s' after subrules\n", def);
		exit(-1);
	}
}

void	add_orule(struct rule	*rule, char	*def)
{
	struct orule	*or = calloc(sizeof(struct orule), 1);

	norules++;
	orules = realloc(orules, sizeof(struct orule*) * norules);
	orules[norules-1] = or;

	or->rule = rule;
	or->nirregs = 0;
	or->irregs = 0;
	rule->orth = or;
	setup_orule(or, def);
	//printf("rule `%s' has %d orthographemic patterns\n", rule->name, or->nsub);
}

struct orule	*lookup_orule(char	*rule)
{
	int	i;

	for(i=0;i<norules;i++)
		if(!strcasecmp(rule, orules[i]->rule->name))
			return orules[i];
	return 0;
}

int	load_irregulars(char	*path)
{
	FILE	*f = fopen(path, "r");
	char	line[1024], copy[1024];
	char	*form =0 , *rule =0 , *stem =0 ;
	struct orule	*or;

	if(!f) { perror(path); return -1; }
	while(fgets(line, 1024, f))
	{
		if(!*line)break;
		if(*line=='\n')continue;
		line[strlen(line)-1] = 0;

		if(*line=='"')continue;	// silly ERG .tab files have "'s at the beginning and ends
		strcpy(copy, line);
		form = strtok(line, " \t");
		if(form)rule = strtok(0, " \t");
		if(rule)stem = strtok(0, " \t");
		if(!form || !rule || !stem || !*form || !*rule || !*stem)
		{
			fprintf(stderr, "orth: invalid irregular form `%s'\n", copy);
			return -1;
		}
		sprintf(copy, "%s_infl_rule", rule);
		or = lookup_orule(copy);
		if(!or)
		{
			//fprintf(stderr, "orth: warning -- irregular for nonextant orule `%s'\n", copy);
			continue;
		}
		//printf("add form `%s', stem `%s' TO rule: %s\n", form, stem, or->rule->name);
		or->nirregs++;
		or->irregs = realloc(or->irregs, sizeof(struct irreg)*or->nirregs);
		or->irregs[or->nirregs-1].stem = strdup(stem);
		or->irregs[or->nirregs-1].form = strdup(form);
	}
	fclose(f);
	return 0;
}

int	load_lrules()
{
	char		fname[256];
	extern char	*grammar_dir;
	extern int	study_lrule(char	*name, struct dg	*dg);

	if(process_tdl_dgs_status("lex-rule", study_lrule))
	{
		fprintf(stderr, "inflr: unable to load, bailing out.\n");
		exit(-1);
	}

	sprintf(fname, "%s/irregs.tab", grammar_dir);
	if(load_irregulars(fname))
	{
		fprintf(stderr, "inflr: unable to load irregulars table `%s'.\n", fname);
		exit(-1);
	}
	return 0;
}
