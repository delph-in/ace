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

#define	NPRIO	11

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

	e->npack = 0;
	e->pack = 0;
#ifdef	BIDIR_SS
	if(e->prio < LEX_PRIO)
		e->prio = LEX_PRIO-1;
#endif

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

int	lexeme_in_agenda(struct lexeme	*lex, int	from, int	to)
{
	struct task	*t;

	for(t=agenda[LEX_PRIO].start;t;t=t->next)
#ifdef	GENERATION
		if(t->edge->lex==lex && t->edge->ep_span == 1<<from)
#else
		if(t->edge->lex==lex && t->edge->from==from && t->edge->to==to)
#endif
			return 1;
	return 0;
}

void	allow_edge_inflr(struct edge	*e, struct irule	*ir)
{
	if(!ir)
	{
		//printf("allow no irule on edge %d\n", e->id);
		e->allow_no_inflr = 1;
	}
	else
	{
		//printf("allow irule %s on edge %d\n", ir->rule->name, e->id);
		e->inflr_allow[ir->rule->rnum] = 1;
	}
}

void	allow_inflr(int	x, struct lexeme	*lex, struct irule	*ir)
{
	struct task	*t;

#ifdef	GENERATION
	for(t=lstart[0];t;t=t->next)
#else
	for(t=lstart[x];t;t=t->next)
#endif
	{
#ifdef	GENERATION
		if(t->edge->ep_span != 1<<x)continue;
#else
		if(!(t->edge->from==x && t->edge->to==x+1))break;
#endif
		if(t->edge->lex==lex)allow_edge_inflr(t->edge, ir);
	}
}

int	reduce_chart(int	length)
{
	struct task	*t1, *t2;
	struct queue	*lag = &agenda[LEX_PRIO];
	char		cover[length];
	int		i;

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
				cover[i] = 1;
			}
		}
	}

	// check that lexemes cover the whole span;
	// if not we have no chance of building a spanning analysis.
	for(i=0;i<length;i++)
		if(!cover[i])
		{
			fprintf(stderr, "NOTE: lexemes do not span position %d!\n", i);
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
	struct task	*t;

	for(t=lstart[x];t;t=t->next)
	{
		if(!(t->edge->from==x && t->edge->to==x+1))break;
		if(lexfirst(t->edge->dg->type, rule))return 0;
	}
	return 1;
}

int	lexrulefilt_l(int	x, int	rule)
{
	struct task	*t;

	for(t=lstart[x];t;t=t->next)
	{
		if(!(t->edge->from==x && t->edge->to==x+1))break;
		if(lexlast(t->edge->dg->type, rule))return 0;
	}
	return 1;
}
