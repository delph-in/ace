#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<sys/time.h>

#include	<assert.h>

#include	"hash.h"
#include	"type.h"

static int	shortcomp(const void	*a, const void	*b) { return *(unsigned short*)a - *(unsigned short*)b; }

void	add_to_type_daughters(struct type	*par, struct type	*d)
{
	par->ndaughters++;
	par->daughters = realloc(par->daughters, sizeof(struct type*)*par->ndaughters);
	par->daughters[par->ndaughters-1] = d;
}

void	recompute_type_daughters()
{
	int	i, j;
	for(i=0;i<ntypes;i++)
	{
		struct type	*t = types[i];
		if(t->daughters)free(t->daughters);
		t->daughters = NULL;
		t->ndaughters = 0;
	}
	for(i=0;i<ntypes;i++)
	{
		struct type	*t = types[i];
		for(j=0;j<t->nparents;j++)
			add_to_type_daughters(t->parents[j], t);
	}
	for(i=0;i<ntypes;i++)
	{
		if(!strcmp(types[i]->name, "G(6653)") || !strcmp(types[i]->name, "G(6656)"))
		{
			printf("type %s\n", types[i]->name);
			printf("parents");
			for(j=0;j<types[i]->nparents;j++)
				printf(" %s", types[i]->parents[j]->name);
			printf("\n");
			printf("daughters");
			for(j=0;j<types[i]->ndaughters;j++)
				printf(" %s", types[i]->daughters[j]->name);
			printf("\n");
		}
	}
}

void	clear_descendents_and_daughters()
{
	int i;
	for(i=0;i<ntypes;i++)
	{
		struct type	*t = types[i];
		if(t->descendents)free(t->descendents);
		t->descendents = NULL;
		if(t->daughters)free(t->daughters);
		t->daughters = NULL;
	}
}

void	recompute_descendents(struct type	*t)
{
	if(t->descendents)return;
	int	max = 1, i;
	for(i=0;i<t->ndaughters;i++)
	{
		recompute_descendents(t->daughters[i]);
		max += t->daughters[i]->ndescendents;
	}
	unsigned short	*list = malloc(sizeof(unsigned short)*max);
	int	n = 1, j, k;
	list[0] = t->tnum;

	for(i=0;i<t->ndaughters;i++)
	{
		unsigned short	*dlist = t->daughters[i]->descendents, dn = t->daughters[i]->ndescendents;
		for(j=0;j<dn;j++)
			list[n++] = dlist[j];
		/*{
			for(k=0;k<n;k++)
				if(list[k] == dlist[j])break;
			if(k<n)continue;
			list[n++] = dlist[j];
		}*/
	}
	// sort descendents and remove duplicates
	qsort(list, n, sizeof(short), shortcomp);
	assert(n == max);
	assert(n >= 1);
	for(i=n=1;i<max;i++)
		if(list[i] != list[n-1])
			list[n++] = list[i];

	t->ndescendents = n;
	t->descendents = realloc(list, sizeof(unsigned short) * n);
}

void	recompute_tnums()
{
	int	i,j;

	struct type	**old_type_array;
	old_type_array = malloc(sizeof(struct type*) * ntypes);
	memcpy(old_type_array, types, sizeof(struct type*)*ntypes);

	// perform a topological sort of the type heirarchy,
	// storing the result in the types[] array, back to front.
	char	*have = calloc(1, ntypes);
	struct type	*top = types[0];
	int idx = ntypes-1;
	void tsort(struct type	*t)
	{
		if(have[t->tnum])return;
		int i;
		for(i=0;i<t->ndaughters;i++)
			tsort(t->daughters[i]);
		have[t->tnum] = 1;
		assert(idx >= 0);
		types[idx--] = t;
	}
	tsort(top);
	assert(idx == -1);
	assert(types[0] == top);

/*
	//printf("sorting\n");fflush(stdout);
	// put all the newly built GLB types in their places
	//for(i=ntypes - nglbs_to_add;i<ntypes;i++)
	for(i=1;i<ntypes;i++)
	{
		struct type	*g = old_type_array[i];
		int	mj = 0;
		//printf("looking for where to put type %s\n", g->name);
		for(j=0;j<i;j++)
			if(descendent_of(g->tnum, types[j]))
			{
				//printf("must be after %s\n", types[j]->name);
				mj = j;
			}
		mj++;
		// mj is the first slot in types[] where 'g' could go
		memmove(types+mj+1, types+mj, sizeof(struct type*) * (ntypes - (mj+1)));
		types[mj] = g;
		//printf("decided %d\n", mj);
	}
	*/


	// renumber all the types
	//printf("renumbering\n");fflush(stdout);
	for(i=0;i<ntypes;i++)
	{
		struct type	*T = types[i];//old_type_array[i];
		T->tnum = i;
	}
	// renumber all the descendent arrays
	for(i=0;i<ntypes;i++)
	{
		struct type	*T = types[i];//old_type_array[i];
		for(j=0;j<T->ndescendents;j++)
			T->descendents[j] = old_type_array[T->descendents[j]]->tnum;
		qsort(T->descendents, T->ndescendents, sizeof(short), shortcomp);
	}

	free(old_type_array);
}

// FIXME
// the problem with recompute_descendents(),
// both for the old version of the GLB code and the new version,
// may be that types below a GLB type never get their ->parent[] array updated to include the new GLB type
//  -> the GLB type doesn't see any daughters
//  -> the GLB type doesn't see any descendents
// NOTE: this proved to be the case, for the old code.
//   i.e. after fixing that, a rebuild_heirarchy() after computing GLBs causes no noticeable damage.

// on input, the ->parents[] links must be correct.
// on output, the ->daughters[] links and ->descendents[] links have been recomputed,
// and the types[] array is reordered according to a topological sort.
rebuild_heirarchy(int	report_changes)
{
	unsigned short	**old_d;
	int	i, *old_dn;
	if(report_changes)
	{
		old_d = malloc(sizeof(unsigned short*) * ntypes);
		old_dn = malloc(sizeof(int) * ntypes);
		for(i=0;i<ntypes;i++)
		{
			old_dn[i] = types[i]->ndescendents;
			old_d[i] = types[i]->descendents;
			types[i]->descendents = NULL;
			types[i]->ndescendents = 0;
		}
	}
	clear_descendents_and_daughters();
	recompute_type_daughters();
	recompute_descendents(types[0]);
	if(report_changes)
	{
		for(i=0;i<ntypes;i++)
		{
			if(old_dn[i] != types[i]->ndescendents)
				printf("type '%s': had %d desc, now %d\n", types[i]->name, old_dn[i], types[i]->ndescendents);
		}
	}
	recompute_tnums();
}
