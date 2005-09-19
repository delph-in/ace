#include	<stdlib.h>
#include	<stdio.h>
#include	<string.h>
#include	<signal.h>
#include	<unistd.h>
#include	<sys/time.h>
#include	<time.h>
#include	<assert.h>

#include	"conf.h"
#include	"chart.h"
#include	"rule.h"
#include	"lexicon.h"
#include	"freeze.h"

#define GENERALIZE_TYPES
#define	ENABLE_HYPERACTIVE	1
//#define	RULE_STATS

int	inhibit_results = 0;
extern int inhibit_pack;

int			chart_size;
struct chart_cell	*cells;
int	total_edges, real_edges, passive_edges, used_edges, pused_edges;
unsigned long long total_passives;

extern int	debug_level;
extern int	question, exhaustive;

int		edge_id;

extern int	*nused, *nhactive, *nactive, *npassive, *nrecomb, *natries, *nptries, used_total, pused_total;
extern int noldslabs;

char	*show_orth_leaves(struct edge	*e);
static void	hyperactive_excursion(struct edge	*e);
int	combine(struct edge	*a, struct edge	*b);

unsigned long long unify_attempts_total, unify_attempts_pass_rf, unify_attempts_pass_qc, unify_attempts_success, unify_attempts_bad_orth;
#ifdef	CLOCKCOUNT
unsigned long long unify_clock, unify_count;
#endif

//int	*rtried;

struct edge	*sentential = 0;

//#define	MAX_CHUNKS	2560	// when to give up on a parse
#define	MAX_CHUNKS	800		// when to give up on a parse
//#define	MAX_CHUNKS	71	// when to give up on a parse

//#define	EXCURSION

extern unsigned int	generation;

void	clear_chart(int	size)
{
	int	i;

	// guard against generation overflow;
	// reset the grammar if we are above 4-billion generations
	if(generation > MAX_GEN_RESET)//4000000000UL)
		reload_frozen_grammar();

	if(cells)
	{
		for(i=0;i<chart_size*chart_size;i++)
		{
			if(cells[i].p_nedges)
				free(cells[i].p_edges);
			if(cells[i].al_nedges)
				free(cells[i].al_edges);
			if(cells[i].ar_nedges)
				free(cells[i].ar_edges);
		}
		free(cells);
	}
	clear_agenda();
	edge_id = 1;

	clear_slab();

	passive_edges = real_edges = total_edges = used_edges = pused_edges = 0;
	chart_size = size;
	cells = calloc(sizeof(struct chart_cell) , chart_size*chart_size);
	sentential = 0;
}

int	next_eid() { return edge_id++; }

void	add_edge_to_chart_cell(struct chart_cell	*cell, struct edge	*edge)
{
	//printf(" -- edge type %s, %d-%d\n", edge->rule?edge->rule->name:"lex", edge->from, edge->to);
	if(edge->have==edge->need)
	{
		cell->p_nedges++;
		cell->p_edges = realloc(cell->p_edges, sizeof(struct edge*) * cell->p_nedges);
		cell->p_edges[cell->p_nedges-1] = edge;
		passive_edges++;
		total_passives++;
	}
	else if(edge->rtl)
	{
		cell->ar_nedges++;
		cell->ar_edges = realloc(cell->ar_edges, sizeof(struct edge*) * cell->ar_nedges);
		cell->ar_edges[cell->ar_nedges-1] = edge;
	}
	else
	{
		cell->al_nedges++;
		cell->al_edges = realloc(cell->al_edges, sizeof(struct edge*) * cell->al_nedges);
		cell->al_edges[cell->al_nedges-1] = edge;
	}

	total_edges++;
	if(edge->to!=edge->from)real_edges++;
}

void	add_edge_to_chart(struct edge	*edge)
{
	int	k = edge->from*chart_size + edge->to;

	add_edge_to_chart_cell(cells+k, edge);
}

int	trace = 0, gen_qc = 0;

//#define TIM
//double base_unify_time = 0, base_finalize_time = 0;

