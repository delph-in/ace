// implementation of Tomabechi's SS Quasi-Destructive Unifier

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<assert.h>
#include	"dag.h"
#include	"freeze.h"

extern char	**fnames;
extern char	*unify_restrictor, *copy_restrictor;
void enable_trim(int trim);

extern int	g_loaded;

#define SHOULD_TRIM(label)			(copy_restrictor && copy_restrictor[label])
#define PACKING_RESTRICTOR(label)	(unify_restrictor && unify_restrictor[label])

// global generation counter for invalidating quasi-destructive operations
unsigned int	generation = 10;

// when freezing a grammar, start all copy generations at 10
void	null_generation() { generation = 10; }
struct darc	*realloc_carcs(struct darc	*ptr, int	old, int	new);

#ifdef	UNIFY_PATHS
extern int	upath[1024];
extern int	upathl, upathgl;
extern struct type *uglb;
#endif

#ifdef	COPY_COUNTS
long long copycount, copytraverse;
#endif

long top_copies, top_unifies;

void dagreset_c(struct dg	*d);

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

#ifdef	HAVE_CHECK_CYCLES
int	check_cycles(struct dg	*dg)
{
	int	i;
	struct darc	*arcs;

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

	arcs = DARCS(dg);
	for(i=0;i<dg->narcs;i++)
		if(check_cycles(arcs[i].dest))return -1;
	if(dg->gen == generation)for(i=0;i<dg->ncarcs;i++)
		if(check_cycles(dg->carcs[i].dest))return -1;
	dg->copy = (void*)2;
	return 0;
}
#endif

static struct dg	*copy_dg_no_comp_arcs(struct dg	*dg)
{
	struct dg	*newcopy;
	struct darc	*aptr, *optr;
	int		i;

	dg = p_dereference_dg(dg);
	if(!dg)return 0;

	// ignore mark left by check_cycles() et al
	if((unsigned long)dg->copy<=5)dg->copy = 0;

	if(dg->copy)return dg->copy;

	newcopy = slab_alloc(sizeof(struct dg) + sizeof(struct darc)*dg->narcs);
	dg->copy = newcopy;
	*newcopy = *dg;
	newcopy->type = newcopy->xtype;
	newcopy->copy = 0;
	newcopy->ncarcs = 0;
	newcopy->carcs = 0;
	//newcopy->narcs = dg->narcs;
	if(dg->gen==9){}//newcopy->gen = 9;
	else
	{
		newcopy->forward = 0;
		newcopy->gen = generation;
	}
	aptr = DARCS(newcopy);//slab_alloc(sizeof(struct darc) * dg->narcs);
	optr = DARCS(dg);

	for(i=0;i<dg->narcs;i++)
	{
		aptr->label = optr->label;
		aptr->dest = copy_dg_no_comp_arcs(optr->dest);
		if(!aptr->dest)return 0;
		aptr++;
		optr++;
	}
	return newcopy;
}

static int darc_cmp(const void	*a, const void	*b)
{
	return ((const struct darc*)a)->label - ((const struct darc*)b)->label;
}

int	copypath[1024], copypathp;

static void print_copy_path(char	*name)
{
	int	i;

	fprintf(stderr, "%s", name);
	for(i=0;i<copypathp;i++)
		fprintf(stderr, " %s.%d", fnames[copypath[i]], copypath[i]);
	fprintf(stderr, "\n");
}

static struct dg	*copy_dg_with_comp_arcs_ss(struct dg	*dg_underef, char	*chret)
{
	struct dg	*dg;
	struct darc	xarc, *aptr, *optr;
	struct dg	*newcopy;
	char		forwarded, i, j, ch, st;
	struct slab_state	state;

#ifdef	COPY_COUNTS
	copytraverse++;
#endif
	if(dg_underef->forward)
	{
		dg = dereference_dg(dg_underef);
		if(!dg)return 0;
	}
	else dg = dg_underef;
	forwarded = (dg != dg_underef);

