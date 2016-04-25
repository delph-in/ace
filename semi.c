#define		_GNU_SOURCE
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<sys/time.h>
#include	<assert.h>
#include	<wctype.h>

#include	"tdl.h"
#include	"dag.h"
#include	"hash.h"
#include	"conf.h"
#include	"unicode.h"

struct type_hierarchy	*semi_th;

enum
{
	semi_variables = 1,
	semi_properties = 2,
	semi_roles = 3,
	semi_predicates = 4
};

int	semi_mode = 0;

int	isoperator(char	c)
{
	if(ispunct(c) && c!='-' && c!='_')return 1;
	else return 0;
}

struct type	*semi_super, *semi_top;

int	process_semi_command(char	*parent, int	lnum, char	*cmd)
{
	if(0) {}
	else if(!strcasecmp(cmd, "variables"))
	{
		semi_mode = semi_variables;
		printf("adding semi-variable as a subtype of %p\n", semi_top);
		add_type_only("semi-variable", 1, &semi_top);
		semi_super = lookup_type("semi-variable");
	}
	else if(!strcasecmp(cmd, "properties"))
	{
		semi_mode = semi_properties;
		add_type_only("semi-property", 1, &semi_top);
		semi_super = lookup_type("semi-property");
	}
	else if(!strcasecmp(cmd, "roles"))
	{
		semi_mode = semi_roles;
		semi_super = NULL;
	}
	else if(!strcasecmp(cmd, "predicates"))
	{
		semi_mode = semi_predicates;
		add_type_only("semi-predicate", 1, &semi_top);
		semi_super = lookup_type("semi-predicate");
	}
	else
	{
		fprintf(stderr, "%s:%d:  unknown SEMI command: `%s'\n", parent, lnum, cmd);
		exit(-1);
	}
	return 0;
}

char	*semi_tokenize(char	**P, int	*L)
{
	char	*p = *P;
	// skip to end of whitespace
	while(isspace(*p))
	{
		if(*p=='\n')(*L)++;
		p++;
	}
	*P = p;
	// anything left?
	if(!*p)return NULL;
	// skip comments
	if(*p == ';')
	{
		while(*p && *p!='\n')p++;
		*P=p;
		return semi_tokenize(P,L);
	}
	// punctuation tokens are single letter tokens
	if(isoperator(*p))
	{
		char	*tok = malloc(2);
		tok[0]=*p++;
		tok[1]=0;
		*P=p;
		return tok;
	}
	// all other tokens extend until whitespace or a punctuation mark
	else
	{
		char	*ts = p;
		while(*p && !isspace(*p) && !isoperator(*p))p++;
		char	*tok = malloc(p-ts+1);
		memcpy(tok,ts,p-ts);
		tok[p-ts]=0;
		*P=p;
		return tok;
	}
}

semi_define(char	*name, int	npar, struct type	**par)
{
	if(npar)add_type_only(name, npar, par);
	else add_type_only(name, 1, &semi_super);
}

semi_declare_role(char	*role, char	*value)
{
	printf("semi role: %s : %s\n", role, value);
}

// find_type is from tdl.c
struct type	*find_type(char	*name, int	constrain);

process_semi_declaration(char	*path, char	**P, int	*Lnum, char	*lhs)
{
	printf("processing declaration for %s:%d `%s'\n", path, *Lnum, lhs);
	char	*end = semi_tokenize(P, Lnum);
	assert(end && *end && isoperator(*end));
	switch(semi_mode)
	{
		case	semi_variables:;
		case	semi_properties:;
		case	semi_predicates:;
			int	npars = 0;
			struct type	*pars[100];
			// read supertypes
			while(*end!='.' && *end!=':')
			{
				assert(*end=='<' || *end=='&');
				free(end);
				char	*parent = semi_tokenize(P, Lnum);
				assert(parent && *parent);
				pars[npars++] = find_type(parent, 0);
				end = semi_tokenize(P, Lnum);
				assert(end&&*end);
				assert(npars<100);
			}
			// read appropriate properties / roles
			if(semi_mode == semi_properties)assert(*end=='.');
			else while(*end!='.')
			{
				assert(*end==':' || *end==',');
				free(end);
				char	*feature = semi_tokenize(P, Lnum);
				assert(feature && *feature);
				char	*value = semi_tokenize(P, Lnum);
				assert(value && *value);
				printf("appropriate to %s: [ %s %s ]\n", lhs, feature, value);
				end = semi_tokenize(P, Lnum);
				assert(end&&*end);
			}
			free(end);
			semi_define(lhs, npars, pars);
			break;
		case	semi_roles:;
			assert(*end==':');
			free(end);
			char	*value = semi_tokenize(P, Lnum);
			assert(value && *value);
			semi_declare_role(lhs, value);
			if(**P=='.')
			{
				end = semi_tokenize(P, Lnum);
				free(end);
			}
			break;
		default: assert(!"no semi mode!");
	}
}

load_semi_path(char	*path)
{
	FILE	*f = fopen(path, "r");
	if(!f){perror(path);exit(-1);}
	fseek(f, 0, SEEK_END);
	long	len = ftell(f);
	rewind(f);
	char	*buffer = malloc(len+1);
	assert(len == fread(buffer, 1, len, f));
	buffer[len] = 0;
	fclose(f);

	char	*p = buffer, *key;
	int	lnum = 1;
	while(key = semi_tokenize(&p, &lnum))
	{
		assert(*key);
		if(*p==':')
		{
			char	*colon = semi_tokenize(&p, &lnum);
			free(colon);
			if(!strcmp(key, "include"))
			{
				char	*file = semi_tokenize(&p, &lnum);
				assert(file && *file);
				char	*iname = path_for_include(path, file, NULL);
				//fprintf(stderr, "SEMI INCLUDE %s\n", iname);
				load_semi_path(iname);
				free(iname);
				free(file);
			}
			else process_semi_command(path, lnum, key);
		}
		else process_semi_declaration(path, &p, &lnum, key);
		free(key);
	}
	free(buffer);
}

int	load_semi(char	*path)
{
	path = "erg.smi";
	semi_th = new_type_hierarchy("semi", semi_top_type);
	struct type_hierarchy	*main_th = default_type_hierarchy();
	set_default_type_hierarchy(semi_th);
	semi_top = lookup_type(semi_top_type);
	load_semi_path(path);
	make_glbs();
	set_default_type_hierarchy(main_th);
}