int	build_edge(struct edge	*a, struct edge	*b, int	arg, struct dg	*dg, int force_noha)
{
	struct edge	*e;
	struct dg	*u;
	struct qc	*qc, *hqc;
	int		nhave, nneed, hyperactive = 0, from, to, spanning = 0;
	extern int	__qc_use_heap;
#ifdef	TIM
	struct timeval	tv1, tv2;
#endif

	if(a->rtl)
	{
		from = b->from;
		to = a->to;
	}
	else
	{
		from = a->from;
		to = b->to;
	}
	spanning = (from==0) && (to==chart_size-1);

	nhave = a->have+1;
	nneed = a->need;
	if(nhave == nneed)
	{
		static struct type *signtype = 0;
		if(!signtype)signtype = lookup_type("sign");

		// building passive
		if(a->dg && b->dg && !spanning && 0)
		{
			// hyperpassive
			hyperactive = 1;
			__qc_use_heap = 1;
#ifdef	GENERALIZE_TYPES
			dg->type = signtype;
#endif
			hqc = extract_qc_arg(dg, -1);
			__qc_use_heap = 0;
			forget_tmp();
			qc = copy_qc(hqc);
			free(hqc);
			u = 0;
			//printf("built hyperpassive on #%d+#%d\n", a->id, b->id);
		}
		else
		{
			// regular passive
#ifdef	TIM
			gettimeofday(&tv1, 0);
#endif
			u = finalize_tmp(dg, 1);
#ifdef	TIM
			gettimeofday(&tv2, 0);
			base_finalize_time += tv2.tv_sec - tv1.tv_sec + 0.000001 * (tv2.tv_usec - tv1.tv_usec);
#endif
			if(!u)return -1;	// cycles might happen, check for them.
#ifdef	GENERALIZE_TYPES
			u->type = u->xtype = signtype;
#endif
			qc = extract_qc_arg(u, -1);
		}
		e = slab_alloc(sizeof(struct edge));
		e->unpack = 0;
		e->qc = qc;
		e->dg = u;
		e->ha_par = a->dg;
		e->ha_dg = b->dg;
		e->ha_arg = arg;
		e->ha_use = 0;
	}
	else
	{
		// building active
		if(a->rule->hyper && !force_noha)
		{
			// hyperactive
			//if(check_cycles(dg))return -1;
			hyperactive = 1;

			__qc_use_heap = 1;
			hqc = extract_qc_arg(dg, a->rtl?(nneed-nhave-1):nhave);
			__qc_use_heap = 0;
#ifdef	EXCURSION
			// with hyperactive excursion, we can't free the extra space later since the QC is above us on the slab
			// and we can't free it now since we need it after building the edge! :(
#else
			forget_tmp();
#endif
			qc = copy_qc(hqc);
			free(hqc);

			u = 0;
#ifdef	RULE_STATS
			nhactive[a->rule->rnum]++;
#endif
		}
		else
		{
			// standard active
#ifdef	TIM
			gettimeofday(&tv1, 0);
#endif
			u = finalize_tmp(dg, 0);
#ifdef	TIM
			gettimeofday(&tv2, 0);
			base_finalize_time += tv2.tv_sec - tv1.tv_sec + 0.000001 * (tv2.tv_usec - tv1.tv_usec);
#endif
			if(!u)return -1;	// cycles might happen, check for them.
			qc = extract_qc_arg(u, a->rtl?(nneed-nhave-1):nhave);
		}
		e = slab_alloc(sizeof(struct edge));
		e->unpack = 0;
		e->dg = u;
		e->ha_par = dg;
		e->ha_dg = b->dg;
		e->ha_arg = arg;
		e->ha_use = 0;
		e->qc = qc;
		//printf("active qc %p (copy of %p)\n", e->qc, qc);
	}
	//e->prio = a->prio+1;
	//if(e->prio>20)e->prio = 20;
	e->prio = a->prio;
	if(nneed==nhave)
	{
		e->prio = 0;
		//e->prio=20;
		//e->prio = (e->to-e->from - 1)/2;
		//if(e->prio>10)e->prio = 10;
	}
	e->need = nneed;
	e->have = nhave;
	e->rule = a->rule;
	e->built_on = 0;
	b->built_on = 1;
	//printf("new edge qc %p\n", e->qc);

	if(!a->have && !a->used){a->used = 1;used_edges++;}
	if(!b->used){b->used=1;pused_edges++;}
	if(b->lex && a->rule->orth)
	{
		//printf("promoted lexeme %s to word %s via rule %s\n", b->dg->xtype->name, u->xtype->name, a->rule->name);
	}
	e->id = next_eid();
	//fprintf(stderr, "build #%d %s on #%d\n", e->id, e->rule->name, b->id);
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

	if(e->rule->lexical)
	{
		e->lex = b->lex;
		if(g_mode == PARSING)
		{
			if(!e->rule->orth)
			{
				e->north_routes = b->north_routes;
				e->orth_routes = b->orth_routes;
			}
			else
			{
				int i;
				if(b->north_routes==-1)
				{
					e->north_routes = -1;
					e->orth_routes = 0;
				}
				else
				{
					e->north_routes = 0;
					e->orth_routes = slab_alloc(sizeof(struct orth_route)*b->north_routes);
					for(i=0;i<b->north_routes;i++)
					{
						struct orth_route *r = b->orth_routes+i;
						if(r->len && r->rules[0]==e->rule->rnum)
						{
							e->north_routes++;
							e->orth_routes[e->north_routes-1].len = r->len-1;
							e->orth_routes[e->north_routes-1].rules = slab_alloc(sizeof(int)*(r->len-1));
							memcpy(e->orth_routes[e->north_routes-1].rules, r->rules+1, sizeof(int)*(r->len-1));
						}
					}
				}
				//printf("realloc %d routes down to %d\n", b->north_routes, e->north_routes);
				//e->orth_routes = slab_realloc(e->orth_routes, sizeof(struct orth_route)*b->north_routes, sizeof(struct orth_route)*e->north_routes);
			}
		}
		else // (g_mode == GENERATING)
		{
			e->north_routes = 0;
			e->orth_routes = 0;
		}
	}
	else
	{
		e->lex = 0;
		e->north_routes = 0;
		e->orth_routes = 0;
	}

	e->ep_span = a->ep_span | b->ep_span;
	e->npack=0;
	e->pack=0;

	if(trace)
	{
		if(g_mode == GENERATING)
		{
			printf("generated #%d %d/%d from #%d, #%d for [%8.8X] with rule %s: ",
				e->id, e->have, e->need, a->id, b->id, e->ep_span, e->rule->name);
			printf("%s\n", show_orth_leaves(e));
		}
		else // (g_mode == PARSING)
		{
			printf("generated #%d %d/%d from #%d, #%d for [%d-%d] with rule %s: ",
				e->id, e->have, e->need, a->id, b->id, e->from, e->to, e->rule->name);
			printf("%s\n", show_orth_leaves(e));
		}
		if(e->have==e->need && trace>1){print_dg(u); printf("\n\n");}
	}
	if(e->need==e->have)
	{
		//printf("\n generated %d/%d for [%d-%d] %s with rule %s\n", e->have, e->need, e->from, e->to, u->xtype->name, e->rule->name);
		//print_dg(u);
		//printf("\n");
#ifdef	RULE_STATS
		npassive[e->rule->rnum]++;
#endif

		if(!exhaustive &&
			((g_mode == GENERATING && e->ep_span == require_ep_span) ||
			(g_mode == PARSING && e->from==0 && e->to==chart_size-1)) &&
			is_root(e->dg))
		{
			//printf("sentential\n");
			e->used = 1;pused_edges++;
			add_edge_to_chart(e);
			sentential = e;	// stop the parser
			return -2;
		}
	}
#ifdef	RULE_STATS
	else nactive[e->rule->rnum]++;
#endif

	e->hex = 0;
#ifdef	EXCURSION
	if(hyperactive && e->have==e->need)
	{
		e->dg = dg;
		hyperactive_excursion(e);
		e->dg = 0;
		generation++;
	}
#endif

	add_agenda(e);
	return 0;
}

