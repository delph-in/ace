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

// 32-byte dg structure fits exactly in one standard cacheline
struct dg
{
	struct type	*type;
	struct type	*xtype;
	struct darc	*arcs;
	struct dg	*forward;
	struct darc	*carcs;
	struct dg	*copy;
	unsigned short	narcs, ncarcs;
	unsigned int	gen;
};

// basic dag operations
int		unify_dg_infrom(struct dg	*dg1, struct dg	*dg2);
struct dg	*unify_dg(struct dg	*a, struct dg	*b, int	arg);
struct dg	*atomic_dg(char	*name);
struct dg	*copy_dg(struct dg	*d);
struct dg	*trim_args(struct dg	*d);
int		lookup_fname(char	*label);
void		null_generation();
void		print_dg(struct dg	*d);
void		unify_backtrace();
void		introduce_feature(int	label, int	type);
void		add_dg_hop(struct dg	*d, int	label, struct dg	*value);
void		add_dg_feature(struct dg	*d, char	*label, struct dg	*value);
int		init_fnames();
int		equivalent_dg(struct dg	*a, struct dg	*b);
void		setup_extra_type_dgs();
void		setup_carcs_stack();

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

// slab operations
void	clear_slab();
void	commit_slab();
void	*slab_realloc(void	*ptr, int	oldlen, int	newlen);
void	print_slab_stats();

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

static inline	void	*slab_alloc(int	len) __attribute__ ((malloc));

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
	extern unsigned int	generation;

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
	int	i, j, k;
	extern unsigned int	generation;

	if(d->forward)d = dereference_dg(d);
	if(!d)return 0;

	//printf("looking for %d\n", label);
	//for(i=0;i<d->narcs;i++)printf("a%d: %d\n", i, d->arcs[i].label);
	for(i=0,j=d->narcs;i<j;)
	{
		k = (i+j)>>1;
		//printf("guess %d: %d\n", k, d->arcs[k].label);
		if(d->arcs[k].label==label){d=d->arcs[k].dest;goto ret;}
		else if(d->arcs[k].label>label)j = k;
		else i = k+1;
	}
	/*for(i=0;i<d->narcs;i++)
		if(d->arcs[i].label==label)
		{
			d = d->arcs[i].dest;
			goto ret;
		}*/
	if(d->gen==generation)
	{
		/*for(i=0;i<d->ncarcs;i++)
			if(d->carcs[i].label==label)
			{
				d = d->carcs[i].dest;
				goto ret;
			}*/
		for(i=0,j=d->ncarcs;i<j;)
		{
			k = (i+j)>>1;
			if(d->carcs[k].label==label){d=d->carcs[k].dest;goto ret;}
			else if(d->carcs[k].label>label)j = k;
			else i = k+1;
		}
	}
	return 0;
ret:
	if(d->forward)return dereference_dg(d);
	else return d;
}

#endif
