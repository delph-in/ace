#define		_GNU_SOURCE
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<sys/time.h>
#include	<assert.h>

#define	ALLOW_SEMI_MODE	0

//#define	DEBUG	printf
#define	DEBUG(...)	do { } while(0)

#include	"unicode.h"
#include	"transfer.h"
#include	"freeze.h"
#include	"chart.h"
#include	"dag.h"
#include	"type.h"
#include	"hash.h"
#include	"conf.h"
#include	"mrs.h"

extern struct type_hierarchy	*semi_th, *main_th;

int	is_semarg_type(struct type	*x);
int equal_semarg_types(struct type	*x, struct type	*y);

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

	if(semi_th && ALLOW_SEMI_MODE)
	{
		// new-fangled system: convert our "internal" TFSes to SEMI-compliant TFSes
		desc->mrs = cont_to_mrs(desc->dg);
		semi_reformat_mrs(desc->mrs, 1);
	}

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
		//printf("rule %s . %s %d = \n", tr->name, feature, desc->neps-1);
		//print_dg(epdg);printf("\n");
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
	static int flagspath[5] = {-1,-1};
	struct dg	*optdg = walk_dg(dg, flagspath, "FLAGS", "OPTIONAL", 0);
	tr->optional = optdg?(!strcmp(optdg->xtype->name, "+")):0;
	tr->dg = dg;
	return tr;
}

void warn_about_equals(struct transfer_rule	*tr)
{
	static int	eqpath[5]={-1};
	struct dg	*eql = walk_dg(tr->dg, eqpath, "FLAGS", "EQUAL", 0);
	while(eql && dg_hop(eql, 1))
	{
		struct dg	*eq = dg_hop(eql, 1);
		eql = dg_hop(eql, 2);
		if(!is_semarg_type(eq->xtype))
		{
			fprintf(stderr, "WARNING: ACE only fully supports EQUAL constraints on `semarg' subtypes.\n");
			fprintf(stderr, "%s: EQUAL constraint on `%s' type\n", tr->name, eq->xtype->name);
		}
	}
}

struct transfer_rule	**transfer_rules;
int						ntransfer_rules;

struct transfer_rule	**trigger_rules;
int						ntrigger_rules;

int	study_trigger_rule(char	*name, struct dg	*dg)
{
	//printf("loading trigger rule '%s'\n", name);
	//printf("	dg type '%s'\n", dg->xtype->name);
	wellform(dg, "transfer rule");
	struct transfer_rule	*tr = dg_to_transfer_rule(name, dg);
	if(!tr)return -1;
	warn_about_equals(tr);
	trigger_rules = realloc(trigger_rules, sizeof(struct transfer_rule*)*(ntrigger_rules+1));
	trigger_rules[ntrigger_rules++] = tr;
	return 0;
}

int	study_transfer_rule(char	*name, struct dg	*dg)
{
	//printf("loading transfer rule '%s'\n", name);
	//printf("	dg type '%s'\n", dg->xtype->name);
	wellform(dg, "transfer rule");
	struct transfer_rule	*tr = dg_to_transfer_rule(name, dg);
	if(!tr)return -1;
	warn_about_equals(tr);
	transfer_rules = realloc(transfer_rules, sizeof(struct transfer_rule*)*(ntransfer_rules+1));
	transfer_rules[ntransfer_rules++] = tr;
	return 0;
}

int	load_transfer_rules()
{
	assert(g_transfer != 0);
	if(process_tdl_dgs_status("rule", study_transfer_rule))
	{
		fprintf(stderr, "transfer rules: unable to load rules, bailing out.\n");
		exit(-1);
	}
	return 0;
}

struct transfer_freezer
{
	struct transfer_rule	**trigger_rules;
	int						ntrigger_rules;
	struct transfer_rule	**idiom_rules;
	int						nidiom_rules;
	char					*non_idiom_root;
	struct transfer_rule	**transfer_rules;
	int						ntransfer_rules;
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

