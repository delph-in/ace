#ifndef	_DAG_H
#define	_DAG_H

#include	<stdio.h>
#include	<assert.h>
#include	"type.h"

#define	UNIFY_PATHS

// tomabechi dgs

#define	MAX_GEN	0xffffffff
#define	MAX_GEN_RESET 4000000000UL

//#define	MAX_GEN	0xffff
//#define	MAX_GEN_RESET 55000

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
#define	DARCS(dg)	((struct darc*)(dg+1))
	struct dg	*forward;
	struct darc	*carcs;
	struct dg	*copy;
	unsigned short	narcs, ncarcs;
	unsigned int	gen;
	//unsigned char	narcs, ncarcs;
	//unsigned short	gen;
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
struct dg	*add_dg_hop(struct dg	*d, int	label, struct dg	*value);
struct dg	*add_dg_feature(struct dg	*d, char	*label, struct dg	*value);
struct dg	*add_dg_path(struct dg	*d, int	*path, int	len, struct dg	*target);
int		init_fnames();
int		equivalent_dg(struct dg	*a, struct dg	*b);
void		setup_extra_type_dgs();
void		setup_carcs_stack();
struct dg	*wellform_dg(struct dg	*dg);
struct dg	*walk_dg(struct dg	*dg, int *save, ...);
void		output_lui_dg(struct dg	*dg, char *wtitle);

// internal operations
int			unify1(struct dg	*dg1, struct dg	*dg2);
struct dg	*copy_dg_with_comp_arcs(struct dg	*dg);

// for hyperactive parsing
int		unify_dg_tmp(struct dg	*dg1, struct dg	*dg2, int	arg);
struct dg	*finalize_tmp(struct dg	*d, int trim);
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

	assert(len>=0);

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

	//if(dg->gen != generation && dg->gen!=9)return dg;
	//if((dg->gen==generation || dg->gen==9) && dg->forward && !dg->forward->forward)return dg->forward;

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
	int	i, j, k, l;
	extern unsigned int	generation;
	struct darc	*arcs;

	if(d->forward)d = dereference_dg(d);
	if(!d)return 0;
	arcs = (struct darc*)(d+1);

	for(i=0,j=d->narcs;i<j;)
	{
		k = (i+j)>>1;
		//printf("guess %d: %d\n", k, arcs[k].label);
		l = arcs[k].label;
		if(l==label){d=arcs[k].dest;goto ret;}
		else if(l>label)j = k;
		else i = k+1;
	}
	if(d->gen==generation)
	{
		for(i=0,j=d->ncarcs;i<j;)
		{
			k = (i+j)>>1;
			l = d->carcs[k].label;
			if(l==label){d=d->carcs[k].dest;goto ret;}
			else if(l>label)j = k;
			else i = k+1;
		}
	}
	return 0;
ret:
	if(d->forward)return dereference_dg(d);
	else return d;
}

#endif
