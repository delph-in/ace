#ifndef	_TYPE_H
#define	_TYPE_H

#include	<stdlib.h>

struct type
{
	char		*name;
	struct dg	*dg;	// indefeasible constraints
	//struct type	*par, *parb;	// multiple inheritance, binary only
	int		ndescendents;	// this could get long-ish! :-P
	unsigned short	*descendents;	// we should probably do a bit vector

	int		tnum;	// my type number

	// immediate parents and daughters
	int			nparents, ndaughters;
	struct type	**parents, **daughters;
};

extern int		ntypes;
extern struct type	**types;

struct type	*get_top();
struct type	*lookup_type(char	*name);
struct type	*add_string(char	*name);
char	*trim_string(char	*str);
int	descendent_of(int	dec, struct type	*of);
void	inval_thash();
void	setup_glb_lut();
int	add_type_only(char	*name, int	npar, struct type	**par);
void	number_strings();
int		load_types();
void	wellform(struct dg	*d, char	*typename);
void	print_glb_stats();
struct type	*temporary_string_type(char *string);
struct dg	*type_dg(char		*name);

struct type	*most_general_type_for_feat(int	feat);

int make_glbs();

void recover_typehash(void *frozen);
void *freeze_typehash();

struct type	*glb(struct type	*a, struct type	*b);
struct type	*lub(struct type	*a, struct type	*b);

struct dg	*find_instance(char	*name);

#endif