	fz->transfer_rules = slab_alloc(sizeof(struct transfer_rule*)*ntransfer_rules);
	for(i=0;i<ntransfer_rules;i++)
		fz->transfer_rules[i] = freeze_transfer_rule(transfer_rules[i]);
	fz->ntransfer_rules = ntransfer_rules;

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
	transfer_rules = fz->transfer_rules;
	ntransfer_rules = fz->ntransfer_rules;
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

int	is_semarg_type(struct type	*x)
{
	static struct type *semarg = NULL;
	if(!semarg)assert(semarg = lookup_type(semarg_type));
	if(x->name[0]=='"')return 0;
	return descendent_of(x->tnum, semarg);
}

int equal_semarg_types(struct type	*x, struct type	*y)
{
	if(semi_th && ALLOW_SEMI_MODE)
	{
		return (x==y)?1:0;
	}
	else
	{
		DEBUG("eq-semarg-types: %s =? %s\n", x->name, y->name);
		if(x==y)return 1;
		char	*semi_x = internal_to_external_var_type(x->name);
		char	*semi_y = internal_to_external_var_type(y->name);
		DEBUG("compare %s [%s] to %s [%s]\n", semi_x, x->name, semi_y, y->name);
		return strcmp(semi_x, semi_y)?0:1;
	}
}

struct type	*semi_find_type(char	*name)
{
	extern struct type_hierarchy	*semi_th;
	assert(semi_th != NULL);
	return hash_find(semi_th->thash, name);
}

int subsume_semarg_types(struct type	*x, struct type	*y)
{
	if(semi_th && ALLOW_SEMI_MODE)
	{
		return (descendent_of(y->tnum, x))?1:0;
	}
	else
	{
		DEBUG("ss-semarg-types: %s >? %s\n", x->name, y->name);
		if(x==y)return 1;
		char	*semi_x = internal_to_external_var_type(x->name);
		char	*semi_y = internal_to_external_var_type(y->name);

		// method 1
		struct type	*sx = semi_find_type(semi_x);
		struct type	*sy = semi_find_type(semi_y);
		assert(sx && sy);
		int	r1 = descendent_of(sy->tnum, sx)?1:0;

		// method 2
		char	*local_x = external_to_internal_var_type(semi_x);
		char	*local_y = external_to_internal_var_type(semi_y);
		// scared yet?
		DEBUG("compare %s [%s [%s]] to %s [%s [%s]]\n", local_x, semi_x, x->name, local_y, semi_y, y->name);
		struct type	*tx = lookup_type(local_x);
		struct type	*ty = lookup_type(local_y);
		assert(tx && ty);
		int	r2 = descendent_of(ty->tnum, tx)?1:0;

		assert(r1 == r2);
		//printf("methods agree for '%s >? %s'\n", x->name, y->name);
		return r2;
	}
}

int	nfailed_transfer_unifies, nsuccessful_transfer_unifies;

void	show_transfer_counts() __attribute__ ((destructor));
void	show_transfer_counts()
{
	if(g_mode == GENERATING)
		fprintf(stderr, "NOTE: transfer did %d successful unifies and %d failed ones\n", nsuccessful_transfer_unifies, nfailed_transfer_unifies);
}

int	test_transfer_alignment(struct transfer_rule	*tr, struct mrs	*mrs, int	count, int	*align, int	total)
{
	int	i;
	for(i=0;i<count;i++)
	{
		static int	pred_fnum = -1, arg0_fnum=-1;
		if(pred_fnum == -1)pred_fnum = lookup_fname("PRED");
		if(arg0_fnum == -1)arg0_fnum = lookup_fname("ARG0");
		struct mrs_ep	*ep = &mrs->rels[align[i]];
		struct dg	*ep_p = dg_hop(ep->dg, pred_fnum);
		struct dg	*ep_a0 = dg_hop(ep->dg, arg0_fnum);
		struct mrs_ep_desc	de = place_in_transfer_rule(tr, i);
		struct dg	*de_p = dg_hop(de.dg, pred_fnum);
		struct dg	*de_a0 = dg_hop(de.dg, arg0_fnum);
		if(de_p && ep_p)
		{
			struct type	*de_pred = de_p->xtype;
			struct type	*ep_pred = ep_p->xtype;
			if(!glb(ep_pred, de_pred))
			{
				// most common failure path
				return -1;
			}
			if(de.pred_re)
			{
				wchar_t	*wide = towide(ep_pred->name);
				DEBUG("need to see if '%S' and '%S' are RE-compatible\n", wide, de.pred_re);
				regmatch_t	mat;
				int	res = regexec(&de.re, wide, 1, &mat, 0);
				free(wide);
				if(res != 0)
				{
					DEBUG("nope.\n");
					return -1;
				}
				DEBUG("yup!\n");
			}
		}
		if(de_a0 && ep_a0)
		{
			if(!glb(de_a0->xtype, ep_a0->xtype))
			{
				return -1;
			}
		}
	}
	setup_tmp();

	// guarantee that the rule nodes get forwarded to the EP nodes,
	// rather than forwarding EP nodes to rule nodes.
	extern int allow_unify_swapping;
	allow_unify_swapping = 0;
	// note: if the rule has multiple re-entrancies for an EQUAL or SUBSUME node,
	// we need to do something slightly cleverer than what happens below.
	// right now, we see whether tr->eq[i]->xtype is compatible with
	// dereference_dg(tr->eq[i])->xtype.
	// i *think* in the case where there are multiple EP nodes that get
	// unified to the same EQ node (or SS node), we just have to walk
	// down the forwarding chain from tr->eq[i] one at a time and check
	// that each one's xtype is compatible with the original xtype.
	// if that turns out not to work... as a last resort, we could
	// recurse on the input EP's dag and find all nodes that are forwarded
	// to the same place as tr->eq[i]... but that sounds slow.
	for(i=0;i<count;i++)
	{
		struct mrs_ep	*ep = &mrs->rels[align[i]];
		struct mrs_ep_desc	de = place_in_transfer_rule(tr, i);
		//if(0 != unify_dg_tmp(ep->dg, de.dg, -1))
		if(0 != unify_dg_tmp(de.dg, ep->dg, -1))
		{
			//printf("TRANSFER: ");
			//unify_backtrace();
			forget_tmp();
			//printf("rule ep:\n");
			//print_dg(de.dg);printf("\n");
			//printf("input ep:\n");
			//print_dg(ep->dg);printf("\n");
			nfailed_transfer_unifies++;
			allow_unify_swapping = 1;
			return -1;
		}
		nsuccessful_transfer_unifies++;
	}
	allow_unify_swapping = 1;

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
			// we require that the *original* type of the node that matched tr->eq[i]
			// is equal to the *original* type of tr->eq[i] itself
			//struct dg	*now = dereference_dg(tr->eq[i]);
			//struct type	*tnow = (now->gen == generation)?now->type:now->xtype;
			//struct type	*tshould = tr->eq[i]->xtype;
			//if(equal_semarg_types(tnow, tshould) == 0)
			struct dg	*ep_original = dereference_dg(tr->eq[i]);
			struct type	*t_ep_original = ep_original->xtype;
			struct type	*t_eq_original = tr->eq[i]->xtype;
			if(equal_semarg_types(t_ep_original, t_eq_original) == 0)
			{
				DEBUG("failed EQ test: should have been type '%s'; actually '%s'\n", tr->eq[i]->xtype->name, t_ep_original->name);
				forget_tmp();
				return -1;
			}
			DEBUG("passed EQ test %d for rule '%s'\n", i, tr->name);
		}
		for(i=0;i<tr->nss;i++)
		{
			if(tr->ss[i]->gen != generation)
			{
				//printf("variable with SS condition untouched; could not be present on matching MRS; fail!\n");
				forget_tmp();
				return -1;
			}
			//struct dg	*now = dereference_dg(tr->ss[i]);
			//struct type	*tnow = (now->gen == generation)?now->type:now->xtype;
			//if(!descendent_of(tnow->tnum, tr->ss[i]->xtype))
			struct dg	*ep_original = dereference_dg(tr->ss[i]);
			struct type	*t_ep_original = ep_original->xtype;
			struct type	*t_ss_original = tr->ss[i]->xtype;
			DEBUG("SS test: %s >? %s\n", t_ss_original->name, t_ep_original->name);
			if(subsume_semarg_types(t_ss_original, t_ep_original) == 0)
			//if(!descendent_of(t_ep_original->tnum, t_ss_original))
			{
				DEBUG("failed SS test\n");
				forget_tmp();
				return -1;
			}
		}
	}
	forget_tmp();
	return 0;
}

