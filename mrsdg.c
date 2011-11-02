#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<assert.h>

#include	"dag.h"
#include	"mrs.h"
#include	"conf.h"
#include	"freeze.h"

static int name;

extern char	*ep_drop;

int		nmultifeatures;
struct multifeature
{
	char	*sub;
	int 	prep;
	char	*str;
}	*multifeatures;

int	inhibit_vpm = 0;

int	invent_ltop;

// dag to mrs conversion

// XXX a lot of strings in here are allocated to the heap,
// and it doesn't look like anybody ever frees them...

static inline char	*mfpath(char	*sub, int prep)
{
	extern char **fnames;
	int i;

	for(i=0;i<nmultifeatures;i++)
		if(multifeatures[i].sub == sub && multifeatures[i].prep == prep)
			return multifeatures[i].str;
	nmultifeatures++;
	multifeatures = realloc(multifeatures, sizeof(struct multifeature)*nmultifeatures);
	multifeatures[nmultifeatures-1].sub = sub;
	multifeatures[nmultifeatures-1].prep = prep;
	multifeatures[nmultifeatures-1].str = malloc(strlen(sub) + 1 + strlen(fnames[prep]) + 1);
	sprintf(multifeatures[nmultifeatures-1].str, "%s.%s", fnames[prep], sub);
	//printf("new mf `%s'\n", multifeatures[nmultifeatures-1].str);
	return multifeatures[nmultifeatures-1].str;
}

static inline void	*slab_malloc(int l) { if(l%4)l += (4-(l%4)); return slab_alloc(l); }
static inline void	*slab_calloc(int a, int b) { int l = a*b; void *p = slab_malloc(l); bzero(p, l); return p; }

int add_var_feats(struct mrs_var *v, int label, struct dg *value)
{
	extern char **fnames;
	int i, flen = strlen(fnames[label]);

	if(!strcmp(fnames[label], "INSTLOC"))return 0;

	if(value->narcs)
	{
		struct dg **arcs = DARCS(value);
		// walk inside
		int	nnew = 0;
		for(i=0;i<value->narcs;i++)
			nnew += add_var_feats(v, value->label[i], arcs[i]);
		// prepend each of the new property names with label
		if(v)
		{
			for(i=v->nprops - nnew;i<v->nprops;i++)
				v->props[i].name = mfpath(v->props[i].name, label);
		}
		return nnew;
	}
	else
	{
		if(v)
		{
			v->nprops++;
			//v->props = slab_realloc(v->props, sizeof(struct mrs_var_property)*(v->nprops-1),
			//									sizeof(struct mrs_var_property)*v->nprops);
			v->props[v->nprops-1].name = fnames[label];//strdup(fnames[label]);
			v->props[v->nprops-1].value = strdup(value->xtype->name);
		}
		return 1;
	}
}

#define	VHSIZE	103928
struct mrs_var	*varhash[VHSIZE];
int				varind;
int	varhash_used = 0, varhash_collis = 0;
static inline unsigned hashcode(struct dg	*dg) { return ((unsigned long long)dg>>5) % (VHSIZE-1); }

void	clear_mrs()
{
	int i;

//static int clear_mrs_timer = -1;
//if(clear_mrs_timer==-1)clear_mrs_timer = new_timer("mrs clear hash");
//start_timer(clear_mrs_timer);
bzero(varhash, sizeof(struct mrs_var*) * VHSIZE);
	/*for(i=0;i<VHSIZE;i++)
	{
		if(varhash[i])
		{
			//varhash[i]->refs = 1;
			//free_mrs_var(varhash[i]);
			varhash[i] = 0;
		}
	}*/
	varhash_used = 0;
	varhash_collis = 0;
//stop_timer(clear_mrs_timer, 1);
}

struct mrs_var	*invent_mrs_var(char	*type, struct mrs_vlist	*vlist)
{
	struct mrs_var	*var = slab_calloc(sizeof(struct mrs_var), 1);
	var->dg = NULL;
	var->type = strdup(type);
	var->name = strdup(type);
	var->vnum = vlist->nvars;
	var->refs = 1;

