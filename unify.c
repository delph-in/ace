// implementation of Tomabechi's (not SS yet) Quasi-Destructive Unifier (1992)

#include	<stdio.h>
#include	<stdlib.h>
#include	"dag.h"

extern char	**fnames;
extern int	g_loaded;

// global generation counter for invalidating quasi-destructive operations
unsigned int	generation = 10;

// when freezing a grammar, start all copy generations at 10
void	null_generation() { generation = 10; }

extern int	upath[1024];
extern int	upathl;

static void dagreset_c(struct dg	*d);

static inline void	p_forward_dg(struct dg	*from, struct dg	*to)
{
	from->forward = to;
	from->gen = 9;
	from->ncarcs = 0;
	from->carcs = 0;
}

static inline void	forward_dg(struct dg	*from, struct dg	*to)
{
	from->forward = to;
	if(from->gen != generation)
	{
		from->ncarcs = 0;
		from->carcs = 0;
		from->type = from->xtype;
		from->gen = generation;
	}
}

int	check_cycles(struct dg	*dg)
{
	int	i;

	if(!dg)return -1;
	if(dg->forward)
	{
		dg = dereference_dg(dg);
		if(!dg)return -1;
	}

	if(dg->gen != generation)
	{
		dg->gen = generation;
		dg->forward = 0;
		dg->ncarcs = 0;
		dg->carcs = 0;
		dg->type = dg->xtype;
	}
	else if(dg->copy == (void*)1)return -1;
	else if(dg->copy == (void*)2)return 0;
	dg->copy = (void*)1;

	for(i=0;i<dg->narcs;i++)
		if(check_cycles(dg->arcs[i].dest))return -1;
	if(dg->gen == generation)for(i=0;i<dg->ncarcs;i++)
		if(check_cycles(dg->carcs[i].dest))return -1;
	dg->copy = (void*)2;
	return 0;
}

static struct dg	*copy_dg_no_comp_arcs(struct dg	*dg)
{
	struct dg	*newcopy;
	struct darc	*aptr;
	int		i;

	dg = p_dereference_dg(dg);
	if(!dg)return 0;

	// ignore mark left by check_cycles()
	if((unsigned long)dg->copy<=2)dg->copy = 0;

	if(dg->copy)return dg->copy;

	newcopy = slab_alloc(sizeof(struct dg));
	dg->copy = newcopy;
	*newcopy = *dg;
	newcopy->copy = 0;
	newcopy->type = newcopy->xtype;
	newcopy->ncarcs = 0;
	newcopy->carcs = 0;
	newcopy->aarcs = dg->narcs;
	newcopy->narcs = 0;
	newcopy->gen = generation;
	if(dg->gen==9)newcopy->gen = 9;
	else newcopy->forward = 0;
	aptr = newcopy->arcs = slab_alloc(sizeof(struct darc) * newcopy->aarcs);

	for(i=0;i<dg->narcs;i++)
	{
		aptr->label = dg->arcs[i].label;
		aptr->dest = copy_dg_no_comp_arcs(dg->arcs[i].dest);
		if(!aptr->dest)return 0;
		aptr++;
	}
	newcopy->narcs = newcopy->aarcs;
	return newcopy;
}

static struct dg	*copy_dg_with_comp_arcs_ss(struct dg	*dg_underef, char	*chret)
{
	struct dg	*dg;
	struct darc	*aptr;
	struct dg	*newcopy;
	char		forwarded, i, ch, st;
	struct slab_state	state;

	if(dg_underef->forward)
	{
		dg = dereference_dg(dg_underef);
		if(!dg)return 0;
	}
	else dg = dg_underef;
	forwarded = (dg != dg_underef);

	// ignore mark left by check_cycles()
	if((unsigned long)dg->copy<=2)dg->copy = 0;

	if(dg->gen != generation)
	{
		dg->copy = 0;
		dg->forward = 0;
		dg->ncarcs = 0;
		dg->carcs = 0;
		dg->type = dg->xtype;
		dg->gen = generation;
	}
	if(dg->copy && dg->copy->gen == generation)
	{
		if(dg == dg->copy) // apparently a cycle
			return 0;
		else
		{
			*chret = 1;	// if 'copy' is set, a copy was made... so changed.
			return dg->copy;
		}
	}

