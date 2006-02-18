struct decomposition
{
	struct edge *edge;
	struct edge *rhs[2];
	float		score;

	int	*seeni1;
	int	ni2, ai2;
};

struct hypothesis
{
	struct decomposition *dec;
	struct hypothesis	*rhs[2];
	int					indices[2];
	struct dg			*dg;		// reconstructed
	char				*string;	// surface yield
	float				score;
	int					checkup;

	struct hypothesis *left, *right;
};

struct unpack_info
{
	struct hypothesis *agenda;
	struct hypothesis **hypotheses;
	int	nhypotheses, ahypotheses;
};

struct mrs;
int iterate_cell_root_hypotheses(struct chart_cell *cell, int (*process)(struct hypothesis *hyp, struct mrs *mrs), int nbest);
struct hypothesis *hypothesize_edge(struct edge *e, int rank);