void	align_transfer_rule0(struct transfer_rule	*tr, struct mrs	*mrs, int	place, int	total, int	*alignment, void	(*callback)(int	*alignment))
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
			void	filtercb(int	*filtalign) { did_filter = 1; }
			align_transfer_rule0(tr, mrs, place, tr->input.neps + tr->context.neps + tr->filter.neps, alignment, filtercb);
			if(did_filter)
			{
				//printf("FILTER fired for rule '%s'.\n", tr->name);
				return;
			}
		}
		callback(alignment);
		return;
	}
	else if(place == total)
	{
		// FILTER callback
		callback(alignment);
		return;
	}

	// try to align 'place' to an unused EP in 'mrs'
	for(i=0;i<mrs->nrels;i++)
	{
		if(mrs->rels[i].active)continue;
		alignment[place] = i;
		DEBUG("how about rule %s spot %d for '%s'?\n", tr->name, place, mrs->rels[i].pred);
		if(0 != test_transfer_alignment(tr, mrs, place+1, alignment, tr->input.neps + tr->context.neps))
			continue;
		DEBUG("passed test.\n");
		mrs->rels[i].active = 1;
		align_transfer_rule0(tr, mrs, place+1, total, alignment, callback);
		mrs->rels[i].active = 0;
	}
}

