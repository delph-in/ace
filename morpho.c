#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<limits.h>
#include	<wchar.h>
#include	<wctype.h>
#include	<ctype.h>
#include	"unicode.h"

#define	USE_GENERICS

//#define	SHOW_ORTH_ROUTE_COUNTS
//#define	SHOW_ORTH_ROUTES

static int	invert_ipat(wchar_t	*pat, int patl, wchar_t	*rep, int repl, wchar_t	*form, int forml, char	*stem, int type);
struct lexeme	*proper_noun_lex(char	*name);

//extern int	wcap[1024];

#include	"conf.h"
#include	"rule.h"
#include	"chart.h"
#include	"morpho.h"
#include	"tdl.h"
#include	"lexicon.h"
#include	"freeze.h"
#include	"hash.h"

#include	"token.h"

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
void	unfold_oroutes(struct ochart_edge *target, struct ochart_edge *visit, int routelen, int *rnums);

int	check_mbs(char	*mbs);
/*int	check_mbs(char	*mbs)
{
	int		len = strlen(mbs);
	wchar_t	*wide = malloc(sizeof(wchar_t)*(len+1));
	if(-1 == mbstowcs(wide, mbs, len+1))
	{
		free(wide);
		return -1;
	}
	else
	{
		free(wide);
		return 0;
	}
}*/

wchar_t	*towide(char	*mbs);
/*wchar_t	*towide(char	*mbs)
{
	int		len = strlen(mbs);
	wchar_t	*wide = malloc(sizeof(wchar_t)*(len+1));
	if(-1 == mbstowcs(wide, mbs, len+1))
	{
		extern char	*current_sentence;
		restore_stderr();
		perror("multibyte decoding failed");
		fprintf(stderr, "on string `%s'\n", mbs);
		if(current_sentence)
			fprintf(stderr, "in sentence `%s'\n", current_sentence);
		exit(-1);
	}
	return wide;
}*/

