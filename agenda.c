#include	<stdlib.h>
#include	<string.h>
#include	<assert.h>
#include	<stdio.h>

#include	"chart.h"
#include	"morpho.h"
#include	"lexicon.h"

extern int	debug_level;

struct task
{
	struct task	*next, *prev;
	struct edge	*edge;
};

// WARNING: only 6 bits are currently allocated to ->prio in chart.h
#define	NPRIO	64	//11

#define	EPSILON_PRIORITY(rule)	(NPRIO-1)
#define	EDGE_PRIORITY(edge)		(NPRIO-1 - (edge->to-edge->from))
#define	LEX_PRIO				(NPRIO-2)

static struct queue
{
	struct task	*start, *end;
}	agenda[NPRIO];

int	highest = -1;

struct task	*lstart[500];

void	clear_agenda()
{
	int	i;

	for(i=0;i<NPRIO;i++)
	{
		agenda[i].start = 0;	// stuff is slab-alloc'd, don't worry about freeing
		agenda[i].end = 0;
	}
	bzero(lstart, sizeof(struct task*)*500);
	highest = -1;
}

void	add_agenda(struct edge	*e)
{
	struct task	*t = slab_alloc(sizeof(struct task));

	e->ntries = 0;
	e->npack = 0;
	e->pack = 0;
	e->unpack = 0;

	if(e->from==e->to)e->prio = EPSILON_PRIORITY(e->rule);
	else e->prio = EDGE_PRIORITY(e);

	if(e->prio < 0 || e->prio >= NPRIO)
	{
		fprintf(stderr, "tried to agendize an edge with priority %d!\n", e->prio);
		exit(-1);
	}

	t->edge = e;
	t->next = 0;
	t->prev = agenda[e->prio].end;
	if(agenda[e->prio].end)agenda[e->prio].end->next = t;
	else agenda[e->prio].start = t;
	agenda[e->prio].end = t;

	if(e->lex && !lstart[e->from])lstart[e->from] = t;

	if(e->prio > highest)highest = e->prio;
}

struct edge	*next_agenda(int	from_start)
{
	struct task	*best;
	int		i;

	for(i=highest;i>=0;i--) if(agenda[i].start)
	{
		if(from_start)
		{
			// pop least recent edge from this queue
			best = agenda[i].start;
			agenda[i].start = best->next;
			if(best==agenda[i].end)
			{
				agenda[i].end = 0;
				highest = best->edge->prio - 1;
			}
			else highest = best->edge->prio;
		}
		else
		{
			// pop most recent edge from this queue
			best = agenda[i].end;
			agenda[i].end = best->prev;
			if(best==agenda[i].start)
			{
				agenda[i].start = 0;
				highest = best->edge->prio - 1;
			}
			else highest = best->edge->prio;
		}
		if(best->next)best->next->prev=best->prev;
		if(best->prev)best->prev->next=best->next;
		return best->edge;
	}
	return 0;
}

void add_agenda_lrule(int x, struct rule *rule)
{
	struct task	*t;
	int			prio = EPSILON_PRIORITY(rule);

	for(t=agenda[prio].start;t;t=t->next)
	{
		//if(t->edge->rule)printf(" -- already %s [%d-%d]\n", t->edge->rule->name, t->edge->from, t->edge->to);
		if(t->edge->rule==rule &&
			((g_mode==GENERATING && t->edge->ep_span == 1<<x) ||
			(g_mode==PARSING && t->edge->from==x && t->edge->to==x)))
		{
			//printf("save work - `%s' was already in the chart for %d\n", rule->name, x);
			return;
		}
	}
	//printf("sadly, have to add `%s' at prio? %d\n", rule->name, prio);
	add_agenda(epsilon_edge(rule, x, 0, 0));
}

