#include	<stdlib.h>
#include	<stdio.h>

#include	"chart.h"
#include	"rule.h"
#include	"lexicon.h"

int			chart_size;
struct chart_cell	*cells;
int	total_edges, real_edges, passive_edges, used_edges;

extern int	debug_level;

int		edge_id;

extern int	*nactive, *npassive, used_total;
extern int noldslabs;

#define	MAX_CHUNKS	680	// when to give up on a parse
#define	FIRST_ONLY

void	clear_chart(int	size)
{
	int	i, j;
	extern unsigned int	generation;

	// guard against generation overflow;
	// reset the grammar if we are above 4-billion generations
	if(generation > 4000000000UL)
		reload_frozen_grammar();

	if(cells)
	{
		for(i=0;i<chart_size*chart_size;i++)
			if(cells[i].nedges)
				free(cells[i].edges);
		free(cells);
	}
	clear_agenda();
	edge_id = 1;

	clear_slab();

	passive_edges = real_edges = total_edges = used_edges = 0;
	chart_size = size;
	cells = calloc(sizeof(struct chart_cell) , chart_size*chart_size);
}

int	next_eid() { return edge_id++; }

void	add_edge_to_chart_cell(struct chart_cell	*cell, struct edge	*edge)
{
	//printf(" -- edge type %s, %d-%d\n", edge->rule?edge->rule->name:"lex", edge->from, edge->to);
	cell->nedges++;
	cell->edges = realloc(cell->edges, sizeof(struct edge*) * cell->nedges);
	cell->edges[cell->nedges-1] = edge;

	total_edges++;
	if(edge->to!=edge->from)real_edges++;
	if(edge->need==edge->have)passive_edges++;
}

void	add_edge_to_chart(struct edge	*edge)
{
	int	k = edge->from*chart_size + edge->to;

	add_edge_to_chart_cell(cells+k, edge);
}

int	trace = 0, gen_qc = 0;

static inline int	fundamental_rule(struct edge	*A, struct edge	*B)
{
	struct edge	*e, *a = 0, *b = 0;
	struct dg	*u;
	struct qc	*qc;
	int	remain, arg, nhave, nneed;
	extern int	__qc_use_heap;

	if(A->have==A->need)b = A;
	else a = A;
	if(B->have==B->need)b = B;
	else a = B;

	// need one active and one passive edge
	if(!a || !b)return 0;
	remain = a->need - a->have;

	//printf("FR: [%d-%d] + [%d-%d]?\n", a->from, a->to, b->from, b->to);
	//printf("    <%d/%d> + <%d/%d>\n", a->have, a->need, b->have, b->need);
	//printf("    %s   + %s\n", a->dg->xtype->name, b->dg->xtype->name);

	if(a->rule->lexical && !b->lex)return 0;	// lexical rules may apply only to lexical edges

	if(a->rtl)
	{
		if(b->to != a->from)return 0;
		if(b->from-(remain-1) < 0)return 0;	// don't produce active edges that can't possibly finish
		arg = a->need - a->have - 1;
	}
	else
	{
		if(a->to != b->from)return 0;
		if(b->to+(remain-1) >= chart_size)return 0;	// don't produce active edges that can't possibly finish
		arg = a->have;
	}

	if(b->rule && !check_rf(a->rule, b->rule, arg))return 0;	// rule filter rejected this combination
	if(!gen_qc && quickcheck(a->qc, b->qc))return 0;	// quickcheck rejected this combination

	if(!a->dg)
	{
		// hyperactive parsing: we don't save active edges' dg's until they're actually used
		a->dg = unify_dg(a->ha_dg, a->ha_par, a->ha_arg);
		if(!a->dg)
		{
			fprintf(stderr, "hyperactive: rescheduled unify failed!\n");
			exit(-1);
		}
	}

	if(trace>1)printf("trying: #%d %s in #%d %s[%d]\n", b->id, b->dg->xtype->name, a->id, a->dg->xtype->name, arg);
	if(!unify_dg_tmp(b->dg, a->dg, arg))
	{
		nhave = a->have+1;
		nneed = a->need;
		if(nhave == nneed)
		{
			// building passive
			u = finalize_tmp(a->dg);
			if(!u)goto fail;	// cycles might happen, check for them.
			e = slab_alloc(sizeof(struct edge));
			e->qc = extract_qc_arg(u, -1);
			e->prio = 0;
			e->dg = u;
		}
		else
		{
			// building active
			if(check_cycles(a->dg))goto fail;
			__qc_use_heap = 1;
			qc = extract_qc_arg(a->dg, a->rtl?(nneed-nhave-1):nhave);
			__qc_use_heap = 0;
			forget_tmp();
			e = slab_alloc(sizeof(struct edge));
			e->prio = a->prio;
			e->ha_par = a->dg;
			e->ha_dg = b->dg;
			e->ha_arg = arg;
			e->dg = 0;
			e->qc = copy_qc(qc);
			//printf("active qc %p (copy of %p)\n", e->qc, qc);
			free(qc);

			// XXX the point is we don't want to do this:
			//u = finalize_tmp(a->dg);
			//if(!u){fprintf(stderr, "tmp-finalize unexpectedly failed!\n");exit(-1);}
			//e->dg = u;
		}
		e->need = nneed;
		e->have = nhave;
		e->rule = a->rule;
		//printf("new edge qc %p\n", e->qc);

		if(!a->have && !a->used){a->used = 1;used_edges++;}
		//if(!b->used){b->used = 1;used_edges++;}
		if(b->lex && a->rule->inflr)
		{
			//printf("promoted lexeme %s to word %s via rule %s\n", b->dg->xtype->name, u->xtype->name, a->rule->name);
		}
		e->id = next_eid();
		e->daughters = slab_alloc(sizeof(struct edge*) * e->have);
		e->rtl = a->rtl;
		e->used = 0;
		if(e->rtl)
		{
			e->from = b->from;
			e->to = a->to;
			memcpy(e->daughters+1, a->daughters, sizeof(struct edge*) * a->have);
			e->daughters[0] = b;
		}
		else
		{
			e->from = a->from;
			e->to = b->to;
			memcpy(e->daughters, a->daughters, sizeof(struct edge*) * a->have);
			e->daughters[arg] = b;
		}
		if(e->rule->lexical)e->lex = b->lex;
		else e->lex = 0;
		if(e->need==e->have)
		{
			// just finished a constituent
			trim_args(e->dg); // fix this for dg's

			//printf("\n generated %d/%d for [%d-%d] %s with rule %s\n", e->have, e->need, e->from, e->to, u->xtype->name, e->rule->name);
			//print_dg(u);
			//printf("\n");
			npassive[e->rule->rnum]++;
#ifdef	FIRST_ONLY
			if(e->from==0 && e->to==chart_size-1 && is_root(e->dg))
			{
				//printf("sentential\n");
				//e->used = 1;used_edges++;
				add_edge_to_chart(e);
				return -1;	// stop the parser
			}
#endif
		}
		else nactive[e->rule->rnum]++;
		if(trace)
		{
			printf("generated #%d %d/%d for [%d-%d] with rule %s:\n", e->id, e->have, e->need, e->from, e->to, e->rule->name);
			if(e->have==e->need && trace>1){print_dg(u); printf("\n\n");}
		}
	
		add_agenda(e);
		if(noldslabs >= MAX_CHUNKS)
		{
			fprintf(stderr, "terminating search, too much RAM\n");
			return -1;	// give up :(
		}
	}
	else
	{
		if(trace>1 || gen_qc)unify_backtrace();
		forget_tmp();
		fail:;
	}

	return 0;
}