	if(dg->gen != generation)
	{
		dg->copy = 0;
		dg->forward = 0;
		dg->ncarcs = 0;
		dg->carcs = 0;
		dg->type = dg->xtype;
		dg->gen = generation;
		if(!dg->narcs && !FROZEN(dg))goto share;
	}
	else
	{
#ifdef	HAVE_CHECK_CYCLES
		// ignore mark left by check_cycles()
		if((unsigned long)dg->copy<=2)dg->copy = 0;
#endif

		if((unsigned long)dg->copy == 3)	// cycle marker
			return 0;
		if((unsigned long)dg->copy == 5)	// sharable marker
			goto share;

		if(dg->copy && dg->copy->gen == generation)
		{
			*chret = 1;	// if 'copy' is set, a copy was made... so changed.
			return dg->copy;
		}
	}

	if(!dg->narcs && dg->type==dg->xtype && !FROZEN(dg) && !dg->ncarcs)goto share;

	// leaf-types can be shared nomatter what
	//if(dg->xtype->ndescendents==0)goto share;

	// mark that we are inside this node, to detect cycles
	dg->copy = (struct dg*)3;//dg;

	get_slabstate(&state);

	newcopy = slab_alloc(sizeof(struct dg) + sizeof(struct darc)*(dg->narcs+dg->ncarcs));
	//*newcopy = *dg;
	newcopy->xtype = newcopy->type = dg->type;
	newcopy->carcs = 0;
	newcopy->gen = generation;
	newcopy->copy = 0;
	newcopy->ncarcs = 0;
	newcopy->forward = 0;
	newcopy->narcs = dg->narcs + dg->ncarcs;
	aptr = DARCS(newcopy);//slab_alloc(sizeof(struct darc) * newcopy->narcs);
	optr = DARCS(dg);

	st = 0;
	if(!dg->ncarcs)
	{
		for(i=0;i<dg->narcs;i++)
		{
			aptr->label = optr->label;
			if(SHOULD_TRIM(aptr->label))
			{
				newcopy->narcs--;
				optr++;
				st=1;
				continue;
			}
			//fprintf(stderr, "copying primary `%s' arc from %s to %p\n",
			//		fnames[aptr->label], newcopy->type->name, optr->dest);
			ch = 0;

//#define	COPY_PATHS
#ifdef	COPY_PATHS
			copypath[copypathp++] = optr->label;
#endif
			aptr->dest = copy_dg_with_comp_arcs_ss(optr->dest, &ch);
#ifdef	COPY_PATHS
			copypathp--;
#endif

			if(ch)st = 1;
			if(!aptr->dest)return 0;
			aptr++;
			optr++;
		}
	}
	else for(j=0,i=0;i<dg->narcs || j<dg->ncarcs;) // mergesort
	{
		if(j<dg->ncarcs && i<dg->narcs)
		{
			if(optr[(int)i].label < dg->carcs[(int)j].label)
				xarc = optr[(int)i++];
			else xarc = dg->carcs[(int)j++];
		}
		else if(i<dg->narcs)xarc = optr[(int)i++];
		else xarc = dg->carcs[(int)j++];
		aptr->label = xarc.label;
		if(SHOULD_TRIM(aptr->label))
		{
			newcopy->narcs--;
			st=1;
			continue;
		}
		//fprintf(stderr, "copying primary `%s' arc from %s to %p\n",
		//		fnames[aptr->label], newcopy->type->name, xarc.dest);
		ch = 0;

#ifdef	COPY_PATHS
		copypath[copypathp++] = xarc.label;
#endif
		aptr->dest = copy_dg_with_comp_arcs_ss(xarc.dest, &ch);
#ifdef	COPY_PATHS
		copypathp--;
#endif

		if(ch)st = 1;
		if(!aptr->dest)return 0;
		aptr++;
	}
	dg->copy = newcopy;

