#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>

#include	"tdl.h"
#include	"dag.h"
#include	"rule.h"
#include	"chart.h"

struct rule	**rules;
int		nrules;

char	*rule_filter;
int	*nactive, *npassive;

rule_stats()
{
	int	i;
	int	nact = 0, npass = 0;

	if(!nactive || !npassive)return;
	for(i=0;i<nrules;i++)
	{
		//printf("%4d %d %s\n", nactive[i], i, rules[i]->name);
		nact+=nactive[i];
		npass+=npassive[i];
	}
	printf("%d active / %d passive edges total\n", nact, npass);
}

int	rule_is_rtl(struct rule	*r)
{
	struct dg	*hd = dg_hop(r->dg, lookup_fname("HEAD-DTR"));
	struct dg	*args, *afirst;

	if(!hd)return 0;
	args = dg_hop(r->dg, lookup_fname("ARGS"));
	if(!args)return 0;
	afirst = dg_hop(args, lookup_fname("FIRST"));
	if(!afirst)return 0;

	if(afirst!=hd)
	{
		//fprintf(stderr, "rule `%s' is right-to-left\n", r->name);
		return 1;
	}
	else
	{
		//fprintf(stderr, "rule `%s' is left-to-right\n", r->name);
		return 0;
	}
}

setup_rule_filter()
{
	int	i, j, k;
	struct rule	*m, *d;
	int	yes = 0, count = 0;
	char	byte;

	fprintf(stderr, "building rule filter...\n");
	rule_filter = calloc(sizeof(char), nrules*nrules);

	for(i=0;i<nrules;i++)
	{
		m = rules[i];
		if(m->ndaughters>8)
		{
			fprintf(stderr, "ERROR: rule `%s' takes %d daughters!\n", m->name);
			exit(-1);
		}
		for(j=0;j<nrules;j++)
		{
			d = rules[j];
			byte = 0;
			for(k=0;k<m->ndaughters;k++)
				if(unify_dg(d->dg, m->dg, k))
				{
					byte |= (1<<k);
					yes++;
				}
			count += m->ndaughters;
			rule_filter[i*nrules + j] = byte;
		}
	}
	fprintf(stderr, "rule filter done (%.1f%%)\n", 100*(float)yes/count);
}

struct edge	*epsilon_edge(struct rule	*r, int	x, int	rtl)
{
	struct edge	*e = slab_alloc(sizeof(struct edge));

	bzero(e, sizeof(struct edge));
	e->from = e->to = x;
	e->id = next_eid();
	e->rule = r;
	e->need = r->ndaughters;
	e->have = 0;
	e->daughters = 0;
	e->dg = r->dg;
	//e->dg = copy_dg(e->dg);
	if(!e->dg)
	{
		fprintf(stderr, "epsilon_edge: bad frozen grammar! rule `%s' has no dg!\n", r->name);
		exit(-1);
	}
	e->lex = 0;
	e->rtl = rtl;

	return e;
}

load_rules_conf()
{
	FILE	*f = fopen("rules.conf", "r");
	char	dir;
	int	save, i;
	char	name[1024];

	if(!f)
	{
		perror("rules.conf");
		fprintf(stderr, "resorting to left-to-right parsing!\n");
		return -1;
	}

	while(!feof(f))
	{
		if(fscanf(f, "%c %d %s\n", &dir, &save, name) != 3)break;
		//printf("dir %c save %d name %s\n", dir, save, name);
		if(dir=='r')
		{
			//printf("rule `%s' should go RTL\n", name);
			for(i=0;i<nrules;i++)
				if(!strcmp(rules[i]->name, name))break;
			if(i==nrules)
				fprintf(stderr, "rules.conf: `%s' is RTL, but no such rule!\n", name);
			else rules[i]->rtl = 1;
		}
	}

	fclose(f);
}

void	agenda_rules(int	count)
{
	int	i, j, rtl;

	if(!nactive)
	{
		nactive = calloc(sizeof(int),nrules);
		npassive = calloc(sizeof(int),nrules);
	}

	for(i=0;i<count;i++)
		for(j=0;j<nrules;j++)
		{
			rtl = rules[j]->rtl;
			if(!rules[j]->inflr)	// i-rules are added separately
			{
				if(	(!rtl && i+rules[j]->ndaughters <= count) ||
					(rtl && i-rules[j]->ndaughters >= 0)	)
					add_agenda(epsilon_edge(rules[j], i, rtl));
			}
		}
}

void	add_rule(char	*name, struct dg	*dg, int	num)
{
	struct rule	*r = calloc(sizeof(struct rule), 1);

	nrules++;
	rules = realloc(rules, sizeof(struct rule*)*nrules);
	rules[nrules-1] = r;

	r->name = strdup(name);
	r->dg = dg;
	r->ndaughters = num;
	r->inflr = 0;
	r->lexical = 0;
	r->rnum = nrules-1;
	r->rtl = rule_is_rtl(r);

	/*printf("rule %s [%d]:  ", name, num);
	print_dg(dg);
	printf("\n");*/
}

int	study_rule(char	*name, struct dg	*dg)
{
	int	i, num = 0;
	struct dg	*p;

	p = dg_hop(dg, 0);	// ARGS
	while(p)
	{
		if(dg_hop(p, 1))num++;
		p = dg_hop(p, 2);	// REST
	}
	add_rule(name, dg, num);
	rules[nrules-1]->lexical = 1;
	return 0;
}

print_rule(char	*name)
{
	struct rule	*rule;
	int	i;

	for(i=0;i<nrules;i++)if(!strcasecmp(rules[i]->name, name))break;
	if(i>=nrules)return -1;

	printf("rule `%s':\n", name);
	print_dg(rules[i]->dg);printf("\n");
}

int	study_lrule(char	*name, struct dg	*dg)
{
	int	i, num = 0;
	struct dg	*p;

	p = dg_hop(dg, 0);	// ARGS
	while(p)
	{
		if(dg_hop(p, 1))num++;
		p = dg_hop(p, 2);	// REST
	}
	add_rule(name, dg, num);
	rules[nrules-1]->lexical = 1;
	return 0;
}

int	load_rules()
{
#ifdef	ERG
	if(iterate_tdl_dgs("../erg/constructions.tdl", study_rule))
#else
	if(iterate_tdl_dgs("../smallgrammars/esslli2k-grammar9/rules.tdl", study_rule))
#endif
	{
		fprintf(stderr, "rules: unable to load constructions.tdl, bailing out.\n");
		exit(-1);
	}
#ifdef	ERG
	if(iterate_tdl_dgs("../erg/lexrinst.tdl", study_lrule))
#else
	if(iterate_tdl_dgs("../smallgrammars/esslli2k-grammar9/lrules.tdl", study_lrule))
#endif
	{
		fprintf(stderr, "rules: unable to load lexrinst.tdl, bailing out.\n");
		exit(-1);
	}
	return 0;
}
