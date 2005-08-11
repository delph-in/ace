#ifndef	CHART_H
#define	CHART_H

#include	"dag.h"
#include	"morpho.h"

#define	GENERATION

#define	BIDIR_SS

struct edge
{
	struct dg	*dg;
	struct rule	*rule;
	signed char		have, need, from, to;
	struct edge	**daughters;
	int		id, age;

	struct lexeme	*lex;		// non-null if this is a lexical edge

	struct qc	*qc;	// for passive edges, qc vector for whole edge;
				// for active edges, qc vector for next daughter

	unsigned	rtl:1,used:1;	// right-to-left instantiation? active been used?
	unsigned	prio:4;

	unsigned	ha_use:4;	// how many times have we been summoned?
	// what dg to unify in to reconstruct this edge's dg
	char		ha_arg:4;
	struct dg	*ha_dg, *ha_par;
	struct edge	*hex;	// what edge did we try to combine with on hyperactive excursion?

	int	ninflr_routes;
	struct inflr_route
	{
		int len;
		int *rules;
	}	*inflr_routes;
	// 1 for allowed inflr's, 0 for disallowed
	//char		*inflr_allow;
	//int		allow_no_inflr;

	struct unpack_info *unpack;

	unsigned int	ep_span;

	int			ntries;
	int			npack;
	struct edge	**pack;
};

struct chart_cell
{
	int		p_nedges;
	struct edge	**p_edges;
	int		al_nedges;
	struct edge	**al_edges;
	int		ar_nedges;
	struct edge	**ar_edges;
};

struct forest
{
	int 		ntrees;
	struct edge **trees;
	float		*scores;
};

#define	LEX_PRIO	10

int		parse(int	nwords, char	*words[], char	*sentence);
void	add_edge_to_chart_cell(struct chart_cell	*cell, struct edge	*edge);
void	add_edge_to_chart(struct edge	*edge);
int		combine(struct edge	*a, struct edge	*b);
int		pack_edge(struct edge	*edge);
void	clear_chart(int	size);
void	free_edge(struct edge	*edge);
int		next_eid();
int		reduce_chart(int	length);
void	output_lui(char	*words[]);
void	final_chart_stats();
char	*show_orth_leaves(struct edge	*e);

void	clear_agenda();
void	add_agenda(struct edge	*e);
struct edge	*next_agenda(int	from_start);

extern struct chart_cell	*cells;
extern unsigned int			require_ep_span;

#define PARSING 1
#define GENERATING 2
extern int g_mode;

#endif