	if(!dg->narcs && dg->type==dg->xtype &&
		!(dg>=(struct dg*)0x60000000 && dg<(struct dg*)0x78000000) && !dg->ncarcs)goto share;

	// mark that we are inside this node, to detect cycles
	dg->copy = dg;

	get_slabstate(&state);

	newcopy = slab_alloc(sizeof(struct dg));
	//*newcopy = *dg;
	newcopy->xtype = newcopy->type = dg->type;
	newcopy->class = _complex;
	newcopy->carcs = 0;
	newcopy->gen = generation;
	newcopy->copy = 0;
	newcopy->ncarcs = 0;
	newcopy->forward = 0;
	newcopy->narcs = newcopy->aarcs = dg->narcs + dg->ncarcs;
	aptr = newcopy->arcs = slab_alloc(sizeof(struct darc) * newcopy->aarcs);

	st = 0;
	for(i=0;i<dg->narcs;i++)
	{
		aptr->label = dg->arcs[(int)i].label;
		//fprintf(stderr, "copying primary `%s' arc from %s to %p\n",
		//		fnames[aptr->label], newcopy->type->name, dg->arcs[i].dest);
		ch = 0;
		aptr->dest = copy_dg_with_comp_arcs_ss(dg->arcs[(int)i].dest, &ch);
		if(ch)st = 1;
		if(!aptr->dest)return 0;
		aptr++;
	}
	for(i=0;i<dg->ncarcs;i++)
	{
		aptr->label = dg->carcs[(int)i].label;
		//fprintf(stderr, "copying comp `%s' arc from %s to %p\n",
		//	fnames[aptr->label], newcopy->type->name, dg->carcs[i].dest);
		ch = 0;
		aptr->dest = copy_dg_with_comp_arcs_ss(dg->carcs[(int)i].dest, &ch);
		if(ch)st = 1;
		if(!aptr->dest)return 0;
		aptr++;
	}
	dg->copy = newcopy;

	if(dg->ncarcs || dg->type!=dg->xtype || st ||
		(dg>=(struct dg*)0x60000000 && dg<(struct dg*)0x78000000))	// don't share dg's from the grammar
	{
		*chret = 1;
		return newcopy;
	}
	else
	{
		// slab-free the node we just built, it's guarranteed to be on top
		set_slabstate(&state);
		share:
		dg->copy = 0;
		if(forwarded)*chret = 1;	// since node was forwarded, consider it changed, even though we're sharing it
		else *chret = 0;
		return dg;
	}
}

static struct dg	*copy_dg_with_comp_arcs(struct dg	*dg)
{
	struct dg	*newcopy;
	struct darc	*aptr;
	int		i;

	dg = dereference_dg(dg);
	if(!dg)return 0;

	// ignore mark left by check_cycles()
	if((unsigned long)dg->copy<=2)dg->copy = 0;

	if(dg->gen != generation)
	{
		dg->copy = 0;
		dg->forward = 0;
		dg->ncarcs = 0;
		dg->carcs = 0;
		dg->type = dg->xtype;
		dg->gen = generation;
	}
	else if(dg->copy && dg->copy->gen == generation)
	{
		// we set the copy's 'forward' to be nonnull while this node is still copying
		// this lets us detect a cycle
		if(dg->copy->forward)
		{
			//fprintf(stderr, "XXX cycle\n");
			return 0;
		}
		return dg->copy;
	}

