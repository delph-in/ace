#include	<stdlib.h>
#include	<stdio.h>

#include	"chart.h"

extern int	debug_level;

static int		nagenda, aagenda;
static struct edge	**agenda;

void	clear_agenda()
{
	nagenda = 0;
}

void	add_agenda(struct edge	*e)
{
	int	i;

	nagenda++;
	if(nagenda>aagenda)
	{
		aagenda += 100;
		agenda = realloc(agenda, sizeof(struct edge*)*aagenda);
	}

	/*if(e->prio)
	{
		for(i=0;i<nagenda-1;i++)
			if(e->prio > agenda[i]->prio)break;
		memmove(agenda+i+1, agenda+i, sizeof(struct edge*)*(nagenda-1-i));
		agenda[i] = e;
	}*/
	agenda[nagenda-1] = e;
}

struct edge	*next_agenda()
{
	int	i, j, k;
	struct edge	*edge;

	if(nagenda)
	{
		// find highest priority edge on queue
		//j = 0;
		j=nagenda-1, k = 0;
		for(i=0;i<nagenda;i++)
			if(agenda[i]->prio > k)j=i, k=agenda[i]->prio;
		edge = agenda[j];
		nagenda--;
		memmove(agenda+j, agenda+j+1, sizeof(struct edge*)*(nagenda-j));
		return edge;
	}
	else return 0;
}
