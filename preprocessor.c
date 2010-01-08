#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<assert.h>
#include	<librepp.h>

#include	"dag.h"
#include	"tdl.h"

struct preprocessor	*repp;

int	load_repp_module_from_file(char	*fname)
{
	return repp_load_module(repp, fname, NULL, 1);
}

int	load_repp(char	*fname)
{
	repp = repp_load(fname);
	return repp?0:-1;
}

void	*freeze_repp()
{
	return repp_clone(repp, slab_alloc);
}

void	recover_repp(void	*ptr)
{
	repp = ptr;
	repp_compile_rules(repp);
}

void	preprocess_line(char	*mbstring, int	*Nwords, char	***Words, int	**CFrom, int	**CTo)
{
	int	result = repp_preprocess(repp, mbstring, Nwords, Words, CFrom, CTo);
	assert(result == 0);
}

void	load_preprocessor(char	*confdir)
{
	load_repp(get_conf_file_path(confdir, "preprocessor"));
	int	repp_module_callback(char	*path) {
		char	*modpath;
		asprintf(&modpath, "%s/%s", confdir, path);
		return load_repp_module_from_file(modpath); }
	iterate_conf_list("preprocessor-modules", repp_module_callback);
}