	if(dg->class == _atomic || dg->class == _bottom)
	{
		newcopy = slab_alloc(sizeof(struct dg));
		dg->copy = newcopy;
		*newcopy = *dg;
		newcopy->xtype = newcopy->type = (dg->gen==generation)?dg->type:dg->xtype;
		newcopy->class = dg->class;
		newcopy->gen = generation;
		newcopy->copy = 0;
		newcopy->forward = 0;
		return newcopy;
	}
	else
	{
		newcopy = slab_alloc(sizeof(struct dg));
		dg->copy = newcopy;
		*newcopy = *dg;
		newcopy->xtype = newcopy->type = (dg->gen==generation)?dg->type:dg->xtype;
		newcopy->class = _complex;
		newcopy->gen = generation;
		newcopy->copy = 0;
		newcopy->ncarcs = 0;
		newcopy->carcs = 0;
		if(dg->gen == generation)
			newcopy->aarcs = dg->narcs + dg->ncarcs;
		else newcopy->aarcs = dg->narcs;
		newcopy->narcs = 0;
		aptr = newcopy->arcs = slab_alloc(sizeof(struct darc) * newcopy->aarcs);

		// mark that we are inside this node, to detect cycles
		newcopy->forward = (void*)1;
		for(i=0;i<dg->narcs;i++)
		{
			aptr->label = dg->arcs[i].label;
			//fprintf(stderr, "copying primary `%s' arc from %s to %p\n",
			//		fnames[aptr->label], newcopy->type->name, dg->arcs[i].dest);
			aptr->dest = copy_dg_with_comp_arcs(dg->arcs[i].dest);
			if(!aptr->dest)return 0;
			aptr++;
		}
		if(dg->gen == generation)
		{
			for(i=0;i<dg->ncarcs;i++)
			{
				aptr->label = dg->carcs[i].label;
				//fprintf(stderr, "copying comp `%s' arc from %s to %p\n",
				//	fnames[aptr->label], newcopy->type->name, dg->carcs[i].dest);
				aptr->dest = copy_dg_with_comp_arcs(dg->carcs[i].dest);
				if(!aptr->dest)return 0;
				aptr++;
			}
		}
		newcopy->forward = 0;
		newcopy->narcs = newcopy->aarcs;
		return newcopy;
	}
}

struct dg	*copy_dg(struct dg	*d)
{
	generation++;
	if(generation==0xffffffff)
	{
		fprintf(stderr, "ERROR: generation overflowed!\n");
		exit(-1);
	}
	return copy_dg_with_comp_arcs(d);
}

// permanent version of unify1 - destroys dg2
int	unify_dg_infrom(struct dg	*dg1, struct dg	*dg2)
{
	int	i, j, k;
	unsigned char	news[256], nn=0;
	struct type	*t, *t1, *t2;
	struct dg	*extra=0;

	dg1 = dereference_dg(dg1);
	dg2 = dereference_dg(dg2);
	if(!dg1 || !dg2)return -1;

	t1 = (dg1->gen==generation)?dg1->type:dg1->xtype;
	t2 = (dg2->gen==generation)?dg2->type:dg2->xtype;

	if(dg1->copy)dg1->copy = 0;
	if(dg2->copy)dg2->copy = 0;
	if(dg1 == dg2)return 0;
	else if(dg1->class == _bottom)	// _bottom is precisely when the dg's type is *top*, confusingly :) this means no arcs, too.
	{
		p_forward_dg(dg1, dg2);
		return 0;
	}
	else if(dg2->class == _bottom)
	{
		p_forward_dg(dg2, dg1);
		return 0;
	}
	else if(dg1->class == _atomic && dg2->class == _atomic)	// _atomic dgs are ones whose type is a leaf and has no appropriate features
	{
		if(t1 == t2)
		{
			p_forward_dg(dg2, dg1);
			return 0;
		}
		else return -1;
	}
	else if(dg1->class == _atomic || dg2->class == _atomic)
		return -1;

	// destructively unify

	t = glb(t1, t2);
	if(!t)return -1;

	if(t != t1 && t!= t2 && t->dg && t->dg->narcs)
	{
		//if(t->dg->gen != generation)
		//	extra = t->dg;
		//else
		{
			// it's REALLY obnoxious that we have to do this
			dagreset_c(t->dg);
			extra = copy_dg_with_comp_arcs(t->dg);
		}
	}
	if(dg1->gen != generation)
	{
		dg1->ncarcs = 0;
		dg1->carcs = 0;
	}
	dg1->xtype = dg1->type = t;
	dg1->forward = 0;
	dg1->gen = generation;
	//fprintf(stderr, "type `%s' at ", t->name); unify_backtrace();

	p_forward_dg(dg2, dg1);

	// unify dg2's arcs into dg1's arcs
	// this version of the unifier ignores carcs... for simplicity
	for(i=0;i<dg2->narcs;i++)
	{
		upath[upathl++] = dg2->arcs[i].label;
		for(j=0;j<dg1->narcs;j++)
			if(dg2->arcs[i].label == dg1->arcs[j].label)break;
		if(j==dg1->narcs)news[nn++] = i;
		else if(unify_dg_infrom(dg1->arcs[j].dest, dg2->arcs[i].dest))return -1;
		upathl--;
	}

	// add remaining dg2 arcs to dg1
	if(nn > 0)
	{
		dg1->arcs = slab_realloc(dg1->arcs, sizeof(struct darc) * dg1->narcs,
					sizeof(struct darc) * (dg1->narcs + nn));

		for(k=0;k<nn;k++)
		{
			i = news[k];
			// add dg2->arcs[i] to dg1->arcs
			dg1->arcs[dg1->narcs++] = dg2->arcs[i];
		}
	}

	if(extra && unify_dg_infrom(dg1, extra))
	{
		if(!g_loaded)
		{
			fprintf(stderr, "UNIFY: failed additional constraints from glb-type `%s'\n", extra->type->name);
			print_dg(extra); printf("\n");
		}
		return -1;
	}

	return 0;
}

