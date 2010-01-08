#include	"dag.h"

extern char	*g_cons_type;
extern char	*g_list_type;
extern char	*g_null_type;
extern char	*g_diff_list_type;

char	*trim_string(char	*str);
int	process_tdl_dgs_status(char	*status, int	(*callback)(char	*lhs, struct dg	*dg));
int	process_tdl_status(char	*status, int	(*callback)(char	*lhs, char	*rhs, void	*ref), void	*ref);
//int	iterate_tdl(char	*path, int	(*callback)(char	*lhs, char	*rhs, void	*ref), void	*ref);
//int	iterate_tdl_dgs(char	*path, int	(*callback)(char	*rhs, struct dg	*dg));
//int	iterate_tdl_types(char	*path);
//int	iterate_tdl_lex(char	*path);

char	*get_conf(char	*lhs);
char	*get_conf_unq(char	*lhs);
char	*get_conf_file_path(char	*dir, char	*lhs);
