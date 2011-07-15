#ifndef	BITVEC_H
#define	BITVEC_H

static inline int	bitvec_and(unsigned long long	*a, unsigned long long	*b, unsigned long long	*r, int	n)
{
	int	i, nz=0;
	for(i=0;i<n;i++)
	{
		r[i] = a[i] & b[i];
		if(r[i])nz=1;
	}
	return nz;
}

static inline int bitvec_subsume(unsigned long long	*x, unsigned long long	*y, int	n)
{
	int	i;
	for(i=0;i<n;i++)
		if((x[i] & y[i]) != y[i])return 0;
	return 1;
}

static inline bitvec_set(unsigned long long	*x, int	bit)
{
	int	w = bit / 64;
	int b = bit % 64;
	x[w] |= ((unsigned long long)1 << b);
}

static inline bitvec_test(unsigned long long	*x, int	bit)
{
	int	w = bit / 64;
	int b = bit % 64;
	return (x[w] & ((unsigned long long)1 << b))?1:0;
}

#endif