static int	unify1(struct dg	*dg1, struct dg	*dg2)
{
	int	i, j, k;
	unsigned char	news[256], nn=0;
	unsigned char	newcs[256], nc=0;
	struct type	*t, *t1, *t2;
	struct dg	*extra=0;

	if(dg1->forward)dg1 = dereference_dg(dg1);
	if(dg2->forward)dg2 = dereference_dg(dg2);
	if(!dg1 || !dg2)return -1;

	t1 = (dg1->gen==generation)?dg1->type:dg1->xtype;
	t2 = (dg2->gen==generation)?dg2->type:dg2->xtype;

	if(dg1->copy)dg1->copy = 0;
	if(dg2->copy)dg2->copy = 0;
	if(dg1 == dg2)return 0;
	/*else if(dg1->class == _bottom)	// _bottom is precisely when the dg's type is *top*, confusingly :) this means no arcs, too.
	{
		forward_dg(dg1, dg2);
		return 0;
	}
	else if(dg2->class == _bottom)
	{
		forward_dg(dg2, dg1);
		return 0;
	}
	else if(dg1->class == _atomic && dg2->class == _atomic)	// _atomic dgs are ones whose type is a leaf and has no appropriate features
	{
		if(t1 == t2)
		{
			forward_dg(dg2, dg1);
			return 0;
		}
		else return -1;
	}
	else if(dg1->class == _atomic || dg2->class == _atomic)
		return -1;*/

	// quasi-destructively unify the four sets of arcs together!

	t = glb(t1, t2);
	if(!t)return -1;

	if(dg1->gen != generation)
	{
		dg1->ncarcs = 0;
		dg1->carcs = 0;
	}
	dg1->type = t;
	dg1->forward = 0;
	dg1->gen = generation;
	//fprintf(stderr, "type `%s' at ", t->name); unify_backtrace();

	forward_dg(dg2, dg1);

	// unify dg2's arcs into dg1's arcs
	for(i=0;i<dg2->narcs;i++)
	{
		upath[upathl++] = dg2->arcs[i].label;
		for(j=0;j<dg1->narcs;j++)
			if(dg2->arcs[i].label == dg1->arcs[j].label)break;
		if(j<dg1->narcs)
		{
			if(unify1(dg1->arcs[j].dest, dg2->arcs[i].dest))return -1;
		}
		else
		{
			for(j=0;j<dg1->ncarcs;j++)
				if(dg2->arcs[i].label == dg1->carcs[j].label)break;
			if(j<dg1->ncarcs)
			{
				if(unify1(dg1->carcs[j].dest, dg2->arcs[i].dest))return -1;
			}
			else news[nn++] = i;
		}
		upathl--;
	}

	// unify dg2's comps arcs into dg1's arcs
	for(i=0;i<dg2->ncarcs;i++)
	{
		upath[upathl++] = dg2->carcs[i].label;
		for(j=0;j<dg1->narcs;j++)
			if(dg2->carcs[i].label == dg1->arcs[j].label)break;
		if(j<dg1->narcs)
		{
			if(unify1(dg1->arcs[j].dest, dg2->carcs[i].dest))return -1;
		}
		else
		{
			for(j=0;j<dg1->ncarcs;j++)
				if(dg2->carcs[i].label == dg1->carcs[j].label)break;
			if(j<dg1->ncarcs)
			{
				if(unify1(dg1->carcs[j].dest, dg2->carcs[i].dest))return -1;
			}
			else newcs[nc++] = i;
		}
		upathl--;
	}
	

	// add remaining dg2 arcs and comps arcs to dg1's comps arcs
	// XXX optimization: use a separate slab allocator for comps arcs
	// since we want to free them *every* time after unify
	// otherwise, they sit uselessly on the slab forever

	if(nn+nc > 0)
	{
		dg1->carcs = slab_realloc(dg1->carcs, sizeof(struct darc) * dg1->ncarcs,
				sizeof(struct darc) * (dg1->ncarcs + nn + nc));

		//dg1->carcs = realloc(dg1->carcs, sizeof(struct darc) * (dg1->ncarcs + nn + nc));

		for(k=0;k<nn;k++)
		{
			i = news[k];
			// add dg2->arcs[i] to dg1->carcs
			dg1->carcs[dg1->ncarcs++] = dg2->arcs[i];
		}
		for(k=0;k<nc;k++)
		{
			i = newcs[k];
			// add dg2->carcs[i] to dg1->carcs
			dg1->carcs[dg1->ncarcs++] = dg2->carcs[i];
		}
	}

	if(t != t1 && t!= t2 && t->dg && t->dg->narcs)
	{
		if(t->dg->gen != generation)
		{
			extra = t->dg;
			extra->gen = generation;
			extra->type = extra->xtype;
			extra->ncarcs = 0;
			extra->carcs = 0;
			extra->forward = 0;
			extra->copy = 0;
		}
		else
		{
			// it's REALLY obnoxious that we have to do this
			dagreset_c(t->dg);
			extra = copy_dg_no_comp_arcs(t->dg);
			dagreset_c(t->dg);
		}
		if(unify1(dg1, extra))
		{
			if(!g_loaded)
			{
				fprintf(stderr, "UNIFY: failed additional constraints from glb-type `%s'\n", t->name);
				print_dg(extra); printf("\n");
			}
			return -1;
		}
	}

	return 0;
}