	vlist->nvars++;
	assert(vlist->nvars < 10240);
	vlist->vars[vlist->nvars-1] = var;
	return var;
}

int	nvars_extracted = 0;

struct mrs_var	*dg_to_mrs_var(struct dg	*dg, struct mrs_vlist *vlist)
{
	static struct type *semarg = 0;

	if(!semarg)
		assert(semarg = lookup_type(semarg_type));

	if(!dg)return NULL;

	if(dg->gen != 1 || !dg->copy || ((struct mrs_var*)dg->copy)->dg!=dg)
	{
		struct dg	**arcs = DARCS(dg);
		unsigned hc = hashcode(dg);
		struct mrs_var	*var, *vnew;
		extern char **fnames;
		int i;

		// check whether a different reading already extracted this variable
		if(varhash[hc] && varhash[hc]->dg==dg)
		{
			var = varhash[hc];
			var->refs = 2;
			var->vnum = vlist->nvars;
			//printf("reuse dg %p, think var %p\n", dg, varhash[hc]);
		}
		else
		{
			//static int var_extract_timer = -1;
			//if(var_extract_timer == -1)var_extract_timer = new_timer("mrs var extr");
			//start_timer(var_extract_timer);
			var = slab_calloc(sizeof(struct mrs_var), 1);
			struct type	*t = dg->type;
			if(t->name[0]=='"' || !descendent_of(t->tnum, semarg)) var->is_const = 1;
			var->dg = dg;
			var->type = var->is_const?strdup("c"):strdup(dg->type->name);
			var->name = strdup(dg->type->name);
			var->vnum = vlist->nvars;
			var->refs = 1;
			if(!var->is_const)
			{
				int	nprops = 0;
				for(i=0;i<dg->narcs;i++)
					nprops += add_var_feats(NULL, dg->label[i], arcs[i]);
				var->props = slab_alloc(sizeof(struct mrs_var_property)*nprops);
				for(i=0;i<dg->narcs;i++)
					add_var_feats(var, dg->label[i], arcs[i]);

				if(!inhibit_vpm)
				{
					vnew = internal_to_external_mrs_var(var);
					//free_mrs_var(var);
					var = vnew;
				}
			}
			//printf("new var %p dg %p\n", var, dg);
			//if(varhash[hc])free_mrs_var(varhash[hc]);
			if(varhash[hc])varhash_collis++;
			else varhash_used++;
			varhash[hc] = var; var->refs++;
			nvars_extracted++;
			//stop_timer(var_extract_timer, 1);
		}

		vlist->nvars++;
		assert(vlist->nvars < 10240);
		vlist->vars[vlist->nvars-1] = var;
		dg->copy = (void*)var;
		dg->gen = 1;
		return var;
	}
	//printf("back to dg %p, think var %p\n", dg, dg->copy);
	((struct mrs_var*)dg->copy)->refs++;
	return (struct mrs_var*)dg->copy;
}

