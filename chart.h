#include	"dag.h"

struct edge
{
	struct dg	*dg;
	struct rule	*rule;
	short		have, need;
	struct edge	**daughters;
	int		from, to;
	int		id;

	struct lexeme	*lex;	// non-null if this is a lexical edge
	int		rtl;	// right-to-left instantiation?
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
