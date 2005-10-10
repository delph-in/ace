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

// dag to mrs conversion

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

void add_var_feats(struct mrs_var *v, int label, struct dg *value)
{
	extern char **fnames;
	int i, onp = v->nprops, flen = strlen(fnames[label]);

	if(!strcmp(fnames[label], "INSTLOC"))return;

	if(value->narcs)
	{
		struct darc *arcs = DARCS(value);
		// walk inside
		for(i=0;i<value->narcs;i++)
			add_var_feats(v, arcs[i].label, arcs[i].dest);
	}
	else
	{
		v->nprops++;
		v->props = slab_realloc(v->props, sizeof(struct mrs_var_property)*(v->nprops-1),
											sizeof(struct mrs_var_property)*v->nprops);
		v->props[v->nprops-1].name = fnames[label];//strdup(fnames[label]);
		v->props[v->nprops-1].value = strdup(value->xtype->name);
		return;
	}
	// prepend each of the new property names with label
	for(i=onp;i<v->nprops;i++)
		v->props[i].name = mfpath(v->props[i].name, label);
}

#define	VHSIZE	10240
struct mrs_var	*varhash[VHSIZE];
int				varind;
static inline unsigned hashcode(struct dg	*dg) { return ((unsigned)dg>>2) % VHSIZE; }

void	clear_mrs()
{
	int i;

	for(i=0;i<VHSIZE;i++)
	{
		if(varhash[i])
		{
			//varhash[i]->refs = 1;
			//free_mrs_var(varhash[i]);
			varhash[i] = 0;
		}
	}
}

struct mrs_var	*dg_to_mrs_var(struct dg	*dg, struct mrs_vlist *vlist)
{
	static struct type *semarg = 0;

	if(!semarg)
		assert(semarg = lookup_type(semarg_type));

	if(!dg)return 0;

	if(dg->gen != 1 || !dg->copy || ((struct mrs_var*)dg->copy)->dg!=dg)
	{
		struct darc	*arcs = DARCS(dg);
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
			vlist->nvars++;
			vlist->vars = slab_realloc(vlist->vars, sizeof(struct mrs_var*)*(vlist->nvars-1),
													sizeof(struct mrs_var*)*vlist->nvars);
			vlist->vars[vlist->nvars-1] = var;
			dg->copy = (void*)var;
			dg->gen = 1;
			return var;
		}

		var = slab_calloc(sizeof(struct mrs_var), 1);
		if(!descendent_of(dg->type->tnum, semarg)) var->is_const = 1;
		var->dg = dg;
		var->type = strdup(dg->type->name);
		var->name = strdup(dg->type->name);
		var->vnum = vlist->nvars;
		var->refs = 1;
		if(!var->is_const)
		{
			for(i=0;i<dg->narcs;i++)
				add_var_feats(var, arcs[i].label, arcs[i].dest);

			vnew = internal_to_external_mrs_var(var);
			//free_mrs_var(var);
			var = vnew; var->refs = 1;
		}

		vlist->nvars++;
		vlist->vars = slab_realloc(vlist->vars, sizeof(struct mrs_var*)*(vlist->nvars-1),
												sizeof(struct mrs_var*)*vlist->nvars);
		vlist->vars[vlist->nvars-1] = var;
		dg->copy = (void*)var;
		dg->gen = 1;
		//printf("new var %p dg %p\n", var, dg);
		//if(varhash[hc])free_mrs_var(varhash[hc]);
		varhash[hc] = var; var->refs++;
		return var;
	}
	//printf("back to dg %p, think var %p\n", dg, dg->copy);
	((struct mrs_var*)dg->copy)->refs++;
	return (struct mrs_var*)dg->copy;
}

