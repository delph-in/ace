#ifndef	_DAG_H
#define	_DAG_H

#include	"type.h"

// tomabechi dgs

struct darc
{
	int		label;
	struct dg	*dest;
};

enum
{
	_atomic = 1,
	_bottom = 2,
	_complex = 3,
};

// 32-byte dg structure fits exactly in one standard cacheline
struct dg
{
	struct type	*type;
	struct type	*xtype;
	struct darc	*arcs;
	struct darc	*carcs;
	struct dg	*forward;
	struct dg	*copy;
	unsigned char	narcs, ncarcs, aarcs;
	unsigned char	class;	// tomabechi's :atom, :bottom, :complex
	unsigned int	gen;
};

int		unify_dg_infrom(struct dg	*dg1, struct dg	*dg2);
struct dg	*unify_dg(struct dg	*a, struct dg	*b, int	arg);
struct dg	*atomic_dg(char	*name);
struct dg	*copy_dg(struct dg	*d);

struct slab_state
{
	int	noldslabs;
	void	*slab, *last;
	int	slabu;
};

static inline void	get_slabstate(struct slab_state	*s)
{
	extern void	*slab, *last;
	extern int	slabu, noldslabs;
	s->noldslabs = noldslabs;
	s->slab = slab;
	s->last = last;
	s->slabu = slabu;
}

static inline void	set_slabstate(struct slab_state	*s)
{
	extern void	*slab, *last;
	extern int	slabu, noldslabs;
	noldslabs = s->noldslabs;
	slab = s->slab;
	last = s->last;
	slabu = s->slabu;
}

static inline	void	*slab_alloc(int	len)
{
	extern void	*last, *slab;
	extern int	slabu, currslabsize;
	extern void	next_slab();

	if(len>currslabsize)
	{
		fprintf(stderr, "allocation request %d too big for slab\n", len);
		exit(-1);
	}
	if(slabu+len > currslabsize)next_slab();
	last = slab + slabu;
	slabu += len;
	return last;
}

static inline struct dg	*p_dereference_dg(struct dg	*dg)
{
	while(dg && dg->gen==9 && dg->forward)dg = dg->forward;
	return dg;
}

static inline struct dg	*dereference_dg(struct dg	*dg)
{
	struct dg	*fw = dg;
	struct dg	*fw2 = dg;
	extern int	generation;

	while(1)
	{
		// step 'fw' twice
		if(fw->gen==generation || fw->gen==9) { if(fw->forward)fw = fw->forward; else return fw; }
		if(fw->gen==generation || fw->gen==9) { if(fw->forward)fw = fw->forward; else return fw; }
		else return fw;
		
		// step 'dg' once
		dg = dg->forward;
		if(dg == fw)
		{
			//fprintf(stderr, "XXX cycle\n");
			return 0;
		}
	}
}

static inline struct dg	*dg_hop(struct dg	*d, int	label)
{
	int	i;
	extern int	generation;

	d = dereference_dg(d);
	if(!d)return 0;

	for(i=0;i<d->narcs;i++)
		if(d->arcs[i].label==label)
			return dereference_dg(d->arcs[i].dest);
	if(d->gen==generation)
	{
		for(i=0;i<d->ncarcs;i++)
			if(d->carcs[i].label==label)
				return dereference_dg(d->carcs[i].dest);
	}
	return 0;
}

#endif
