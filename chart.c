#include	<stdlib.h>
#include	<stdio.h>
#include	<string.h>
#include	<signal.h>
#include	<unistd.h>
#include	<sys/time.h>
#include	<assert.h>

#include	"chart.h"
#include	"rule.h"
#include	"lexicon.h"
#include	"freeze.h"
#include	"morpho.h"

int			chart_size;
struct chart_cell	*cells;
int	total_edges, real_edges, passive_edges, used_edges, pused_edges;

extern int	debug_level;
extern int	question, exhaustive, lui_chart, inhibit_pack;

int		edge_id;

extern int	*nused, *nhactive, *nactive, *npassive, *nrecomb, used_total, pused_total;
extern int noldslabs;
extern int	rnohyper[1000];

char	*show_orth_leaves(struct edge	*e);
static void	hyperactive_excursion(struct edge	*e);
static inline int	combine(struct edge	*a, struct edge	*b);

void	print_sem(struct dg	*dg);
void	print_rel(struct dg	*rel);
int		unfold_edge(struct edge	*e);

//int	*rtried;

struct edge	*sentential = 0;
unsigned int	require_ep_span = 0;

int	cancel_parse;

#define	MAX_CHUNKS	680	// when to give up on a parse
//#define	MAX_CHUNKS	71	// when to give up on a parse
//#define	FIRST_ONLY

//#define	EXCURSION

extern unsigned int	generation;

