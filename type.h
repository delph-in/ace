#ifndef	_TYPE_H
#define	_TYPE_H

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
struct type	*glb(struct type	*a, struct type	*b);

#endif
