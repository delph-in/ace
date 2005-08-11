#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<assert.h>

#include "mrs.h"
#include "lexicon.h"
#include "chart.h"

extern int	question, exhaustive, lui_chart, inhibit_pack, trace;

/* from erg/lkb/globas.lsp:
(setf *gen-ignore-rules*
  '(punct_bang_rule punct_semicol_rule punct_colon_rule
    punct_rparen_rule punct_lparen_rule punct_dqright_rule punct_dqleft_rule
    punct_sqright_rule punct_sqleft_rule punct_hyphen_rule
    punct_comma_informal_rule)) */

char *gen_ignore_rules[] = {"punct_bang_rule", "punct_semicol_rule", "punct_colon_rule",
							"punct_rparen_rule", "punct_lparen_rule", "punct_dqright_rule", "punct_dqleft_rule",
							"punct_sqright_rule", "punct_sqleft_rule", "punct_hyphen_rule", "punct_comma_informal_rule",
							"fillhead_non_wh" // XXX woodley added
							};
int ngen_ignore_rules = sizeof(gen_ignore_rules) / sizeof(char*);

unsigned int	require_ep_span = 0;

int	compatible_rels(char	*a, char	*b)
{
	//return !strcmp(a,b);
	struct type	*ta = lookup_type(a), *tb = lookup_type(b), *g = glb(ta, tb);
	if(g==ta || g==tb)return 1;
	else return 0;
}

int	gen_process_edge(struct edge	*e)
{
	struct chart_cell	*c = cells;
	int	i, pack = 0;

	if(!inhibit_pack && e->have == e->need && !e->lex)
	{
		pack = pack_edge(e);
		if(pack==1)
			return 0;	// big savings! woohoo!
	}
	if(pack==0)add_edge_to_chart(e);

	// try to combine against any other edge with disjoint ep_span
	if(e->have == e->need)
	{
		// passive edge
		for(i=0;i<c->al_nedges;i++)
			if(! (e->ep_span & c->al_edges[i]->ep_span))
				if(combine(c->al_edges[i], e) == -1)return -1;
		for(i=0;i<c->ar_nedges;i++)
			if(! (e->ep_span & c->ar_edges[i]->ep_span))
				if(combine(c->ar_edges[i], e) == -1)return -1;
	}
	else
	{
		// active edge
		for(i=0;i<c->p_nedges;i++)
			if(! (e->ep_span & c->p_edges[i]->ep_span))
				if(combine(e, c->p_edges[i]) == -1)return -1;
	}
	return 0;
}