static int	pack_edge(struct edge	*edge)
{
	struct chart_cell	*c = cells + edge->from*chart_size + edge->to;
	int			i;
	extern const int	qcbytes;

	// since we're taking first-parse-only, this is ok perhaps.
	for(i=0;i<c->nedges;i++)
		if(c->edges[i]->need==c->edges[i]->have &&
			!c->edges[i]->lex &&
			c->edges[i]->dg->xtype == edge->dg->xtype &&
			//!equiv_quickcheck(c->edges[i]->qc, edge->qc) &&
			!memcmp(c->edges[i]->qc, edge->qc, qcbytes) &&
			equivalent_dg(c->edges[i]->dg, edge->dg))
		{
			//fprintf(stderr, "packed %p in %p!\n", edge, c->edges[i]);
			//printf("host: "); print_dg(c->edges[i]->dg); printf("\n");
			//printf("pack: "); print_dg(edge->dg); printf("\n");
			// XXX should record
			if(edge->lex)printf("HUH? packed a lexical edge!\n");
			if(c->edges[i]->lex)printf("HUH? packed into a lexical edge!\n");
			return 1;
		}
	return 0;
}

static int	process_edge(struct edge	*e)
{
	int			i, j;
	struct chart_cell	*c;

	//printf("processing [%d-%d] edge;    ", e->from, e->to);
	//printf("  status: %d/%d\n", e->have, e->need);

	if(e->have == e->need && !e->lex && pack_edge(e))return 0;	// big savings! woohoo!
	add_edge_to_chart(e);

	// try to combine with edges on either side
	for(i=0;i<=e->from;i++)
	{
		c = cells + e->from + i*chart_size;
		for(j=0;j<c->nedges;j++)
			if(fundamental_rule(c->edges[j], e) == -1)return -1;
	}
	for(i=e->to;i<chart_size;i++)
	{
		c = cells + i + e->to*chart_size;
		for(j=0;j<c->nedges;j++)
			if(fundamental_rule(e, c->edges[j]) == -1)return -1;
	}
	return 0;
}

int	is_root(struct dg	*d);