char	*freeze_tombs(wchar_t	*w)
{
	char	*str = slab_alloc(4*(wcslen(w)+1));
	if(-1 == wcstombs(str, w, 1+4*wcslen(w)))
	{
		restore_stderr();
		perror("multibyte encoding failed");
		fprintf(stderr, "on wcs `%S'\n", w);
		exit(-1);
	}
	return str;
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

char	*disable_signs = 0;

show_form_to_relation(struct edge	*le, struct token	**toklist)
{
	if(le->lex->stemlen > 1)return 0;
	// SYNSEM LKEYS KEYREL PRED
	static int save[100] = {-1};
	struct dg	*preddg = walk_dg(le->dg, save, "SYNSEM", "LKEYS", "KEYREL", "PRED", NULL);
	if(!preddg) fprintf(stderr, "warning: lexeme `%s' has null pred\n", le->lex->word);
	else printf("FORM-TO-RELATION	%s	%s\n", toklist[0]->string, preddg->xtype->name);
}

char	*mbsdowncase(char	*mbsin)
{
	int	len = strlen(mbsin);
	wchar_t	wcs[len+5];
	int	n = mbstowcs(wcs, mbsin, len+3);
	int	i;
	for(i=0;i<n;i++)
		wcs[i] = towlower(wcs[i]);
	int	olen = wcstombs(NULL, wcs, 0) + 1;
	char	*mbsout = malloc(n + 2);
	wcstombs(mbsout, wcs, olen+1);
	return mbsout;
}

int	lexical_lookup(struct lattice	*tc, void	(*edge_handler)(struct edge	*e, struct token	**tokens))
{
	int				i, j, k;

	sort_lattice(tc);
	//print_token_chart(tc);

	// compute all the orthography variations of each token
	for(i=0;i<tc->nedges;i++)
	{
		//printf("edge %d\n", i);
		struct lattice_edge	*lat_e = tc->edges[i];
		struct token	*t = lat_e->token;
		t->oc = slab_alloc(sizeof(struct ochart));
		bzero(t->oc, sizeof(struct ochart));
		// compute a lowercase version of t->string
		char	*low = mbsdowncase(t->string);
		// compute all possible stems, with dynamic programming
		add_ochart_edge(0, t->oc, low, 0);
		free(low);
		// enumerate all possible ways of reaching each possible stem
		for(j=0;j<t->oc->nedges;j++)
			unfold_oroutes(t->oc->edges[j], t->oc->edges[j], 0, 0);
#ifdef	SHOW_ORTH_ROUTE_COUNTS
		for(j=0;j<t->oc->nedges;j++)
		{
			printf("%s -> %s [%d ways]\n", t->string, t->oc->edges[j]->form, t->oc->edges[j]->north_routes);
#ifdef	SHOW_ORTH_ROUTES
			for(k=0;k<t->oc->edges[j]->north_routes;k++)
			{
				struct orth_route *r = t->oc->edges[j]->orth_routes+k;
				int					l;
				printf("	");
				for(l=0;l<r->len;l++)printf(" %s", rules[r->rules[l]]->name);
				printf("\n");
			}
#endif
		}
#endif
	}

	// do lexical lookup on each variation of each token
	for(i=0;i<tc->nedges;i++)
	{
		struct lattice_edge	*lat_e = tc->edges[i];
		struct token	*t = lat_e->token;
		struct ochart	*oc = t->oc;
		//printf("LEXICAL_LOOKUP on token:\n"); print_dg(t->dg); printf("\n");
		for(j=0;j<oc->nedges;j++)
		{
			struct ochart_edge *e = oc->edges[j];
			struct token	*mwe_trace[10];

			int	mwe_lex_handler(struct lexeme	*lex, struct lattice_edge	*mwe_lat_e, int	pos)
			{
				struct token	*tok = mwe_lat_e->token;
				int	nles = 0, i;
				mwe_trace[pos] = tok;
				if(pos < lex->stemlen-1)
				{
					// try to align one more token to lex[pos]
					if(pos==0)
					{
						// the 0th position in MWEs can only have prefix orules
						if(!is_type_only_oedge(e, or_prefix))return 0;
					}
					else if(pos < (lex->stemlen - 1))
					{
						// the inner positions in MWEs can't have any orules
						char	stemcmp[1024];
						sprintf(stemcmp, "\"%s\"", tok->string);
						if(strcasecmp(lex->stem[pos]->name, stemcmp))return 0;
					}
					for(i=0;i<mwe_lat_e->to->nfollowers;i++)
						nles += mwe_lex_handler(lex, mwe_lat_e->to->followers[i], pos+1);
					return nles;
				}
				else
				{
					int	noroutes = 0;
					struct oroute	*oroutes = NULL;

					// the end position in MWEs can only have suffix orules
					// see if there's a hypothesized form that can be reached just by suffixes
					// and that matches the MWE's stem form
					for(i=0;i<tok->oc->nedges;i++)
					{
						struct ochart_edge	*e2 = tok->oc->edges[i];
						char	stemcmp[1024];
						sprintf(stemcmp, "\"%s\"", e2->form);
						if(strcasecmp(lex->stem[pos]->name, stemcmp))continue;
						if(is_type_only_oedge(e2, or_suffix))
						{
							// yes, 'e2' represents a plausible analysis of
							//   this token as the last part of this MWE
							break;
						}
					}
					if(i==tok->oc->nedges)return 0;	// nope.

					// we have a full alignment of lex onto the token chart,
					// specifically lex[k] => mwe_trace[k]
					struct edge	*le = lexical_edge(lex, lat_e->from->id, mwe_lat_e->to->id - lat_e->from->id, 0);
					// go through the analyses of the last token again and record all the possible oroutes
					for(i=0;i<tok->oc->nedges;i++)
					{
						struct ochart_edge	*e2 = tok->oc->edges[i];
						char	stemcmp[1024];
						sprintf(stemcmp, "\"%s\"", e2->form);
						if(strcasecmp(lex->stem[pos]->name, stemcmp))continue;
						if(is_type_only_oedge(e2, or_suffix))
						{
							// yes, 'e2' represents a plausible analysis of
							//   this token as the last part of this MWE
							add_mwe_routes(le, e, e2);
						}
					}
					if(install_tokens_in_le(le, mwe_trace) != 0)return 0;
					edge_handler(le, mwe_trace);
					return 1;
				}
			}
			int lex_handler(struct lexeme *lex)
			{
				int k;
				struct edge			*le = 0;
				if(disable_signs && strstr(disable_signs, lex->word))return 0;
				if(lex->stemlen!=1)
					return mwe_lex_handler(lex, lat_e, 0);
				else
				{
					le = lexical_edge(lex, lat_e->from->id, lat_e->to->id-lat_e->from->id, 0);
					le->north_routes = e->north_routes;
					le->orth_routes = e->orth_routes;
					if(install_tokens_in_le(le, &t) != 0)return 0;
					extern int form_to_relation;
					if(form_to_relation)
						show_form_to_relation(le, &t);
					edge_handler(le, &t);
					return 1;
				}
			}
			visit_lexicon(e->form, lex_handler);
		}
/*#ifdef	USE_GENERICS
		char	*carg = get_token_carg(t);
		//printf("carg='%s' for token; trying generic entries (postag = %s)\n", carg, t->postag);
		struct ochart_edge	*carg_e = NULL;
		for(j=0;j<oc->nedges;j++)if(!strcasecmp(carg, oc->edges[j]->form))carg_e = oc->edges[j];
		//printf(" ... carg_e = %p\n", carg_e);
		if(carg_e)
		{
			for(j=0;j<ngeneric_les;j++)
			{
				if(test_first_token_in_lex_dg(generic_les[j]->dg, t) != 0)
				{
					//fprintf(stderr, "TEST rejected generic '%s'\n", generic_les[j]->word);
					continue;
				}
				struct edge	*le = lexical_edge(generic_les[j], lat_e->from->id, lat_e->to->id-lat_e->from->id, 0);
				le->north_routes = carg_e->north_routes;
				le->orth_routes = carg_e->orth_routes;
				if(install_tokens_in_le(le, &t) != 0)continue;
				//fprintf(stderr, "for carg '%s' and pos '%s', issued generic '%s'\n", carg, t->postag, le->lex->word);
				edge_handler(le, &t);
			}
		}
#endif*/
//#undef USE_GENERICS
#ifdef	USE_GENERICS
		int k;
		for(k=0;k<oc->nedges;k++)
		{
			struct ochart_edge	*carg_e = oc->edges[k];
			for(j=0;j<ngeneric_les;j++)
			{
				if(test_first_token_in_lex_dg(generic_les[j]->dg, t) != 0)
				{
					//fprintf(stderr, "TEST rejected generic '%s'\n", generic_les[j]->word);
					continue;
				}
				struct edge	*le = lexical_edge(generic_les[j], lat_e->from->id, lat_e->to->id-lat_e->from->id, 0);
				le->north_routes = carg_e->north_routes;
				le->orth_routes = carg_e->orth_routes;
				if(install_tokens_in_le(le, &t) != 0)continue;
				//fprintf(stderr, "for carg '%s' and pos '%s', issued generic '%s'\n", carg, t->postag, le->lex->word);
				extern int form_to_relation;
				if(form_to_relation)
					show_form_to_relation(le, &t);
				edge_handler(le, &t);
			}
		}
#endif
	}

/*
#ifdef	UNKNOWN
	for(i=0;i<nwords;i++)
	{
		if(cover[i] == 0)
		{
			char	*stem = freeze_string(words[i]);
			int		l;
			struct edge *le;
			while(stem[0]=='"' || stem[0]=='\'' || stem[0]=='[' || stem[0]=='(')stem++;
			l = strlen(stem)-1;
			while(l>=0 && (stem[l]=='"' || stem[l]=='\'' || stem[l]==']' || stem[l]==')' || stem[l]=='?' || stem[l]=='!' || stem[l]==',' || stem[l]=='.'))
				l--;
			stem[l+1] = 0;
			//printf("NOTE: suffix removal from `%s' produced `%s'\n", words[i], stem);
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
					edge_handler(le);
				}
			}
		}
	}
#endif
	free(tags);
	free(unk);*/

	return 0;
}

int	*longer_rule_list_rev(int nr, int *ru, struct orule	*or)
{
	int *r = slab_alloc(sizeof(int)*(nr+1));
	memcpy(r,ru,sizeof(int)*nr);
	r[nr] = or->rule->rnum;
	return r;
}

/* record that you can get to stem form 'e' from the 
 * surface form by applying rules 'rnums' each in the backwards direction
 */
int record_oroute(struct ochart_edge *e, int routelen, int *rnums)
{
	int i, j;

	// see if this route is already recorded on the edge
	// XXX can this actually happen?
	for(i=0;i<e->north_routes;i++)
	{
		struct orth_route *r = e->orth_routes+i;
		if(r->len == routelen && !memcmp(r->rules, rnums, sizeof(int)*routelen))
		{
			assert(!"duplicate oroute recorded");
			return 0;
		}
	}

	// check if it is doomed by transitive rule filter
	for(i=0;i<routelen;i++)
	{
		if(i+1 < routelen)
		{
			if(nosplexrftc[rnums[i+1]*nrules + rnums[i]] == 0)
			{
				fprintf(stderr, "NOTE: killing rule chain containing adjacent pair `%s %s'\n", rules[rnums[i]]->name, rules[rnums[i+1]]->name);
				return 0;
			}
		}
		for(j=i+1;j<routelen;j++)
		{
			extern char	**rf_tc;
			if(!rf_tc[rnums[j]][rnums[i]])
			{
				fprintf(stderr, "NOTE: killing rule chain containing pair `%s %s'\n", rules[rnums[i]]->name, rules[rnums[j]]->name);
				return 0;
			}
		}
	}

	// otherwise, add it
	e->north_routes++;
	e->orth_routes = slab_realloc(e->orth_routes,
		sizeof(struct orth_route) * (e->north_routes-1),
		sizeof(struct orth_route) * e->north_routes);
	e->orth_routes[e->north_routes-1].len = routelen;
	e->orth_routes[e->north_routes-1].rules = rnums;
	return 1;
}

/* enumerate all the paths of backwards rules that reach 'target'.
 * 'visit' is an intermediate form, past which we've already determined
 * a path of backwards rules leading to 'target'.
 * so really we want to figure out how to get from start to 'visit'.
 */
void unfold_oroutes(struct ochart_edge *target, struct ochart_edge *visit, int routelen, int *rnums)
{
	int i;

	// don't go down garden paths
	if(routelen >= ortho_max_rules)
	{
		if(ortho_warn_max_rules)
		{
			fprintf(stderr, "WARNING: ran up against orthographemic rule chain limit (configured at %d) during orthographemic analysis\n", ortho_max_rules);
			fprintf(stderr, "WARNING:   (to suppress this warning, use `ortho-warn-on-max-rules := no.')\n");
			fprintf(stderr, "WARNING: rule chain from '%s' to '%s':\n", visit->form, target->form);
			for(i=0;i<routelen;i++)
				fprintf(stderr, "  %s", rules[rnums[i]]->name);
			fprintf(stderr, "\n");
		}
		return;
	}

	//printf("unfold '%s' [%d parents]\n", visit->form, visit->nparents);

	for(i=0;i<visit->nparents;i++)
	{
		struct ochart_parent	*d = visit->parents+i;
		if(d->edge)
		{
			// we got here by applying d->rule backwards to d->edge
			extern char	*nosplexrftc;
			if(routelen > 0 && !nosplexrftc[d->rule->rule->rnum * nrules + rnums[routelen-1]])
			{
				//fprintf(stderr, "NOTE: nosplexrftc blocked exploration of '%s' when last rule was '%s'\n",
					//d->rule->rule->name, rules[rnums[routelen-1]]->name);
				continue;
			}
			// see if d->rule is incompatible with any rules already on this o-route
			int j;
			for(j=0;j<routelen;j++)
			{
				extern char	**rf_tc;
				if(!rf_tc[d->rule->rule->rnum][rnums[j]])
				{
					// transitively closed rule filter says we can't
					// apply rnums[j] once d->rule has been applied.
					//fprintf(stderr, "RFTC blocked exploration of route containing pair '%s(%s)'.\n",
						//rules[rnums[j]]->name, d->rule->rule->name);
					break;
				}
				if(rnums[j] == d->rule->rule->rnum)
				{
					// this rule is already on the agenda.
					// that is legal for certain cases,
					// like multiple punctuation marks: "I won!!!!!"
					// but it is an infinite loop for certain other cases,
					// such as a rule that happens to have no orthographemic effect
					// for certain inputs, like the past tense of "let".
					if(!strcmp(visit->form, d->edge->form))
					{
						//fprintf(stderr, "blocking duplicate rule application with no spelling change.\n");
						break;
					}
				}
			}
			if(j<routelen) continue;
			// recurse and see about ways to get to d->edge from the starting form.
			assert(d->rule != NULL);
			int		*mnums = longer_rule_list_rev(routelen, rnums, d->rule);
			unfold_oroutes(target, d->edge, routelen+1, mnums);
		}
		else
		{
			// we're at the starting form;
			// record the complete route to get here.
			record_oroute(target, routelen, rnums);
		}
	}
}

/* record that you can reach the form 'daughter' from the form 'parent' by applying 'rule' backwards */
void add_oparent(struct ochart_edge *daughter, struct ochart_edge *parent, struct orule *rule)
{
	int i;
	for(i=0;i<daughter->nparents;i++)
		if(daughter->parents[i].edge==parent && daughter->parents[i].rule == rule)return;
	//if(parent)printf("add %p [`%s' %s] parent of %p `%s'\n", parent, parent->form, rule->rule->name, daughter, daughter->form);
	daughter->nparents++;
	daughter->parents = slab_realloc(daughter->parents,
				sizeof(struct ochart_parent) * (daughter->nparents-1),
				sizeof(struct ochart_parent) * daughter->nparents);
	daughter->parents[daughter->nparents-1].edge = parent;
	daughter->parents[daughter->nparents-1].rule = rule;
}

/* main orthographemic exploration system
 *   on entry, we got to 'form' by applying 'rule' backwards to 'parent'
 *   our task is to flush out the chart by producing all other forms
 *   reachable from 'form' by applying more o-rules backwards
 *
 * note: we work in wchar_t's much of the time...
 * note: 'parent' and 'rule' are either both NULL or both not NULL
 */
void	add_ochart_edge(struct ochart_edge	*parent, struct ochart *chart, char *form, struct orule *rule)
{
	int	i;
	struct ochart_edge *edge = 0;

	assert( (parent==NULL && rule==NULL) || (parent!=NULL && rule!=NULL) );

	// check for a compatible edge already in the chart
	for(i=0;i<chart->nedges;i++)
		if(!strcmp(chart->edges[i]->form, form))
		{
			// pack into this edge; the forms possible from here have already been explored.
			add_oparent(chart->edges[i], parent, rule);
			return;
		}

	wchar_t	*wform = towide(form);
	int forml = wcslen(wform);

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
		int				j, k, l, r;//, *mrules = 0;
		char			stem[1024];
		struct orule	*or = orules[i];
		char	*irreg_stem = hash_find(or->irregs_form_to_stem, form);
		if(irreg_stem)
		{
			add_ochart_edge(edge, chart, irreg_stem, or);
			// NOTE: we do NOT want to cut the search here, actually.
			// in the case of FORWARD morphology,
			// we would want to; i.e. an irregular spelling supresses regular rules
			// but for BACKWARDS morphology,
			// a given surface form could in theory be both the irregular spelling
			// of one base form AND the regular spelling of a DIFFERENT base form.
			// continue;
		}

		for(j=or->nsub-1;j>=0;j--)
		{
			struct orule_sub *sub = or->sub+j;
			if(!invert_ipat(sub->pat, sub->patl, sub->rep, sub->repl, wform, forml, stem, or->type))
			{
				// under regular morphology,
				// 'stem' would inflect to 'form'.
				// but, if there's an irregs.tab statement saying something different happens,
				// then don't go down this route.
				char	*irreg_form = hash_find(or->irregs_stem_to_form, stem);
				if(irreg_form)
				{
					//fprintf(stderr, "hah! we avoided being tricked; `%s' doesn't disinflect to `%s' under `%s', since that would go to `%s'\n", form, stem, or->rule->name, check?check:"(null)");
					continue;
				}

				//fprintf(stderr, "form `%S' = stem `%s' under rule `%s'[%d], i.e. `%S' replaced by `%S'\n",
				//		wform, stem, or->rule->name, j, sub->pat, sub->rep);
				add_ochart_edge(edge, chart, stem, or);
			}
		}
	}
	free(wform);
	return;
}