static struct slab_state	tmpstate;

int	unify_dg_tmp(struct dg	*dg1, struct dg	*dg2, int	arg)
{
	struct dg	*to;
	int		i = 0;

	get_slabstate(&tmpstate);

	if(arg>=0)
	{
		to = dg_hop(dg2, 0);	// 0 is guaranteed to be ARGS
		while(to && i<arg)
		{
			to = dg_hop(to, 2);	// REST
			i++;
		}
		if(to)to = dg_hop(to, 1);	// FIRST
		if(!to)
		{
			fprintf(stderr, "unify_dg: couldn't get arg %d position in %s\n", arg, dg2->xtype->name);
			exit(-1);
		}
	}
	else to = dg2;

	upathl = 0;
	return unify1(to, dg1);
}

struct dg	*finalize_tmp(struct dg	*d)
{
	char		ch = 0;
	struct dg	*res = copy_dg_with_comp_arcs_ss(d, &ch);

	if(!res)set_slabstate(&tmpstate);
	generation++;
	if(generation==0xffffffff)
	{
		fprintf(stderr, "ERROR: generation overflowed!\n");
		exit(-1);
	}
	return res;
}

void	forget_tmp()
{
	set_slabstate(&tmpstate);
	generation++;
	if(generation==0xffffffff)
	{
		fprintf(stderr, "ERROR: generation overflowed!\n");
		exit(-1);
	}
}