int	parse(int	nwords, char	*words[], char	*sentence)
{
	int		i, j, k;
	int		num_parses = 0, num_entries;
	struct dg	*root;
	struct dg	*sem;
	struct edge	*edge;

	// setup the chart
	clear_chart(nwords+1);
	if(add_lexemes_to_chart(nwords, words))return -1;

	agenda_rules(nwords+1);

	// do the actual parsing
	while( (edge = next_agenda()) )
		if(process_edge(edge))break;

	// show analyses
	num_entries = cells[chart_size-1].nedges;
	for(i=0;i<num_entries;i++)
	{
		edge = cells[chart_size-1].edges[i];
		if(edge->have != edge->need)continue;	// only passive edges, please!
		root = edge->dg;
		if(!is_root(root))continue;

		//unfold_edge(edge);

		num_parses++;
		sem = dg_hop(root, lookup_fname("SYNSEM"));
		if(sem)sem = dg_hop(sem, lookup_fname("LOCAL"));
		if(sem)sem = dg_hop(sem, lookup_fname("CONT"));
		if(!sem)
		{
			//fprintf(stderr, "parse had no semantics!\n");
			continue;
		}
		if(num_parses!=1)continue;	// just print one parse
		if(sentence)puts(sentence);

		//print_dg(sem);
		//printf("\n\n");

		print_sem(sem);
		printf("\n\n");
		fflush(stdout);
	}

	if(debug_level)
	{
		fprintf(stderr, "%d readings. added %d / %d edges to chart (%d fully instantiated, %d actives used)\t", num_parses, total_edges, real_edges, passive_edges, used_edges);
		print_slab_stats();
	}
	used_total += used_edges;

	return num_parses;
}

unfold_edge(struct edge	*e)
{
	int	i;

	if(e->rule)fprintf(stderr, "%s\n", e->rule->name);
	for(i=0;i<e->have;i++)
		unfold_edge(e->daughters[i]);
}

int	base_index;

int	skolemize(struct dg	*index)
{
	if(!index)return -1;

	if(index->gen != 1)
	{
		index->ncarcs = base_index++;
		index->gen = 1;
	}
	return index->ncarcs;
}

print_sem(struct dg	*dg)
{
	int	idx;
	struct dg	*list, *rel, *hook;

	base_index = 1;
	hook = dg_hop(dg, lookup_fname("HOOK"));
	if(hook)idx = skolemize(dg_hop(hook, lookup_fname("INDEX")));
	else idx = -1;

	if(idx!=1)
	{
		fprintf(stderr, "SEM had no skolemizable HOOK\n");
		return;
	}

	list = dg_hop(dg, lookup_fname("RELS"));
	if(list)list = dg_hop(list, lookup_fname("LIST"));
	if(!list)
	{
		fprintf(stderr, "SEM had no RELS.LIST\n");
		return;
	}

	while(list)
	{
		rel = dg_hop(list, 1);	// 1 = FIRST
		if(rel)print_rel(rel);
		//else break;//fprintf(stderr, "list element had no FIRST!\n");

		list = dg_hop(list, 2);	// 2 = REST
	}
}

print_rel(struct dg	*rel)
{
	int		i, idx, narg;
	char		afeat[32];
	struct dg	*pred;
	struct dg	*arg;
	char		relstr[1024];
	static int	predf, cargf, argf[10], init = 0;

	if(!init)
	{
		predf = lookup_fname("PRED");
		cargf = lookup_fname("CARG");
		for(i=0;i<10;i++)
		{
			sprintf(afeat, "ARG%d", i);
			argf[i] = lookup_fname(afeat);
		}
		init=1;
	}

	pred = dg_hop(rel, predf);
	if(!pred)pred = dg_hop(rel, cargf);
	if(!pred)
	{
		fprintf(stderr, "REL had no PRED or CARG\n");
		return;
	}

	if(pred->type->name[0]=='"')
	{
		strcpy(relstr, pred->type->name+1);
		if(relstr[0] && relstr[strlen(relstr)-1]=='"')
			relstr[strlen(relstr)-1] = 0;
	}
	else strcpy(relstr, pred->type->name);

	narg = 0;
	for(i=0;i<10;i++)
	{
		arg = dg_hop(rel, argf[i]);
		if(arg)narg=i+1;
	}

	for(i=0;i<narg;i++)
	{
		arg = dg_hop(rel, argf[i]);
		if(!arg)idx = base_index++;	// make one up!
		else idx = skolemize(arg);
		if(idx==-1)
		{
			fprintf(stderr, "REL.%s not skolemizable\n", afeat);
			return;
		}
		if(i==0 && idx==1)
			printf("	+%s", relstr);
		else if(i==0)printf("	%s", relstr);
		printf(" %d", idx);
	}
	if(narg==0)
	{
		//fprintf(stderr, "REL had no ARGs\n");
	}
	else printf("\n");
}