#ifdef	EXCURSION
// try to find another passive edge to combine with while 'e' is still valid
static void	hyperactive_excursion(struct edge	*e)
{
	int					i, j, remain, arg;
	struct chart_cell	*c;
	struct edge			*b;
	extern int highest; // XXX snooping on agenda.c

	if(g_mode == GENERATING)return;	// XXX why do we need to do this?

	if(e->prio < highest)return;

	// active edge
	remain = e->need - e->have;
	if(e->rtl)
	{
		arg = e->need - e->have - 1;
		for(i=0;i<=e->from;i++)
		{
			c = cells + e->from + i*chart_size;
			for(j=0;j<c->p_nedges;j++)
				if(c->p_edges[j]->from - (remain-1) >= 0)
				{
					b = c->p_edges[j];
					if(!b->dg)continue;	// can't play with hyperpassive edges
					if(b->rule && !check_rf(e->rule, b->rule, arg))continue;	// rule filter rejected this combination
					if(!gen_qc && quickcheck(e->qc, b->qc))continue;			// quickcheck rejected this combination
					//fprintf(stderr, "HEX %d+%d\n", e->id, b->id);
					if(combine(e, b) == 1){}//fprintf(stderr, " +\n");
					e->hex = b;
					return;
				}
		}
	}
	else
	{
		arg = e->have;
		for(i=e->to;i<chart_size;i++)
		{
			c = cells + i + e->to*chart_size;
			for(j=0;j<c->p_nedges;j++)
				if(c->p_edges[j]->to + (remain-1) < chart_size)
				{
					b = c->p_edges[j];
					if(!b->dg)continue;	// can't play with hyperpassive edges
					if(b->rule && !check_rf(e->rule, b->rule, arg))continue;	// rule filter rejected this combination
					if(!gen_qc && quickcheck(e->qc, b->qc))continue;			// quickcheck rejected this combination
					//fprintf(stderr, "HEX %d+%d\n", e->id, b->id);
					if(combine(e, b) == 1){}//fprintf(stderr, " +\n");
					e->hex = b;
					return;
				}
		}
	}
}
#endif

