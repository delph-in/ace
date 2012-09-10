#include	<stdarg.h>
#include	<stdlib.h>
#include	<stdio.h>
#include	<string.h>
#include	<assert.h>
#include	<sys/types.h>
#include	<unistd.h>
#include	<sys/mman.h>
#include	"dag.h"
#include	"conf.h"

#define	MAX_FEATURES	(256*256-1)

extern int	debug_level;
extern int	loud_noises;

// monotonic allocator for dags, can be rolled back to cancel a failed unification
#define	SLAB_SIZE	(1024*1024)	// 1MB per slab

void		**oldslabs;
int		noldslabs, aoldslabs;
int		currslabsize;

void	*slab, *last;
int	slabu;

extern void	*freeze_point, *freeze_next;
extern int	freeze_fd;

long long	accum_ram_usage = 0;

#ifdef	UNIFY_PATHS
int	upath[1024];
int	upathl, upathgl;
struct type	*uglb;
struct type	*ut1, *ut2;
#endif

int slab_usage() { return (noldslabs-1)*SLAB_SIZE + slabu; }

int	slab_size_in_bytes()
{
	return (noldslabs-1)*SLAB_SIZE + slabu;
}

void	print_slab_stats()
{
	fprintf(stderr, "RAM: %dk\n", (noldslabs-1)*1024 + slabu/1024);
}

void	next_slab();

void	commit_slab()
{
	if(freeze_next)
	{
		printf("freezing %.1fM to file map %p\n", (float)slabu/1024/1024, freeze_point);
		if(ftruncate(freeze_fd, slabu)) { perror("ftruncate to final length"); exit(-1); }
		if(msync(freeze_point, slabu, MS_ASYNC)) { perror("msync"); exit(-1); }
		close(freeze_fd);
		freeze_next = NULL;
	}
	else if(debug_level) { fprintf(stderr, "permanent "); print_slab_stats(); fprintf(stderr, "\n"); }
	currslabsize = SLAB_SIZE;
	noldslabs = 0;
	aoldslabs = 0;
	next_slab();
}

void	clear_slab()
{
	int	i;

	if(freeze_next)
	{
		noldslabs = 0;
		last = slab = freeze_next;
		slabu = 0;
	}
	else
	{
		if(noldslabs)
		{
			unsigned long long	ram = (unsigned long long)SLAB_SIZE*(noldslabs-1) + slabu;
			accum_ram_usage += ram;
		}
		// leave up to 64 allocated slabs around for next parse!
		if(aoldslabs>64)
		{
			for(i=64;i<aoldslabs;i++)
			{
				free(oldslabs[i]);
				oldslabs[i] = 0;
			}
			aoldslabs = 64;
		}
		noldslabs = 0;
		next_slab();
		currslabsize = SLAB_SIZE;
	}
}

void	next_slab()
{
	if(freeze_next)
	{
		fprintf(stderr, "ran out of room in the freezer; try a larger `freezer-megabytes' value.\n");
		exit(-1);
	}
	noldslabs++;
	if(noldslabs > aoldslabs)
	{
		aoldslabs++;
		if(debug_level>1 && noldslabs>1)fprintf(stderr, "slab: moving to next slab with old at %dkbytes\n", slabu/1024);
		oldslabs = realloc(oldslabs, sizeof(void*)*aoldslabs);
		oldslabs[aoldslabs-1] = malloc(SLAB_SIZE);
	}
	slab = oldslabs[noldslabs-1];
	slabu = 0;
	last = slab;
}

void	*slab_realloc(void	*ptr, int	oldlen, int	newlen)
{
	/*if(ptr == last && slabu -oldlen +newlen < currslabsize)
	{
		slabu += newlen - oldlen;
		return ptr;
	}
	else*/
	{
		void *to = slab_alloc(newlen);
		memcpy(to, ptr, oldlen);
		return to;
	}
}

struct dg	*walk_dg(struct dg	*dg, int *save, ...)
{
	va_list	va;
	int		i = 0;
	char	*fname;

	va_start(va, save);
	if(save[0]==-1)
	{
		while((fname = va_arg(va, char*)) != NULL)
		{
			save[i] = lookup_fname(fname);
			if(dg)dg = dg_hop(dg, save[i]);
			i++;
		}
	}
	else while((fname = va_arg(va, char*)) != NULL)
	{
		if(dg)dg = dg_hop(dg, save[i++]);
	}
	va_end(va);
	return dg;
}

