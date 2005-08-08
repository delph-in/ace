#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<ctype.h>

int	invert_ipat_suffix(char	*pat, char	*rep, char	*form, char	*stem);
struct lexeme	*proper_noun_lex(char	*name);
struct edge	*lexical_edge(struct lexeme	*lex, int	i, int	len);

extern int	wcap[1024];

#include	"rule.h"
#include	"chart.h"
#include	"morpho.h"
#include	"tdl.h"
#include	"lexicon.h"

#define	MAX_ORTH_RULES	5

int		nirules;
struct irule	**irules;

int	add_lexemes_to_chart(int	nwords, char	**words)
{
	int	i, simple, compound;
	int	max = 0;

	for(i=0;i<nwords;i++)
	{
		simple = add_lexemes_to_chart_for_form(i, words[i], 0, 0);
		compound = add_multilexemes_to_chart(i, nwords-i, words+i);
		if(compound>max)max = compound;
		if(!simple && !max)
		{
			fprintf(stderr, "NOTE: No lexeme found for `%s'\n", words[i]);
			return -1;
		}
		if(max>0)max--;
	}
	return 0;
}

int	*longer_rule_list(int nr, int *ru, struct irule	*ir)
{
	int *r = slab_alloc(sizeof(int)*(nr+1));
	memcpy(r+1,ru,sizeof(int)*nr);
	r[0] = ir->rule->rnum;
	return r;
}

