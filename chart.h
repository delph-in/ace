#include	"dag.h"

struct edge
{
	struct dg	*dg;
	struct rule	*rule;
	short		have, need;
	struct edge	**daughters;
	short		from, to;
	int		id;

	struct lexeme	*lex;		// non-null if this is a lexical edge
	short		rtl,used;	// right-to-left instantiation? active been used?

	struct qc	*qc;	// for passive edges, qc vector for whole edge;
				// for active edges, qc vector for next daughter
};

struct chart_cell
{
	int		nedges;
	struct edge	**edges;
};

void	add_edge_to_chart_cell(struct chart_cell	*cell, struct edge	*edge);
void	add_edge_to_chart(struct edge	*edge);
void	clear_chart(int	size);
void	free_edge(struct edge	*edge);
