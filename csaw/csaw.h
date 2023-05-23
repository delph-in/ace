struct tree;
struct lattice;
void	csaw_reset_pcfg_tokens();
void	csaw_add_pcfg_token(int	from, int	to, char	*term, char	*deriv, char	*surface);
void	csaw_parse_these_tokens(int	nbest, void (*visitor)(struct tree*,void*), void *visitorctx);
void	csaw_free_chart();
struct tree	*csaw_parse_lattice(struct lattice	*lexical_chart);

struct edge	*dublin_last_ditch_root_edge();
