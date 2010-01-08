#include	"maxent.h"

struct decomposition
{
	struct edge *edge;
	struct edge *rhs[2];

	int	*seeni1;
	int	ni2, ai2;
};

struct hypothesis
{
	// internal to a hypothesis as a tree
	// even if there are several ways of scoring a tree (different grandparents),
	// this portion remains constant and can be a shared structure in theory...
	struct decomposition *dec;
	struct hypothesis	*rhs[2];
	struct dg			*dg;		// reconstructed
	char				*string;	// surface yield
	int					checkup;

	// internal to a hypothesis as a scored tree
	// this part will be different for different ancestry contexts
	int					indices[2];
	float				score;
	struct scoring_ancestry	ancestry;
};

struct hypagenda_item
{
	struct hypothesis	*h;

	// as an element of an agenda
	struct hypagenda_item *left, *right;
};

struct unpack_info	// used by hypothesize_edge() to keep track of what hypothesis is ranked next, and to cache
{
	struct context_unpack_info
	{
		struct hypagenda_item *agenda;
		struct hypothesis **hypotheses;
		int	nhb, nhypotheses, ahypotheses;

		struct scoring_ancestry	ancestry;
	}	**cuis;
	int	ncuis;
	// could also include a list of (struct hypothesis*) for reuse...
};

struct mrs;
int iterate_cell_root_hypotheses(struct chart_cell *cell, int (*process)(struct hypothesis *hyp, struct mrs *mrs), int nbest);
struct hypothesis *hypothesize_edge(struct edge *e, int rank, struct scoring_ancestry	ancestry, struct hypagenda_item	**customAgenda);