char	*lgroups[256];

void	set_lgroup(int	group, wchar_t	*letters)
{
	//printf("letter group %c = {%S}\n", group, letters);
	if(lgroups[group])
	{
		fprintf(stderr, "morpho: redefining letter group `%c' not allowed\n", group);
		exit(-1);
	}
	lgroups[group] = calloc(256, sizeof(char));
	while(*letters)	// XXX taking the low 8 bits of a wchar_t is *not* the right way of storing the letter set :-P
		lgroups[group][255 & ((int)*letters++)] = 1;
}

/*wchar_t	*wcsdup(wchar_t	*str)
{
	int len = wcslen(str);
	wchar_t	*w = malloc((len+1)*sizeof(wchar_t));
	return memcpy(w, str, (len+1)*sizeof(wchar_t));
}*/

wchar_t	*wcsdup_esc(wchar_t	*str)
{
	int len = wcslen(str);
	wchar_t	*w = malloc((len+1)*sizeof(wchar_t)), *p, *r = w;
	for(p=str;*p;p++)
		if(*p=='\\' && p[1])*r++ = *++p;
		else *r++ = *p;
	*r = 0;
	return w;
}

wchar_t	*trim_wcs(wchar_t	*str)
{
	int len;
	while(*str == ' ' || *str=='\t')++str;
	len = wcslen(str);
	while(len>0 && (str[len-1]==' ' || str[len-1]=='\t'))len--;
	str[len] = 0;
	return str;
}

