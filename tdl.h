#include	"dag.h"

char	*trim_string(char	*str);
int	iterate_tdl(char	*path, int	(*callback)(char	*lhs, char	*rhs, void	*ref), void	*ref);
int	iterate_tdl_dgs(char	*path, int	(*callback)(char	*rhs, struct dg	*dg));