void	dg_to_mrs_ep(struct dg *dg, struct mrs_vlist *vlist, struct mrs_ep	*ep)
{
	struct darc	*arcs = DARCS(dg);
	struct dg		*pred;
	static int		lblf = -1, predf;
	int				i, j;

	if(lblf==-1)
	{
		lblf = lookup_fname("LBL");
		predf = lookup_fname("PRED");
	}
	pred = dg_hop(dg, predf);
	if(!pred)
	{
		fprintf(stderr, "EP dag had no PRED\n");
		exit(-1);
	}
	bzero(ep, sizeof(*ep));
	ep->pred = strdup(pred->xtype->name);;
	ep->dg = dg;
	ep->lbl = dg_to_mrs_var(dg_hop(dg, lblf), vlist);
	for(i=0;i<dg->narcs;i++)
	{
		extern char	**fnames;
		if(arcs[i].label == lblf || arcs[i].label==predf)continue;
		if(ep_drop[arcs[i].label])continue;
		ep->nargs++;
		ep->args = slab_realloc(ep->args, sizeof(struct mrs_ep_role)*(ep->nargs-1),
											sizeof(struct mrs_ep_role)*ep->nargs);
		ep->args[ep->nargs-1].name = freeze_string(fnames[arcs[i].label]);
		ep->args[ep->nargs-1].value = dg_to_mrs_var(arcs[i].dest, vlist);
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

	dagreset_c(cont);

	load_ep_drop();
	hook = dg_hop(cont, hookf);
	rels = dg_hop(cont, relsf);
	hcons = dg_hop(cont, hconsf);
	if(hook)
	{
		mrs->ltop = dg_to_mrs_var(dg_hop(hook, ltopf), &mrs->vlist);
		mrs->index = dg_to_mrs_var(dg_hop(hook, indexf), &mrs->vlist);
		mrs->xarg = dg_to_mrs_var(dg_hop(hook, xargf), &mrs->vlist);
	}
	// XXX come on, just go count them first... slab_realloc is slow.
	DO_DIFFLIST(rels, struct dg *rel,
		mrs->nrels++;
		mrs->rels = slab_realloc(mrs->rels, sizeof(struct mrs_ep)*(mrs->nrels-1),
											sizeof(struct mrs_ep)*mrs->nrels);
		dg_to_mrs_ep(rel, &mrs->vlist, mrs->rels+mrs->nrels-1);
		mrs->rels[mrs->nrels-1].epnum = mrs->nrels-1;
	);
	DO_DIFFLIST(hcons, struct dg *hc,
		mrs->nhcons++;
		mrs->hcons = slab_realloc(mrs->hcons, sizeof(struct mrs_hcons)*(mrs->nhcons-1),
												sizeof(struct mrs_hcons)*mrs->nhcons);
		dg_to_mrs_hc(hc, &mrs->vlist, mrs->hcons+mrs->nhcons-1);
	);
	return mrs;
}

struct mrs	*extract_mrs(struct dg	*root)
{
	struct dg	*cont = dg_path(root, extract_mrs_path);
	assert(cont || !"extract_mrs: no semantics in given dag!\n");
	return cont_to_mrs(cont);
}

// mrs to dag conversion

void	dagify_mrs_var(struct mrs_var	*evar, struct dg	*skolem)
{
	struct dg	*dg;
	struct mrs_var *var;
	int			i, path[16], len;

	var = external_to_internal_mrs_var(evar);
	dg = atomic_dg(var->type);
	for(i=0;i<var->nprops;i++)
	{
		char	*vtype = var->props[i].value, *str = strdup(var->props[i].name), *p = str, *p2;
		len = 0; do
		{
			p2 = strchr(p, '.');
			if(p2)*p2 = 0;
			path[len++] = lookup_fname(p);
			p = p2?(p2+1):0;
		} while(p);
		dg = (struct dg*)add_dg_path(dg, path, len, atomic_dg(vtype));
	}
	dg = add_dg_feature(dg, "INSTLOC", skolem);
	evar->dg = wellform_dg(dg);

	//free_mrs_var(var);
}

void dagify_mrs_ep(struct mrs_ep *ep)
{
	struct dg	*dg = atomic_dg("relation");
	int			i;

	dg = add_dg_feature(dg, "PRED", atomic_dg(ep->pred));
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
	struct dg	*last = atomic_dg("*top*"), *list = last;
	struct dg	*rels = add_dg_feature(atomic_dg("*top*"), "LAST", last);
	int			i;

	for(i=neps-1;i>=0;i--)
	{
		struct dg *cons = atomic_dg("*top*");
		cons = add_dg_hop(cons, 2, list);
		list = cons = add_dg_hop(cons, 1, eps[i]->dg);
	}
	return add_dg_feature(rels, "LIST", list);
}