void	setup_morpho(char	*line)
{
	char	*end;
	int	letter, over;

	line = trim_string(line);
	end = line + strlen(line)-1;
	if(*end != ')' || *line != '(')
	{
		fprintf(stderr, "morpho: setup command `%s' malformed\n", line);
		exit(-1);
	}
	*end = 0;

	end = line+1;
	while(*end && *end!=' ' && *end!='\t' && *end!='(')end++;
	over = *end;
	if(*end)*end=0;
	if(strcmp(line, "(letter-set"))
	{
		fprintf(stderr, "morpho: setup command `%s' not supported\n", line);
		exit(-1);
	}
	*end = over;

	wchar_t	*wide = trim_wcs(towide(end)), *wend;

	if(*wide != '(' || wide[1] != '!')
	{
		synt:
		fprintf(stderr, "morpho: letter-set syntax is (!x abcd...)\n");
		exit(-1);
	}
	wend = wide + wcslen(wide)-1;
	if(*wend != ')')goto synt;
	*wend = 0;
	wide = wide+2;//trim_wcs(wide+2);

	if(*wide < 0 || *wide > 255)
	{
		fprintf(stderr, "morpho: letter-set name must be ASCII\n");
		exit(-1);
	}
	letter = *wide;
	if(wide[1] != ' ' && wide[1] != '\t')goto synt;
	set_lgroup(letter, trim_wcs(wcsdup_esc(wide+2)));
}