struct dg	*atomic_dg(char	*name)
{
	struct type	*type = lookup_type(name);
	struct dg	*dg;

	if(!type)
	{
		fprintf(stderr, "atomic_dg: no such type as `%s'\n", name);
		exit(-1);
		return NULL;
	}

	dg = slab_alloc(sizeof(struct dg));
	bzero(dg, sizeof(struct dg));
	dg->gen = 10;
	dg->type = dg->xtype = type;
	return dg;
}

struct dg	*add_dg_hop(struct dg	*d, int	label, struct dg	*value)
{
	int	i, olen, nlen;
	struct dg	*dn;
	struct dg	**arcs;

	//printf("add hop [%d -> %p], current arcs:\n", label, value);
	//for(i=0;i<d->narcs;i++)printf("   %d -> %p\n", d->label[i], DARCS(d)[i]);

	d = dereference_dg(d);
	// find in sorted order
	for(i=0;i<d->narcs;i++)if(d->label[i]>=label)break;

	if(i==d->narcs || d->label[i]>label)
	{
		// need to grow list of arcs
		int	oab = DAG_EXTRA_LABELS_SIZE(d);
		d->narcs++;
		int	ab = DAG_EXTRA_LABELS_SIZE(d);
		olen = sizeof(struct dg) + oab + sizeof(struct dg*) * (d->narcs-1);
		nlen = sizeof(struct dg) + ab + sizeof(struct dg*) * d->narcs;
		dn = slab_realloc(d, olen, nlen);
		if(dn != d)
		{
			// oops! we left the old dg behind, forward it along
			d->gen = 9;
			d->forward = dn;
			d = dn;
		}
		// arcs go directly after the `struct dg'
		struct dg	**oarcs;
		arcs = (struct dg**)((void*)d + sizeof(*d) + ab);
		oarcs = (struct dg**)((void*)d + sizeof(*d) + oab);
		memmove(arcs, oarcs, sizeof(struct dg*)*(d->narcs-1));

		memmove(arcs+i+1, arcs+i, sizeof(struct dg*)*(d->narcs-1-i));
		memmove(d->label+i+1, d->label+i, 2*(d->narcs-1-i));
	}
	else arcs = DARCS(d);

	d->label[i] = label;
	arcs[i] = value;

	//printf("now arcs:\n", label, value);
	//for(i=0;i<d->narcs;i++)printf("   %d -> %p\n", d->label[i], DARCS(d)[i]);

	return d;
}

int		nfnames;
char		**fnames;

int	subsume_forward, subsume_backward;

// due to subgraph sharing, a node 'a' can appear in both inputs to subsume_dg1(a,b).
// it is not automatically equivalent; different reentrancies can appear from the (nonidentical) rest of the dags into 'a'
// e.g.:   x = [ A: #1=moo B: #2=moo C: #2=moo ]   and y = [ A: #2=moo B: #2=moo C: #3=moo ]
// we would use subsume_same_dg(#2) and notice that #2 is reentrant with #1 on the left and #3 on the right.
// make sure 'copy' is either null or self-referential all the way through 'a's arcs
int	subsume_same_dg(struct dg	*a)
{
	int	i;
	extern unsigned int	generation;
	struct dg	**arcs;

	//return 0;
	//printf("sssd %p?\n", a);
	a = p_dereference_dg(a);

	if(a->gen!=generation)
	{
		a->forward = a->copy = 0;
		a->carcs = 0; a->ncarcs = 0;
		a->type = a->xtype;
		a->gen = generation;
	}

	if(a->copy==a && a->forward==a)return 1;
	if(a->copy)subsume_forward = 0;
	if(a->forward)subsume_backward = 0;
	if(!subsume_forward && !subsume_backward)return 0;
	//if(a->copy || a->forward)return 0;

	a->copy = a;
	a->forward = a;

	arcs = DARCS(a);
	for(i=0;i<a->narcs;i++)
	{
		upath[upathl++] = a->label[i];
		if(!subsume_same_dg(arcs[i]))return 0;
		upathl--;
	}
	return 1;
}