void	align_transfer_rule(struct transfer_rule	*tr, struct mrs	*mrs, void	(*callback)(int	*alignment))
{
	int	i;
	for(i=0;i<mrs->nrels;i++)
		mrs->rels[i].active = 0;

	DEBUG("trying to align %s\n", tr->name);
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

	if(semi_th && ALLOW_SEMI_MODE)
		semi_reformat_mrs(mrsin, 0);

	for(i=0;i<ntrigger_rules;i++)
	{
		void	found_alignment(int	*alignment)
		{
			struct dg	*flags = dg_hop(trigger_rules[i]->dg, lookup_fname("FLAGS"));
			struct dg	*trig = dg_hop(flags, lookup_fname("TRIGGER"));
			char	lexid[256];
			sprintf(lexid, "%.*s", (int)strlen(trig->xtype->name)-2, trig->xtype->name+1);
			if(temporary_expedient(lexid))
			{
				//printf("not triggering '%s' via '%s' because it is disabled for generation\n", lexid, trigger_rules[i]->name);
				return;
			}
			//printf("triggering '%s' via '%s', aligned on %s\n", lexid, trigger_rules[i]->name, mrsin->rels[alignment[0]].pred);
			trigger(lexid);
		}
		align_transfer_rule(trigger_rules[i], mrsin, found_alignment);
	}
}




void	skolemize_for_transfer(struct mrs	*m)
{
	int	i;
	static int instloc = -1;
	if(instloc ==-1)instloc = lookup_fname("INSTLOC");

	// tweak the INSTLOC fields of the mrs variables
	// so that they won't unify with each other.

	struct type_hierarchy	*th = default_type_hierarchy();

	for(i=0;i<m->vlist.nvars;i++)
	{
		struct dg	*dg;
		if(semi_th && ALLOW_SEMI_MODE)
			dg = m->vlist.vars[i]->semi_dg;
		else dg = m->vlist.vars[i]->dg;

		if(!dg)continue;	// invented LTOP, INDEX, XARG
		if(m->vlist.vars[i]->is_const)continue;

		/*printf("dag to skolemize for %s%d = \n", m->vlist.vars[i]->type, i);
		print_dg(dg);
		printf("\n");*/

		struct dg	*iloc = dg_hop(dg, instloc);
		assert(iloc != NULL);
		assert(i<th->nstrings);
		iloc->type = iloc->xtype = th->strings[i];
	}
}