void	dg_to_mrs_ep(struct dg *dg, struct mrs_vlist *vlist, struct mrs_ep	*ep)
{
	struct dg	**arcs = DARCS(dg);
	struct dg		*pred;
	static int		lblf = -1, predf, cfromf, ctof;
	int				i, j;
	extern int enable_token_avms;

	if(lblf==-1)
	{
		lblf = lookup_fname("LBL");
		predf = lookup_fname("PRED");
		if(enable_token_avms)
		{
			cfromf = lookup_fname("CFROM");
			ctof = lookup_fname("CTO");
		}
	}
	pred = dg_hop(dg, predf);
	if(!pred)
	{
		fprintf(stderr, "EP dag had no PRED\n");
		exit(-1);
	}
	bzero(ep, sizeof(*ep));
	ep->pred = strdup(pred->xtype->name);
	ep->dg = dg;
	ep->lbl = dg_to_mrs_var(dg_hop(dg, lblf), vlist);
	int	neps = 0;
	for(i=0;i<dg->narcs;i++)
	{
		extern char	**fnames;
		if(dg->label[i] == lblf || dg->label[i]==predf)continue;
		if(ep_drop[dg->label[i]])continue;
		neps++;
	}
	ep->args = slab_alloc(sizeof(struct mrs_ep_role) * neps);
	for(i=0;i<dg->narcs;i++)
	{
		extern char	**fnames;
		if(dg->label[i] == lblf || dg->label[i]==predf)continue;
		if(ep_drop[dg->label[i]])continue;
		ep->nargs++;
		ep->args[ep->nargs-1].name = freeze_string(fnames[dg->label[i]]);
		ep->args[ep->nargs-1].value = dg_to_mrs_var(arcs[i], vlist);
	}
	ep->cfrom = ep->cto = -1;
	if(enable_token_avms)
	{
		struct dg	*cfrom_dg = dg_hop(dg, cfromf);
		struct dg	*cto_dg = dg_hop(dg, ctof);
		if(cfrom_dg!=NULL && cto_dg!=NULL)
		{
			if(cfrom_dg->type->name[0]=='"' && cto_dg->type->name[0]=='"')
			{
				ep->cfrom = atoi(cfrom_dg->type->name+1);
				ep->cto = atoi(cto_dg->type->name+1);
			}
		}
	}
}

void	dg_to_mrs_hc(struct dg *dg, struct mrs_vlist *vlist, struct mrs_hcons	*hc)
{
	struct dg		*harg, *larg;
	static int		hargf = -1, largf;
	static struct type *qeq, *geq, *leq;

	if(hargf==-1)
	{
		hargf = lookup_fname("HARG");
		largf = lookup_fname("LARG");
		qeq = lookup_type("qeq");
		geq = lookup_type("geq");
		leq = lookup_type("leq");
	}
	if(dg->type == qeq)hc->type = hcons_qeq;
	else if(dg->type == geq)hc->type = hcons_geq;
	else if(dg->type == leq)hc->type = hcons_leq;
	else
	{
		fprintf(stderr, "unknown HCONS type `%s'\n", dg->type->name);
		exit(-1);
	}
	hc->hi = dg_to_mrs_var(dg_hop(dg, hargf), vlist);
	hc->lo = dg_to_mrs_var(dg_hop(dg, largf), vlist);
}

#define	DO_DIFFLIST(dlist, args, code) do { void lambda(args) {code} do_difflist(dlist, lambda); } while(0)

void do_list(struct dg *list, void (*process)(struct dg *item))
{
	struct dg *item;

	if(list) for(item=dg_hop(list,1);list && item;list=dg_hop(list,2),item=dg_hop(list,1))
		process(item);
}

void do_difflist(struct dg *dlist, void (*process)(struct dg *item))
{
	struct dg *list = dg_hop(dlist, lookup_fname("LIST"));
	do_list(list, process);
}

struct mrs	*cont_to_mrs(struct dg	*cont)
{
	static int	hookf = -1, ltopf, indexf, xargf, relsf, hconsf;
	struct dg	*hook, *rels, *hcons;
	struct mrs	*mrs = slab_calloc(sizeof(struct mrs), 1);

	if(!cont)return mrs;

	// allocate temporary space for lots and lots of mrs_var*'s
	static struct mrs_var	**tmp_vlist = NULL;
	if(!tmp_vlist)tmp_vlist = malloc(sizeof(struct mrs_var*) * 10240);
	// take a chance on crashing or doing some other bad thing
	// if the MRS has more than 10240 variables...
	mrs->vlist.vars = tmp_vlist;

	name = 0;
	if(hookf==-1)
	{
		hookf = lookup_fname("HOOK");
		ltopf = lookup_fname("LTOP");
		indexf = lookup_fname("INDEX");
		xargf = lookup_fname("XARG");
		relsf = lookup_fname("RELS");
		hconsf = lookup_fname("HCONS");
	}
	//printf("extract from cont: "); print_dg(cont); printf("\n");

//	static int other_timer = -1;
//	if(other_timer==-1)other_timer = new_timer("reset CONT");
//	start_timer(other_timer);
	dgreset_r(cont);
//	stop_timer(other_timer, 1);