int	generate(struct mrs	*mrs)
{
	extern struct edge *sentential;
	struct edge	*edge;
	char		*str;
	int			i, count = 0;

	struct dg	**sk;

	clear_chart(1);

	printf("trying to generate from MRS:\n");
	print_mrs(mrs); printf("\n");

	// set up the skolem constants we'll need.
	sk = malloc(sizeof(struct dg*) * mrs->vlist.nvars);
	for(i=0;i<mrs->vlist.nvars;i++)
	{
		extern struct type	**strings;
		sk[i] = atomic_dg("*top*");
		sk[i]->type = sk[i]->xtype = strings[i];
	}

	// set up some lexemes for the generator to play with
	// XXX should refuse to add a lexeme multiple times for the same input EP
	require_ep_span = 0;
	for(i=0;i<mrs->nrels;i++)
	{
		struct mrs_ep *p = mrs->rels+i;
		int skolem(struct edge	*e, struct lexeme *lex)
		{
			// XXX for multi-rel lexemes, need to add a copy for *every* alignment of its rels to the input rels.
			struct dg	*dag = e->dg;
			extern void	skolemize_rel_as(struct dg	*rel, struct dg	*a0, struct dg	*a1, struct dg	*a2, struct dg	*a3);
			// check whether this lexeme has any extra EPs that we can't use
			static int	savef[10] = {-1,-1,-1,-1,-1,-1,-1,-1};
			struct dg	*rels = walk_dg(dag, savef, "SYNSEM", "LOCAL", "CONT", "RELS", "LIST", 0), *rel = rels?dg_hop(rels, 1):0;
			static int predf = -1;
			//printf("    consider `%s'\n", lex->word);
			if(predf==-1)predf = lookup_fname("PRED");
			while(rel)
			{
				struct dg *pred = dg_hop(rel, lookup_fname("PRED"));
				char	*predn = pred?pred->xtype->name:0;
				if(predn && compatible_rels(predn, p->pred))
				{
					printf(" skolemized `%s' relation of `%s'\n", predn, lex->word);
					skolemize_rel(rel, p, &mrs->vlist, sk);
				}
				else if(predn)
				{
					int j;
					for(j=0;j<mrs->nrels;j++) if(i!=j)
						if(compatible_rels(predn, mrs->rels[j].pred))
						{
							struct mrs *m = mrs;
							printf(" `%s' is compatible with `%s'\n",
										predn, m->rels[j].pred);
							break;
						}
					if(j==mrs->nrels)return -1;	// this relation isn't in the input semantics
				}
				rels = dg_hop(rels, 2);
				rel = rels?dg_hop(rels, 1):0;
			}
			printf("GEN-add `%s'\n", lex->word);
			return 0;
		}
		printf(" -- lexical lookup for relation `%s'\n", p->pred);
		int nlex = add_lexemes_for_relation(i, p->pred, skolem);
		if(!nlex)
		{
			printf("NOTE: no lexemes provide acceptable relation `%s'\n", p->pred);
			printf("NOTE: we hope it's in some rule\n");
			//return -1;
		}
		else require_ep_span |= (1 << i);
	}
	if(mrs->nrels > 32)
	{
		fprintf(stderr, "NOTE: WARNING: generator currently only supports up to 32 EPs\n");
		return -1;
	}

	// add vacuous lexemes which trigger rules specify
	int trigger(struct edge	*e, struct lexeme *lex)
	{
		int	i;
		for(i=0;i<mrs->ntriggers;i++)
		{
			if(!strcmp(lex->word, mrs->triggers[i]))
			{
				printf("GEN-add `%s'\n", lex->word);
				e->ep_span = 0;
				return 0;
			}
		}
		return -1;
	}
	add_lexemes_for_relation(-1, "vacuous", trigger);

	for(i=0;i<mrs->nrels;i++)
	{
		struct mrs_ep	*p = mrs->rels+i;
		int rskolem(struct edge *e, struct rule *rule)
		{
			struct dg	*dag = e->dg;
			static int savef[] = {-1,-1,-1,-1};
			struct dg	*rels = walk_dg(dag, savef, "C-CONT", "RELS", "LIST", 0), *rel = rels?dg_hop(rels, 1):0;
			static int predf = -1;
			//printf("    consider `%s'\n", lex->word);
			if(predf==-1)predf = lookup_fname("PRED");
			while(rel)
			{
				static int predf=-1;
				struct dg *pred = walk_dg(rel, &predf, "PRED", 0);
				char	*predn = pred?pred->xtype->name:0;;
				assert(predn != 0 || !"C-CONT RELS LIST FIRST had no PRED!");
				if(compatible_rels(predn, p->pred))
				{
					printf(" should skolemize `%s' relation of rule `%s'\n", predn, rule->name);
				}
				else
				{
					int j;
					for(j=0;j<mrs->nrels;j++) if(i!=j)
						if(compatible_rels(predn, mrs->rels[j].pred))
							break;
					if(j==mrs->nrels)return -1;	// this relation isn't in the input semantics
				}
				rels = dg_hop(rels, 2);
				rel = rels?dg_hop(rels, 1):0;
			}
			// other semantically vacuous rules all go in!
			return 0;
		}
		add_rules_for_relation(p->pred, rskolem);
	}
	sentential = 0;
	int ryes(struct edge *e, struct rule *rule)
	{
		int i;
		for(i=0;i<ngen_ignore_rules;i++)	// XXX should really just store a bit on `struct rule' for whether the generator ignores it
			if(!strcmp(rule->name, gen_ignore_rules[i]))
				return -1;
		return 0;
	}
	add_rules_for_relation("vacuous", ryes);

	printf("start generating...\n");
	while( (edge = next_agenda(0)) )
	{
		//printf("... process %p = #%d\n", edge, edge->id);
		if(gen_process_edge(edge)){/*printf("terminated!\n");break;*/}
	}

	/*if(sentential)
	{
		i = cells->p_nedges+1;
		edge = sentential;
		goto show;
	}
	else*/
	{
		for(i=0;i<cells->p_nedges;i++)
		{
			edge = cells->p_edges[i];
			if(edge->ep_span == require_ep_span && is_root(edge->dg))
			{
				show:
				if(trace>0)printf("edge #%d is sentential\n", edge->id);
				if(trace>1){print_dg(edge->dg); printf("\n");}
				printf("%s\n", str = show_orth_leaves(edge));
				free(str);
				count++;
			}
		}
	}
	printf("NOTE: %d passive, %d active edges in final generation chart\n",
		cells->p_nedges, cells->al_nedges + cells->ar_nedges);
	if(lui_chart)
	{
		char	*words[] = {"generate"};
		output_lui(words);
	}

	return count;
}
