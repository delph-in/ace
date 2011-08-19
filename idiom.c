#define		_GNU_SOURCE
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<sys/time.h>
#include	<assert.h>

#include	"unicode.h"
#include	"transfer.h"
#include	"freeze.h"
#include	"chart.h"
#include	"dag.h"
#include	"type.h"
#include	"hash.h"
#include	"conf.h"

struct transfer_rule	**idiom_rules;
int						nidiom_rules;

char	*non_idiom_root = NULL;

int	study_idiom_rule(char	*name, struct dg	*dg)
{
	//printf("loading idiom rule '%s'\n", name);
	//printf("	dg type '%s'\n", dg->xtype->name);
	struct transfer_rule	*tr = dg_to_transfer_rule(name, dg);
	if(!tr)return -1;
	idiom_rules = realloc(idiom_rules, sizeof(struct idiom_rule*)*(nidiom_rules+1));
	idiom_rules[nidiom_rules++] = tr;
	return 0;
}

int	is_idiom_ep(char	*ep)
{
	if(strlen(ep) < 6)return 0;
	char	*tail = ep + strlen(ep) - 6;
	if(!strcmp(tail, "_i_rel"))return 1;
	if(!strcmp(tail, "-i_rel"))return 1;
	return 0;
}

int	is_idiom_free(struct mrs	*m)
{
	int	i;
	for(i=0;i<m->nrels;i++)
		if(is_idiom_ep(m->rels[i].pred))return 0;
	return 1;
}

int	check_idioms(struct dg	*dg, struct mrs	*m)
{
	if(non_idiom_root)
	{
		//printf("non_idiom_root = %s\n", non_idiom_root);
		static struct dg	*non_idiom_root_dg = NULL;
		if(!non_idiom_root_dg)
		{
			non_idiom_root_dg = find_instance(non_idiom_root);
			if(!non_idiom_root_dg)
			{
				fprintf(stderr, "runtime error: unable to find instance `%s'\n", non_idiom_root);
				exit(-1);
			}
		}
		int res = unify_dg_tmp(dg, non_idiom_root_dg, -1);
		if(res == 0)return 1;	// no further checking necessary.
	}

	if(!m)m = extract_mrs(dg);

	struct mrs	**results;
	int	n = transfer_mrs(nidiom_rules, idiom_rules, m, &results);
	//printf("%d idiom rule transfer result[s]\n", n);

	int	i;
	for(i=0;i<n;i++)
	{
		//print_mrs(results[i]);
		if(is_idiom_free(results[i]))
		{
			//printf("idiom free.\n");
			return 1;
		}
	}
	return 0;
}