// on input, assume a and b both have noncurrent generation
// on output, 'copy' pointers are used, so don't try to copy them until next generation
int	subsume_dg1(struct dg	*a, struct dg	*b)
{
	int	i, j;
	extern unsigned int	generation;
	struct dg	**aarcs, **barcs;

	//printf("ssdg1 %p %s [%d] %p %s [%d]?\n", a, a->xtype->name, a->xtype->tnum, b, b->xtype->name, b->xtype->tnum);

	a = p_dereference_dg(a);
	b = p_dereference_dg(b);

	if(a==b)
	{
		int	sssd = subsume_same_dg(a);
		//printf("sssd = %d\n", sssd);
		return sssd;
	}
	if(a->gen!=generation)
	{
		//printf("reset lhs\n");
		a->forward = a->copy = 0;
		a->carcs = 0; a->ncarcs = 0;
		a->type = a->xtype;
		a->gen = generation;
	}
	if(b->gen!=generation)
	{
		//printf("reset rhs\n");
		b->forward = b->copy = 0;
		b->carcs = 0; b->ncarcs = 0;
		b->type = b->xtype;
		b->gen = generation;
	}

	int	types_identical = (a->xtype == b->xtype) || (a->xtype->tnum==-1 && b->xtype->tnum==-1 && !strcmp(a->xtype->name, b->xtype->name));

	if(!types_identical)
	{
		struct type_hierarchy	*th = default_type_hierarchy();
		if(b->xtype->name[0]=='"')
		{
			if(!descendent_of(th->strtype->tnum, a->xtype))subsume_forward = 0;
		}
		else
		{
			if(!descendent_of(b->xtype->tnum, a->xtype))subsume_forward = 0;
		}
		if(a->xtype->name[0]=='"')
		{
			if(!descendent_of(th->strtype->tnum, b->xtype))subsume_backward = 0;
		}
		else
		{
			if(!descendent_of(a->xtype->tnum, b->xtype))subsume_backward = 0;
		}
	}

	if(a->copy == b && b->forward == a)return 1;

	if(!a->copy)a->copy = b;
	else if(a->copy != b)
	{
		subsume_forward = 0;
		//printf("not forward... (lhs = %p)\n", a->copy);
	}

	if(!b->forward)b->forward = a;
	else if(b->forward != a)
	{
		subsume_backward = 0;
		//printf("not backward... (rhs = %p)\n", b->forward);
	}

	if(!subsume_forward && !subsume_backward) { ut1 = a->xtype; ut2 = b->xtype; return 0; }
	//if(a->copy || b->forward)return 0;

	//a->copy = b;
	//b->forward = a;

	//printf("maybe %p [%s] <=> %p [%s]\n", a, a->xtype->name, b, b->xtype->name);

	aarcs = DARCS(a);
	barcs = DARCS(b);
	if(types_identical && a->narcs==b->narcs)
	{
		// well-formedness and sortedness conditions guarantee that since
		// types match exactly, arc labels match exactly...
		for(i=0;i<a->narcs;i++)
		{
			assert(a->label[i] == b->label[i]);
			upath[upathl++] = a->label[i];
			if(!subsume_dg1(aarcs[i], barcs[i]))return 0;
			upathl--;
		}
	}
	else if(subsume_forward && a->narcs<=b->narcs) // b is subtype of a, so has strict superset of features
	{
		for(i=j=0;i<b->narcs;i++)
		{
			if(j>=a->narcs)break;
			if(a->label[j] == b->label[i])
			{
				upath[upathl++] = a->label[j];
				if(!subsume_dg1(aarcs[j], barcs[i]))return 0;
				upathl--;
				j++;
			}
		}
	}
	else if(subsume_backward && b->narcs<=a->narcs) // a is subtype of b, so has strict superset of features
	{
		for(i=j=0;i<a->narcs;i++)
		{
			if(j>=b->narcs)break;
			if(a->label[i] == b->label[j])
			{
				upath[upathl++] = a->label[i];
				if(!subsume_dg1(aarcs[i], barcs[j]))return 0;
				upathl--;
				j++;
			}
		}
	}
	else
	{
		/*printf(" `%s'#%d vs `%s'#%d flags %d %d\n", a->xtype->name, a->narcs, b->xtype->name, b->narcs, subsume_forward, subsume_backward);
		for(i=0;i<a->narcs;i++)printf(" %s", fnames[a->label[i]]); printf("\n");
		for(i=0;i<b->narcs;i++)printf(" %s", fnames[b->label[i]]); printf("\n");
		unify_backtrace();*/
		return 0;
	}
	return 1;
}