	nvars_extracted = 0;

	load_ep_drop();
	hook = dg_hop(cont, hookf);
	rels = dg_hop(cont, relsf);
	hcons = dg_hop(cont, hconsf);
	if(hook)
	{
		if(invent_ltop)
		{
			// XXX hack see chat w/ oe 2010-08-30
			mrs->ltop = invent_mrs_var(handle_type, &mrs->vlist);
		}
		else mrs->ltop = dg_to_mrs_var(dg_hop(hook, ltopf), &mrs->vlist);
		mrs->index = dg_to_mrs_var(dg_hop(hook, indexf), &mrs->vlist);
		mrs->xarg = dg_to_mrs_var(dg_hop(hook, xargf), &mrs->vlist);
	}
//	static int	rel_timer = -1;
//	if(rel_timer == -1)rel_timer = new_timer("extract EPs");
//	start_timer(rel_timer);
	int	nrels = 0;
	DO_DIFFLIST(rels, struct dg *rel, nrels++; );
	mrs->rels = slab_alloc(sizeof(struct mrs_ep) * nrels);
	DO_DIFFLIST(rels, struct dg *rel,
		mrs->nrels++;
		dg_to_mrs_ep(rel, &mrs->vlist, mrs->rels+mrs->nrels-1);
		mrs->rels[mrs->nrels-1].epnum = mrs->nrels-1;
	);
//	stop_timer(rel_timer, nrels);
//	static int	hc_timer = -1;
//	if(hc_timer == -1)hc_timer = new_timer("extract HCs");
//	start_timer(hc_timer);
	int	nhc = 0;
	DO_DIFFLIST(hcons, struct dg *hc, nhc++; );
	mrs->hcons = slab_alloc(sizeof(struct mrs_hcons) * nhc);
	DO_DIFFLIST(hcons, struct dg *hc,
		mrs->nhcons++;
		dg_to_mrs_hc(hc, &mrs->vlist, mrs->hcons+mrs->nhcons-1);
	);
//	stop_timer(hc_timer, nhc);
	// give ``mrs'' its own slab allocated vlist
	mrs->vlist.vars = slab_alloc(sizeof(struct mrs_var*) * mrs->vlist.nvars);
	memcpy(mrs->vlist.vars, tmp_vlist, sizeof(struct mrs_var*) * mrs->vlist.nvars);

	//printf("extracted %d / %d vars ; %d / %d spots used in cache with %d collisions\n", nvars_extracted, mrs->vlist.nvars, varhash_used, VHSIZE, varhash_collis);
	return mrs;
}

struct mrs	*extract_mrs(struct dg	*root)
{
	struct dg	*cont = dg_path(root, extract_mrs_path);
	assert(cont || !"extract_mrs: no semantics in given dag!\n");
	//print_dg(cont);
	return cont_to_mrs(cont);
}

// mrs to dag conversion

struct dg	*dagify_mrs_var1(struct mrs_var	*var, struct dg	*skolem)
{
	struct dg	*dg = atomic_dg(var->type);
	int			i, path[16], len;
	for(i=0;i<var->nprops;i++)
	{
		char	*vtype = var->props[i].value, *str = strdup(var->props[i].name), *p = str, *p2;
		len = 0;
		do {
			p2 = strchr(p, '.');
			if(p2)*p2 = 0;
			path[len++] = lookup_fname(p);
			p = p2?(p2+1):0;
		} while(p);
		dg = (struct dg*)add_dg_path(dg, path, len, atomic_dg(vtype));
	}
	if(skolem)dg = add_dg_feature(dg, "INSTLOC", skolem);
	return dg;
}

static void	semi_reformat_mrs_var(struct mrs_var	*var, int	forward)
{
	if(var->semi_dg)return;
	if(var->is_const)
	{
		if(!var->dg)dagify_mrs_var(var, NULL);
		var->semi_dg = var->dg;
		return;
	}
	struct dg	*instloc_dg = NULL;
	if(var->dg)
	{
		struct dg	*internal_dg = var->dg;
		static int	instloc = -1;
		if(instloc == -1)instloc = lookup_fname("INSTLOC");
		instloc_dg = dg_hop(internal_dg, instloc);
	}
	if(!instloc_dg)instloc_dg = atomic_dg(semi_top_type);

	var->semi_dg = dagify_mrs_var1(var, instloc_dg);

	if(var->dg && forward)
		permanent_forward_dg(var->dg, var->semi_dg);
}