	if(dg->ncarcs || dg->type!=dg->xtype || st || FROZEN(dg))
	{
		*chret = 1;
#ifdef	COPY_COUNTS
		copycount++;
#ifdef	COPY_PATHS
		print_copy_path("COPY");
#endif
#endif
		return newcopy;
	}
	else
	{
		// slab-free the node we just built, it's guarranteed to be on top
		set_slabstate(&state);
		share:
		// mark this node as sharable in case someone reenters into it
		dg->copy = (struct dg*)5;
		if(forwarded)*chret = 1;	// since node was forwarded, parent must consider it changed, even though we're sharing it
		else *chret = 0;
#ifdef	COPY_PATHS
		print_copy_path("SHARE");
#endif
		return dg;
	}
}

struct dg	*copy_dg_with_comp_arcs(struct dg	*dg)
{
	struct dg	*newcopy;
	struct darc	*aptr, *optr;
	int		i, nnar;

	dg = dereference_dg(dg);
	if(!dg)return 0;

#ifdef	HAVE_CHECK_CYCLES
	// ignore mark left by check_cycles()
	if((unsigned long)dg->copy<=2)dg->copy = 0;
#endif

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
			//fprintf(stderr, "NOTE: cycle\n");
			return 0;
		}
		return dg->copy;
	}

	if(dg->gen == generation)
		nnar = dg->narcs + dg->ncarcs;
	else nnar = dg->narcs;
	newcopy = slab_alloc(sizeof(struct dg) + sizeof(struct darc)*nnar);
	dg->copy = newcopy;
	*newcopy = *dg;
	newcopy->xtype = newcopy->type = (dg->gen==generation)?dg->type:dg->xtype;
	newcopy->gen = generation;
	newcopy->copy = 0;
	newcopy->ncarcs = 0;
	newcopy->carcs = 0;
	newcopy->narcs = nnar;
	aptr = DARCS(newcopy);//slab_alloc(sizeof(struct darc) * newcopy->narcs);
	optr = DARCS(dg);

	// mark that we are inside this node, to detect cycles
	newcopy->forward = (void*)1;
	for(i=0;i<dg->narcs;i++)
	{
		if(PACKING_RESTRICTOR(optr->label))
			{ optr++; newcopy->narcs--; continue; }
		aptr->label = optr->label;
		//fprintf(stderr, "copying primary `%s' arc from %s to %p\n",
		//		fnames[aptr->label], newcopy->type->name, optr->dest);
		aptr->dest = copy_dg_with_comp_arcs(optr->dest);
		if(!aptr->dest)return 0;
		aptr++;
		optr++;
	}
	if(dg->gen == generation)
	{
		for(i=0;i<dg->ncarcs;i++)
		{
			if(PACKING_RESTRICTOR(dg->carcs[i].label))
				{ newcopy->narcs--; continue; }
			aptr->label = dg->carcs[i].label;
			//fprintf(stderr, "copying comp `%s' arc from %s to %p\n",
			//	fnames[aptr->label], newcopy->type->name, dg->carcs[i].dest);
			aptr->dest = copy_dg_with_comp_arcs(dg->carcs[i].dest);
			if(!aptr->dest)return 0;
			aptr++;
		}
		qsort(DARCS(newcopy), newcopy->narcs, sizeof(struct darc), darc_cmp);
	}
	newcopy->forward = 0;
	return newcopy;
}

struct dg	*copy_dg(struct dg	*d)
{
	generation++;
	if(generation==MAX_GEN)
	{
		fprintf(stderr, "ERROR: generation overflowed!\n");
		exit(-1);
	}
	top_copies++;
	return copy_dg_with_comp_arcs(d);
}

