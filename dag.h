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
struct dg	*dg_hop(struct dg	*d, int	label);
struct dg	*atomic_dg(char	*name);
struct dg	*dereference_dg(struct dg	*dg);
struct dg	*copy_dg(struct dg	*d);

void	*slab_alloc(int	len);

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

#endif
