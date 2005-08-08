#include	<stdio.h>
#include	<stdlib.h>

#include	"tdl.h"
#include	"dag.h"

int		nroots;
struct dg	**roots;

int	is_root(struct dg	*dg)
{
	int		i, res;

	for(i=0;i<nroots;i++)
	{
		res = unify_dg_tmp(dg, roots[i], -1);
		forget_tmp();
		if(!res)return 1;
		//if(unify_dg(dg, roots[i], -1))	// yeah, yeah, this gets picked up by the slab free
		//	return 1;
	}
	return 0;
}

int	study_roots(char	*lhs, struct dg	*dg)
{
	nroots++;
	roots = realloc(roots, sizeof(struct dg*) * nroots);
	roots[nroots-1] = dg;
	return 0;
}

int	load_roots()
{
#ifdef	ERG
	//if(iterate_tdl_dgs("../erg/roots.tdl", study_roots))
	if(process_tdl_dgs_status("root", study_roots))
#else
	if(iterate_tdl_dgs("../smallgrammars/esslli2k-grammar9/roots.tdl", study_roots))
#endif
	{
		fprintf(stderr, "roots: unable to load, bailing out.\n");
		exit(-1);
	}
	return 0;
}
