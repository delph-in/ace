#include	<stdlib.h>
#include	<stdio.h>
#include	<string.h>
#include	<signal.h>
#include	<time.h>

#include	"freeze.h"
#include	"chart.h"
#include	"token.h"
#include	"lattice-mapping.h"
#include	"timer.h"

static void	skip_whitespace(char	**P)
{
	while(**P == ' ' || **P == '\t' || **P == '\n')
		*P = 1 + *P;
}

static char	peek(char	**P)
{
	skip_whitespace(P);
	return **P;
}

static void expect(char	**P, char	ex)
{
	skip_whitespace(P);
	if(**P != ex)
	{
		fprintf(stderr, "expected '%c', but input is %.16s\n", ex, *P);
		exit(-1);
	}
	*P = 1 + *P;
}

static int read_int(char	**P)
{
	skip_whitespace(P);
	int x = atoi(*P);
	while(**P == '+' || **P == '-')
		*P = 1 + *P;
	while(isdigit(**P))
		*P = 1 + *P;
	return x;
}

static double read_double(char	**P)
{
	skip_whitespace(P);
	double	x = atof(*P);
	read_int(P);
	if(peek(P) == '.')
	{
		*P = 1 + *P;
		while(isdigit(**P))
			*P = 1 + *P;
	}
	return x;
}

static char	*read_string(char	**P)
{
	skip_whitespace(P);
	expect(P, '"');
	char	build[1024];
	int		l = 0;
	while(**P != '"' && **P != 0)
	{
		if(**P == '\\')
		{
			*P = 1 + *P;
			build[l++] = **P;
		}
		else build[l++] = **P;
		*P = 1 + *P;
		if(l > 1000)
		{
			fprintf(stderr, "string was too long\n");
			exit(-1);
		}
	}
	expect(P, '"');
	build[l] = 0;
	return freeze_string(build);
}

struct token	yy_read_token(char	**yy, int	*vfrom, int	*vto)
{
	struct token	T;
	bzero(&T,sizeof(T));
	expect(yy, '(');
	read_int(yy);	// id
	expect(yy, ',');
	*vfrom = read_int(yy);
	expect(yy, ',');
	*vto = read_int(yy);
	expect(yy, ',');
	expect(yy, '<');
	T.cfrom = read_int(yy);
	expect(yy, ':');
	T.cto = read_int(yy);
	expect(yy, '>');
	expect(yy, ',');
	read_int(yy);	// ?
	expect(yy, ',');
	T.string = read_string(yy);
	expect(yy, ',');
	read_int(yy);	// ?
	expect(yy, ',');
	read_string(yy);	// ?
	expect(yy, ',');
	while(peek(yy) == '"')
	{
		char	*tag = read_string(yy);
		double	prob = read_double(yy);	// probability
		T.npostags++;
		T.postags = slab_realloc(T.postags, sizeof(struct postag)*(T.npostags-1), sizeof(struct postag)*T.npostags);
		T.postags[T.npostags-1].tag = tag;
		char	str[64]; sprintf(str, "%f", prob);
		T.postags[T.npostags-1].prob = freeze_string(str);
	}
	expect(yy, ')');
	build_token_dag(&T);
	return T;
}

int	parse_yy(char	*yy)
{
	clock_t	start = clock();
	struct lattice	*tc = slab_alloc(sizeof(*tc));
	bzero(tc, sizeof(*tc));
	struct lattice_vertex	*get_vtx(int	id)
	{
		while(id >= tc->nvertices)
			new_lattice_vertex(tc);
		return tc->vertices[id];
	}
	while(peek(&yy) == '(')
	{
		struct lattice_edge	*e = slab_alloc(sizeof(*e));
		bzero(e, sizeof(*e));
		int	vfrom, vto;
		e->token = slab_alloc(sizeof(struct token));
		bzero(e->token, sizeof(struct token));
		*e->token = yy_read_token(&yy, &vfrom, &vto);
		e->token->eid = next_eid();
		e->from = get_vtx(vfrom);
		e->to = get_vtx(vto);
		add_lattice_edge(tc, e);
	}
	int i, j;
	char	notstart[tc->nvertices];
	bzero(notstart, sizeof(notstart));
	tc->start = tc->end = NULL;
	for(i=0;i<tc->nvertices;i++)
	{
		if(tc->vertices[i]->nfollowers == 0)
		{
			if(tc->end == NULL)
				tc->end = tc->vertices[i];
			else assert(!"YY mode: input token lattice had non-unique end vertex");
		}
		else for(j=0;j<tc->vertices[i]->nfollowers;j++)
			notstart[tc->vertices[i]->followers[j]->to->id] = 1;
	}
	for(i=0;i<tc->nvertices;i++)
		if(!notstart[i])
		{
			if(tc->start == NULL)
				tc->start = tc->vertices[i];
			else assert(!"YY mode: input token lattice had non-unique start vertex");
		}
	return parse_with_token_chart(tc, start);
}
