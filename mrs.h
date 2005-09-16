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
	int			vnum;

	struct dg		*dg;
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
	int			nargs:15;
	int			active:1;	// used in semantic lookup enumeration stage
	unsigned	epnum:16;	// so we can just pass mrs_ep's around and know their ids

	struct dg	*dg;
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

void		print_mrs(struct mrs	*mrs);
void		print_mrs_var(struct mrs_var	*var);
struct mrs	*parse_mrs(char	*buffer);
struct mrs	*read_mrs(FILE	*f);
void		free_mrs(struct mrs	*m);
struct mrs_var	*mrs_find_ep_role(struct mrs_ep	*ep, char	*role);
void		semantic_lookup(struct mrs	*mrs);
int			mrs_subsumes(struct mrs	*in, struct mrs	*out);
struct mrs	*extract_mrs(struct dg	*root);
void		dagify_mrs_var(struct mrs_var	*var, struct dg	*skolem);
void		dagify_mrs_ep(struct mrs_ep *ep);
struct dg	*dagify_rels_from_eps(int neps, struct mrs_ep	*eps[]);
void		*freeze_vpm();
void		recover_vpm(void *vpm);
struct mrs_var	*internal_to_external_mrs_var(struct mrs_var	*vin);
struct mrs_var	*external_to_internal_mrs_var(struct mrs_var	*vin);