int	recall_hyper(struct edge	*b)
{
#ifdef	TIM
	struct timeval	tv1, tv2;
	gettimeofday(&tv1, 0);
#endif

#ifdef	RULE_STATS
	nrecomb[b->rule->rnum]++;
#endif
	//printf("hyperpassive reconstruct #%d from %p[%d] <- %p\n", b->id, b->ha_dg, b->ha_arg, b->ha_par);
	if(unify_dg_tmp(b->ha_dg, b->ha_par, b->ha_arg))
	{
		fprintf(stderr, "hyperpassive: rescheduled unify failed!\n");
		exit(-1);
	}
	b->dg = finalize_tmp(b->ha_par, (b->have==b->need)?1:0);
#ifdef	TIM
	gettimeofday(&tv2, 0);
	base_unify_time += tv2.tv_sec - tv1.tv_sec + 0.000001 * (tv2.tv_usec - tv1.tv_usec);
#endif
	if(!b->dg)
	{
		// latent cycle detection
		//fprintf(stderr, "NOTE: hyperpassive: latent cycle\n");
		b->have = -1;
		return -1;
	}
	return 0;
}

inline int	combine(struct edge	*a, struct edge	*b)
{
	int	i, arg, force_noha = !ENABLE_HYPERACTIVE, res;
	struct dg	*rdg, *pdg;
#ifdef	TIM
	struct timeval	tv1, tv2;
#endif

	if(a->hex == b)return 0;	// already did this
	if(a->have==-1)return 0;	// ghosted hyperactive edge -- got kicked out of the chart for a latent cycle
	if(b->have==-1)return 0;	// ghosted hyperpassive edge -- got hicked out of the chart for a latent cycle

//	if(b->lex && b->north_routes==-1 && a->need==1)return 0;	// unknown-word edges may not undergo unaries

	//printf("FR: [%d-%d] + [%d-%d]?\n", a->from, a->to, b->from, b->to);
	//printf("    <%d/%d> + <%d/%d>\n", a->have, a->need, b->have, b->need);
	//printf("    %s   + %s\n", a->dg->xtype->name, b->dg->xtype->name);

	if(trace>1)printf("[%8.8X #%d [lex %d orth %p] + %8.8X #%d [lex %p]] rule %s?\n",
		0/*a->ep_span*/, a->id, a->rule->lexical, a->rule->orth, 0/*b->ep_span*/, b->id, b->lex, a->rule->name);

	if(a->rule->lexical)
	{
		if(!b->lex)return 0;	// lexical rules may apply only to lexical edges
		// if 'a' is orthographemic, make sure it's the right one
		if(a->rule->orth && g_mode == PARSING && b->north_routes != -1)
		{
			// look at each route 'b' allows and see if this is the first rule on it
			for(i=0;i<b->north_routes;i++)
			{
				struct orth_route *r = b->orth_routes+i;
				if(r->len && r->rules[0] == a->rule->rnum)
					break;
			}
			if(i==b->north_routes)return 0;
			//printf(" #%d admits rule %s as a next orthographemic\n", b->id, a->rule->name);
		}
	}
	else if(b->lex && g_mode == PARSING)
	{
		// 'b' is lexical but 'a' is a syntax rule; see if 'b' might be done with its orthographemics
		for(i=0;i<b->north_routes;i++)
			if(b->orth_routes[i].len == 0)break;
		if(i==b->north_routes)return 0;
	}

	if(a->rtl)arg = a->need - a->have - 1;
	else arg = a->have;

	int rf = (b->rule && !check_rf(a->rule, b->rule, arg));

	// don't allow span-only rules to finish on a non-span
	if(a->rule->frag_only)return 0;	// XXX remove if we decide to accept fragments

	if(g_mode == PARSING && a->have == a->need-1)
	{
		if( (a->from==0 || b->from==0) && (a->to==chart_size || b->to==chart_size) )
		{
			// spanning edge
			extern char	*rule_root_utc;
			if(!rule_root_utc[a->rule->rnum]) return 0;
		}
		else
		{
			// non-spanning edge
			if(a->rule->span_only) return 0;
		}
	}

	unify_attempts_total++;
	if(rf)return 0;
	//if(b->rule && !check_rf(a->rule, b->rule, arg))return 0;	// rule filter with no packing rejected this

/*
	// XXX this doesn't play fair if we allow edges to be packed into b after we try to combine b with other stuff :(
	// if we can force the right chart traversal order somehow, this will help reduce spurious rule applications.
	for(i=-1;i<b->npack;i++)
	{
		struct edge *B = (i>=0)?b->pack[i]:b;
		if(!B->rule || check_rf(a->rule, B->rule, arg))break;
	}
	if(!b->npack && i==b->npack)return 0;	// all packed 'b's were rejected by rule filter
*/

	unify_attempts_pass_rf++;
	if(!gen_qc && quickcheck(a->qc, b->qc))return 0;	// quickcheck rejected this combination
	unify_attempts_pass_qc++;

	//rtried[a->rule->rnum]++;

#ifdef	RULE_STATS
	if(a->ntries==0)natries[a->rule->rnum]++;
	a->ntries++;
	if(b->ntries==0)
	{
		if(b->rule)nptries[b->rule->rnum]++;
		else nptries[nrules]++;
	}
	b->ntries++;
#endif

	if(!a->dg)
	{
		// hyperactive parsing: we don't save active edges' dg's until they're actually used
		if(a->ha_use > 2 || !b->dg)
		{
			if(recall_hyper(a))return 0;
			rdg = a->dg;
		}
		else
		{
#ifdef	RULE_STATS
			nrecomb[a->rule->rnum]++;
#endif
#ifdef	TIM
			gettimeofday(&tv1, 0);
#endif
			unify_dg_tmp(a->ha_dg, a->ha_par, a->ha_arg);
#ifdef	TIM
			gettimeofday(&tv2, 0);
			base_unify_time += tv2.tv_sec - tv1.tv_sec + 0.000001 * (tv2.tv_usec - tv1.tv_usec);
#endif
			rdg = a->ha_par;
			force_noha = 1;
			a->ha_use++;
		}
	}
	else rdg = a->dg;

	if(!b->dg)
	{
		// hyperpassive parsing: we don't save passive edges' dg's until they're actually used
		if(b->ha_use > 2 || 1)
		{
			if(recall_hyper(b))return 0;
			pdg = b->dg;
		}
		else
		{
#ifdef	RULE_STATS
			nrecomb[b->rule->rnum]++;
#endif
#ifdef	TIM
			gettimeofday(&tv1, 0);
#endif
			unify_dg_tmp(b->ha_dg, b->ha_par, b->ha_arg);
#ifdef	TIM
			gettimeofday(&tv2, 0);
			base_unify_time += tv2.tv_sec - tv1.tv_sec + 0.000001 * (tv2.tv_usec - tv1.tv_usec);
#endif
			pdg = b->ha_par;
			force_noha = 1;
			b->ha_use++;
		}
	}
	else pdg = b->dg;
	//printf("recombine %s\n", a->rule->name);

	if(trace>1)printf("trying: #%d %s in #%d %s[%d]\n", b->id, pdg->xtype->name, a->id, rdg->xtype->name, arg);
#ifdef	TIM
	gettimeofday(&tv1, 0);
#endif
#ifdef	CLOCKCOUNT
	clock_t	T = clock();
#endif
	res = unify_dg_tmp(pdg, rdg, arg);
#ifdef	CLOCKCOUNT
	unify_clock += clock()-T;
	unify_count++;
#endif
#ifdef	TIM
	gettimeofday(&tv2, 0);
	base_unify_time += tv2.tv_sec - tv1.tv_sec + 0.000001 * (tv2.tv_usec - tv1.tv_usec);
#endif

	if(!res)unify_attempts_success++;

	if(!res && a->rule->orth)
	{
		// check whether orthographemic component accepts
		// XXX this is sooo slow.
		char *sub = show_orth_leaves(b), *foo;
		foo = inflect(a->rule->orth, sub);
		free(sub);
		if(!foo)
		{
			res = -1;
			unify_attempts_bad_orth++;
		}
	}

	if(!res)
	{
		if(build_edge(a, b, arg, rdg, force_noha) == -2)return -1;
		if(noldslabs >= MAX_CHUNKS)
		{
			fprintf(stderr, "NOTE: terminating search, too much RAM\n");
			return -1;	// give up :(
		}
		return 1;
	}
	else
	{
#ifdef	UNIFY_PATHS
		extern struct path	unify_backtrace_path();
		if(trace>1)unify_backtrace();
		if(gen_qc)gqc_count_failure(unify_backtrace_path());
#endif
		forget_tmp();
	}

	return 0;
}

