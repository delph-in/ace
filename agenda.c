#include	<stdlib.h>
#include	<stdio.h>

#include	"chart.h"

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

void	clear_agenda()
{
	int	i;

	for(i=0;i<NPRIO;i++)
	{
		agenda[i].start = 0;	// stuff is slab-alloc'd, don't worry about freeing
		agenda[i].end = 0;
	}
	highest = -1;
}

void	add_agenda(struct edge	*e)
{
	struct task	*t = slab_alloc(sizeof(struct task));

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

	if(e->prio > highest)highest = e->prio;
}

struct edge	*next_agenda()
{
	struct task	*best;
	int		i;

	for(i=highest;i>=0;i--) if(agenda[i].start)
	{
		// pop most recent edge from this queue
		best = agenda[i].end;
		agenda[i].end = best->prev;
		if(best==agenda[i].start)
		{
			agenda[i].start = 0;
			highest--;
		}
		else highest = best->edge->prio;
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
		if(t->edge->lex==lex && t->edge->from==from && t->edge->to==to)
			return 1;
	return 0;
}
