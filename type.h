#ifndef	_TYPE_H
#define	_TYPE_H

#include	<stdlib.h>

struct type
{
	char		*name;
	struct dg	*dg;	// indefeasible constraints
	//struct type	*par, *parb;	// multiple inheritance, binary only
	int		ndescendents;	// this could get long-ish! :-P
	int		*descendents;	// we should probably do a bit vector

	int		tnum;	// my type number
};

struct type	*get_top();
struct type	*lookup_type(char	*name);
char	*trim_string(char	*str);
int	descendent_of(int	dec, struct type	*of);
void	inval_thash();
void	setup_glb_lut();
int	add_type_only(char	*name, int	npar, struct type	**par);
void	number_strings();
int		load_types();

static inline struct type	*glb(struct type	*a, struct type	*b)
{
	int					x=0, y=0;
	extern struct type	*full_glb_calc(struct type	*a, struct type	*b, int	x, int	y);
	extern short		**glb_lut;
	extern struct type	*strtype, **types;
	extern int	g_loaded;

	if(a==b)return a;

	if(a->name[0]=='"')
	{
		if(b->name[0]=='"')return 0;
		else if(strtype && descendent_of(strtype->tnum, b))return a;
		else return 0;
	}
	if(b->name[0]=='"')
	{
		if(strtype && descendent_of(strtype->tnum, a))return b;
		else return 0;
	}

	if(a->tnum==b->tnum)
	{
		fprintf(stderr, "ERROR: type numbers match but pointers do not for `%s', `%s'\n", a->name, b->name);
		exit(-1);
	}

	if(g_loaded)
	{
		if(a->tnum > b->tnum)y = a->tnum, x = b->tnum;
		else y = b->tnum, x = a->tnum;
		if(glb_lut[y][x] != -1)
			return types[glb_lut[y][x]];
	}

	return full_glb_calc(a, b, x, y);
}

#endif
