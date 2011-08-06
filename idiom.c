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

void	skolemize_for_idioms(struct mrs	*m)
{
	int	i;
	static int instloc = -1;
	if(instloc ==-1)instloc = lookup_fname("INSTLOC");

	// tweak the INSTLOC fields of the mrs variables
	// so that they won't unify with each other.

	for(i=0;i<m->vlist.nvars;i++)
	{
		struct dg	*dg = m->vlist.vars[i]->dg;
		if(!dg)continue;	// invented LTOP, INDEX, XARG
		if(m->vlist.vars[i]->is_const)continue;

		/*printf("dag to skolemize for %s%d = \n", m->vlist.vars[i]->type, i);
		print_dg(dg);
		printf("\n");*/

		struct dg	*iloc = dg_hop(dg, instloc);
		assert(iloc != NULL);
		extern int nstrings;
		extern struct type	**strings;
		assert(i<nstrings);
		iloc->type = iloc->xtype = strings[i];
	}
}

void	unskolemize_for_idioms(struct mrs	*m)
{
	int	i;
	static int instloc = -1;
	if(instloc ==-1)instloc = lookup_fname("INSTLOC");

	// tweak the INSTLOC fields of the mrs variables
	// so that they won't unify with each other.

	for(i=0;i<m->vlist.nvars;i++)
	{
		struct dg	*dg = m->vlist.vars[i]->dg;
		if(!dg)continue;	// invented LTOP, INDEX, XARG
		if(m->vlist.vars[i]->is_const)continue;

		/*printf("dag to skolemize for %s%d = \n", m->vlist.vars[i]->type, i);
		print_dg(dg);
		printf("\n");*/

		struct dg	*iloc = dg_hop(dg, instloc);
		assert(iloc != NULL);
		iloc->type = iloc->xtype = get_top();
	}
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

	skolemize_for_idioms(m);

	int	i;
	for(i=0;i<nidiom_rules;i++)
	{
		int found = 0;
		void	found_alignment() { found = 1; }
		align_transfer_rule(idiom_rules[i], m, found_alignment);
		if(found)
		{
			//printf("licensed by idiom rule `%s'\n", idiom_rules[i]->name);
			break;
		}
	}

	unskolemize_for_idioms(m);

	if(i<nidiom_rules)return 1;
	//printf("found a reading that doesn't pass the idiom test\n");
	return 0;
}
