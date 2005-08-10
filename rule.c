#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>

#include	"tdl.h"
#include	"dag.h"
#include	"type.h"
#include	"rule.h"
#include	"chart.h"

struct rule	**rules;
int		nrules;

char	*rule_filter;
int	*nhactive, *nactive, *npassive, *nrecomb, *nused, *nptries, *natries;
int	used_total = 0, pused_total = 0;
extern int nfinal, trace;

extern unsigned int generation;

int	rprio[1000], rnohyper[1000];

int	lexrulefilt_l(int	x, int	rule);
int	lexrulefilt_f(int	x, int	rule);

struct qc	**ruleqc;

void	rule_stats()
{
	int	i;
	int	nact = 0, npass = 0, nrec = 0, nhact = 0, natry=0, nptry=0;

	if(!nactive || !npassive || !nrecomb || !nhactive || !nused)return;
	for(i=0;i<nrules;i++)
	{
		//if(nactive[i])printf("%4d %.3f %d %s\n", nactive[i], (float)nrecomb[i] / nactive[i], i, rules[i]->name);
		//printf("%32s %6d %6d %6d %.1f\n", rules[i]->name, nused[i], npassive[i], nactive[i], 100*(float)nused[i]/npassive[i]);
		nact+=nactive[i];
		npass+=npassive[i];
		nrec+=nrecomb[i];
		nhact+=nhactive[i];
		natry+=natries[i];
		nptry+=nptries[i];
	}
	//nptry += nptries[nrules];
	fprintf(stderr, "NOTE: %d active (%d hyper) / %d passive edges total (%d actives used, %d passives used, %d reformed) ; %d final edges\n", nact, nhact, npass, used_total, pused_total, nrec, nfinal);
	fprintf(stderr, "NOTE: %d a-tries = %.1f%%, %d p-tries = %.1f%%\n", natry, 100*(float)natry / nact, nptry, 100*(float)nptry / npass);
	free(nactive);
	free(npassive);
	free(nhactive);
	free(nrecomb);
	free(nused);
	free(natries);
	free(nptries);
}