void	allow_edge_inflr_route(struct edge	*e, int nrules, int *rules)
{
	e->ninflr_routes++;
	e->inflr_routes = slab_realloc(e->inflr_routes,
		sizeof(struct inflr_route)*(e->ninflr_routes-1),
		sizeof(struct inflr_route)*e->ninflr_routes);
	e->inflr_routes[e->ninflr_routes-1].len = nrules;
	e->inflr_routes[e->ninflr_routes-1].rules = rules;
}

void sanity_check_agenda(int length)
{
	struct task *t1;
	struct queue	*lag = &agenda[LEX_PRIO];

	for(t1=lag->start;t1;t1=t1->next)
	{
		printf("#%d: %p `%s' from %d to %d\n", t1->edge->id,  t1, t1->edge->lex->word, t1->edge->from, t1->edge->to);
		assert(! (t1->edge->from<0 || t1->edge->to<0 || t1->edge->from>length || t1->edge->to>length));
	}
	printf("passed sanity\n");
}

int	reduce_chart(int	length, char **words)
{
	extern int debug_level;
	struct task	*t1, *t2;
	struct queue	*lag = &agenda[LEX_PRIO];
	char		cover[1024];
	int		i;

	//sanity_check_agenda(length);
	bzero(cover, length);
	for(t1=lag->start;t1;t1=t1->next)
	{
		assert(t1->edge->lex);
		if(!t1->edge->lex->select)goto kept;
		//printf("%s selects %s\n", t1->edge->lex->word, t1->edge->lex->select->name);
		for(t2=lag->start;t2;t2=t2->next)
		{
			assert(t2->edge->lex);
			if(!t2->edge->lex->pred)continue;
			if(glb(t2->edge->lex->pred, t1->edge->lex->select))break;
		}
		if(!t2)
		{
			//printf("edge %d, lexeme %s dropped by reducer\n", t1->edge->id, t1->edge->lex->word);
			if(t1->next)t1->next->prev = t1->prev;
			if(t1->prev)t1->prev->next = t1->next;
			if(t1==lag->start)lag->start = t1->next;
			if(t1==lag->end)lag->end = t1->prev;
		}
		else
		{
			//printf("provided by %s with pred %s\n", t2->edge->lex->word, t2->edge->lex->pred->name);
			kept:
			for(i=t1->edge->from;i<t1->edge->to;i++)
			{
				//printf("%s provides %d\n", t1->edge->lex->word, i);
				//printf("`%s' from %d to %d\n", t1->edge->lex->word, t1->edge->from, t1->edge->to);
				cover[i] = 1;
			}
		}
	}

	// check that lexemes cover the whole span;
	// if not we have no chance of building a spanning analysis.
	for(i=0;i<length;i++)
		if(!cover[i])
		{
			if(debug_level)fprintf(stderr, "NOTE: lexemes do not span position %d `%s'!\n", i, words[i]);
			return -1;
		}

	return 0;
}

// can 'lt' fit in first position of rule?
static inline int	lexfirst(struct type	*lt, int	rule)
{
	extern char	**type_rftc;
	return type_rftc[rule][lt->tnum];
}

// can 'lt' fit in last position of rule?
static inline int	lexlast(struct type	*lt, int	rule)
{
	extern char	**type_rltc;
	return type_rltc[rule][lt->tnum];
}

int	lexrulefilt_f(int	x, int	rule)
{
	extern char	**type_rftc;
	struct task	*t;

	if(!type_rftc)return 0;
	for(t=lstart[x];t;t=t->next)
	{
		if(!(t->edge->from==x && t->edge->lex))break;
		if(lexfirst(t->edge->dg->type, rule))return 0;
	}
	return 1;
}

int	lexrulefilt_l(int	x, int	rule)
{
	extern char	**type_rltc;
	struct task	*t;
	int			s;

	if(!type_rltc)return 0;
	// for each starting position
	for(s=0;s<x;s++)
	{
		for(t=lstart[s];t;t=t->next)
		{
			if(!(t->edge->to==x && t->edge->lex))break;
			if(lexlast(t->edge->dg->type, rule))return 0;
		}
	}
	return 1;
}