int	is_root(struct dg	*d);

char	*show_orth_leaves(struct edge	*e)
{
	int		i;
	char	*sub, *ret;

	if(!e->have)
	{
		if(e->lex)
		{
			int len = 0, i;
			for(i=0;i<e->lex->stemlen;i++)len += strlen(e->lex->stem[i]->name) + 1 - 2;
			ret = malloc(len+2); *ret = 0;
			for(i=0;i<e->lex->stemlen;i++)
			{
				if(i)strcat(ret, " ");
				strcat(ret, e->lex->stem[i]->name+1);
				ret[strlen(ret)-1] = 0;
			}
			return ret;
		}
		else return strdup("[null]");
	}
	if(e->have==1)
	{
		sub = show_orth_leaves(e->daughters[0]);
		if(e->rule->orth)
		{
			//printf("applying orth rule `%s' to `%s'\n", e->rule->name, sub);
			ret = inflect(e->rule->orth, sub);
			free(sub);
			return strdup(ret);	// higher-up show_orth_leaves() might want to free what we return... blah.
		}
		else return sub;
	}
	else
	{
		for(ret=calloc(1,1),i=0;i<e->have;i++)
		{
			sub = show_orth_leaves(e->daughters[i]);
			ret = realloc(ret, strlen(ret) + strlen(sub) + 2);
			if(i)strcat(ret, " ");
			strcat(ret, sub);
			free(sub);
		}
		return ret;
	}
}

