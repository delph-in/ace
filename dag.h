#ifndef	_DAG_H
#define	_DAG_H

#include	<stdio.h>
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

// basic dag operations
int		unify_dg_infrom(struct dg	*dg1, struct dg	*dg2);
struct dg	*unify_dg(struct dg	*a, struct dg	*b, int	arg);
struct dg	*atomic_dg(char	*name);
struct dg	*copy_dg(struct dg	*d);
struct dg	*trim_args(struct dg	*d);

// for hyperactive parsing
int		unify_dg_tmp(struct dg	*dg1, struct dg	*dg2, int	arg);
struct dg	*finalize_tmp(struct dg	*d);
int		check_cycles(struct dg	*dg);
void		forget_tmp();

// quickcheck operations
struct qc	*extract_qc(struct dg	*d);
struct qc	*extract_qc_arg(struct dg	*d, int	arg);
int		quickcheck(struct qc	*a, struct qc	*b);
int		equiv_quickcheck(struct qc	*a, struct qc	*b);
struct qc	*copy_qc(struct qc	*in);

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

	// align these special cases
	/*if(len==32)
	{
		if((unsigned int)last & 31)
		{
			slabu += 32 - ((unsigned int)last & 31);
			last += 32 - ((unsigned int)last & 31);
		}
	}
	else if(!(len&7))
	{
		if((unsigned int)last & 7)
		{
			slabu += 8 - ((unsigned int)last & 7);
			last += 8 - ((unsigned int)last & 7);
		}
	}*/

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
			fprintf(stderr, "XXX deref cycle\n");
			return 0;
		}
	}
}

static inline struct dg	*dg_hop(struct dg	*d, int	label)
{
	int	i;
	extern int	generation;

	if(d->forward)d = dereference_dg(d);
	if(!d)return 0;

	for(i=0;i<d->narcs;i++)
		if(d->arcs[i].label==label)
		{
			d = d->arcs[i].dest;
			if(d->forward)return dereference_dg(d);
			else return d;
		}
	if(d->gen==generation)
	{
		for(i=0;i<d->ncarcs;i++)
			if(d->carcs[i].label==label)
			{
				d = d->carcs[i].dest;
				if(d->forward)return dereference_dg(d);
				else return d;
			}
	}
	return 0;
}

#endif
