#include <stdio.h>

struct mrs_var
{
	char		*name, *type;
	struct mrs_var_property
	{
		char	*name;
		char	*value;
	}			*props;
	short		nprops:15, is_const:1;
	short		refs;
};

struct mrs_ep
{
	char			*pred;
	struct mrs_var	*lbl;
	struct mrs_ep_role
	{
		char			*name;
		struct mrs_var	*value;
	}	*args;
	int		nargs;
};

struct mrs_hcons
{
	struct mrs_var	*hi, *lo;
	enum mrs_hcons_type
	{
		hcons_qeq,
		hcons_leq,
		hcons_geq
	}	type;
};

struct mrs_vlist
{
	int				nvars;
	struct mrs_var	**vars;
};

struct mrs
{
	struct mrs_var	*ltop, *index, *xarg;
	struct mrs_ep	*rels;
	int				nrels;
	struct mrs_hcons *hcons;
	int				nhcons;
	struct mrs_vlist vlist;
	char			**triggers;
	int				ntriggers;
};

struct mrs	*read_mrs(FILE	*f);
void		free_mrs(struct mrs	*m);
