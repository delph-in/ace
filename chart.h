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
	// what dg to unify in to reconstruct this active edge's dg
	char		ha_arg;
	struct dg	*ha_dg, *ha_par;
};

struct chart_cell
{
	int		nedges;
	struct edge	**edges;
};

#define	LEX_PRIO	10

void	add_edge_to_chart_cell(struct chart_cell	*cell, struct edge	*edge);
void	add_edge_to_chart(struct edge	*edge);
void	clear_chart(int	size);
void	free_edge(struct edge	*edge);

void	clear_agenda();
void	add_agenda(struct edge	*e);
struct edge	*next_agenda();
