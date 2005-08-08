#include	"dag.h"

char	*trim_string(char	*str);
int	process_tdl_dgs_status(char	*status, int	(*callback)(char	*lhs, struct dg	*dg));
int	process_tdl_status(char	*status, int	(*callback)(char	*lhs, char	*rhs, void	*ref), void	*ref);
//int	iterate_tdl(char	*path, int	(*callback)(char	*lhs, char	*rhs, void	*ref), void	*ref);
//int	iterate_tdl_dgs(char	*path, int	(*callback)(char	*rhs, struct dg	*dg));
//int	iterate_tdl_types(char	*path);
//int	iterate_tdl_lex(char	*path);
