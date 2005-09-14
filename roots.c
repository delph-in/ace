#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

#include	"tdl.h"
#include	"dag.h"
#include	"freeze.h"
#include	"chart.h"

struct instance
{
	char		*name;
	int			root_flag;
	struct dg	*dg;
}	*instances;
int	ninstances;

char	*use_roots = 0;

struct instances
{
	struct instance	*instances;
	int				ninstances;
};

void	*freeze_instances()
{
	struct instances	*in = slab_alloc(sizeof(struct instances));
	int					i;
	in->instances = slab_alloc(sizeof(struct instance)*ninstances);
	in->ninstances = ninstances;
	for(i=0;i<ninstances;i++)
	{
		in->instances[i].name = freeze_string(instances[i].name);
		in->instances[i].root_flag = instances[i].root_flag;
		in->instances[i].dg = freeze_dg(instances[i].dg);
	}
	return in;
}

void	recover_instances(void	*ptr)
{
	struct instances	*in = ptr;
	instances = in->instances;
	ninstances = in->ninstances;

	if(use_roots)
	{
		int i;
		char *p;
		for(i=0;i<ninstances;i++)instances[i].root_flag = 0;
		for(p=strtok(use_roots, " ");p;p=strtok(0, " "))
		{
			for(i=0;i<ninstances;i++)
				if(!strcasecmp(p, instances[i].name))
					break;
			if(i<ninstances)instances[i].root_flag = (unsigned)-1;
			else
			{
				fprintf(stderr, "roots: no such instance as `%s' available\n", p);
				exit(-1);
			}
		}
	}
}

extern int trace;

int	is_root(struct dg	*dg)
{
	int		i, res, flag = (g_mode == PARSING)?1:2;

	for(i=0;i<ninstances;i++) if(instances[i].root_flag & flag)
	{
		res = unify_dg_tmp(dg, instances[i].dg, -1);
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
	}
	return 0;
}

int	study_instance(char	*lhs, struct dg	*dg)
{
	ninstances++;
	instances = realloc(instances, sizeof(struct instance) * ninstances);
	instances[ninstances-1].name = strdup(lhs);
	instances[ninstances-1].root_flag = 1*check_conf_list("parsing-roots", lhs)
									  | 2*check_conf_list("generation-roots", lhs);
	instances[ninstances-1].dg = dg;
	return 0;
}

int	load_instances()
{
	if(process_tdl_dgs_status("instance", study_instance))
	{
		fprintf(stderr, "instances: unable to load, bailing out.\n");
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