// main entry point
struct dg	*unify_dg(struct dg	*dg1, struct dg	*dg2, int	arg)
{
	if(!unify_dg_tmp(dg1, dg2, arg))
		return finalize_tmp(dg2);
	else
	{
		forget_tmp();
		return 0;
	}
}

static long	marker;

static void dagreset_c(struct dg	*d)
{
	int	i;

	d = p_dereference_dg(d);
	if(!d)return;

	d->copy = 0;
	for(i=0;i<d->narcs;i++)
		dagreset_c(d->arcs[i].dest);
}

static void dgreset_r(struct dg	*d)
{
	int	i;

	d = dereference_dg(d);
	if(!d)return;

	if(d->gen == generation)return;
	d->ncarcs = 0;
	d->carcs = 0;
	d->copy = 0;
	d->forward = 0;
	if(d->gen != generation)
		d->type = d->xtype;
	d->gen = generation;

	for(i=0;i<d->narcs;i++)
		dgreset_r(d->arcs[i].dest);
}

static void dgmark(struct dg	*d)
{
	int	i;

	d->ncarcs++;
	if(d->ncarcs>1)return;

	for(i=0;i<d->narcs;i++)
		dgmark(d->arcs[i].dest);
}

extern char	**fnames;

static void	rprint_dg(struct dg	*d)
{
	int	i;

	if(d->copy)
	{
		printf("<%lld>", (long)d->copy);
		return;
	}

	// multi-visited, give it a tag.
	if(d->ncarcs>1)
	{
		d->copy = (struct dg*)(marker++);
		printf("<%lld>= ", (long)d->copy);
	}

	//printf("(%p) ", d);
	if(!d->narcs)
		printf("%s", (d->gen==generation?d->type:d->xtype)->name);
	else
	{
		printf("#D[%s", (d->gen==generation?d->type:d->xtype)->name);
		for(i=0;i<d->narcs;i++)
		{
			printf(" %s: ", fnames[d->arcs[i].label]);
			rprint_dg(d->arcs[i].dest);
		}
		printf("]");
	}
}

static void flat_print_dg(struct dg	*d)
{
	int	i;

	d = dereference_dg(d);

	printf("(%p) ", d);
	if(!d->narcs && (d->gen!=generation || !d->ncarcs))
		printf("%s", (d->gen==generation?d->type:d->xtype)->name);
	else
	{
		printf("#D[%s", (d->gen==generation?d->type:d->xtype)->name);
		for(i=0;i<d->narcs;i++)
		{
			printf(" %s: ", fnames[d->arcs[i].label]);
			flat_print_dg(d->arcs[i].dest);
		}
		if(d->gen==generation && d->ncarcs)for(i=0;i<d->ncarcs;i++)
		{
			printf(" /%s: ", fnames[d->carcs[i].label]);
			flat_print_dg(d->carcs[i].dest);
		}
		printf("]");
	}
}

void	print_dg(struct dg	*d)
{
	if(generation>=0xfffffff0)
	{
		fprintf(stderr, "ERROR: generation overflowed!\n");
		exit(-1);
	}
	generation++;
	dgreset_r(d);
	generation++;
	dgmark(d);
	marker = 1;
	rprint_dg(d);
	generation++;
	dgreset_r(d);
	generation++;
}

//char	*trims[] = {ARGS HEAD-DTR NON-HEAD-DTR LCONJ-DTR RCONJ-DTR DTR MOD-DTR NONMOD-DTR};

// clone top node of the dg, and don't include trim'd features
struct dg	*trim_args(struct dg	*d)
{
	int		i, narc = 0;
	struct dg	*copy;

	copy = slab_alloc(sizeof(struct dg));
	*copy = *d;
	for(i=0;i<d->narcs;i++)
		if(!(d->arcs[i].label==0 || (d->arcs[i].label>=5 && d->arcs[i].label<=11)))
			narc++;
	copy->narcs = narc;
	copy->arcs = slab_alloc(sizeof(struct dg) * narc);
	narc = 0;
	for(i=0;i<d->narcs;i++)
	{
		if(!(d->arcs[i].label==0 || (d->arcs[i].label>=5 && d->arcs[i].label<=11)))
			copy->arcs[narc++] = d->arcs[i];
	}
	return copy;
}