int	appropriate_letter(int	let, int	group)
{
	let = ((unsigned int)let) & 255;	// XXX for now we're only looking about 8 bits of wchar_t for letter groups
	if(group>255 || group<0)return 0;
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
	wchar_t	*wstr = towide(str);
	char	*res=0;
	int		i, strl = wcslen(wstr);

	//printf("inflect '%S' using '%s'\n", wstr, or->rule->name);

	res = hash_find(or->irregs_stem_to_form, str);
	if(!res)
	{
		int slen = strlen(str)+10;
		if(slen%4)slen = (slen&~3)+4;
		res = slab_alloc(slen * MB_LEN_MAX);
		strcpy(res, str);
		for(i=or->nsub-1;i>=0;i--)
		{
			struct orule_sub *sub = or->sub+i;
			if(!invert_ipat(sub->rep, sub->repl, sub->pat, sub->patl, wstr, strl, res, or->type))
			{
				//printf("applied (%S %S)\n", sub->pat, sub->rep);
				break;
			}
		}
		if(i<0)res=NULL;
		if(!or->nsub)res = freeze_string(str);
	}
	free(wstr);
	//printf("inflected[%s] `%s' to `%s'\n", or->rule->name, str, res);
	return res;
}

int	invert_ipat_prefix(wchar_t	*pat, int patl, wchar_t	*rep, int repl, wchar_t	*form, int forml, wchar_t	*stem)
{
	int	let, lclear = 0;
	int	ri = 0, fi = 0;
	wchar_t	*rstem = stem, *rpat = pat, *rrep = rep, letters[256];

	if(repl==1 && rep[0]=='*')ri++;	// trivially match
	else do
	{
		let = form[fi++];
		//printf("now to match `%c'; [%d %d]\n", let, len, len2);
		if(ri+1<repl && rep[ri]=='!')
		{
			if(!lclear){bzero(letters, 256*sizeof(wchar_t));lclear=1;}
			if(appropriate_letter(let, rep[++ri]))
			{
				assert((int)rep[ri] < 256);
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
		memcpy(stem, form+fi, sizeof(wchar_t)*(forml-fi+1));
		//printf(" form `%S' -> stem `%S' under pat `%S' rep `%S'\n", form, rstem, rpat, rrep);
		return 0;
	}
	else return -1;
}

int	invert_ipat_suffix(wchar_t	*pat, int patl, wchar_t	*rep, int repl, wchar_t	*form, int forml, wchar_t	*stem)
{
	int	let, lclear = 0;
	int	len = forml-1, len2 = repl-1;
	wchar_t	*rstem = stem, *rpat = pat, *rrep = rep, letters[256];

	if(repl==1 && rep[0]=='*')len2--;	// trivially match
	else do
	{
		let = form[len--];
		//printf("now to match `%c'; [%d %d]\n", let, len, len2);
		if(len2>0 && rep[len2-1]=='!')
		{
			if(!lclear){bzero(letters, 256*sizeof(wchar_t));lclear=1;}
			if(appropriate_letter(let, rep[len2]))
			{
				//printf("`%c' is appropriate for `%c'\n", let, rep[len2]);
				assert((int)rep[len2] < 256);
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
		wcsncpy(stem, form, len+1);
		stem += len+1;
		if(*pat!='*') while(*pat)
		{
			if(*pat != '!' || !pat[1])
				*stem++ = *pat++;
			else
			{
				assert((int)pat[1] < 256);
				*stem++ = letters[(int)pat[1]];
				pat+=2;
			}
		}
		*stem++ = 0;
		//fprintf(stderr, " form `%ls' -> stem `%ls' under pat `%ls' rep `%ls' [len %d len2 %d]\n", form, rstem, rpat, rrep, len, len2);
		return 0;
	}
	else return -1;
}

static int	invert_ipat(wchar_t	*pat, int patl, wchar_t	*rep, int repl, wchar_t	*form, int forml, char	*stem, int type)
{
	wchar_t	wstem[1024];
	int		res;

	if(type == or_suffix)
		res = invert_ipat_suffix(pat, patl, rep, repl, form, forml, wstem);
	else res = invert_ipat_prefix(pat, patl, rep, repl, form, forml, wstem);

	if(!res)
	{
		if(-1 == wcstombs(stem, wstem, 1024))
		{
			perror("multibyte encoding failed");
			exit(-1);
		}
	}

	return res;
}

void	setup_orule(struct orule	*or, char	*def)
{
	char	*sp, *type;

	or->nsub = 0;
	or->sub = 0;
	or->type = or_suffix;

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

	wchar_t	*wdef = towide(def), *next, *wsp;

	while(*wdef == '(')
	{
		struct orule_sub *sub;
		wdef++;
		for(wsp=wdef;*wsp && (*wsp!=')' || wsp[-1]=='\\');wsp++) {}
		if(!*wsp)
		{
			fprintf(stderr, "morpho: subrule `%S' is not closed\n", wdef);
			exit(-1);
		}
		*wsp = 0;
		next = wsp+1;
		for(wsp=wdef;*wsp && ((*wsp!=' ' && *wsp!='\t') || wsp[-1]=='\\');wsp++) {}
		if(!*wsp)
		{
			fprintf(stderr, "morpho: subrule `%S' has no RHS\n", wdef);
			exit(-1);
		}
		*wsp = 0;
		wsp = trim_wcs(wsp+1);
		//printf("subrule lhs `%S', rhs `%S'\n", wdef, wsp);
		or->nsub++;
		or->sub = realloc(or->sub, sizeof(struct orule_sub)*or->nsub);
		sub = or->sub + or->nsub-1;
		sub->pat = wcsdup_esc(wdef);
		sub->patl = wcslen(sub->pat);
		sub->rep = wcsdup_esc(wsp);
		sub->repl = wcslen(sub->rep);

		wdef = trim_wcs(next);
	}
	if(*wdef)
	{
		fprintf(stderr, "morpho: syntax for orthographemic patterns is %%type (pat rep) (pat rep) ...\n");
		fprintf(stderr, "   trailing garbage `%S' after subrules\n", wdef);
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
	or->irregs_stem_to_form = hash_new("stem_to_form");
	or->irregs_form_to_stem = hash_new("form_to_stem");
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

struct orule	*make_identity_orule_for_rule(struct rule	*r)
{
	add_orule(r, NULL);
	return orules[norules-1];
}

int	load_irregulars(char	*path)
{
	FILE	*f = fopen(path, "r");
	char	line[1024], copy[1024];
	char	*form = NULL, *rule = NULL, *stem = NULL;
	struct orule	*or;

	char	*rule_suffix = get_conf("irregular-form-rule-suffix");

	if(!f) { perror(path); return 0; }
	while(fgets(line, 1024, f))
	{
		if(!*line)break;
		if(*line=='\n')continue;
		line[strlen(line)-1] = 0;

		if(!*line)continue;
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
		if(rule_suffix)
			asprintf(&rule, "%s%s", rule, rule_suffix);
		or = lookup_orule(rule);
		if(!or)
		{
			struct rule	*r = lookup_rule(rule);
			if(!r)
			{
				fprintf(stderr, "orth: warning -- irregular for nonextant orule `%s'\n", rule);
				continue;
			}
			else or = make_identity_orule_for_rule(r);
		}
		//printf("add form `%s', stem `%s' TO rule: %s\n", form, stem, or->rule->name);
		stem = strdup(stem);
		form = strdup(form);
		// only record the first match in each direction for each (rule,string) pair.
		if(!hash_find(or->irregs_stem_to_form, stem))
			hash_add(or->irregs_stem_to_form, stem, form);
		if(!hash_find(or->irregs_form_to_stem, form))
			hash_add(or->irregs_form_to_stem, form, stem);
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
	return 0;
}

load_all_irregulars()
{
	int	hits = 0;
	int callback(char	*path)
	{
		extern char	*confdir;
		char	*mypath = malloc(strlen(confdir) + strlen(path) + 8);
		sprintf(mypath, "%s/%s", confdir, path);
		//printf("real path to irregulars file: %s\n", mypath);
		show_task_name("reading irregular forms");
		printf("from %s\n", path);
		if(load_irregulars(mypath))
		{
			fprintf(stderr, "inflr: unable to load irregulars table `%s'.\n", path);
			exit(-1);
		}
		free(mypath);
		hits++;
		return 0;
	}
	iterate_conf_list("irregular-forms", callback);
	if(!hits) callback("../irregs.tab");
	return 0;
}

struct hash	*freeze_string_hash(struct hash	*h)
{
	struct hash_bucket *walk;
	int	i;
	for(i=0;i<h->size;i++)
		for(walk=h->buckets[i];walk;walk=walk->next)
			walk->value = freeze_string(walk->value);
	return freeze_hash(h);
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

	orout->irregs_stem_to_form = freeze_string_hash(orin->irregs_stem_to_form);
	orout->irregs_form_to_stem = freeze_string_hash(orin->irregs_form_to_stem);

	return orout;
}
