#ifndef	CHART_H
#define	CHART_H

#include	"dag.h"
#include	"morpho.h"

//#define	RULE_STATS

#undef	CLOCKCOUNT
#define	IFCLOCK(code)

struct edge
{
	struct dg	*dg;
	struct rule	*rule;
	signed char		have, need, from, to;
	struct edge	**daughters;
	int			id:29;
	unsigned	built_on:1, frozen:1, frosted:1;

	struct lexeme	*lex;		// non-null if this is a lexical edge

	struct qc	*qc;	// for passive edges, qc vector for whole edge;
				// for active edges, qc vector for next daughter

	unsigned	rtl:1,used:1;	// right-to-left instantiation? active been used?
	unsigned	prio:6;

	unsigned	ha_use:4;	// how many times have we been summoned?
	// what dg to unify in to reconstruct this edge's dg
	char		ha_arg:4;
	struct dg	*ha_dg, *ha_par;
	struct edge	*hex;	// what edge did we try to combine with on hyperactive excursion?

	int	north_routes;
	struct orth_route
	{
		int len;
		int *rules;
	}	*orth_routes;

	struct unpack_info *unpack;

	unsigned long long	ep_span;
	unsigned long long	accessible_vars, inaccessible_vars;

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

// general chart processing equipment
int		combine(struct edge	*a, struct edge	*b);
int	filter(struct edge	*a, struct edge	*b);
int	unify_and_build1(struct edge	*a, struct edge	*b, struct edge	**Eout, int	force_noha);
int	unify_and_build(struct edge	*a, struct edge	*b);
void	add_edge_to_chart_cell(struct chart_cell	*cell, struct edge	*edge);
void	add_edge_to_chart(struct edge	*edge);
int		recall_hyper(struct edge	*b);
void	clear_chart(int	size);
void	free_edge(struct edge	*edge);
int		next_eid();

// parser
int	parse(int	nwords, char	**words, int	*cfrom, int	*cto, char	*sentence);

// display
void	output_lui_chart(char	*words[]);
void	output_lui_dg(struct dg	*dg, char *wtitle);
struct mrs;
int		output_lui_trees(struct chart_cell	*c, char *sent, int best_only, struct mrs	*require_mrs);
char	*label_dag(struct dg	*dg, char *fallback);
void	final_chart_stats();
char	*show_orth_leaves(struct edge	*e);

// agenda
struct edge	*next_agenda(int	from_start);
void		clear_agenda();
void		add_agenda(struct edge	*e);
int			reduce_chart(int	length);//, char **words);

// packing
int			pack_edge(struct edge	*edge);
struct dg	*restrict_copy_dg(struct dg	*dg, int for_generation);

// checking rule compatibility
int	check_lexical_rule_constraints(struct rule	*rule, struct edge	*b);
void	setup_lexrule_edge(struct edge	*e);

extern struct chart_cell	*cells;
extern int					chart_size;
extern unsigned long long	require_ep_span;

extern int enable_generalize_edge_top_types;

#define PARSING 1
#define GENERATING 2
extern int g_mode;

#endif