static struct dg	*main_atomic_dg(char	*type)
{
	extern struct type_hierarchy	*main_th, *semi_th;
	set_default_type_hierarchy(main_th);
	struct dg	*d = atomic_dg(type);
	set_default_type_hierarchy(semi_th);
	return d;
}

static void semi_reformat_mrs_ep(struct mrs_ep *ep, int	forward)
{
	struct dg	*dg = atomic_dg(semi_top_type);
	int			i;

	dg = add_dg_feature(dg, "PRED", main_atomic_dg(ep->pred));
	dg = add_dg_feature(dg, "LBL", ep->lbl->semi_dg);
	for(i=0;i<ep->nargs;i++)
	{
		if(ep->args[i].value->is_const == 0)
		{
			dg = add_dg_feature(dg, ep->args[i].name, ep->args[i].value->semi_dg);
		}
		else
		{
			struct dg *k = main_atomic_dg(ep->args[i].value->name);
			dg = add_dg_feature(dg, ep->args[i].name, k);
		}
	}
	if(ep->dg && forward)
		permanent_forward_dg(ep->dg, dg);
	ep->dg = dg;
}

void	semi_reformat_mrs(struct mrs	*m, int	forward)
{
	struct type_hierarchy	*tmp = default_type_hierarchy();
	extern struct type_hierarchy	*semi_th;
	set_default_type_hierarchy(semi_th);

	int	i;
	for(i=0;i<m->vlist.nvars;i++)
		semi_reformat_mrs_var(m->vlist.vars[i], forward);
	for(i=0;i<m->nrels;i++)
		semi_reformat_mrs_ep(&m->rels[i], forward);

	set_default_type_hierarchy(tmp);
}

void	dagify_mrs_var(struct mrs_var	*evar, struct dg	*skolem)
{
	struct dg	*dg;
	struct mrs_var *var;

	if(!evar->is_const)
	{
		var = external_to_internal_mrs_var(evar);
		dg = dagify_mrs_var1(var, skolem);
		evar->dg = wellform_dg(dg);
	}
	else
	{
		// dagify a const? hmm.
		char	string[strlen(evar->name)+10];
		sprintf(string, "%.*s", (int)(strlen(evar->name)-2), evar->name+1);
		evar->dg = atomic_dg(get_top()->name);
		evar->dg->type = evar->dg->xtype = temporary_string_type(freeze_string(string));
	}
}

void dagify_mrs_ep(struct mrs_ep *ep)
{
	struct dg	*dg = atomic_dg("relation");
	int			i;

	dg = add_dg_feature(dg, "PRED", atomic_dg(ep->pred));
	dg = add_dg_feature(dg, "LBL", ep->lbl->dg);
	for(i=0;i<ep->nargs;i++)
	{
		if(ep->args[i].value->is_const == 0)
		{
			dg = add_dg_feature(dg, ep->args[i].name, ep->args[i].value->dg);
		}
		else
		{
			struct dg *k = atomic_dg(ep->args[i].value->name);
			dg = add_dg_feature(dg, ep->args[i].name, k);
		}
	}
	ep->dg = wellform_dg(dg);
}

struct dg	*dagify_rels_from_eps(int neps, struct mrs_ep	*eps[])
{
	struct dg	*last = atomic_dg(top_type), *list = last;
	struct dg	*rels = add_dg_feature(atomic_dg(top_type), "LAST", last);
	int			i;

	for(i=neps-1;i>=0;i--)
	{
		struct dg *cons = atomic_dg(top_type);
		cons = add_dg_hop(cons, 2, list);
		list = cons = add_dg_hop(cons, 1, eps[i]->dg);
	}
	return add_dg_feature(rels, "LIST", list);
}