int	rule_is_rtl(struct rule	*r)
{
	//struct dg	*hd = dg_hop(r->dg, lookup_fname("HEAD-DTR"));
	struct type	*plus = lookup_type("+");
	struct dg	*args, *asec, *afkey;

	if(!plus)return 0;
	//if(!hd)return 0;
	args = dg_hop(r->dg, lookup_fname("ARGS"));
	if(!args)return 0;
	args = dg_hop(args, lookup_fname("REST"));
	if(!args)return 0;
	asec = dg_hop(args, lookup_fname("FIRST"));
	if(!asec)return 0;
	afkey = dg_hop(asec, lookup_fname("KEY-ARG"));
	// ARGS REST FIRST KEY-ARG

	if(afkey && afkey->xtype == plus)
	//if(asec==hd)
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

void	setup_rule_filter()
{
	int	i, j, k;
	struct rule	*m, *d;
	struct dg	*mdg;
	int	yes = 0, count = 0, res;
	char	byte;

	fprintf(stderr, "rule filter... "); fflush(stderr);
	rule_filter = calloc(sizeof(char), nrules*nrules);

	for(i=0;i<nrules;i++)
	{
		m = rules[i];
		if(m->ndaughters>8)
		{
			fprintf(stderr, "ERROR: rule `%s' takes %d daughters!\n", m->name, m->ndaughters);
			exit(-1);
		}
		mdg = copy_dg(m->dg);	// necessary for the case when we try to unify a rule into itself!
		for(j=0;j<nrules;j++)
		{
			d = rules[j];
			byte = 0;
			for(k=0;k<m->ndaughters;k++)
			{
				res = unify_dg_tmp(d->dg, mdg, k);
				forget_tmp();
				//if(unify_dg(d->dg, mdg, k))
				if(res==0)
				{
					byte |= (1<<k);
					yes++;
				}
			}
			count += m->ndaughters;
			rule_filter[i*nrules + j] = byte;
		}
	}
	fprintf(stderr, " %.1f%% blocked\n", 100*(float)(count-yes)/count);
	lex_filter_rules();
}

struct edge	*epsilon_edge(struct rule	*r, int	x, int	rtl)
{
	struct edge	*e = slab_alloc(sizeof(struct edge));
	extern int	__qc_use_heap;

	bzero(e, sizeof(struct edge));
	e->age = 0;
	e->from = e->to = x;
	e->id = next_eid();
	e->rule = r;
	e->need = r->ndaughters;
	//printf("add rule `%s' as edge #%d\n", r->name, e->id);
	e->dg = r->dg;
	if(!e->dg)
	{
		fprintf(stderr, "epsilon_edge: bad frozen grammar! rule `%s' has no dg!\n", r->name);
		exit(-1);
	}
	//e->dg = copy_dg(e->dg);
	e->rtl = rtl;

	if(!ruleqc)ruleqc = calloc(sizeof(struct qc*), nrules);

	if(!ruleqc[r->rnum])
	{
		__qc_use_heap = 1;
		if(rtl)ruleqc[r->rnum] = extract_qc_arg(r->dg, r->ndaughters-1);
		else ruleqc[r->rnum] = extract_qc_arg(r->dg, 0);
		__qc_use_heap = 0;
	}
	e->qc = ruleqc[r->rnum];
	e->prio = rprio[r->rnum];
	if(r->lexical && e->prio==0)e->prio = 9;

	if(trace>1)printf("epsilon [%d] of %s is #%d prio %d\n", x, r->name, e->id, e->prio);

	return e;
}

int	load_rules_conf()
{
	FILE	*f = fopen("rules.conf", "r");
	char	dir, hyper;
	int	save, i, prio;
	char	name[1024];

	if(!f)
	{
		char fname[1024];
		sprintf(fname, "%s/.rules.conf", getenv("HOME"));
		f = fopen(fname, "r");
	}
	if(!f)
	{
		perror("rules.conf");
		fprintf(stderr, "resorting to left-to-right parsing!\n");
		return -1;
	}

	while(!feof(f))
	{
		prio = 0;
		if(fscanf(f, "%c %c %d %s %d\n", &dir, &hyper, &save, name, &prio) < 3)break;
		//printf("dir %c save %d name %s\n", dir, save, name);
		for(i=0;i<nrules;i++)
			if(!strcmp(rules[i]->name, name))break;
		if(i==nrules)
		{
			fprintf(stderr, "rules.conf: `%s': no such rule!\n", name);
			exit(-1);
		}
		if(dir=='r')
		{
			//printf("rule `%s' should go RTL\n", name);
			//rules[i]->rtl = 1;
		}
		if(hyper=='a')rnohyper[i] = 1;
		rprio[i] = prio;
	}

//	for(i=0;i<nrules;i++)
//		printf("rule `%s' inflr %p\n", rules[i]->name, rules[i]->inflr);

	fclose(f);
	return 0;
}

int nonempty_ccont(struct dg	*dg)
{
	if(dg)dg = dg_hop(dg, lookup_fname("C-CONT"));
	if(dg)dg = dg_hop(dg, lookup_fname("RELS"));
	if(dg)dg = dg_hop(dg, lookup_fname("LIST"));
	if(dg)dg = dg_hop(dg, lookup_fname("FIRST"));
	if(dg)return 1;
	else return 0;
}

void	agenda_rules(int	count)
{
	int	i, j, rtl, nlf = 0;

#ifdef	RULE_STATS
	if(!nactive)
	{
		nactive = calloc(sizeof(int),nrules);
		nhactive = calloc(sizeof(int),nrules);
		npassive = calloc(sizeof(int),nrules);
		nrecomb = calloc(sizeof(int),nrules);
		nused = calloc(sizeof(int),nrules);
		natries = calloc(sizeof(int),nrules);
		nptries = calloc(sizeof(int),nrules+1);
	}
#endif

	for(i=0;i<count;i++)
	{
		for(j=0;j<nrules;j++)
		{
			rtl = rules[j]->rtl;
			if(!rules[j]->inflr)	// i-rules are added separately
			{
				if(	(!rtl && i+rules[j]->ndaughters <= count) ||
					(rtl && i-rules[j]->ndaughters >= 0)	)
				{
					if(!rtl && lexrulefilt_f(i, j))
					{
						//printf(" rule %s F-filtered for position %d\n", rules[j]->name, i);
						nlf++;
						continue;
					}
					if(rtl && lexrulefilt_l(i, j))
					{
						//printf(" rule %s L-filtered for position %d\n", rules[j]->name, i);
						nlf++;
						continue;
					}
					add_agenda(epsilon_edge(rules[j], i, rtl));
				}
			}
		}
	}
	//fprintf(stderr, "NOTE: lex-filtered %d epsilon-edges\n", nlf);
}

void	add_rule(char	*name, struct dg	*dg, int	num)
{
	struct rule	*r = calloc(sizeof(struct rule), 1);

#ifdef	ERG
	static int	rnamef = -1;
	struct dg	*rnamedg;

	if(rnamef==-1)rnamef = lookup_fname("RULE-NAME");
	rnamedg = dg_hop(dg, rnamef);
	if(rnamedg)
	{
		dg->xtype->dg = add_dg_hop(dg->xtype->dg, rnamef, rnamedg);
		//printf("added rule name `%s' to rule type `%s'\n", rnamedg->xtype->name, dg->xtype->name);
	}
#endif

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

	add_semantic_index(r, dg, 1);

	//printf("rule %s [%d]:  ", name, num);
	//print_dg(dg);
	//printf("\n");
}

int	study_rule(char	*name, struct dg	*dg)
{
	int	num = 0;
	struct dg	*p;

	p = dg_hop(dg, 0);	// ARGS
	while(p)
	{
		if(dg_hop(p, 1))num++;
		p = dg_hop(p, 2);	// REST
	}
	add_rule(name, dg, num);
	rules[nrules-1]->lexical = 0;
	return 0;
}

void	print_rule(char	*name)
{
	int	i;

	for(i=0;i<nrules;i++)if(!strcasecmp(rules[i]->name, name))break;
	if(i>=nrules)return;

	printf("rule `%s':\n", name);
	print_dg(rules[i]->dg);printf("\n");
}

int	study_lrule(char	*name, struct dg	*dg)
{
	int	num = 0;
	struct dg	*p;
	extern char	*g_inflr_def;

	p = dg_hop(dg, 0);	// ARGS
	while(p)
	{
		if(dg_hop(p, 1))num++;
		p = dg_hop(p, 2);	// REST
	}
	add_rule(name, dg, num);
	rules[nrules-1]->lexical = 1;

	if(g_inflr_def)add_inflr(rules[nrules-1], g_inflr_def);
	return 0;
}

int	load_rules()
{
#ifdef	ERG
	//if(iterate_tdl_dgs("../erg/constructions.tdl", study_rule))
	if(process_tdl_dgs_status("rule", study_rule))
#else
	if(iterate_tdl_dgs("../smallgrammars/esslli2k-grammar9/rules.tdl", study_rule))
#endif
	{
		fprintf(stderr, "rules: unable to load constructions.tdl, bailing out.\n");
		exit(-1);
	}
	return 0;
}

// output rule filter left daughter graph as a .dot file
void	dot_rf(char	*fname)
{
	int	i, j;
	FILE	*f = fopen(fname, "w");

	for(i=0;i<nrules;i++)
	{
		for(j=0;j<nrules;j++)
		{
			if(check_rf(rules[i], rules[j], 0))
				fprintf(f, "%s -> %s\n", rules[i]->name, rules[j]->name);
		}
	}
	fclose(f);
}

char	**rf_tc, **rl_tc;

// compute the set of rules which can appear as the first constituent of this rule, somewhere down the left edge of the subtree
char	*lex_filter_rule(struct rule	*r, int arg)
{
	char	*table;
	int	i, j, n, tot = 1;

	table = calloc(sizeof(char), nrules);
	table[r->rnum] = 1;
	do
	{
		n = 0;
		for(i=0;i<nrules;i++) if(table[i])
		{
			for(j=0;j<nrules;j++) if(!table[j] && check_rf(rules[i], rules[j], arg))
			{
				table[j] = 1;
				n++;
				tot++;
			}
		}
	} while(n);

	//printf("FIRST(%s) = %d / %d\n", r->name, tot, nrules);
	//for(i=0;i<nrules;i++) if(table[i]) printf("%s\n", rules[i]->name);

	return table;
}

char	**type_rf, **type_rl;
char	**type_rftc, **type_rltc;

char	*type_filter_rule(struct rule	*r, int arg)
{
	char	*table;
	int	i, count = 0, res;
	//struct slab_state	slab;

	table = calloc(sizeof(char), ntypes);
	for(i=0;i<ntypes;i++)
	{
		// XXX hack
		if(generation > MAX_GEN_RESET)generation = 11;
		// XXX hack

		if(!types[i]->dg)continue;
		res = unify_dg_tmp(types[i]->dg, r->dg, arg);
		forget_tmp();
		//get_slabstate(&slab);
		//if(unify_dg(types[i]->dg, r->dg, arg))
		if(res==0)
		{
			table[i] = 1;
			count++;
		}
		//set_slabstate(&slab);
	}
	//printf("FIRST-TYPE(%s) = %d / %d\n", r->name, count, ntypes);
	return table;
}

void	lex_filter_rules()
{
	int	i, j, k;

	fprintf(stderr, "building rf-transitive closure\n");
	rf_tc = malloc(sizeof(char*) * nrules);
	rl_tc = malloc(sizeof(char*) * nrules);
	for(i=0;i<nrules;i++)
	{
		rf_tc[i] = lex_filter_rule(rules[i], 0);
		rl_tc[i] = lex_filter_rule(rules[i], rules[i]->ndaughters-1);
	}

	fprintf(stderr, "building type-rule filter\n");
	type_rf = malloc(sizeof(char*) * nrules);
	type_rl = malloc(sizeof(char*) * nrules);
	for(i=0;i<nrules;i++)
	{
		type_rf[i] = type_filter_rule(rules[i], 0);
		type_rl[i] = type_filter_rule(rules[i], rules[i]->ndaughters-1);
	}

	fprintf(stderr, "composing  type-rule closured filter\n");
	type_rftc = malloc(sizeof(char*) * nrules);
	type_rltc = malloc(sizeof(char*) * nrules);
	for(i=0;i<nrules;i++)
	{
		type_rftc[i] = calloc(sizeof(char), ntypes);
		type_rltc[i] = calloc(sizeof(char), ntypes);
		for(j=0;j<ntypes;j++)
		{
			if(type_rf[i][j])type_rftc[i][j] = 1;
			else for(k=0;k<nrules;k++)
				if(rf_tc[i][k] && type_rf[k][j])
				{
					type_rftc[i][j] = 1;
					break;
				}
			if(type_rl[i][j])type_rltc[i][j] = 1;
			else for(k=0;k<nrules;k++)
				if(rl_tc[i][k] && type_rl[k][j])
				{
					type_rltc[i][j] = 1;
					break;
				}
		}
	}
}
