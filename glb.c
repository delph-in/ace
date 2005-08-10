#include	<stdlib.h>
#include	<stdio.h>

#include	"chart.h"
#include	"type.h"

int	glb_ncollis, glb_nhit, glb_nmiss;

#define	HSIZE		32768
#define	ROLLOVER	4

extern int	glb_ncollis, glb_nhit, glb_nmiss;
struct type	*calc_glb(struct type	*a, struct type	*b);

static struct glb_hashent
{
	struct type	*glb, *a, *b;
	int		use;
}	*htab;

void	init_glb_cache()
{
	if(!htab)htab = calloc(sizeof(struct glb_hashent), HSIZE);
}

struct type	*glb(struct type	*a, struct type	*b)
{
	unsigned int	h;
	int		i, j, mi = 0, min = 0x3fff0000;
	register struct glb_hashent	*ht = htab;

	if(a==b)return a;
	if(a>b)
	{
		h = (unsigned int)a;
		a = b;
		b = (struct type*)h;
	}

	h = (((unsigned int)a>>6) ^ ((unsigned int)b>>0)) & (HSIZE-1);

	// check for a,b in 
	for(j=0;j<ROLLOVER;j++)
	{
		i = (h+j) & (HSIZE-1);
		if(ht[i].a==a && ht[i].b==b)
		{
			//ht[i].use++;
			//if(j)glb_ncollis++;
			//else glb_nhit++;
			//goto ret;
			return ht[i].glb;
		}
		if(!ht[i].use)	// this automatically wins
			goto grab;
		if(ht[i].use < min)
		{
			mi = i;
			min = ht[i].use;
		}
	}
	i = mi;
	//i = h;

grab:
	glb_nmiss++;
	ht[i].a = a;
	ht[i].b = b;
	ht[i].use = 1;
	ht[i].glb = calc_glb(a, b);

	ret:
	return ht[i].glb;
}

struct type	*calc_glb(struct type	*a, struct type	*b)
{
	extern struct type	*strtype;
	int	ia, ib;

	if(a->name[0]=='"')
	{
		if(b->name[0]=='"')return 0;
		else if(strtype && descendent_of(strtype->tnum, b))return a;
		else return 0;
	}
	if(b->name[0]=='"')
	{
		if(strtype && descendent_of(strtype->tnum, a))return b;
		else return 0;
	}

	//fprintf(stderr, "glb %s, %s?\n", a->name, b->name);
	ia = ib = 0;
	while(ia < a->ndescendents && ib < b->ndescendents)
	{
		if(a->descendents[ia] == b->descendents[ib])
		{
			// must be the glb, since types are in descending order
			return types[a->descendents[ia]];
		}
		else if(a->descendents[ia] < b->descendents[ib])ia++;
		else ib++;
	}
	//fprintf(stderr, "no glb for %s, %s\n", a->name, b->name);
	return 0;
}

void	print_glb_stats()
{
	//fprintf(stderr, "NOTE: glb hash: %d direct hits, %d collisions, %d misses\n", glb_nhit, glb_ncollis, glb_nmiss);
}
