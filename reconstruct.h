#include	"tree.h"

#include	"hash.h"
#include	"dag.h"
#include	"rule.h"
#include	"lexicon.h"
#include	"mrs.h"

struct dg	*reconstruct_tree(struct tree	*t, void	(*callback)(struct tree	*t, struct dg	*d));
void	free_mrs_stuff(struct mrs	*m);
struct lexeme	*get_lex_by_name_hash(char	*name);

extern int	reconstruction_noise_level;
