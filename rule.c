#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<sys/time.h>
#include	<assert.h>

#include	"tdl.h"
#include	"dag.h"
#include	"type.h"
#include	"rule.h"
#include	"chart.h"
#include	"conf.h"
#include	"freeze.h"

//#define	RULE_STATS

struct dg	*constrain_rule_rels(struct dg *dg, struct dg *rels);

struct rule	**rules;
int		nrules;

char	*rule_filter;
int	*nhactive, *nactive, *npassive, *nrecomb, *nused, *nptries, *natries;
int	used_total = 0, pused_total = 0;
extern int nfinal, trace, inhibit_pack;

extern unsigned int generation;
extern int gen_qc;

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
		if(nactive[i])printf("%4d %.3f %d %s\n", nactive[i], (float)nrecomb[i] / nactive[i], i, rules[i]->name);
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

struct edge	*epsilon_edge(struct rule	*r, int	x, int	rtl, struct dg	*rels)
{
	struct edge	*e = slab_alloc(sizeof(struct edge));
	extern int	__qc_use_heap;

	bzero(e, sizeof(struct edge));
	e->age = 0;
	e->from = e->to = x;
	e->id = next_eid();
	e->rule = r;
	e->need = r->ndaughters;
	e->dg = inhibit_pack?r->dg:((g_mode==PARSING)?r->packdg:r->gpackdg);
	if(!e->dg)
	{
		fprintf(stderr, "epsilon_edge: bad frozen grammar! rule `%s' has no dg!\n", r->name);
		exit(-1);
	}
	if(rels)
	{
		e->dg = constrain_rule_rels(r->dg, rels);
		if(!e->dg)
		{
			//printf("failed to constrain rule; "); unify_backtrace();
			return 0;
		}
	}
	e->rtl = rtl;

	if(!ruleqc)ruleqc = calloc(sizeof(struct qc*), nrules);

	if(!ruleqc[r->rnum] && !gen_qc)
	{
		__qc_use_heap = 1;
		if(rtl)ruleqc[r->rnum] = extract_qc_arg(r->dg, r->ndaughters-1);
		else ruleqc[r->rnum] = extract_qc_arg(r->dg, 0);
		__qc_use_heap = 0;
	}
	e->qc = ruleqc[r->rnum];

	if(trace>1)
	{
		printf("epsilon [%d] of %s is #%d\n", x, r->name, e->id);
		print_dg(e->dg); printf("\n\n");
	}

	return e;
}

// return a copy of 'dg' with 'rels' unified into the C-CONT RELS slot
struct dg	*constrain_rule_rels(struct dg *dg, struct dg *rels)
{
	extern long top_copies;
	extern int	upathl;
	struct slab_state	slab;
	struct dg			*target;
	get_slabstate(&slab);

	target = dg_path(dg, rule_rels_path);

#ifdef UNIFY_PATHS
	upathl = 0;
#endif
	if(0 != unify1(target, rels))
	{ fail:
		set_slabstate(&slab);
		generation++;
		return 0;
	}
	// the unification succeeded; copy out the result, applying the packing restrictor
	enable_packing(2);
	dg = copy_dg_with_comp_arcs(dg);
	enable_packing(0);
	generation++;
	if(generation==MAX_GEN)
	{
		fprintf(stderr, "ERROR: generation overflowed!\n");
		exit(-1);
	}
	if(!dg)goto fail;
	top_copies++;
	return dg;
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
			if(!rules[j]->orth)	// i-rules are added separately
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
					add_agenda(epsilon_edge(rules[j], i, rtl, 0));
				}
			}
		}
	}
	//fprintf(stderr, "NOTE: lex-filtered %d epsilon-edges\n", nlf);
}

void	install_rule_name(struct dg	*dg, char *name)
{
	static int	rnamef = -1;
	struct dg	*rnamedg;

	if(rnamef==-1)rnamef = lookup_fname("RNAME");
	rnamedg = dg_hop(dg, rnamef);
	if(!rnamedg)
	{
		dg->xtype->dg = add_dg_hop(dg->xtype->dg, rnamef, rnamedg);
		//printf("added rule name `%s' to rule type `%s'\n", rnamedg->xtype->name, dg->xtype->name);
	}
}

void	add_rule(char	*name, struct dg	*dg, int	num)
{
	struct rule	*r = calloc(sizeof(struct rule), 1);
	int			i;

	install_rule_name(dg, name);

	nrules++;
	rules = realloc(rules, sizeof(struct rule*)*nrules);
	rules[nrules-1] = r;

	r->name = strdup(name);
	r->dg = dg;
	r->packdg = restrict_copy_dg(dg, 0);
	r->gpackdg = restrict_copy_dg(dg, 1);
	r->ndaughters = num;
	r->orth = 0;
	r->lexical = 0;
	r->rnum = nrules-1;
	r->rtl = rule_is_rtl(r);

	configure_rule(r);

	//printf("rule %s [%d]:  ", name, num);
	//print_dg(dg);
	//printf("\n");
}

