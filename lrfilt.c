#include	<stdio.h>
#include	<stdlib.h>

#include	"type.h"
#include	"rule.h"
#include	"dag.h"

void	build_one_lr_filter(struct type	*lextype)
{
	int		ndg = 0, i, j, k, hei;
	struct dg	**dg = 0, *res;
	int		*height = 0, nfit = 0;
	struct qc	**qc = 0;
	struct qc	*tqc, *rqc[nrules];
	char		fit_rule[nrules];
	struct slab_state	state;

	clear_slab();
	dg = malloc(sizeof(struct dg*));
	height = malloc(sizeof(int));
	qc = malloc(sizeof(struct qc*));
	dg[0] = copy_dg(lextype->dg);
	height[0] = 1;
	qc[0] = extract_qc(dg[0]);
	ndg = 1;
	for(i=0;i<nrules;i++)rqc[i] = extract_qc_arg(rules[i]->dg, 0);

	// for each dg we can build...
	for(i=0;i<ndg;i++)
	{
		//fprintf(stderr, "%d/%d\n", i, ndg);
		// for each rule we can try it in...
		for(j=0;j<nrules;j++)
		{
			hei = height[i] + 1;//rules[j]->ndaughters-1;
			if(hei > 6)continue;
			if(!quickcheck(qc[i], rqc[j]))
			{
				get_slabstate(&state);
				res = unify_dg(dg[i], rules[j]->dg, 0);
			}
			else res=0;
			if(res)
			{
				// trim args so we have a chance of terminating
				res = trim_args(res);
				tqc = extract_qc(res);
				// see if we already know about this
				for(k=0;k<ndg;k++)
					if(!equiv_quickcheck(tqc, qc[k]) && 1!=equivalent_dg(res, dg[k]))// && hei==height[i])
						break;
				if(k<ndg)
				{
					//fprintf(stderr, "(already had, %d/%d)\n", i, ndg);
					set_slabstate(&state);
					continue;
				}
				// we got a new one
				ndg++;
				dg = realloc(dg, sizeof(struct dg*)*ndg);
				height = realloc(height, sizeof(int)*ndg);
				qc = realloc(qc, sizeof(struct qc*)*ndg);
				dg[k] = res;
				height[k] = hei;
				qc[k] = tqc;
				//print_dg(res);printf("\n");
				if(!fit_rule[j])
				{
					fit_rule[j] = 1;
					fprintf(stderr, "lextype `%s' fits in rule `%s'\n", lextype->name, rules[j]->name);
					nfit++;
				}
			}
		}
	}
	clear_slab();
	fprintf(stderr, " -- built a total of %d constituents for %d/%d rules on top of `%s'\n", ndg, nfit, nrules, lextype->name);
}

void	build_lr_filter()
{
	struct type	*n_intr_le = lookup_type("n_intr_le");

	if(!n_intr_le) { fprintf(stderr, "NOTE: lr-filter unable to find n_intr_le type\n"); exit(-1); }
	build_one_lr_filter(n_intr_le);
}