void	clear_chart(int	size)
{
	int	i;

	// guard against generation overflow;
	// reset the grammar if we are above 4-billion generations
	if(generation > 4000000000UL)
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
	require_ep_span = 0;

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
	int		nhave, nneed, hyperactive = 0;
	extern int	__qc_use_heap;
#ifdef	TIM
	struct timeval	tv1, tv2;
#endif

	nhave = a->have+1;
	nneed = a->need;
	if(nhave == nneed)
	{
		// building passive
#ifdef	TIM
		gettimeofday(&tv1, 0);
#endif
		u = finalize_tmp(dg, 1);
#ifdef	TIM
		gettimeofday(&tv2, 0);
		base_finalize_time += tv2.tv_sec - tv1.tv_sec + 0.000001 * (tv2.tv_usec - tv1.tv_usec);
#endif
		if(!u)return -1;	// cycles might happen, check for them.
		e = slab_alloc(sizeof(struct edge));
		e->qc = extract_qc_arg(u, -1);
		e->dg = u;
		e->age = (nhave==1)?(b->age+1):0;	// binary passive edges get age 0; unary get daughter.age+1
	}
	else
	{
		// building active
		if(!rnohyper[a->rule->rnum] && !force_noha)
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
			nhactive[a->rule->rnum]++;
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
		e->dg = u;
		e->ha_par = dg;
		e->ha_dg = b->dg;
		e->ha_arg = arg;
		e->ha_use = 0;
		e->qc = qc;
		e->age = 0;	// 'age' is only used on passive edges
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
	//printf("new edge qc %p\n", e->qc);

	if(!a->have && !a->used){a->used = 1;used_edges++;}
	if(!b->used){b->used=1;pused_edges++;}
	if(b->lex && a->rule->inflr)
	{
		//printf("promoted lexeme %s to word %s via rule %s\n", b->dg->xtype->name, u->xtype->name, a->rule->name);
	}
	e->id = next_eid();
	//fprintf(stderr, "build #%d %s age %d on #%d\n", e->id, e->rule->name, e->age, b->id);
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
	if(e->rule->lexical && !e->rule->inflr)
	{
		e->lex = b->lex;
		e->allow_no_inflr = b->allow_no_inflr;
		e->inflr_allow = b->inflr_allow;
	}
	else e->lex = 0;

#ifdef	GENERATION
	e->ep_span = a->ep_span | b->ep_span;
#endif
	e->npack=0;
	e->pack=0;

	if(trace)
	{
#ifdef	GENERATION
		printf("generated #%d %d/%d from #%d, #%d for [%8.8X] with rule %s: ", e->id, e->have, e->need, a->id, b->id, e->ep_span, e->rule->name);
		printf("%s\n", show_orth_leaves(e));
#else
		printf("generated #%d %d/%d for [%d-%d] with rule %s:\n", e->id, e->have, e->need, e->from, e->to, e->rule->name);
#endif
		if(e->have==e->need && trace>1){print_dg(u); printf("\n\n");}
	}
	if(e->need==e->have)
	{
		// just finished a constituent
		// chop off ARGS et al
		// actually, finalize_tmp() does this for us now.
		//e->dg = trim_args(e->dg);

		//printf("\n generated %d/%d for [%d-%d] %s with rule %s\n", e->have, e->need, e->from, e->to, u->xtype->name, e->rule->name);
		//print_dg(u);
		//printf("\n");
		npassive[e->rule->rnum]++;
//#ifdef	FIRST_ONLY
		if(!exhaustive &&
#ifdef	GENERATION
			e->ep_span == require_ep_span &&
#else
			e->from==0 && e->to==chart_size-1 &&
#endif
			is_root(e->dg))
		{
			//printf("sentential\n");
			e->used = 1;pused_edges++;
			add_edge_to_chart(e);
			sentential = e;	// stop the parser
			return -2;
		}
//#endif
	}
	else nactive[e->rule->rnum]++;

	e->hex = 0;
#ifdef	EXCURSION
	if(hyperactive)
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

#ifdef GENERATION
	return;
#endif

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

static inline int	combine(struct edge	*a, struct edge	*b)
{
	int	arg, force_noha = 0, res;
	struct dg	*rdg;
#ifdef	TIM
	struct timeval	tv1, tv2;
#endif

	if(a->hex == b)return 0;	// already did this
	if(a->have==-1)return 0;	// ghosted hyperactive edge -- got kicked out of the chart for a latent cycle

	//printf("FR: [%d-%d] + [%d-%d]?\n", a->from, a->to, b->from, b->to);
	//printf("    <%d/%d> + <%d/%d>\n", a->have, a->need, b->have, b->need);
	//printf("    %s   + %s\n", a->dg->xtype->name, b->dg->xtype->name);

	//if(trace>1)printf("[%8.8X #%d [lex %d infl %p] + %8.8X #%d [lex %d]] rule %s?\n",
		//a->ep_span, a->id, a->rule->lexical, a->rule->inflr, b->ep_span, b->id, b->lex, a->rule->name);

	if(a->rule->lexical)
	{
		//if(!b->lex)return 0;	// lexical rules may apply only to lexical edges
		if(b->lex && a->rule->inflr && !b->inflr_allow[a->rule->rnum])return 0;
	}
	else if(b->lex && !b->allow_no_inflr)return 0;

	if(a->rtl)arg = a->need - a->have - 1;
	else arg = a->have;

	if(b->rule && !check_rf(a->rule, b->rule, arg))return 0;	// rule filter rejected this combination
	if(!gen_qc && quickcheck(a->qc, b->qc))return 0;	// quickcheck rejected this combination

	//rtried[a->rule->rnum]++;

	if(!a->dg)
	{
		// hyperactive parsing: we don't save active edges' dg's until they're actually used
		nrecomb[a->rule->rnum]++;
		if(a->ha_use > 2)
		{
#ifdef	TIM
			gettimeofday(&tv1, 0);
#endif
			a->dg = unify_dg(a->ha_dg, a->ha_par, a->ha_arg);
#ifdef	TIM
			gettimeofday(&tv2, 0);
			base_unify_time += tv2.tv_sec - tv1.tv_sec + 0.000001 * (tv2.tv_usec - tv1.tv_usec);
#endif
			if(!a->dg)
			{
				// latent cycle detection
				// this doesn't seem to happen... not sure why?
				// update: it has happened at least once
				fprintf(stderr, "NOTE: hyperactive: latent cycle\n");
				a->have = -1;
				return 0;
				//fprintf(stderr, "hyperactive: rescheduled unify failed!\n");
				//exit(-1);
			}
			rdg = a->dg;
		}
		else
		{
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
	//printf("recombine %s\n", a->rule->name);

	if(trace>1)printf("trying: #%d %s in #%d %s[%d]\n", b->id, b->dg->xtype->name, a->id, rdg->xtype->name, arg);
#ifdef	TIM
	gettimeofday(&tv1, 0);
#endif
	res = unify_dg_tmp(b->dg, rdg, arg);
#ifdef	TIM
	gettimeofday(&tv2, 0);
	base_unify_time += tv2.tv_sec - tv1.tv_sec + 0.000001 * (tv2.tv_usec - tv1.tv_usec);
#endif
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
		if(trace>1 || gen_qc)unify_backtrace();
		forget_tmp();
	}

	return 0;
}

static void record_packing(struct edge *host, struct edge *e)
{
	host->npack++;
	host->pack = slab_realloc(host->pack, (host->npack-1)*sizeof(struct edge*), host->npack*sizeof(struct edge*));
	host->pack[host->npack-1] = e;
}

static int	pack_edge(struct edge	*edge)
{
	struct chart_cell	*c = cells + edge->from*chart_size + edge->to;
	int			i, ssd;
	//extern const int	qcbytes;

	// since we're taking first-parse-only, this is ok perhaps.
	for(i=0;i<c->p_nedges;i++)
		if(!c->p_edges[i]->lex
//#ifdef	BIDIR_SS
		&& c->p_edges[i]->age == edge->age
//#endif
#ifdef	GENERATION
		&& c->p_edges[i]->ep_span == edge->ep_span
#endif
		//&& c->p_edges[i]->dg->xtype == edge->dg->xtype
		&& !equiv_quickcheck(c->p_edges[i]->qc, edge->qc)
		//&& !memcmp(c->p_edges[i]->qc, edge->qc, qcbytes)
			)
		{
			//fprintf(stderr, "PACK? #%d subs #%d\n", c->p_edges[i]->id, edge->id);
			ssd = equivalent_dg(c->p_edges[i]->dg, edge->dg);
			if(ssd == 1)
			{
				//fprintf(stderr, "packed #%d in #%d!\n", edge->id, c->p_edges[i]->id);
				//printf("host: "); print_dg(c->p_edges[i]->dg); printf("\n");
				//printf("pack: "); print_dg(edge->dg); printf("\n");
				//printf("EQUIV = %d\n", equivalent_dg(c->p_edges[i]->dg, edge->dg));
				record_packing(c->p_edges[i], edge);
				return 1;
			}
#ifdef	BIDIR_SS
			else if(ssd == -1)
			{
				edge->npack = c->p_edges[i]->npack;
				edge->pack = c->p_edges[i]->pack;
				c->p_edges[i]->npack = 0;
				c->p_edges[i]->pack = 0;
				record_packing(edge, c->p_edges[i]);
				c->p_edges[i] = edge;
				return 1;
			}
#endif
		}
	return 0;
}

static int	gen_process_edge(struct edge	*e)
{
#ifdef	GENERATION
	struct chart_cell	*c = cells;
	int	i;

	if(!inhibit_pack && e->have == e->need && !e->lex && pack_edge(e))
		return 0;	// big savings! woohoo!
	add_edge_to_chart(e);

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
#endif
	return 0;
}

static long long packcount = 0;
extern long long copycount, copytraverse;

static int	parse_process_edge(struct edge	*e)
{
	int			i, j, remain;
	struct chart_cell	*c;

	//printf("processing [%d-%d] edge;    ", e->from, e->to);
	//printf("  status: %d/%d\n", e->have, e->need);

	/*if(e->have==e->need)
		e->dg->xtype = lookup_type("*top*");*/
	if(!inhibit_pack && e->have == e->need && !e->lex && pack_edge(e))
	{
		packcount++;
		return 0;	// big savings! woohoo!
	}
	add_edge_to_chart(e);

	// try to combine with edges on either side
	if(e->have == e->need)
	{
		// passive edge
		for(i=0;i<=e->from;i++)
		{
			c = cells + e->from + i*chart_size;
			for(j=0;j<c->al_nedges;j++)
			{
				remain = c->al_edges[j]->need - c->al_edges[j]->have;
				if(e->to + (remain-1) < chart_size)
					if(combine(c->al_edges[j], e) == -1)return -1;
			}
		}
		for(i=e->to;i<chart_size;i++)
		{
			c = cells + i + e->to*chart_size;
			for(j=0;j<c->ar_nedges;j++)
			{
				remain = c->ar_edges[j]->need - c->ar_edges[j]->have;
				if(e->from - (remain-1) >= 0)
					if(combine(c->ar_edges[j], e) == -1)return -1;
			}
		}
	}
	else
	{
		// active edge
		remain = e->need - e->have;
		if(e->rtl)
		{
			for(i=0;i<=e->from;i++)
			{
				c = cells + e->from + i*chart_size;
				for(j=0;j<c->p_nedges;j++)
					if(c->p_edges[j]->from - (remain-1) >= 0)
						if(combine(e, c->p_edges[j]) == -1)return -1;
			}
		}
		else
		{
			for(i=e->to;i<chart_size;i++)
			{
				c = cells + i + e->to*chart_size;
				for(j=0;j<c->p_nedges;j++)
					if(c->p_edges[j]->to + (remain-1) < chart_size)
						if(combine(e, c->p_edges[j]) == -1)return -1;
			}
		}
	}
	return 0;
}

int	is_root(struct dg	*d);

void	alarm_handler(int	sig)
{
	fprintf(stderr, "NOTE: cancelling parse, too long\n");
	cancel_parse = 1;
}

char	*show_orth_leaves(struct edge	*e)
{
	int		i;
	char	*sub, *ret;

	if(!e->have)
	{
		if(e->lex)
		{
			// XXX fix me
			ret = strdup(e->lex->stem[0]->name+1);
			ret[strlen(ret)-1] = 0;
			return ret;
		}
		else return strdup("[null]");
	}
	if(e->have==1)
	{
		sub = show_orth_leaves(e->daughters[0]);
		if(e->rule->inflr)
		{
			ret = inflect(e->rule->inflr, sub);
			free(sub);
			return ret;
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

#ifdef	GENERATION
int	generate()
{
	struct edge	*edge;
	char		*str;
	struct dg	*sk1, *sk2, *sk3;
	int			i, count = 0;
	extern struct type	**strings;

	clear_chart(1);
	sk1 = atomic_dg("*top*"); sk1->type = sk1->xtype = strings[0];
	sk2 = atomic_dg("*top*"); sk2->type = sk2->xtype = strings[1];
	sk3 = atomic_dg("*top*"); sk3->type = sk3->xtype = strings[2];
	// set up some lexemes for the generator to play with
	add_generative_lexeme("dog", sk1, 0, 0, 0);
	add_generative_lexeme("cat", sk2, 0, 0, 0);
	add_generative_lexeme("sometimes", sk3, 0, 0, 0);
	add_generative_lexeme("those", sk1, 0, 0, 0);
	add_generative_lexeme("certain", 0, sk2, 0, 0);
	add_generative_lexeme("chase", 0, sk1, sk2, 0);

	agenda_rules(1);
	sentential = 0;

	while( (edge = next_agenda(0)) )
	{
		//printf("... process %p = #%d\n", edge, edge->id);
		if(gen_process_edge(edge)){printf("terminated!\n");break;}
	}

	if(sentential)
	{
		i = cells->p_nedges+1;
		edge = sentential;
		goto show;
	}
	else
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

	return count;
}
#endif

int	nfinal;

int	parse(int	nwords, char	*words[], char	*sentence)
{
	int		i, count;
	int		num_parses = 0, num_entries;
	struct dg	*root;
	struct dg	*sem;
	struct edge	*edge;

	// setup the chart
	clear_chart(nwords+1);
	if(add_lexemes_to_chart(nwords, words))return -1;
	if(reduce_chart(nwords))return -1;

	//if(rtried)free(rtried);
	//rtried = calloc(sizeof(int),nrules);

	agenda_rules(nwords+1);

	cancel_parse = 0;
	signal(SIGALRM, alarm_handler);

	//alarm(10);
	//ualarm(100000);

	// do the actual parsing
	while( !cancel_parse && (edge = next_agenda(0)) )
		if(parse_process_edge(edge))break;

	alarm(0);

	/*num_entries = 0;
	for(i=0;i<nrules;i++)if(rtried[i])
	{
		num_entries++;
		printf("rule %32s: %8d tries\n", rules[i]->name, rtried[i]);
	}
	printf(" -- %d / %d were tried\n", num_entries, nrules);*/

	// show analyses
//#ifdef	FIRST_ONLY
	if(!exhaustive && sentential) { i=0;num_entries=0;edge = sentential; root = edge->dg; goto show; }
//#endif
	num_entries = cells[chart_size-1].p_nedges;
	for(i=0;i<num_entries;i++)
	{
		edge = cells[chart_size-1].p_edges[i];
		if(edge->have != edge->need)continue;	// only passive edges, please!
		root = edge->dg;
		if(!is_root(root))continue;


		show:
		count = unfold_edge(edge);
		num_parses+=count;
		sem = dg_hop(root, lookup_fname("SYNSEM"));
		if(sem)sem = dg_hop(sem, lookup_fname("LOCAL"));
		if(sem)sem = dg_hop(sem, lookup_fname("CONT"));
		if(!sem)
		{
			//fprintf(stderr, "parse had no semantics!\n");
			continue;
		}
		if(sentence && !question)printf("SENT: %s\n", sentence);

		//print_dg(root);
		//printf("\n\n");

		print_sem(sem);
		if(!question)printf("\n\n");
		fflush(stdout);
//#ifdef	FIRST_ONLY
		if(!exhaustive)break;
//#endif
	}

	if(debug_level)
	{
		fprintf(stderr, "NOTE: %d readings. added %d / %d edges to chart (%d fully instantiated, %d actives used, %d passives used)\t",
					num_parses, total_edges, real_edges, passive_edges, used_edges, pused_edges);
		print_slab_stats();
		if(lui_chart)output_lui(words);
	}
	used_total += used_edges;
	pused_total += pused_edges;

	return num_parses;
}

void	final_chart_stats()
{
	fprintf(stderr, "NOTE: %lld packings, %lld / %lld = %.1f%% copies\n", packcount,
		copycount, copytraverse, 100*(float)copycount / copytraverse);
#ifdef TIM
	fprintf(stderr, "NOTE: unify was %.4fs\n", base_unify_time);
	fprintf(stderr, "NOTE: finalize was %.4fs\n", base_finalize_time);
	base_unify_time = 0;
	base_finalize_time = 0;
#endif
}

int	unfold_edge(struct edge	*e)
{
	int	i, j, sum = 0, count;
	struct edge	*E;

	nfinal++;
	if(e->rule)
	{
		nused[e->rule->rnum]++;
		//if(e->rule)fprintf(stderr, "%s\n", e->rule->name);
	}
	for(j=0;j<e->npack+1;j++)
	{
		count=1;
		E = (j==e->npack)?e:e->pack[j];
		for(i=0;i<E->have;i++)
			count *= unfold_edge(E->daughters[i]);
		sum += count;
	}

	return sum;
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

void	fix_ellipsis(struct dg	*rel);

void	print_sem(struct dg	*dg)
{
	int	idx;
	struct dg	*list, *rel, *hook, *savelist;

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
	if(question)
	{
		savelist = list;
		while(list)
		{
			rel = dg_hop(list, 1);	// 1 = FIRST
			if(rel)fix_ellipsis(rel);
			//else break;//fprintf(stderr, "list element had no FIRST!\n");

			list = dg_hop(list, 2);	// 2 = REST
		}
		list = savelist;
	}

	while(list)
	{
		rel = dg_hop(list, 1);	// 1 = FIRST
		if(rel)print_rel(rel);
		//else break;//fprintf(stderr, "list element had no FIRST!\n");

		list = dg_hop(list, 2);	// 2 = REST
	}
}

void	fix_ellipsis(struct dg	*rel)
{
	int		i, narg;
	char		afeat[32];
	struct dg	*pred;
	struct dg	*arg;
	static int	predf, argf[10], init = 0;

	if(!init)
	{
		predf = lookup_fname("PRED");
		for(i=0;i<10;i++)
		{
			sprintf(afeat, "ARG%d", i);
			argf[i] = lookup_fname(afeat);
		}
		init=1;
	}

	pred = dg_hop(rel, predf);
	if(!pred)
	{
		fprintf(stderr, "REL had no PRED\n");
		return;
	}
	if(strcmp(pred->xtype->name, "ellipsis_rel"))return;

	narg = 0;
	for(i=0;i<10;i++)
	{
		arg = dg_hop(rel, argf[i]);
		if(arg)narg=i+1;
	}
	if(narg!=2)
	{
		fprintf(stderr, "ellipsis_rel had %d args\n", narg);
		return;
	}
	arg = dg_hop(rel, argf[0]);
	i = skolemize(arg);
	if(i == base_index-1)
	{
		base_index--;
		arg = dg_hop(rel, argf[1]);
		i = skolemize(arg);
	}
	if(i==-1)
	{
		fprintf(stderr, "ellipsis's REL not skolemizable\n");
		return;
	}
}

void	print_rel(struct dg	*rel)
{
	int		i, idx, narg;
	char		afeat[32];
	struct dg	*pred, *carg;
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
	if(!pred)
	{
		fprintf(stderr, "REL had no PRED\n");
		return;
	}
	if(!strcmp(pred->xtype->name, "named_rel"))
		carg = dg_hop(rel, cargf);
	else carg = 0;

	if(pred->xtype->name[0]=='"')
	{
		strcpy(relstr, pred->xtype->name+1);
		if(relstr[0] && relstr[strlen(relstr)-1]=='"')
			relstr[strlen(relstr)-1] = 0;
	}
	else strcpy(relstr, pred->xtype->name);

	if(question) do
	{
		if(!strncmp(relstr, "bare_", 5))return;
		if(!strcmp(relstr, "unspec_rel"))return;
		if(!strcmp(relstr, "unspec_loc_rel"))return;
		if(!strcmp(relstr, "time_rel"))
		{
			// time_rel doesn't (easily) tell us how it connects
			printf("	tempwild 99999\n");
			return;
		}
		if(!strcmp(relstr, "ellipsis_rel"))return;
		if(!strcmp(relstr, "abstr_deg_rel"))return;
		if(!strcmp(relstr, "number_rel"))return;
		if(!strcmp(relstr, "person_rel"))return;
		if(strstr(relstr, "_q_"))return;
		if(strstr(relstr, "_n_"))break;
		if(!strcmp(relstr, "pron_rel"))break;
		if(!strcmp(relstr, "thing_rel"))
			strcpy(relstr, "nwild");
	} while(0);

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
		/*if(i==0 && idx==1)
			printf("	+%s", relstr);
		else */if(i==0)printf("	%s", relstr);
		printf(" %d", idx);
	}
	if(carg)
	{
		printf(" %s", carg->xtype->name);
		narg++;
	}
	if(narg==0)
	{
		//fprintf(stderr, "REL had no ARGs\n");
	}
	else printf("\n");
}

void	output_lui(char	*words[])
{
	int	i, j, k, l;
	struct edge	*e;
	extern char	*current_sentence;
	FILE	*lf = popen("yzlui", "w");

	fprintf(lf, "chart 1 %d \"%s\"", chart_size-1, current_sentence);
	for(i=0;i<chart_size-1;i++)
		fprintf(lf, " #E[%d %d %d %d \"%s\" orth [ ]]", -i-1, -i-1, i, i+1, words[i]);

	for(i=0;i<chart_size;i++)
		for(j=0;j<chart_size;j++)
			for(k=0;k<cells[j*chart_size + i].p_nedges;k++)
			{
				e = cells[j*chart_size + i].p_edges[k];
				if(e->rule)
				{
					fprintf(lf, " #E[%d %d %d %d %s %s []", e->id, e->id, j, i, e->rule->name, e->rule->name);
					for(l=0;l<e->have;l++)
						fprintf(lf, " %d", e->daughters[l]->id);
					fprintf(lf, "]");
				}
				else if(e->lex)
				{
					fprintf(lf, " #E[%d %d %d %d %s %s [] %d]", e->id, e->id, j, j, e->lex->word, e->lex->word, -1-j);
				}
			}
	fprintf(lf, "\n");
	fflush(lf);
	pclose(lf);
}