int	study_rule(char	*name, struct dg	*dg)
{
	int	num = 0;
	struct dg	*p;

	wellform(dg, name);

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
	extern char	*g_orth_def;

	wellform(dg, name);

	p = dg_hop(dg, 0);	// ARGS
	while(p)
	{
		if(dg_hop(p, 1))num++;
		p = dg_hop(p, 2);	// REST
	}
	add_rule(name, dg, num);
	rules[nrules-1]->lexical = 1;

	if(g_orth_def)add_orule(rules[nrules-1], g_orth_def);
	return 0;
}

int	load_rules()
{
	if(process_tdl_dgs_status("rule", study_rule))
	{
		fprintf(stderr, "rules: unable to load constructions.tdl, bailing out.\n");
		exit(-1);
	}
	return 0;
}

char	**rf_tc, **rl_tc;
char	**type_rf, **type_rl;
char	**type_rftc, **type_rltc;

char	*setup_rule_filter1(int pack)
{
	int	i, j, k;
	struct rule	*m, *d;
	struct dg	*mdg;
	int	yes = 0, count = 0, res;
	char	byte, *rule_filter;

	show_task_name("rule filter...");
	rule_filter = calloc(sizeof(char), nrules*nrules);

	for(i=0;i<nrules;i++)
	{
		m = rules[i];
		if(m->ndaughters>8)
		{
			fprintf(stderr, "ERROR: rule `%s' takes %d daughters!\n", m->name, m->ndaughters);
			exit(-1);
		}
		mdg = copy_dg(pack?( (pack==1) ? m->packdg : m->gpackdg ) : m->dg );	// necessary for the case when we try to unify a rule into itself!
		for(j=0;j<nrules;j++)
		{
			d = rules[j];
			byte = 0;
			for(k=0;k<m->ndaughters;k++)
			{
				res = unify_dg_tmp(pack?( (pack==1) ? d->packdg : d->gpackdg ) : d->dg , mdg, k);
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
	printf("%.1f%% blocked\n", 100*(float)(count-yes)/count);
	return rule_filter;
}

struct rule_filters
{
	char	*nopack;
	char	*pack;
	char	*gpack;
	char	**rftc, **rltc;
	char	**type_rf, **type_rl;
	char	**type_rftc, **type_rltc;
}	*filters;

void setup_rule_filter()
{
	filters = calloc(sizeof(*filters), 0);
	filters->nopack = setup_rule_filter1(0);
	filters->pack = setup_rule_filter1(1);
	filters->gpack = setup_rule_filter1(2);
	rule_filter = filters->nopack;	// type_rxtc is applied before packing starts
	lex_filter_rules();
}

char	**freeze_rxtc(char	**ptr, int dim)
{
	if(!ptr)return 0;
	char	**ref = slab_alloc(nrules * sizeof(char*));
	int	len;
	char	*data;
	int	i;

	len = (nrules*dim + 3) & ~3;
	data = slab_alloc(len);
	for(i=0;i<nrules;i++)
	{
		ref[i] = data;
		data += dim;
		memcpy(ref[i], ptr[i], dim);
	}
	return ref;
}

void	*freeze_rule_filter()
{
	filters = freeze_block(filters, sizeof(*filters));
	filters->nopack = freeze_block(filters->nopack, sizeof(char) * nrules*nrules);
	filters->pack = freeze_block(filters->pack, sizeof(char) * nrules*nrules);
	filters->gpack = freeze_block(filters->gpack, sizeof(char) * nrules*nrules);
	filters->rftc = freeze_rxtc(rf_tc, nrules);
	filters->rltc = freeze_rxtc(rl_tc, nrules);
	filters->type_rf = freeze_rxtc(type_rf, ntypes);
	filters->type_rl = freeze_rxtc(type_rl, ntypes);
	filters->type_rftc = freeze_rxtc(type_rftc, ntypes);
	filters->type_rltc = freeze_rxtc(type_rltc, ntypes);
	return filters;
}

void	recover_rule_filter(void *rf)
{
	filters = rf;
	rf_tc = filters->rftc;
	rl_tc = filters->rltc;
	type_rf = filters->type_rf;
	type_rl = filters->type_rl;
	type_rftc = filters->type_rftc;
	type_rltc = filters->type_rltc;
}

void	install_rf(int what)
{
	if(what==0)rule_filter = filters->nopack;
	else if(what==1)rule_filter = filters->pack;
	else if(what==2)rule_filter = filters->gpack;
	else assert(!"Not reached");
}

// compute the set of rules which can appear as the x'th constituent of this rule, somewhere down the left edge of the subtree
char	*compute_rxtc(int	rnum, int right)
{
	char	*table;
	int	i, j, n, tot = 1;

	table = calloc(sizeof(char), nrules);
	table[rnum] = 1;
	do
	{
		n = 0;
		for(i=0;i<nrules;i++) if(table[i])
		{
			for(j=0;j<nrules;j++) if(!table[j] && check_rf(rules[i], rules[j], right?rules[i]->ndaughters-1:0))
			{
				table[j] = 1;
				n++;
				tot++;
			}
		}
	} while(n);

	return table;
}

char	*type_filter_rule(struct rule	*r, int arg, struct qc **typeqc)
{
	struct qc *rqc = extract_qc_arg(r->dg, arg);
	char	*table;
	int	i, j, count = 0, res;

	table = calloc(sizeof(char), ntypes);

	// bottom up, since most types will succeed and we can check if subtypes did
	for(i=ntypes-1;i>=0;i--)
	{
		struct type *t = types[i];

		if(generation > MAX_GEN_RESET)generation = 11; // XXX hack

		if(!t->dg)continue;
		// check descendents to see if more-specific types than me have been shown compatible
		for(j=1;j<16 && j<t->ndescendents;j++)
			if(table[t->descendents[j]])
				goto yes;
		if(quickcheck(rqc, typeqc[i]) != 0)
			continue;
		res = unify_dg_tmp(t->dg, r->dg, arg);
		forget_tmp();
		if(res==0)
		{
			yes:
			table[i] = 1;
			count++;
		}
	}
	return table;
}

void	lex_filter_rules()
{
	char **type_rf_t, **type_rl_t;

	if(gen_qc)return;

	int build_rule_filter_transitive_closure()
	{
		int i;

		rf_tc = malloc(sizeof(char*) * nrules);
		rl_tc = malloc(sizeof(char*) * nrules);
		for(i=0;i<nrules;i++)
		{
			rf_tc[i] = compute_rxtc(i, 0);
			rl_tc[i] = compute_rxtc(i, 1);
		}
		return 0;
	}
	run_task("rf-transitive closure...", build_rule_filter_transitive_closure);

	int build_type_rule_filter()
	{
		int i, j;
		struct qc *tqc[ntypes];

		type_rf = malloc(sizeof(char*) * nrules);
		type_rl = malloc(sizeof(char*) * nrules);
		type_rf_t = malloc(sizeof(char*) * ntypes);
		type_rl_t = malloc(sizeof(char*) * ntypes);
		for(i=0;i<ntypes;i++)
			tqc[i] = types[i]->dg?extract_qc(types[i]->dg):0;
		for(i=0;i<nrules;i++)
		{
			type_rf[i] = type_filter_rule(rules[i], 0, tqc);
			type_rl[i] = type_filter_rule(rules[i], rules[i]->ndaughters-1, tqc);
		}
		for(i=0;i<ntypes;i++)
		{
			type_rf_t[i] = malloc(nrules);
			for(j=0;j<nrules;j++)type_rf_t[i][j] = type_rf[j][i];
			type_rl_t[i] = malloc(nrules);
			for(j=0;j<nrules;j++)type_rl_t[i][j] = type_rl[j][i];
		}
		return 0;
	}
	run_task("type-rule filter...", build_type_rule_filter);

	int compose_type_rule_closured_filter()
	{
		int	i, j, k;

		type_rftc = malloc(sizeof(char*) * nrules);
		type_rltc = malloc(sizeof(char*) * nrules);
		for(i=0;i<nrules;i++)
		{
			type_rftc[i] = calloc(sizeof(char), ntypes);
			type_rltc[i] = calloc(sizeof(char), ntypes);
			for(j=0;j<ntypes;j++)
			{
				char *fit_rule_f = type_rf_t[j];
				char *fit_rule_l = type_rl_t[j];
				char *accept_rule_f = rf_tc[i];
				char *accept_rule_l = rl_tc[i];

				if(type_rf[i][j])type_rftc[i][j] = 1;
				else for(k=0;k<nrules;k++)
					if(accept_rule_f[k] && fit_rule_f[k])
					{
						type_rftc[i][j] = 1;
						break;
					}
				if(type_rl[i][j])type_rltc[i][j] = 1;
				else for(k=0;k<nrules;k++)
					if(accept_rule_l[k] && fit_rule_l[k])
					{
						type_rltc[i][j] = 1;
						break;
					}
			}
		}
		return 0;
	}
	run_task("type-rule-closed filter...", compose_type_rule_closured_filter);
}

// output rule filter left daughter graph as a .dot file
void	dot_rf(char	*fname)
{
	int	i, j;
	FILE	*f = fopen(fname, "w");

	for(i=0;i<nrules;i++) if(rules[i]->ndaughters!=1)
	{
		for(j=0;j<nrules;j++) if(rules[j]->ndaughters!=1)
		{
			//if(check_rf(rules[i], rules[j], 0))
			if(rf_tc[i][j])
				fprintf(f, "\"%s\" -> \"%s\"\n", rules[i]->name, rules[j]->name);
		}
	}
	fclose(f);
}
