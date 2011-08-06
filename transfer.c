#define		_GNU_SOURCE
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<sys/time.h>

#include	"unicode.h"
#include	"transfer.h"
#include	"freeze.h"
#include	"chart.h"
#include	"dag.h"
#include	"type.h"
#include	"hash.h"
#include	"conf.h"

void	check_pred_re(struct mrs_ep_desc	*ep)
{
	struct dg	*pred = dg_hop(ep->dg, lookup_fname("PRED"));
	if(!pred)return;
	char	*tn = pred->xtype->name;
	if(tn[0] != '"')return;
	if(tn[1] != '~')return;
	// looks like we have a regular expression on our hands
	extern struct type	*strtype;
	pred->type = pred->xtype = get_top();//strtype;
	char	*re = malloc(strlen(tn));
	strcpy(re, tn+2);
	re[strlen(tn+2)-1] = 0;
	ep->pred_re = towide(re);
	//printf("discovered pred RE '%S'\n", ep->pred_re);

	int	err = regcomp(&ep->re, ep->pred_re, REGEX_FLAGS);
	if(err)
	{
		wchar_t	buf[1024];
		regerror(err, &ep->re, buf, 1023);
		fprintf(stderr, "transfer-rule: while compiling `%S': %S\n", ep->pred_re, buf);
		exit(-1);
	}
}

int	process_transfer_rule_part(struct mrs_desc	*desc, struct transfer_rule	*tr, char	*feature)
{
	struct dg	*dg = dg_hop(tr->dg, lookup_fname(feature));
	if(!dg) { fprintf(stderr, "transfer rule '%s': no '%s' feature.\n", tr->name, feature); return -1; }
	desc->dg = dg;

	// count and save pointers to the EPs
	struct dg	*rels = dg_hop(dg, lookup_fname("RELS"));
	struct dg	*list = dg_hop(rels, lookup_fname("LIST"));
	while(list) { if(dg_hop(list, lookup_fname("FIRST")))desc->neps++; list = dg_hop(list, lookup_fname("REST")); }
	desc->eps = malloc(sizeof(struct mrs_ep_desc)*desc->neps);
	desc->neps=0;
	list = dg_hop(rels, lookup_fname("LIST"));
	while(list)
	{
		struct dg	*epdg = dg_hop(list, lookup_fname("FIRST"));
		if(!epdg)break;
		desc->eps[desc->neps].pred_re = NULL;
		desc->eps[desc->neps].dg = epdg;
		check_pred_re(&desc->eps[desc->neps]);
		desc->neps++;
		list = dg_hop(list, lookup_fname("REST"));
	}

	// count the HCONS
	struct dg	*hcons = dg_hop(dg, lookup_fname("HCONS"));
	list = dg_hop(hcons, lookup_fname("LIST"));
	while(list) { if(dg_hop(list, lookup_fname("FIRST")))desc->nhcons++; list = dg_hop(list, lookup_fname("REST")); }
	return 0;
}

struct transfer_rule	*dg_to_transfer_rule(char	*name, struct dg	*dg)
{
	struct transfer_rule	*tr = calloc(sizeof(struct transfer_rule), 1);
	tr->name = strdup(name);
	tr->dg = dg;
	return tr;
}

struct transfer_rule	**trigger_rules;
int						ntrigger_rules;

int	study_trigger_rule(char	*name, struct dg	*dg)
{
	//printf("loading trigger rule '%s'\n", name);
	//printf("	dg type '%s'\n", dg->xtype->name);
	struct transfer_rule	*tr = dg_to_transfer_rule(name, dg);
	if(!tr)return -1;
	trigger_rules = realloc(trigger_rules, sizeof(struct trigger_rule*)*(ntrigger_rules+1));
	trigger_rules[ntrigger_rules++] = tr;
	return 0;
}

struct transfer_freezer
{
	struct transfer_rule	**trigger_rules;
	int						ntrigger_rules;
	struct transfer_rule	**idiom_rules;
	int						nidiom_rules;
	char					*non_idiom_root;
};

extern struct transfer_rule	**idiom_rules;
extern int						nidiom_rules;

struct transfer_rule	*freeze_transfer_rule(struct transfer_rule	*tr)
{
	struct transfer_rule	*fr = slab_alloc(sizeof(*fr));
	bzero(fr, sizeof(*fr));
	fr->name = freeze_string(tr->name);
	fr->dg = freeze_dg(tr->dg);
	return fr;
}

