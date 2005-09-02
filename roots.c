#include	<stdio.h>
#include	<stdlib.h>

#include	"tdl.h"
#include	"dag.h"
#include	"freeze.h"

int		nroots;
struct dg	**roots;

extern int trace;

int	is_root(struct dg	*dg)
{
	int		i, res;

	for(i=0;i<nroots;i++)
	{
		res = unify_dg_tmp(dg, roots[i], -1);
		forget_tmp();
		if(!res)
			return 1;
#ifdef UNIFY_PATHS
		else if(trace>2)
		{
			printf("not root: ");
			unify_backtrace();
		}
#endif
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
	if(process_tdl_dgs_status("root", study_roots))
	{
		fprintf(stderr, "roots: unable to load, bailing out.\n");
		exit(-1);
	}
	return 0;
}

struct label
{
	char 		*name;
	struct dg	*dg;
};

static struct label_set
{
	struct label	*labels;
	int				nlabels;
}	*lset;

void	*freeze_labels()
{
	struct label_set	*ls = slab_alloc(sizeof(*ls));
	int					i;

	ls->nlabels = lset->nlabels;
	ls->labels = slab_alloc(sizeof(struct label)*ls->nlabels);
	for(i=0;i<ls->nlabels;i++)
	{
		ls->labels[i].name = freeze_string(lset->labels[i].name);
		ls->labels[i].dg = freeze_dg(lset->labels[i].dg);
	}
	return ls;
}

void	recover_labels(void	*ls)
	{ lset = ls; }

char	*label_dag(struct dg	*dg, char *fallback)
{
	int i;

	for(i=0;i<lset->nlabels;i++)
	{
		lset->labels[i].dg->xtype = lset->labels[i].dg->type = lookup_type("*top*");
		int res = unify_dg_tmp(dg, lset->labels[i].dg, -1);
		forget_tmp();
		if(!res)return lset->labels[i].name;
	}

	return fallback;
}

int	study_label(char	*lhs, struct dg	*dg)
{
	char	*name;
	struct dg *lname = dg_hop(dg, lookup_fname("LNAME"));
	if(!lname)
	{
		fprintf(stderr, "label `%s': no LNAME feature!\n", lhs);
		return 0;
	}
	name = lname->xtype->name;

	if(!lset)lset = calloc(sizeof(struct label_set), 1);
	lset->nlabels++;
	lset->labels = realloc(lset->labels, sizeof(struct label) * lset->nlabels);
	lset->labels[lset->nlabels-1].name = name;
	lset->labels[lset->nlabels-1].dg = dg;
	return 0;
}

int	load_labels()
{
	if(process_tdl_dgs_status("parse-node", study_label))
	{
		fprintf(stderr, "parse-nodes: unable to load, bailing out.\n");
		exit(-1);
	}
	return 0;
}