void	unskolemize_for_transfer(struct mrs	*m)
{
	int	i;
	static int instloc = -1;
	if(instloc ==-1)instloc = lookup_fname("INSTLOC");

	// untweak the INSTLOC fields of the mrs variables

	for(i=0;i<m->vlist.nvars;i++)
	{
		struct dg	*dg = m->vlist.vars[i]->dg;
		if(!dg)continue;	// invented LTOP, INDEX, XARG
		if(m->vlist.vars[i]->is_const)continue;

		struct dg	*iloc = dg_hop(dg, instloc);
		assert(iloc != NULL);
		iloc->type = iloc->xtype = get_top();
	}
}


struct mrs	*transfer_mrs_result(struct transfer_rule	*tr, int	*alignment, struct mrs	*min)
{
	// for now, we only support deleting EPs.
	// XXX when we support creating new EPs (and hence new variables),
	// we won't be able to just copy straight over the hcons and vlist anymore
	assert(tr->output.neps == 0);
	assert(tr->output.nhcons == 0);
	assert(tr->input.nhcons == 0);

	//printf("activated rule '%s' to ditch %d EPs\n", tr->name, tr->input.neps);

	struct mrs	*mout = slab_alloc(sizeof(*mout));
	bzero(mout, sizeof(*mout));
	mout->nhcons = min->nhcons;
	mout->hcons = min->hcons;
	mout->vlist = min->vlist;
	mout->ltop = min->ltop;
	mout->index = min->index;
	mout->xarg = min->xarg;
	mout->nrels = min->nrels - tr->input.neps;
	mout->rels = slab_alloc(sizeof(struct mrs_ep) * mout->nrels);
	int i, j;
	char	ep_ditch[min->nrels];
	bzero(ep_ditch, sizeof(ep_ditch));
	for(i=0;i<tr->input.neps;i++)
		ep_ditch[alignment[i]] = 1;
	for(i=j=0;i<min->nrels;i++)
		if(!ep_ditch[i])
			mout->rels[j++] = min->rels[i];
	return mout;
}

struct transfer_mrs_set
{
	int	n;
	struct mrs	**mrs;
};

int	transfer_mrs1(struct transfer_rule	*tr, struct transfer_mrs_set	*ts)
{
	int	i, nresult = 0;
	struct mrs	**result = NULL;
	for(i=0;i<ts->n;i++)
	{
		struct mrs	*min = ts->mrs[i];
		int	nalignments = 0;
		void	found_alignment(int	*alignment)
		{
			struct mrs	*mout = transfer_mrs_result(tr, alignment, min);
			// XXX TODO: figure out how to pack results
			// the tricky question is whether to put a packed MRS on the final result list or not

			// new result.
			ts->mrs = slab_realloc(ts->mrs, sizeof(struct mrs*)*ts->n, sizeof(struct mrs*)*(ts->n+1));
			ts->mrs[ts->n++] = mout;

			//printf("rule fired: %s\n", tr->name);
			//print_mrs(mout);printf("\n");

			nalignments++;
		}
		align_transfer_rule(tr, min, found_alignment);
		if(nalignments == 0 || tr->optional)
		{
			// keep this mrs
			result = slab_realloc(result, sizeof(struct mrs*)*nresult, sizeof(struct mrs*)*(nresult+1));
			result[nresult++] = min;
		}
		else
		{
			// ditch this mrs
		}
	}
	ts->n = nresult;
	ts->mrs = result;
}

int	transfer_mrs(int	nrules, struct transfer_rule	**rules, struct mrs	*m, struct mrs	***Results)
{
	int	i;

	if(semi_th && ALLOW_SEMI_MODE)
		semi_reformat_mrs(m, 0);

	skolemize_for_transfer(m);

	struct transfer_mrs_set	ts;
	ts.n = 1;
	ts.mrs = slab_alloc(sizeof(*m));
	ts.mrs[0] = m;

	for(i=0;i<nrules;i++)
		transfer_mrs1(rules[i], &ts);

	unskolemize_for_transfer(m);

	if(Results)*Results = ts.mrs;
	return ts.n;
}