void	*freeze_transfer()
{
	int	i;
	struct transfer_freezer	*fz = slab_alloc(sizeof(*fz));

	fz->trigger_rules = slab_alloc(sizeof(struct transfer_rule*)*ntrigger_rules);
	for(i=0;i<ntrigger_rules;i++)
		fz->trigger_rules[i] = freeze_transfer_rule(trigger_rules[i]);
	fz->ntrigger_rules = ntrigger_rules;

	fz->idiom_rules = slab_alloc(sizeof(struct transfer_rule*)*nidiom_rules);
	for(i=0;i<nidiom_rules;i++)
		fz->idiom_rules[i] = freeze_transfer_rule(idiom_rules[i]);
	fz->nidiom_rules = nidiom_rules;
	if(non_idiom_root)fz->non_idiom_root = freeze_string(non_idiom_root);
	else fz->non_idiom_root = NULL;
	//printf("freezer: %s\n", fz->non_idiom_root);

	return fz;
}

int	thaw_transfer_rule(struct transfer_rule	*tr)
{
	//printf("thawing rule '%s'\n", tr->name);
	//print_dg(tr->dg);
	//printf("\n\n");
	if(0 != process_transfer_rule_part(&tr->input, tr, "INPUT"))
		return -1;
	if(0 != process_transfer_rule_part(&tr->context, tr, "CONTEXT"))
		return -1;
	if(0 != process_transfer_rule_part(&tr->filter, tr, "FILTER"))
		return -1;
	if(0 != process_transfer_rule_part(&tr->output, tr, "OUTPUT"))
		return -1;

	tr->eq = calloc(sizeof(struct dg*), 10);
	tr->ss = calloc(sizeof(struct dg*), 10);
	tr->neq = tr->nss = 0;
	static int	eqpath[5]={-1}, sspath[5]={-1};
	struct dg	*eql = walk_dg(tr->dg, eqpath, "FLAGS", "EQUAL", 0);
	struct dg	*ssl = walk_dg(tr->dg, sspath, "FLAGS", "SUBSUME", 0);
	//printf("eql = \n");
	//print_dg(eql); printf("\n");
	while(eql && dg_hop(eql, 1))
	{
		tr->eq[tr->neq++] = dg_hop(eql, 1);
		eql = dg_hop(eql, 2);
	}
	while(ssl && dg_hop(ssl, 1))
	{
		tr->ss[tr->nss++] = dg_hop(ssl, 1);
		ssl = dg_hop(ssl, 2);
	}
	//printf("rule %s has %d EQs and %d SSs\n", tr->name, tr->neq, tr->nss);

	return 0;
}

void	recover_transfer(void	*transfer)
{
	int	i;
	struct transfer_freezer	*fz = transfer;
	if(g_mode == GENERATING)
	{
		trigger_rules = fz->trigger_rules;
		ntrigger_rules = fz->ntrigger_rules;
		for(i=0;i<ntrigger_rules;i++)
			thaw_transfer_rule(trigger_rules[i]);
		//fprintf(stderr, "NOTE: thawed %d trigger rules.\n", ntrigger_rules);
	}
	idiom_rules = fz->idiom_rules;
	nidiom_rules = fz->nidiom_rules;
	for(i=0;i<nidiom_rules;i++)
		thaw_transfer_rule(idiom_rules[i]);
	non_idiom_root = fz->non_idiom_root;
	//printf("recover: %s\n", non_idiom_root);
}

struct mrs_ep_desc	place_in_transfer_rule(struct transfer_rule	*tr, int	pl)
{
	if(pl < tr->input.neps)return tr->input.eps[pl];
	pl -= tr->input.neps;
	if(pl < tr->context.neps)return tr->context.eps[pl];
	pl -= tr->context.neps;
	if(pl < tr->filter.neps)return tr->filter.eps[pl];
	fprintf(stderr, "asked for nonexistant place in transfer rule '%s'\n", tr->name);
}