int	equivalent_dg(struct dg	*a, struct dg	*b, int Fw, int Bk)
{
	int	res;
	extern unsigned int	generation;

	generation++;
	if(generation==MAX_GEN)
	{
		fprintf(stderr, "generation counter overflowed!\n");
		exit(-1);
	}

	upathl = 0;
	ut1 = ut2 = NULL;
	subsume_forward = Fw;
	subsume_backward = Bk;
	res = subsume_dg1(a, b);
	generation++;
	if(generation==MAX_GEN)
	{
		fprintf(stderr, "generation counter overflowed!\n");
		exit(-1);
	}

	//fprintf(stderr, "subs %p %p = %d %d %d\n", a, b, res, subsume_forward, subsume_backward);

	//if(res && subsume_forward && !subsume_backward)printf("would have subsumed forward\n");
	//else if(res && subsume_backward && !subsume_forward)printf("would have subsumed backward\n");
	/*if(res && subsume_backward && !subsume_forward)
			fprintf(stderr, "NOTE: back-subsumption\n");
	if(res && subsume_forward)
			fprintf(stderr, "NOTE: >= subsumption\n");*/

	if(res)
	{
		//if(subsume_forward && subsume_backward)return 1;
		//else return 0;
		//printf("subs-forward = %d, subs-backward = %d\n", subsume_forward, subsume_backward);
		if(subsume_forward && subsume_backward)return 2;
		if(subsume_forward)return 1;
		if(subsume_backward)return -1;
	}
	return 0;
	//return res;
}

int		*introduce;	// what type introduced this fname?

struct dg	*wellform_dg(struct dg	*dg)
{
	struct dg	*wfee;
	// slight trick: wellform() only applies to stuff inside its argument, not to the top level.
	wfee = atomic_dg(top_type); wfee = add_dg_hop(wfee, 1, dg);
	wellform(wfee, "mrs variable");
	return dg_hop(wfee, 1);
}

struct type	*most_general_type_for_feat(struct type_hierarchy	*th, int	feat)
{
	if(feat<0 || feat>=nfnames)return 0;
	if(introduce[feat] == -1)
	{
		fprintf(stderr, "ERROR: no type is appropriate for feature `%s' yet\n", fnames[feat]);
		exit(-1);
	}
	struct dg	*tdg = th->types[introduce[feat]]->dg, *h = dg_hop(tdg, feat);
	return h->xtype;
}

struct type	*most_general_type_for_dg(struct dg	*d)
{
	struct type_hierarchy	*th = default_type_hierarchy();
	int	i, intro = -1, k;
	struct type	*up = 0, *ty;

	for(i=0;i<d->narcs;i++)
	{
		k = introduce[d->label[i]];
		if(k==-1)
		{
			fprintf(stderr, "ERROR: tried to wellform a dag containing feature `%s', "
					"which was never introduced in the type heirarchy\n", fnames[d->label[i]]);
			exit(-1);
		}
		ty = th->types[k];
		/*if(k>intro)
		{
			//printf("dag `%s' uses `%s', introduced by %s\n",
					//d->type->name, fnames[d->label[i]],
					//th->types[introduce[d->label[i]]]->name);
			intro = k;
		}*/
		if(!up)up = ty;
		else
		{
			struct type *nup;
			nup = glb(up, ty);
			if(!up)
			{
				fprintf(stderr, "type inference:  no glb for `%s' and `%s'\n", up->name, ty->name);
				exit(-1);
			}
			up = nup;
		}
	}
	//if(intro==-1)return d->type;
	if(!up)
	{
		//if(d->narcs)printf("type inferred for %d features [%s ...] = null\n", d->narcs, fnames[d->label[0]]);
		return d->type;
	}
	else
	{
		//printf("type inferred for %d features = %s\n", d->narcs, up->name);
		return glb(d->type, up);
		//return glb(d->type, types[intro]);
		//if(descendent_of(d->type->tnum, types[intro]))return d->type;
		//return types[intro];
	}
}

void	introduce_feature(int	label, int	type)
{
	if(introduce[label]==-1 || introduce[label] > type)
	{
		introduce[label] = type;
		//printf("type %s introduces feature %s\n", default_type_hierarchy()->types[type]->name, fnames[label]);
	}
}

int reload_restrictors = 1, restrictor_size = 0;

