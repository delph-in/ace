#ifndef	CHART_H
#define	CHART_H

#include	"dag.h"

struct edge
{
	struct dg	*dg;
	struct rule	*rule;
	signed char		have, need, from, to;
	struct edge	**daughters;
	int		id;

	struct lexeme	*lex;		// non-null if this is a lexical edge

	struct qc	*qc;	// for passive edges, qc vector for whole edge;
				// for active edges, qc vector for next daughter

	unsigned	rtl:1,used:1;	// right-to-left instantiation? active been used?
	unsigned	prio:4;
	unsigned	ha_use:4;	// how many times have we beensummoned?
	// what dg to unify in to reconstruct this active edge's dg
	char		ha_arg:4;
	struct dg	*ha_dg, *ha_par;
};

struct chart_cell
{
	int		nedges;
	struct edge	**edges;
};

#define	LEX_PRIO	10

int	parse(int	nwords, char	*words[], char	*sentence);
void	add_edge_to_chart_cell(struct chart_cell	*cell, struct edge	*edge);
void	add_edge_to_chart(struct edge	*edge);
void	clear_chart(int	size);
void	free_edge(struct edge	*edge);
int	next_eid();
int	lexeme_in_agenda(struct lexeme	*lex, int	from, int	to);
int	reduce_chart(int	length);
void	output_lui(char	*words[]);

void	clear_agenda();
void	add_agenda(struct edge	*e);
struct edge	*next_agenda();

#endif