// permanent version of unify1 - destroys dg2
int	unify_dg_infrom(struct dg	*dg1, struct dg	*dg2)
{
	int	i, j, k;
	unsigned char	news[256], nn=0;
	struct type	*t, *t1, *t2;
	struct dg	*extra=0;
	struct darc	*arcs1, *arcs2;

	dg1 = dereference_dg(dg1);
	dg2 = dereference_dg(dg2);
	if(!dg1 || !dg2)return -1;

	t1 = (dg1->gen==generation)?dg1->type:dg1->xtype;
	t2 = (dg2->gen==generation)?dg2->type:dg2->xtype;

	if(dg1->copy)dg1->copy = 0;
	if(dg2->copy)dg2->copy = 0;
	if(dg1 == dg2)return 0;

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
			dagreset_c(t->dg);
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
	arcs1 = DARCS(dg1);
	arcs2 = DARCS(dg2);
	for(j=0,i=0;i<dg2->narcs;i++)
	{
		for(;j<dg1->narcs && arcs2[i].label>arcs1[j].label;j++){}
		if(j==dg1->narcs || arcs2[i].label != arcs1[j].label)news[nn++] = i;
		else
		{
#ifdef	UNIFY_PATHS
			upath[upathl++] = arcs2[i].label;
#endif
			if(unify_dg_infrom(arcs1[j].dest, arcs2[i].dest))return -1;
#ifdef	UNIFY_PATHS
			upathl--;
#endif
		}
	}

	// add remaining dg2 arcs to dg1
	if(nn > 0)
	{
		/* XXX quick and dirty replacement for the efficient code below
		dg1->arcs = slab_realloc(dg1->arcs, sizeof(struct darc) * dg1->narcs,
					sizeof(struct darc) * (dg1->narcs + nn));

		for(j=dg1->narcs,k=nn-1;k>=0;k--)
		{
			i = news[k];
			while(j>0 && dg2->arcs[i].label<dg1->arcs[j-1].label)
			{
				dg1->arcs[j+k] = dg1->arcs[j-1];
				j--;
			}
			// add dg2->arcs[i] to dg1->arcs
			dg1->arcs[j+k] = dg2->arcs[i];
		}
		dg1->narcs+=nn;
		*/
		for(k=0;k<nn;k++)
			dg1 = add_dg_hop(dg1, arcs2[news[k]].label, arcs2[news[k]].dest);
		//qsort(dg1->arcs, dg1->narcs, sizeof(struct darc), darc_cmp);
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

static struct dg	*extra_ocons, *extra_substcons, *extra_anti_synsem;
static struct type	*ocons, *substcons, *anti_synsem;

#define		CSDEPTH	10240
struct darc	*carcstack;
int		carcsp, carcsgen;

struct darc	*alloc_carcs(int	n)
{
	struct darc	*ret;

	if(carcsgen != generation) { carcsgen = generation; carcsp = 0; }
	assert(carcsp +n <= CSDEPTH);
	ret = carcstack + carcsp;
	carcsp+=n;
	return ret;
}

struct darc	*realloc_carcs(struct darc	*ptr, int	old, int	new)
{
	struct darc	*nptr;

	if(carcsgen != generation) { carcsgen = generation; carcsp = 0; assert(!old); }
	if(ptr+old == carcstack+carcsp)
	{
		carcsp += new-old;
		return ptr;
	}
	else
	{
		nptr = alloc_carcs(new);
		memcpy(nptr, ptr, old*sizeof(struct darc));
		return nptr;
	}
}

void	setup_carcs_stack()
{
	carcstack = malloc(sizeof(struct darc)*CSDEPTH);
}

void	setup_extra_type_dgs()
{
	clear_slab();
#ifdef	ERG
	ocons = lookup_type("*ocons*");
	extra_ocons = copy_dg(ocons->dg);
	substcons = lookup_type("*substcons*");
	extra_substcons = copy_dg(substcons->dg);
	anti_synsem = lookup_type("anti_synsem");
	extra_anti_synsem = copy_dg(anti_synsem->dg);
#endif
	commit_slab();
}

struct dg	*get_extra_type_dg(struct type	*t)
{
	struct dg	*extra = 0;

	if(t==ocons)
		if(extra_ocons->gen != generation)
			extra = extra_ocons;
	if(t==substcons)
		if(extra_substcons->gen != generation)
			extra = extra_substcons;
	if(t==anti_synsem)
		if(extra_anti_synsem->gen != generation)
			extra = extra_anti_synsem;

	if(extra)
	{
		extra->gen = generation;
		extra->type = extra->xtype;
		extra->ncarcs = 0;
		extra->carcs = 0;
		extra->forward = 0;
		extra->copy = 0;
	}
	else
	{
		dagreset_c(t->dg);
		//fprintf(stderr, "NOTE: extra copy of `%s'\n", t->name);
		extra = copy_dg_no_comp_arcs(t->dg);
		dagreset_c(t->dg);
	}
	return extra;
}

int	unify1(struct dg	*dg1, struct dg	*dg2)
{
	int	i, j, k;
	unsigned char	news[32], nn=0;
	unsigned char	newcs[32], nc=0;
	struct type	*t, *t1, *t2;
	struct dg	*extra=0;
	struct darc	*arcs1, *arcs2;

	if(dg1->forward)dg1 = dereference_dg(dg1);
	if(dg2->forward)dg2 = dereference_dg(dg2);
	if(!dg1 || !dg2)return -1;

	if(dg1->copy)dg1->copy = 0;
	if(dg2->copy)dg2->copy = 0;
	if(dg1 == dg2)return 0;

	if(!FROZEN(dg2))
	{
		register int balance = 0;
		if(FROZEN(dg1))goto swap;
		if(dg2->gen==generation && (dg2->ncarcs || dg2->type!=dg2->xtype))balance += 5;
		if(dg1->gen==generation && (dg1->ncarcs || dg1->type!=dg1->xtype))balance -= 5;
		balance += (7*dg2->narcs)/5;
		balance -= 1*dg1->narcs;
		if(balance>0)
		{
			swap:
			// trick: swap to make dg1 non-frozen if possible
			// otherwise try to make the more specific dag dg1
			// to avoid copying info
			extra = dg1;
			dg1 = dg2;
			dg2 = extra;
			extra = 0;
		}
	}

	t1 = (dg1->gen==generation)?dg1->type:dg1->xtype;
	t2 = (dg2->gen==generation)?dg2->type:dg2->xtype;

	// quasi-destructively unify the four sets of arcs together!

	if(t1 == t2)t = t1;
	else
	{
		t = glb(t1, t2);
		if(!t)
		{
			//printf("no glb for %s and %s\n", t1->name, t2->name);
			return -1;
		}
	}

	if(dg1->gen != generation)
	{
		dg1->ncarcs = 0;
		dg1->carcs = 0;
		dg1->gen = generation;
	}
	dg1->type = t;
	dg1->forward = 0;
	//fprintf(stderr, "type `%s' at ", t->name); unify_backtrace();

	forward_dg(dg2, dg1);

	arcs1 = DARCS(dg1);
	arcs2 = DARCS(dg2);
	// unify dg2's arcs into dg1's arcs
	for(j=0,k=0,i=0;i<dg2->narcs;i++)
	{
		if(k>=dg1->narcs && j>=dg1->ncarcs)break;
#ifdef	UNIFY_PATHS
		upath[upathl++] = arcs2[i].label;
#endif
		for(;k<dg1->narcs && arcs2[i].label > arcs1[k].label;k++) {}
		if(k<dg1->narcs && arcs2[i].label == arcs1[k].label)
		{
			if(unify1(arcs1[k++].dest, arcs2[i].dest))return -1;
		}
		else
		{
			for(;j<dg1->ncarcs && arcs2[i].label > dg1->carcs[j].label;j++) {}
			if(j<dg1->ncarcs && arcs2[i].label == dg1->carcs[j].label)
			{
				if(unify1(dg1->carcs[j++].dest, arcs2[i].dest))return -1;
			}
			else news[nn++] = i;
		}
#ifdef	UNIFY_PATHS
		upathl--;
#endif
	}
	for(;i<dg2->narcs;i++)news[nn++] = i;

	// unify dg2's comps arcs into dg1's arcs
	for(j=0,k=0,i=0;i<dg2->ncarcs;i++)
	{
		if(k>=dg1->narcs && j>=dg1->ncarcs)break;
#ifdef	UNIFY_PATHS
		upath[upathl++] = dg2->carcs[i].label;
#endif
		for(;k<dg1->narcs && dg2->carcs[i].label > arcs1[k].label;k++) {}
		if(k<dg1->narcs && dg2->carcs[i].label == arcs1[k].label)
		{
			if(unify1(arcs1[k++].dest, dg2->carcs[i].dest))return -1;
		}
		else
		{
			for(;j<dg1->ncarcs && dg2->carcs[i].label > dg1->carcs[j].label;j++) {}
			if(j<dg1->ncarcs && dg2->carcs[i].label == dg1->carcs[j].label)
			{
				if(unify1(dg1->carcs[j++].dest, dg2->carcs[i].dest))return -1;
			}
			else newcs[nc++] = i;
		}
#ifdef	UNIFY_PATHS
		upathl--;
#endif
	}
	for(;i<dg2->ncarcs;i++)newcs[nc++] = i;

	// add remaining dg2 arcs and comps arcs to dg1's comps arcs
	if(nn+nc > 0)
	{
		//dg1->carcs = slab_realloc(dg1->carcs, sizeof(struct darc) * dg1->ncarcs,
		//		sizeof(struct darc) * (dg1->ncarcs + nn + nc));
		dg1->carcs = realloc_carcs(dg1->carcs, dg1->ncarcs, dg1->ncarcs + nn + nc);

		//dg1->carcs = realloc(dg1->carcs, sizeof(struct darc) * (dg1->ncarcs + nn + nc));

		// merge-sort
		j=k=0;
		/*if(nn&&nc)while(1)
		{
			if(arcs2[news[j]].label < dg2->carcs[newcs[k]].label)
			{
				dg1->carcs[dg1->ncarcs++] = arcs2[news[j++]];
				if(j==nn)break;
			}
			else
			{
				dg1->carcs[dg1->ncarcs++] = dg2->carcs[newcs[k++]];
				if(k==nc)break;
			}
		}*/
		while(j<nn) dg1->carcs[dg1->ncarcs++] = arcs2[news[j++]];
		while(k<nc) dg1->carcs[dg1->ncarcs++] = dg2->carcs[newcs[k++]];
		if(nn+nc < dg1->ncarcs || (nc&&nn))
		{
			//fprintf(stderr, "NOTE: had to sort %d carcs\n", dg1->ncarcs);
			qsort(dg1->carcs, dg1->ncarcs, sizeof(struct darc), darc_cmp);
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
			extra = get_extra_type_dg(t);
		}
		uglb = extra->xtype;
		//printf("applying glb constraint `%s'\n", uglb->name);
		upathgl = upathl;
		if(unify1(dg1, extra))
		{
			if(!g_loaded)
			{
				fprintf(stderr, "UNIFY: failed additional constraints from glb-type `%s'\n", t->name);
				print_dg(extra); printf("\n");
			}
			return -1;
		}
		uglb = 0;
	}

	return 0;
}

static struct slab_state	tmpstate;

int	unify_dg_tmp(struct dg	*dg1, struct dg	*dg2, int	arg)
{
	struct dg	*to;
	int		i = 0;

	top_unifies++;
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

#ifdef	UNIFY_PATHS
	upathl = 0;
	uglb = 0;
#endif
	return unify1(to, dg1);
}

struct dg	*trim_copy(struct dg	*d)
{
	char	ch = 0;
	enable_trim(1);
	return copy_dg_with_comp_arcs_ss(d, &ch);
}

struct dg	*finalize_tmp(struct dg	*d, int trim)
{
	char		ch = 0;
	struct dg	*res;

	enable_trim(trim);
	res = copy_dg_with_comp_arcs_ss(d, &ch);

	if(!res)set_slabstate(&tmpstate);
	generation++;
	if(generation==MAX_GEN)
	{
		fprintf(stderr, "ERROR: generation overflowed!\n");
		exit(-1);
	}

	top_copies++;
	return res;
}

struct dg	*finalize_tmp_noss(struct dg	*d)
{
	struct dg	*res;

	res = copy_dg_with_comp_arcs(d);
	if(!res)set_slabstate(&tmpstate);
	generation++;
	if(generation==MAX_GEN)
	{
		fprintf(stderr, "ERROR: generation overflowed!\n");
		exit(-1);
	}

	top_copies++;
	return res;
}

void	forget_tmp()
{
	set_slabstate(&tmpstate);
	generation++;
	if(generation==MAX_GEN)
	{
		fprintf(stderr, "ERROR: generation overflowed!\n");
		exit(-1);
	}
}

// main entry point
struct dg	*unify_dg(struct dg	*dg1, struct dg	*dg2, int	arg)
{
	if(!unify_dg_tmp(dg1, dg2, arg))
		return finalize_tmp(dg2, 0);
	else
	{
		forget_tmp();
		return 0;
	}
}

struct dg	*unify_dg_noss(struct dg	*dg1, struct dg	*dg2, int	arg)
{
	if(!unify_dg_tmp(dg1, dg2, arg))
		return finalize_tmp_noss(dg2);
	else
	{
		forget_tmp();
		return 0;
	}
}

static long	marker;

void dagreset_c(struct dg	*d)
{
	int	i;
	struct darc	*arcs;

	d = p_dereference_dg(d);
	if(!d)return;

	d->copy = 0;
	arcs = DARCS(d);
	for(i=0;i<d->narcs;i++)
		dagreset_c(arcs[i].dest);
}

static void dgreset_r(struct dg	*d)
{
	int	i;
	struct darc	*arcs;

	d = p_dereference_dg(d);
	if(!d)return;

	d->ncarcs = 0;
	d->carcs = 0;
	d->copy = 0;
	d->forward = 0;
	d->type = d->xtype;
	d->gen = generation;

	arcs = DARCS(d);
	for(i=0;i<d->narcs;i++)
		dgreset_r(arcs[i].dest);
}

static void dgmark(struct dg	*d)
{
	int	i;
	struct darc	*arcs;

	d = p_dereference_dg(d);
	d->ncarcs++;
	if(d->ncarcs>1)return;

	arcs = DARCS(d);
	for(i=0;i<d->narcs;i++)
		dgmark(arcs[i].dest);
}

extern char	**fnames;

static void	rprint_dg(struct dg	*d)
{
	int	i;
	struct darc	*arcs;

	d = p_dereference_dg(d);

	if(d->copy)
	{
#ifdef	LP64
		printf("<%lld>", (long)d->copy);
#else
		printf("<%ld>", (long)d->copy);
#endif
		return;
	}

	// multi-visited, give it a tag.
	if(d->ncarcs>1)
	{
		d->copy = (struct dg*)(marker++);
#ifdef	LP64
		printf("<%lld>= ", (long)d->copy);
#else
		printf("<%ld>= ", (long)d->copy);
#endif
	}

	//printf("(%p) ", d);
	if(!d->narcs)
	{
		char	*tname = (d->gen==generation?d->type:d->xtype)->name;
		if(tname[0]=='"' && tname[strlen(tname)-1]!='"')
			printf("'%s", tname+1);	// funny types 'foo get internally stored as "foo
		else printf("%s", tname);
	}
	else
	{
		printf("#D[%s", (d->gen==generation?d->type:d->xtype)->name);
		arcs = DARCS(d);
		for(i=0;i<d->narcs;i++)
		{
			printf(" %s: ", fnames[arcs[i].label]);
			rprint_dg(arcs[i].dest);
		}
		printf("]");
	}
}

static void flat_print_dg(struct dg	*d)
{
	int	i;
	struct darc	*arcs;

	d = dereference_dg(d);

	printf("(%p) ", d);
	if(!d->narcs && (d->gen!=generation || !d->ncarcs))
		printf("%s", (d->gen==generation?d->type:d->xtype)->name);
	else
	{
		printf("#D[%s", (d->gen==generation?d->type:d->xtype)->name);
		arcs = DARCS(d);
		for(i=0;i<d->narcs;i++)
		{
			printf(" %s: ", fnames[arcs[i].label]);
			flat_print_dg(arcs[i].dest);
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
	if(generation>=(MAX_GEN-15))
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