int	lookup_fname(char	*label)
{
	int	i;
	extern int g_mode, g_transfer;

	for(i=0;i<nfnames;i++)
		if(!strcasecmp(fnames[i], label))return i;
	nfnames++;
	fnames = realloc(fnames, sizeof(char*) * nfnames);
	fnames[i] = strdup(label);
	introduce = realloc(introduce, sizeof(int) * nfnames);
	introduce[i] = -1;
	if(!g_transfer)
	{
		reload_restrictors = 1;
		if(nfnames >= MAX_FEATURES) { fprintf(stderr, "ERROR: new feature '%s' is %d'th\n", label, MAX_FEATURES); exit(-1); }
		if(g_mode != -1) { fprintf(stderr, "WARNING: added new feature `%s' at runtime\n", label); }
	}
	return i;
}

#ifdef	UNIFY_PATHS
void	unify_backtrace()
{
	int	i;

	fflush(stdout);
	fprintf(stdout, "unify failed at path: ");
	for(i=0;i<upathl;i++)fprintf(stdout, "%s ", fnames[upath[i]]);
	fprintf(stdout, "\n");
	fflush(stdout);
	if(uglb) fprintf(stdout, "    inside glb constraint for `%s' depth %d\n", uglb->name, upathgl);
	if(ut1 && ut2)
		fprintf(stdout, "  might have been trying to unify types %s + %s\n", ut1->name, ut2->name);
}

void	subsume_backtrace()
{
	int	i;

	fflush(stdout);
	fprintf(stdout, "subsumption failed at path: ");
	for(i=0;i<upathl;i++)fprintf(stdout, "%s ", fnames[upath[i]]);
	fprintf(stdout, "\n");
	fflush(stdout);
	if(ut1 && ut2)
		fprintf(stdout, "  types %s + %s\n", ut1->name, ut2->name);
}

struct path	unify_backtrace_path()
{
	struct path p = {.count = upathl, .fnums = upath};
	return p;
}
#endif

struct dg	*add_dg_feature(struct dg	*d, char	*label, struct dg	*value)
{
	return add_dg_hop(d, lookup_fname(label), value);
}

int	init_fnames()
{
	nfnames = 3;
	fnames = malloc(sizeof(char*)*nfnames);
	introduce = malloc(sizeof(int)*nfnames);
	memset(introduce, -1, sizeof(int)*nfnames);

	fnames[0] = strdup("ARGS");
	fnames[1] = strdup("FIRST");
	fnames[2] = strdup("REST");

	return 0;
}

// array saying for each feature whether to drop it during unification / copying
char	*copy_restrictor;
char	*unify_restrictor;

char	*deleted_daughters, *parsing_packing, *generating_packing, *ep_drop;

void print_restrictor(char *name, char *restr)
{
	int i;

	printf("%s:", name);
	for(i=0;i<nfnames;i++)
		if(restr[i])printf(" %s", fnames[i]);
	printf("\n");
}

void load_restrictors()
{
	int		i;
	extern int g_mode, g_transfer;
	if(g_transfer)return;
	assert(g_mode == -1);

	if(deleted_daughters)free(deleted_daughters);
	if(parsing_packing)free(parsing_packing);
	if(generating_packing)free(generating_packing);
	if(ep_drop)free(ep_drop);
	deleted_daughters = calloc(nfnames, 1);
	parsing_packing = calloc(nfnames, 1);
	generating_packing = calloc(nfnames, 1);
	ep_drop = calloc(nfnames, 1);
	for(i=0;i<nfnames;i++)
	{
		deleted_daughters[i] = check_conf_list("deleted-daughters", fnames[i]);
		parsing_packing[i] = check_conf_list("parsing-packing-restrictor", fnames[i]);
		generating_packing[i] = check_conf_list("generation-packing-restrictor", fnames[i]);
		ep_drop[i] = check_conf_list("mrs-deleted-roles", fnames[i]);
	}

	restrictor_size = nfnames;
	reload_restrictors = 0;
}

void	load_ep_drop()
{
	if(reload_restrictors)load_restrictors();
}

void enable_trim(int trim)
{
	if(reload_restrictors)load_restrictors();
	if(trim)copy_restrictor = deleted_daughters;
	else copy_restrictor = 0;
}

void enable_packing(int how)
{
	if(reload_restrictors)load_restrictors();
	if(how==0)unify_restrictor = 0;
	else if(how==1)unify_restrictor = parsing_packing;
	else if(how==2)unify_restrictor = generating_packing;
}

struct dg	*noninline_dg_hop(struct dg	*d, int	f)
{
	return dg_hop(d, f);
}