int	nfinal;

visit_roots(void (*visitor)(struct edge *e))
{
	int i, num_entries;
	struct edge *edge;

	num_entries = cells[chart_size-1].p_nedges;
	for(i=0;i<num_entries;i++)
	{
		edge = cells[chart_size-1].p_edges[i];
		if(is_root(edge->dg))
			visitor(edge);
	}
}

void	final_chart_stats()
{
	extern unsigned long long ndecomposed;
	packing_report();
	if(debug_level)
	{
		fprintf(stderr, "NOTE: unify filters: %lld total, %lld rf (%.1f%%), %lld qc (%.1f%% / %.1f%%), %lld success (%.1f%% / %.1f%%), %lld bad orth (%.1f%% / %.1f%%)\n", unify_attempts_total,
					unify_attempts_pass_rf, 100*(double)unify_attempts_pass_rf / unify_attempts_total,
					unify_attempts_pass_qc, 100*(double)unify_attempts_pass_qc / unify_attempts_total, 100*(double)unify_attempts_pass_qc / unify_attempts_pass_rf,
					unify_attempts_success, 100*(double)unify_attempts_success / unify_attempts_total, 100*(double)unify_attempts_success / unify_attempts_pass_qc,
					unify_attempts_bad_orth, 100*(double)unify_attempts_bad_orth / unify_attempts_total, 100*(double)unify_attempts_bad_orth / unify_attempts_success);
		fprintf(stderr, "NOTE: %lld / %lld (%.1f%%) passive edges were connected to roots\n", ndecomposed, total_passives, 100*(double)ndecomposed / total_passives);
	}
#ifdef TIM
	fprintf(stderr, "NOTE: unify was %.4fs\n", base_unify_time);
	fprintf(stderr, "NOTE: finalize was %.4fs\n", base_finalize_time);
	base_unify_time = 0;
	base_finalize_time = 0;
#endif
	if(gen_qc)gqc_result();
}
