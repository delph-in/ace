#ifndef	BITVEC_H
#define	BITVEC_H

#include	<string.h>

static inline int bitvec_n(int	bits)
{
	return (bits+63)/64;
}

static inline unsigned long long	*bitvec_slab_alloc(int	n)
{
	return slab_alloc(sizeof(unsigned long long)*n);
}

// r = 0
static inline void bitvec_clear_all(unsigned long long	*r, int	n)
{
	bzero(r, sizeof(unsigned long long)*n);
}

// r = a & b
static inline int	bitvec_and(unsigned long long	*a, unsigned long long	*b, unsigned long long	*r, int	n)
{
	int	i, nz=0;
	for(i=0;i<n;i++)
	{
		unsigned long long	R = a[i] & b[i];
		if(r)r[i] = R;
		if(R)nz=1;
	}
	return nz;
}

// r = a & ~b
static inline int	bitvec_andnot(unsigned long long	*a, unsigned long long	*b, unsigned long long	*r, int	n)
{
	int	i, nz=0;
	for(i=0;i<n;i++)
	{
		unsigned long long	R = a[i] & ~b[i];
		if(r)r[i] = R;
		if(R)nz=1;
	}
	return nz;
}

// r = a | b
static inline void	bitvec_or(unsigned long long	*a, unsigned long long	*b, unsigned long long	*r, int	n)
{
	int	i;
	for(i=0;i<n;i++)
		r[i] = a[i] | b[i];
}

// returns 1 if every bit that is 1 in y is also 1 in x
// (i.e. x is a superset of y)
static inline int bitvec_subsume(unsigned long long	*x, unsigned long long	*y, int	n)
{
	int	i;
	for(i=0;i<n;i++)
		if((x[i] & y[i]) != y[i])return 0;
	return 1;
}

// returns 1 if x == y
static inline int bitvec_equal(unsigned long long	*x, unsigned long long	*y, int	n)
{
	int	i;
	for(i=0;i<n;i++)
		if(x[i] != y[i])return 0;
	return 1;
}

// sets a bit in x
static inline bitvec_set(unsigned long long	*x, int	bit)
{
	int	w = bit / 64;
	int b = bit % 64;
	x[w] |= ((unsigned long long)1 << b);
}

// returns the value of a bit in x
static inline bitvec_test(unsigned long long	*x, int	bit)
{
	int	w = bit / 64;
	int b = bit % 64;
	return (x[w] & ((unsigned long long)1 << b))?1:0;
}

static inline int	bit_count(unsigned long long	x)
{
	unsigned int x1 = (unsigned int)x;
	unsigned int x2 = (unsigned int)(x>>32);
	return __builtin_popcount(x1) + __builtin_popcount(x2);
}

static inline int bitvec_three_way_or_count(unsigned long long	*a, unsigned long long	*b, unsigned long long	*c, int	n)
{
	int	i, C = 0;
	for(i=0;i<n;i++)
	{
		unsigned long long	x = a[i] | b[i] | c[i];
		C += bit_count(x);
	}
	return C;
}

#endif
