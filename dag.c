#include	<stdlib.h>
#include	<stdio.h>
#include	<string.h>
#include	<sys/types.h>
#include	<unistd.h>
#include	<sys/mman.h>
#include	"dag.h"

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

int	upath[1024];
int	upathl;

void	print_slab_stats()
{
	fprintf(stderr, "RAM: %dk\n", (noldslabs-1)*1024 + slabu/1024);
}

void	next_slab();

void	commit_slab()
{
	if(freeze_next)
	{
		fprintf(stderr, "freezing %.1fM to file map %p\n", (float)slabu/1024/1024, freeze_point);
		if(ftruncate(freeze_fd, slabu)) { perror("ftruncate to final length"); exit(-1); }
		if(msync(freeze_point, slabu, MS_SYNC)) { perror("msync"); exit(-1); }
		close(freeze_fd);
		freeze_next = 0;
	}
	else if(debug_level) { fprintf(stderr, "permanent: "); print_slab_stats(); }
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
		accum_ram_usage += SLAB_SIZE*(noldslabs-1) + slabu;
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
		slab_alloc(newlen);
		memcpy(last, ptr, oldlen);
		return last;
	}
}

struct dg	*atomic_dg(char	*name)
{
	struct type	*type = lookup_type(name);
	struct dg	*dg;

	if(!type)
	{
		fprintf(stderr, "tdl: no such type as `%s'\n", name);
		return 0;
	}

	dg = slab_alloc(sizeof(struct dg));
	bzero(dg, sizeof(struct dg));
	dg->gen = 10;
	dg->type = dg->xtype = type;
	return dg;
}

static inline void alloc_darcs(struct dg *d, int num, int	anum)
{
	if(d->narcs+num > d->aarcs)
	{
		d->aarcs += anum;
		d->arcs = slab_realloc(d->arcs, sizeof(struct darc) * (d->aarcs-anum), sizeof(struct darc) * d->aarcs);
	}
}

void	add_dg_hop(struct dg	*d, int	label, struct dg	*value)
{
	int	i;

	d = dereference_dg(d);
	for(i=0;i<d->narcs;i++)if(d->arcs[i].label==label)break;
	if(i==d->narcs)
	{
		alloc_darcs(d, 1, 10);
		d->narcs++;
	}
	d->arcs[i].label = label;
	d->arcs[i].dest = value;
}

int		nfnames;
char		**fnames;

// make sure 'copy' is either null or self-referential all the way through 'a's arcs
int	equiv_same_dg(struct dg	*a)
{
	int	i;
	extern int	generation;

	//a = dereference_dg(a);
	if(a->gen!=generation)
	{
		a->forward = a->copy = 0;
		a->carcs = 0; a->ncarcs = 0;
		a->type = a->xtype;
		a->gen = generation;
	}
	if(a->copy==a && a->forward==a)return 1;
	if(a->copy || a->forward)return 0;
	a->copy = a;
	a->forward = a;
	for(i=0;i<a->narcs;i++)
		if(!equiv_same_dg(a->arcs[i].dest))return 0;
	return 1;
}

// on input, assume a and b both have noncurrent generation
// on output, 'copy' pointers are used, so don't try to copy them until next generation
int	equivalent_dg1(struct dg	*a, struct dg	*b)
{
	int	i, j;
	extern int	generation;

	//a = dereference_dg(a);
	//b = dereference_dg(b);
	//if(!a || !b)return 0;
	if(a==b)return equiv_same_dg(a);
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

	if(a->xtype != b->xtype)return 0;
	if(a->narcs != b->narcs)return 0;

	if(a->copy == b && b->forward == a)return 1;
	if(a->copy || b->forward)return 0;

	a->copy = b;
	b->forward = a;

	//printf("maybe %p <=> %p\n", a, b);

	for(i=0;i<a->narcs;i++)
	{
		for(j=0;j<b->narcs;j++)if(b->arcs[j].label==a->arcs[i].label)break;
		if(j>=b->narcs)return 0;
		if(a->arcs[i].label>=12 && a->arcs[i].label<=15)	// STEM, RELS, HCONS, RULE-NAME
			continue;
		//printf("feature %s: host %p pack %p\n", fnames[a->arcs[i].label],
				//a->arcs[i].dest, b->arcs[j].dest);
		if(!equivalent_dg1(a->arcs[i].dest, b->arcs[j].dest))return 0;
	}
	return 1;
}

int	equivalent_dg(struct dg	*a, struct dg	*b)
{
	int	res;
	extern unsigned int	generation;

	/*generation++;
	if(generation==0xffffffff)
	{
		fprintf(stderr, "generation counter overflowed!\n");
		exit(-1);
	}*/
	res = equivalent_dg1(a, b);
	generation++;
	if(generation==0xffffffff)
	{
		fprintf(stderr, "generation counter overflowed!\n");
		exit(-1);
	}
	return res;
}

int		*introduce;	// what type introduced this fname?
extern struct type	**types;

struct type	*most_general_type_for_dg(struct dg	*d)
{
	int	i, intro = -1, k;

	d = dereference_dg(d);
	for(i=0;i<d->narcs;i++)
	{
		k = introduce[d->arcs[i].label];
		if(k>intro)
		{
			//printf("dag `%s' uses `%s', introduced by %s\n",
					//d->type->name, fnames[d->arcs[i].label],
					//types[introduce[d->arcs[i].label]]->name);
			intro = k;
		}
	}
	if(intro==-1)return d->type;
	else
	{
		return glb(d->type, types[intro]);
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

int	lookup_fname(char	*label)
{
	int	i;

	for(i=0;i<nfnames;i++)
		if(!strcasecmp(fnames[i], label))return i;
	nfnames++;
	fnames = realloc(fnames, sizeof(char*) * nfnames);
	fnames[i] = strdup(label);
	introduce = realloc(introduce, sizeof(int) * nfnames);
	introduce[i] = -1;
	return i;
}

void	unify_backtrace()
{
	int	i;

	fflush(stdout);
	fprintf(stderr, "unify failed at path: ");
	for(i=0;i<upathl;i++)fprintf(stderr, "%s ", fnames[upath[i]]);
	fprintf(stderr, "\n");
	fflush(stderr);
}

void	add_dg_feature(struct dg	*d, char	*label, struct dg	*value)
{
	add_dg_hop(d, lookup_fname(label), value);
}

int	init_fnames()
{
	nfnames = 16;
	fnames = malloc(sizeof(char*)*nfnames);
	introduce = malloc(sizeof(int)*nfnames);
	memset(introduce, -1, sizeof(int)*nfnames);

	fnames[0] = strdup("ARGS");
	fnames[1] = strdup("FIRST");
	fnames[2] = strdup("REST");
	fnames[3] = strdup("SEM");
	fnames[4] = strdup("HEAD");

	fnames[5] = strdup("HEAD-DTR");
	fnames[6] = strdup("NON-HEAD-DTR");
	fnames[7] = strdup("LCONJ-DTR");
	fnames[8] = strdup("RCONJ-DTR");
	fnames[9] = strdup("DTR");
	fnames[10] = strdup("MOD-DTR");
	fnames[11] = strdup("NONMOD-DTR");

	fnames[12] = strdup("STEM");
	fnames[13] = strdup("RELS");
	fnames[14] = strdup("HCONS");
	fnames[15] = strdup("RULE-NAME");
	return 0;
}