int	add_lexemes_to_chart_for_form(int	x, char	*form, int nrules, int *rnums)
{
	int	i, j, count = 0, n;
	char	stem[1024];
	struct irule	*ir;
	struct irreg	*irr;
	struct lexeme	*lex;

	for(i=0;i<nrules;i++)printf("  ");
	printf("exploring hypothesis `%s' with rule chain:", form);
	for(i=0;i<nrules;i++)printf(" %s", rules[rnums[i]]->name);
	printf("\n");

	// get any no-further-inflecting lexemes
	n = add_lexemes_to_chart_for_stem(x, form, nrules, rnums);
	count += n;
	if(!strcmp(form, "is"))return count;

	if(nrules >= MAX_ORTH_RULES)return count;

	for(i=0;i<nirules;i++)
	{
		int	*mrules = 0;

		ir = irules[i];
		if(!ir->nsub)
		{
			if(!mrules)mrules = longer_rule_list(nrules, rnums, ir);
			n = add_lexemes_to_chart_for_stem(x, form, nrules+1, mrules);
			count += n;
#ifndef	GENERATION
			if(n)add_agenda_lrule(x, ir->rule);//(epsilon_edge(ir->rule, x, 0));
#endif
		}
		else
		{
			for(j=0;j<ir->nirregs;j++)
			{
				irr = ir->irregs+j;
				if(!strcmp(form, irr->form))
				{
					//fprintf(stderr, "used irreg `%s -> %s' for rule %s\n",
					//		irr->stem, irr->form, ir->rule->name);
					if(!mrules)mrules = longer_rule_list(nrules, rnums, ir);
					n = add_lexemes_to_chart_for_form(x, irr->stem, nrules+1, mrules);
					count += n;
#ifndef	GENERATION
					if(n)add_agenda_lrule(x, ir->rule);//(epsilon_edge(ir->rule, x, 0));
#endif
					break;
				}
			}
			if(j<ir->nirregs)continue;	// if we got it with an irreg, don't try patterns
			for(j=ir->nsub-1;j>=0;j--)
			{
				if(!invert_ipat_suffix(ir->subpat[j], ir->subrep[j], form, stem))
				{
					// good guess; now check whether 'stem' actually inflects to 'form'
					/*char *check = inflect(ir, stem);
					if(!check || strcasecmp(check, form))
					{
						fprintf(stderr, "hah! we avoided being tricked; `%s' doesn't disinflect to `%s' under `%s', since that would go to `%s'\n",
							form, stem, ir->rule->name, check?check:"(null)");
						if(check)free(check);
						continue;
					}
					free(check);*/

					//fprintf(stderr, "form `%s' = stem `%s' under rule `%s'[%d], i.e. `%s' replaced by `%s'\n",
					//		form, stem, ir->rule->name, j, ir->subpat[j], ir->subrep[j]);
					if(!mrules)mrules = longer_rule_list(nrules, rnums, ir);
					n = add_lexemes_to_chart_for_form(x, stem, nrules+1, mrules);
					count += n;
					if(n)
					{
#ifndef	GENERATION
						add_agenda_lrule(x, ir->rule);//(epsilon_edge(ir->rule, x, 0));
#endif
						//break;
					}
				}
			}
		}
	}

	if(nrules==0 && !count && wcap[x]/* && x!=0*/)	// unknown uppercase word not at start of sentence; proper noun?
	{
		sprintf(stem, "\"%s\"", form);
		lex = proper_noun_lex(stem);
		add_agenda(lexical_edge(lex, x, 1));
		allow_inflr(x, lex, 0, 0);
		count++;
		for(i=0;i<nirules;i++)
			if(irules[i]->nsub==0)
			{
#ifndef	GENERATION
				add_agenda_lrule(x, irules[i]->rule);//(epsilon_edge(irules[i]->rule, x, 0));
#endif
				allow_inflr(x, lex, 1, longer_rule_list(0,0, irules[i]));
				count++;
			}
		return 1;
	}

	//if(count)printf("** found %d lexeme/rules for `%s'\n", count, form);
	return count;
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

char	*inflect(struct irule	*ir, char	*str)
{
	char	*res=0;
	int		i;

	for(i=0;i<ir->nirregs;i++)
		if(!strcmp(str, ir->irregs[i].stem))
			{res = strdup(ir->irregs[i].form);break;}
	if(!res)
	{
		res = malloc(strlen(str) + 10);
		strcpy(res, str);
		for(i=ir->nsub-1;i>=0;i--)
			if(!invert_ipat_suffix(ir->subrep[i], ir->subpat[i], str, res))
			{
				//printf("applied (%s %s)\n", ir->subpat[i], ir->subrep[i]);
				break;
			}
	}
	//printf("inflected[%s] `%s' to `%s'\n", ir->rule->name, str, res);
	return res;
}

int	invert_ipat_suffix(char	*pat, char	*rep, char	*form, char	*stem)
{
	int	let;
	int	len = strlen(form)-1, len2 = strlen(rep)-1;
	char	*rstem = stem, *rpat = pat, *rrep = rep, letters[256];

	bzero(letters, 256);
	if(len2==0 && rep[len2]=='*' && len>=0)len2--;	// trivially match
	else do
	{
		let = form[len--];
		//printf("now to match `%c'; [%d %d]\n", let, len, len2);
		if(len2>0 && rep[len2-1]=='!')
		{
			if(appropriate_letter(let, rep[len2]))
			{
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
		// success!
		strncpy(stem, form, len+1);
		stem += len+1;
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
		*stem++ = 0;
		//printf(" form `%s' -> stem `%s' under pat `%s' rep `%s' [len %d len2 %d]\n", form, rstem, rpat, rrep, len, len2);
		return 0;
	}
	else return -1;
}

void	setup_irule(struct irule	*ir, char	*def)
{
	char	*sp, *type, *next;

	ir->nsub = 0;
	ir->subpat = 0;
	ir->subrep = 0;

	if(!def)return;
	for(sp=def;*sp && *sp!=' ' && *sp!='\t';sp++) {}
	if(!*sp)
	{
		fprintf(stderr, "morpho: malformed inflr pattern `%s'\n", def);
		exit(-1);
	}

	*sp = 0;
	type = trim_string(def);
	def = trim_string(sp+1);
	if(strcmp(type, "suffix"))
	{
		fprintf(stderr, "morpho: only `suffix' inflr patterns are supported (not `%s')\n", type);
		exit(-1);
	}

	while(*def == '(')
	{
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
		ir->nsub++;
		ir->subpat = realloc(ir->subpat, sizeof(char*)*ir->nsub);
		ir->subrep = realloc(ir->subrep, sizeof(char*)*ir->nsub);
		ir->subpat[ir->nsub-1] = strdup(def);
		ir->subrep[ir->nsub-1] = strdup(sp);

		def = trim_string(next);
	}
	if(*def)
	{
		fprintf(stderr, "morpho: syntax for inflr patterns is %%suffix (pat rep) (pat rep) ...\n");
		fprintf(stderr, "   trailing garbage `%s' after subrules\n", def);
		exit(-1);
	}
}

void	add_inflr(struct rule	*rule, char	*def)
{
	struct irule	*ir = calloc(sizeof(struct irule), 1);

	nirules++;
	irules = realloc(irules, sizeof(struct irule*) * nirules);
	irules[nirules-1] = ir;

	ir->rule = rule;
	ir->nirregs = 0;
	ir->irregs = 0;
	rule->inflr = ir;
	setup_irule(ir, def);
	//printf("rule `%s' has %d inflection patterns\n", rule->name, ir->nsub);
}


/*int	study_inflr(char	*name, struct dg	*dg)
{
	extern int	study_lrule(char	*name, struct dg	*dg);
	extern char	*g_inflr_def;
	extern struct rule	**rules;
	extern int		nrules;

	if(study_lrule(name, dg))return -1;
	if(g_inflr_def)add_inflr(rules[nrules-1], g_inflr_def);
	return 0;
}*/

struct irule	*lookup_irule(char	*rule)
{
	int	i;

	for(i=0;i<nirules;i++)
		if(!strcasecmp(rule, irules[i]->rule->name))
			return irules[i];
	return 0;
}

int	load_irregulars(char	*path)
{
	FILE	*f = fopen(path, "r");
	char	line[1024], copy[1024];
	char	*form =0 , *rule =0 , *stem =0 ;
	struct irule	*ir;

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
			fprintf(stderr, "inflr: invalid irregular form `%s'\n", copy);
			return -1;
		}
		sprintf(copy, "%s_infl_rule", rule);
		ir = lookup_irule(copy);
		if(!ir)
		{
			//fprintf(stderr, "inflr: warning -- irregular for nonextant irule `%s'\n", copy);
			continue;
		}
		//printf("add form `%s', stem `%s' TO rule: %s\n", form, stem, ir->rule->name);
		ir->nirregs++;
		ir->irregs = realloc(ir->irregs, sizeof(struct irreg)*ir->nirregs);
		ir->irregs[ir->nirregs-1].stem = strdup(stem);
		ir->irregs[ir->nirregs-1].form = strdup(form);
	}
	fclose(f);
	return 0;
}

int	load_inflr()
{
	extern int	study_lrule(char	*name, struct dg	*dg);

#ifndef	ERG
	if(iterate_tdl_dgs("../smallgrammars/esslli2k-grammar9/lrules.tdl", study_lrule))
	{
		fprintf(stderr, "rules: unable to load lexrinst.tdl, bailing out.\n");
		exit(-1);
	}
#endif
#ifdef	ERG
	//if(iterate_tdl_dgs("../erg/inflr.tdl", study_lrule) || iterate_tdl_dgs("../erg/lexrinst.tdl", study_lrule))
	if(process_tdl_dgs_status("lex-rule", study_lrule))
#else
	if(iterate_tdl_dgs("../smallgrammars/esslli2k-grammar9/inflr.tdl", study_lrule))
#endif
	{
		fprintf(stderr, "inflr: unable to load, bailing out.\n");
		exit(-1);
	}

#ifdef	ERG
	if(load_irregulars("../erg/irregs.tab"))
	{
		fprintf(stderr, "inflr: unable to load irregulars table.\n");
		exit(-1);
	}
#endif
	return 0;
}