int	test_transfer_alignment(struct transfer_rule	*tr, struct mrs	*mrs, int	count, int	*align, int	total)
{
	int	i;
	setup_tmp();
	for(i=0;i<count;i++)
	{
		struct mrs_ep	*ep = &mrs->rels[align[i]];
		struct mrs_ep_desc	de = place_in_transfer_rule(tr, i);
		if(0 != unify_dg_tmp(ep->dg, de.dg, -1))
			{ forget_tmp(); return -1; }
		if(de.pred_re)
		{
			struct dg	*p = dg_hop(ep->dg, lookup_fname("PRED"));
			wchar_t	*wide = towide(p->type->name);
			//printf("need to see if '%S' and '%S' are RE-compatible\n", wide, de.pred_re);
			regmatch_t	mat;
			int	res = regexec(&de.re, wide, 1, &mat, 0);
			free(wide);
			if(res != 0)
			{
				//printf("nope.\n");
				forget_tmp();
				return -1;
			}
			//else printf("yup!\n");
		}
	}
	if(count == total)
	{
		// unification is complete; test the EQUAL and SUBSUME conditions
		extern unsigned int	generation;
		for(i=0;i<tr->neq;i++)
		{
			if(tr->eq[i]->gen != generation)
			{
				//printf("variable with EQ condition untouched; could not be present on matching MRS; fail!\n");
				forget_tmp();
				return -1;
			}
			struct dg	*now = dereference_dg(tr->eq[i]);
			struct type	*tnow = (now->gen == generation)?now->type:now->xtype;
			if(tr->eq[i]->xtype != tnow)
			{
				//printf("failed EQ test: should have been type '%s'; actually '%s'\n", tr->eq[i]->xtype->name, tnow->name);
				forget_tmp();
				return -1;
			}
			/*printf("passed EQ test %d for rule '%s'\n", i, tr->name);
			printf("tr->eq[i]->xtype = %s\n", tr->eq[i]->xtype->name);
			printf("now = %p ; eq[i] = %p\n", now, tr->eq[i]);
			printf("now is %s\n", (now->gen==generation)?"current":"stale");*/
		}
		for(i=0;i<tr->nss;i++)
		{
			if(tr->ss[i]->gen != generation)
			{
				//printf("variable with SS condition untouched; could not be present on matching MRS; fail!\n");
				forget_tmp();
				return -1;
			}
			struct dg	*now = dereference_dg(tr->ss[i]);
			struct type	*tnow = (now->gen == generation)?now->type:now->xtype;
			if(!descendent_of(tnow->tnum, tr->ss[i]->xtype))
			{
				//printf("failed SS test\n");
				forget_tmp();
				return -1;
			}
		}
	}
	forget_tmp();
	return 0;
}

void	align_transfer_rule0(struct transfer_rule	*tr, struct mrs	*mrs, int	place, int	total, int	*alignment, void	(*callback)())
{
	int	i;
	if(place == tr->input.neps + tr->context.neps && place == total)
	{
		/*printf("produced a complete alignment for rule '%s' [%d input, %d ctx]: ", tr->name, tr->input.neps, tr->context.neps);
		print_dg(tr->dg);
		for(i=0;i<total;i++)
			printf(" %d [%s]", alignment[i], mrs->rels[alignment[i]].pred);
		printf("\n");*/
		if(tr->filter.neps)
		{
			// see if we can match the filter portion... uh oh!
			int	did_filter = 0;
			void	filtercb() { did_filter = 1; }
			align_transfer_rule0(tr, mrs, place, tr->input.neps + tr->context.neps + tr->filter.neps, alignment, filtercb);
			if(did_filter)
			{
				//printf("FILTER fired for rule '%s'.\n", tr->name);
				return;
			}
		}
		callback();
		return;
	}
	else if(place == total)
	{
		callback();
		return;
	}

	// try to align 'place' to an unused EP in 'mrs'
	for(i=0;i<mrs->nrels;i++)
	{
		if(mrs->rels[i].active)continue;
		alignment[place] = i;
		//printf("how about rule %s spot %d for '%s'?\n", tr->name, place, mrs->rels[i].pred);
		if(0 != test_transfer_alignment(tr, mrs, place+1, alignment, tr->input.neps + tr->context.neps))
			continue;
		mrs->rels[i].active = 1;
		align_transfer_rule0(tr, mrs, place+1, total, alignment, callback);
		mrs->rels[i].active = 0;
	}
}

void	align_transfer_rule(struct transfer_rule	*tr, struct mrs	*mrs, void	(*callback)())
{
	int	i;
	for(i=0;i<mrs->nrels;i++)
		mrs->rels[i].active = 0;

	int	nplaces = tr->input.neps + tr->context.neps + tr->filter.neps;
	int	alignment[nplaces];
	align_transfer_rule0(tr, mrs, 0, tr->input.neps + tr->context.neps, alignment, callback);
}

void	activate_trigger_rules(struct mrs	*mrsin, void	trigger(char	*lexid))
{
	int	i;
	/*printf("input MRS var DGs:\n");
	for(i=0;i<mrsin->vlist.nvars;i++)
	{
		struct mrs_var	*v = mrsin->vlist.vars[i];
		printf("%s%d: ", v->type, v->vnum);
		print_dg(v->dg);
		printf("\n");
	}*/
	// note: generate() has already built the ep->dg's for each input relation
	for(i=0;i<ntrigger_rules;i++)
	{
		void	found_alignment()
		{
			struct dg	*flags = dg_hop(trigger_rules[i]->dg, lookup_fname("FLAGS"));
			struct dg	*trig = dg_hop(flags, lookup_fname("TRIGGER"));
			char	lexid[256];
			sprintf(lexid, "%.*s", (int)strlen(trig->xtype->name)-2, trig->xtype->name+1);
			//printf("triggering '%s' via '%s'\n", lexid, trigger_rules[i]->name);
			trigger(lexid);
		}
		align_transfer_rule(trigger_rules[i], mrsin, found_alignment);
	}
}
