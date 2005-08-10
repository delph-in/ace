int		load_mem(char *fname);
void	*freeze_stochastic_model();
void	recover_stochastic_model(void *frozen);
float	maxent_score(char *lhs, char *rhs1, char *rhs2);
