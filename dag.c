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
#endif

int slab_usage() { return (noldslabs-1)*SLAB_SIZE + slabu; }

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
		freeze_next = 0;
	}
	else if(debug_level) { printf("permanent "); print_slab_stats(); }
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
		if(noldslabs)accum_ram_usage += SLAB_SIZE*(noldslabs-1) + slabu;
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
		fprintf(stderr, "slab: needed more room but next spot is in freezer!\n");
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
		while((fname = va_arg(va, char*)) != 0)
			if(dg)dg = dg_hop(dg, save[i++] = lookup_fname(fname));
	}
	else while((fname = va_arg(va, char*)) != 0)
			if(dg)dg = dg_hop(dg, save[i++]);
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
		return 0;
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
	struct darc	*arcs;

	d = dereference_dg(d);
	// find in sorted order
	arcs = DARCS(d);
	for(i=0;i<d->narcs;i++)if(arcs[i].label>=label)break;

	if(i==d->narcs || arcs[i].label>label)
	{
		// need to grow list of arcs
		d->narcs++;
		olen = sizeof(struct dg) + sizeof(struct darc) * (d->narcs-1);
		nlen = sizeof(struct dg) + sizeof(struct darc) * d->narcs;
		dn = slab_realloc(d, olen, nlen);
		if(dn != d)
		{
			// oops! we left the old dg behind, forward it along
			d->gen = 9;
			d->forward = dn;
			d = dn;
			// arcs go directly after the `struct dg'
			arcs = DARCS(d);
		}

		memmove(arcs+i+1, arcs+i, sizeof(struct darc)*(d->narcs-1-i));
	}
	arcs[i].label = label;
	arcs[i].dest = value;

	return d;
}

int		nfnames;
char		**fnames;

int	subsume_forward, subsume_backward;

// make sure 'copy' is either null or self-referential all the way through 'a's arcs
int	subsume_same_dg(struct dg	*a)
{
	int	i;
	extern unsigned int	generation;
	struct darc	*arcs;

	//return 0;
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
		if(!subsume_same_dg(arcs[i].dest))return 0;
	return 1;
}

// on input, assume a and b both have noncurrent generation
// on output, 'copy' pointers are used, so don't try to copy them until next generation
int	subsume_dg1(struct dg	*a, struct dg	*b)
{
	int	i, j;
	extern unsigned int	generation;
	struct darc	*aarcs, *barcs;

	a = p_dereference_dg(a);
	b = p_dereference_dg(b);

	//if(!a || !b)return 0;
	if(a==b)return subsume_same_dg(a);
	if(a->gen!=generation)
	{
		a->forward = a->copy = 0;
		a->carcs = 0; a->ncarcs = 0;
		a->type = a->xtype;
		a->gen = generation;
	}
	if(b->gen!=generation)
	{
		b->forward = b->copy = 0;
		b->carcs = 0; b->ncarcs = 0;
		b->type = b->xtype;
		b->gen = generation;
	}

	if(a->xtype != b->xtype)
	{
		if(!descendent_of(b->xtype->tnum, a->xtype))subsume_forward = 0;
		if(!descendent_of(a->xtype->tnum, b->xtype))subsume_backward = 0;
	}

	if(a->copy == b && b->forward == a)return 1;
	if(a->copy)subsume_forward = 0;
	if(b->forward)subsume_backward = 0;
	if(!subsume_forward && !subsume_backward)return 0;
	//if(a->copy || b->forward)return 0;

	a->copy = b;
	b->forward = a;

	//printf("maybe %p <=> %p\n", a, b);

	aarcs = DARCS(a);
	barcs = DARCS(b);
	if(a->xtype==b->xtype && a->narcs==b->narcs)
	{
		// well-formedness and sortedness conditions guarantee that since
		// types match exactly, arc labels match exactly...
		for(i=0;i<a->narcs;i++)
			if(!subsume_dg1(aarcs[i].dest, barcs[i].dest))return 0;
	}
	else if(subsume_forward && a->narcs>=b->narcs) // b is subtype of a, so has strict superset of features
	{
		for(i=j=0;i<b->narcs;i++)
		{
			if(j>=a->narcs)break;
			if(aarcs[j].label == barcs[i].label)
			{
				if(!subsume_dg1(aarcs[j].dest, barcs[i].dest))return 0;
				j++;
			}
		}
	}
	else if(subsume_backward && b->narcs>=a->narcs) // a is subtype of b, so has strict superset of features
	{
		for(i=j=0;i<a->narcs;i++)
		{
			if(j>=b->narcs)break;
			if(aarcs[i].label == barcs[j].label)
			{
				if(!subsume_dg1(aarcs[i].dest, barcs[j].dest))return 0;
				j++;
			}
		}
	}
	else return 0;
	return 1;
}

