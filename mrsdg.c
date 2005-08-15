#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<assert.h>

#include	"dag.h"
#include	"mrs.h"

static int name;

static char	*ep_drop[] = {"WLINK", "LBL", "CTO", "CFROM", "PARAMS", "PRED", 0};
static char	*var_drop[] = {"--TPC", "--PSV", "STATIVE", "SORT", "INSTLOC", 0};

// dag to mrs conversion

void add_var_feats(struct mrs_var *v, int label, struct dg *value)
{
	extern char **fnames;
	int i, onp = v->nprops, flen = strlen(fnames[label]);

	for(i=0;var_drop[i];i++)
		if(!strcmp(fnames[label], var_drop[i]))return;

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
		v->props = realloc(v->props, sizeof(struct mrs_var_property)*v->nprops);
		v->props[v->nprops-1].name = strdup(fnames[label]);
		v->props[v->nprops-1].value = strdup(value->xtype->name);
		return;
	}
	// prepend each of the new property names with label
	for(i=onp;i<v->nprops;i++)
	{
		char opn[1024];
		strcpy(opn, v->props[i].name);
		v->props[i].name = realloc(v->props[i].name, strlen(opn) + flen + 2);
		sprintf(v->props[i].name, "%s.%s", fnames[label], opn);
	}
}

struct mrs_var	*dg_to_mrs_var(struct dg	*dg, struct mrs_vlist *vlist)
{
	static struct type *event = 0, *ref_ind, *handle, *individual;

	if(!event)
	{
		event = lookup_type("event");
		ref_ind = lookup_type("ref-ind");
		handle = lookup_type("handle");
		individual = lookup_type("individual");
	}

	if(!dg)return 0;
	if(dg->gen != 1)
	{
		struct darc	*arcs = DARCS(dg);
		struct mrs_var	*var = malloc(sizeof(struct mrs_var));
		char type, str[16];
		extern char **fnames;
		int i;
		if(dg->type == event)type='e';
		else if(dg->type == handle)type='h';
		else if(dg->type == ref_ind)type='x';
		else if(dg->type == individual)type='i';
		else type='u';

		var->dg = dg;
		//sprintf(str, "%c%d", type, ++name[type-'a']); var->name = strdup(str);
		sprintf(str, "%c%d", type, ++name); var->name = strdup(str);
		str[0]=type;str[1]=0; var->type = strdup(str);
		var->props = 0;
		var->nprops = 0;
		for(i=0;i<dg->narcs;i++)
			add_var_feats(var, arcs[i].label, arcs[i].dest);
		var->is_const = 0;
		var->refs = 0;
		var->vnum = vlist->nvars;
		vlist->nvars++;
		vlist->vars = realloc(vlist->vars, sizeof(struct mrs_var*)*vlist->nvars);
		vlist->vars[vlist->nvars-1] = var;
		dg->copy = (void*)var;
		dg->gen = 1;
	}
	((struct mrs_var*)dg->copy)->refs++;
	return (struct mrs_var*)dg->copy;
}

void	dg_to_mrs_ep(struct dg *dg, struct mrs_vlist *vlist, struct mrs_ep	*ep)
{
	struct darc	*arcs = DARCS(dg);
	struct dg		*pred;
	static int		lblf = -1, cfromf, ctof, wlinkf, predf, paramsf;
	int				i, j;

	if(lblf==-1)
	{
		lblf = lookup_fname("LBL");
		predf = lookup_fname("PRED");
		wlinkf = lookup_fname("WLINK");
		cfromf = lookup_fname("CFROM");
		ctof = lookup_fname("CTO");
		paramsf = lookup_fname("PARAMS");
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
		for(j=0;ep_drop[j];j++)
			if(!strcmp(fnames[arcs[i].label], ep_drop[j]))break;
		if(ep_drop[j])continue;
		ep->nargs++;
		ep->args = realloc(ep->args, sizeof(struct mrs_ep_role)*ep->nargs);
		ep->args[ep->nargs-1].name = strdup(fnames[arcs[i].label]);
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
	struct mrs	*mrs = calloc(sizeof(struct mrs), 1);

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

	hook = dg_hop(cont, hookf);
	rels = dg_hop(cont, relsf);
	hcons = dg_hop(cont, hconsf);
	if(hook)
	{
		mrs->ltop = dg_to_mrs_var(dg_hop(hook, ltopf), &mrs->vlist);
		mrs->index = dg_to_mrs_var(dg_hop(hook, indexf), &mrs->vlist);
		mrs->xarg = dg_to_mrs_var(dg_hop(hook, xargf), &mrs->vlist);
	}
	DO_DIFFLIST(rels, struct dg *rel,
		mrs->nrels++;
		mrs->rels = realloc(mrs->rels, sizeof(struct mrs_ep)*mrs->nrels);
		dg_to_mrs_ep(rel, &mrs->vlist, mrs->rels+mrs->nrels-1);
		mrs->rels[mrs->nrels-1].epnum = mrs->nrels-1;
	);
	DO_DIFFLIST(hcons, struct dg *hc,
		mrs->nhcons++;
		mrs->hcons = realloc(mrs->hcons, sizeof(struct mrs_hcons)*mrs->nhcons);
		dg_to_mrs_hc(hc, &mrs->vlist, mrs->hcons+mrs->nhcons-1);
	);
	return mrs;
}

struct mrs	*extract_mrs(struct dg	*root)
{
	static int savef[4] = {-1,-1,-1,-1};
	struct dg	*cont = walk_dg(root, savef, "SYNSEM", "LOCAL", "CONT", NULL);
	assert(cont || !"extract_mrs: no SYNSEM.LOCAL.CONT!\n");
	return cont_to_mrs(cont);
}

// mrs to dag conversion

void	dagify_mrs_var(struct mrs_var	*var, struct dg	*skolem)
{
	struct dg	*dg;
	char		*type = "individual";
	int			i, path[16], len;

	switch(var->type[0]) {
		case	'e': type = "event";			break;
		case	'x': type = "event_or_index";	break;
		case	'u': type = "event_or_index";	break;
		case	'h': type = "handle";			break;
	}

	dg = atomic_dg(type);
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
	if(var->type[0]=='e' || var->type[0]=='u' || var->type[0]=='x')dg = add_dg_feature(dg, "INSTLOC", skolem);
	var->dg = wellform_dg(dg);
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
			if(ep->args[i].value->type[0]!='h')	// XXX maybe some day we'll be allowed to skolemize handles
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

	for(i=0;i<neps;i++)
	{
		struct dg *cons = atomic_dg("*top*");
		cons = add_dg_hop(cons, 2, list);
		list = cons = add_dg_hop(cons, 1, eps[i]->dg);
	}
	return add_dg_feature(rels, "LIST", list);
}