int	equivalent_dg(struct dg	*a, struct dg	*b)
{
	int	res;
	extern unsigned int	generation;

	generation++;
	if(generation==MAX_GEN)
	{
		fprintf(stderr, "generation counter overflowed!\n");
		exit(-1);
	}

	subsume_forward = subsume_backward = 1;
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
	wfee = atomic_dg("*top*"); wfee = add_dg_hop(wfee, 1, dg);
	wellform(wfee, "mrs variable");
	return dg_hop(wfee, 1);
}

struct type	*most_general_type_for_dg(struct dg	*d)
{
	int	i, intro = -1, k;
	struct type	*up = 0, *th;
	struct darc	*arcs = DARCS(d);

	for(i=0;i<d->narcs;i++)
	{
		k = introduce[arcs[i].label];
		if(k==-1)
		{
			fprintf(stderr, "ERROR: tried to wellform a dag containing feature `%s', "
					"which was never introduced in the type heirarchy\n", fnames[arcs[i].label]);
			exit(-1);
		}
		th = types[k];
		/*if(k>intro)
		{
			//printf("dag `%s' uses `%s', introduced by %s\n",
					//d->type->name, fnames[arcs[i].label],
					//types[introduce[arcs[i].label]]->name);
			intro = k;
		}*/
		if(!up)up = th;
		else
		{
			struct type *nup;
			nup = glb(up, th);
			if(!up)
			{
				fprintf(stderr, "type inference:  no glb for `%s' and `%s'\n", up->name, th->name);
				exit(-1);
			}
			up = nup;
		}
	}
	//if(intro==-1)return d->type;
	if(!up)
	{
		//if(d->narcs)printf("type inferred for %d features [%s ...] = null\n", d->narcs, fnames[arcs[0].label]);
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
		//printf("type %s introduces feature %s\n", types[type]->name, fnames[label]);
	}
}

int reload_restrictors = 1, restrictor_size = 0;

int	lookup_fname(char	*label)
{
	int	i;
	extern int g_mode;

	for(i=0;i<nfnames;i++)
		if(!strcasecmp(fnames[i], label))return i;
	nfnames++;
	fnames = realloc(fnames, sizeof(char*) * nfnames);
	fnames[i] = strdup(label);
	introduce = realloc(introduce, sizeof(int) * nfnames);
	introduce[i] = -1;
	reload_restrictors = 1;
	if(g_mode != -1) { fprintf(stderr, "WARNING: added new feature `%s' at runtime\n", label); }
	return i;
}

#ifdef	UNIFY_PATHS
void	unify_backtrace()
{
	int	i;

	fflush(stdout);
	fprintf(stdout, "unify failed at path: ");
	for(i=0;i<upathl;i++)fprintf(stdout, "%s ", fnames[upath[i]]);
	if(uglb) fprintf(stderr, "    inside glb constraint for `%s' depth %d\n", uglb->name, upathgl);
	fprintf(stdout, "\n");
	fflush(stdout);
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
	extern int g_mode;
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
