#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

#include	"mrs.h"

void	free_mrs_var(struct mrs_var	*v)
{
	int	i;

	if(!v)return;
	v->refs--;
	if(v->refs>0)return;
	for(i=0;i<v->nprops;i++)
	{
		free(v->props[i].name);
		free(v->props[i].value);
	}
	free(v->name);
	if(v->type)free(v->type);
	if(v->props)free(v->props);
	free(v);
}

void	free_mrs_ep(struct mrs_ep	*ep)
{
	int	i;
	free(ep->pred);
	free_mrs_var(ep->lbl);
	for(i=0;i<ep->nargs;i++)
	{
		free(ep->args[i].name);
		free_mrs_var(ep->args[i].value);
	}
	if(ep->args)free(ep->args);
}

void	free_mrs_hcons(struct mrs_hcons	*hc)
{
	free_mrs_var(hc->hi);
	free_mrs_var(hc->lo);
}

void	free_mrs(struct mrs	*m)
{
	int i;
	if(!m)return;
	free_mrs_var(m->ltop);
	free_mrs_var(m->index);
	free_mrs_var(m->xarg);
	for(i=0;i<m->nrels;i++)
		free_mrs_ep(m->rels+i);
	if(m->rels)free(m->rels);
	for(i=0;i<m->nhcons;i++)
		free_mrs_hcons(m->hcons+i);
	if(m->hcons)free(m->hcons);
	free(m);
}

static char	*parse_word(char	**P, int advance)
{
	static char	*saved = 0;
	char	*p = *P, *ret;

	if(saved)
	{
		ret = saved;
		if(advance)saved = 0;
		//printf("sord `%s'\n", ret);
		return ret;
	}
	if(*p==' ')p++;
	ret = p;
	while(*p && *p!=' ')p++;
	if(*p==' '){*p=0;p++;}
	if(!advance)saved = ret;
	*P = p;
	//if(ret)printf("word `%s'\n", ret);
	return *ret?ret:0;
}

#define	FAIL(reason)	do { fprintf(stderr, "MRS parsing error: %s (near `%*s')\n", reason, 16, *P); return 0; } while(0)

struct mrs_var	*parse_mrs_const(char	**P)
{
	char	*c = parse_word(P, 1);
	struct mrs_var *var = calloc(sizeof(struct mrs_var), 1);
	var->is_const = 1;
	var->name = strdup(c);
	return var;
}

struct mrs_var	*parse_mrs_var(struct mrs_vlist	*vlist, char	**P)
{
	struct mrs_var	var, *V;
	int		defined = 0, i;
	char	*word;

	bzero(&var, sizeof(var));
	var.name = parse_word(P, 1);
	if(!var.name)FAIL("var lacks a name");
	if((word=parse_word(P,0)) && !strcmp("[", word))
	{
		char	*type;
		parse_word(P, 1);
		defined = 1;
		type = parse_word(P, 1);
		if(!type)FAIL("var lacks a type");
		var.type = strdup(type);
		
		while((word = parse_word(P,1)) && strcmp(word,"]"))
		{
			struct mrs_var_property *prop;
			var.nprops++;
			var.props = realloc(var.props, sizeof(struct mrs_var_property)*var.nprops);
			prop = var.props + var.nprops-1;
			// trim off pesky ':' at end of role name
			if(word[0]!=':' && word[strlen(word)-1]==':')
				word[strlen(word)-1] = 0;
			prop->name = strdup(word);
			prop->value = (word=parse_word(P,1))?strdup(word):0;
			//printf(" { prop `%s' value `%s' }\n", prop->name, prop->value);
		}
	}

	// done parsing, check for reentrancies
	for(i=0;i<vlist->nvars;i++)
		if(!strcmp(vlist->vars[i]->name, var.name))
		{
			if(defined)
			{
				var.name = vlist->vars[i]->name;
				*vlist->vars[i] = var;
			}
			vlist->vars[i]->refs++;
			return vlist->vars[i];
		}

	V = malloc(sizeof(var));
	*V = var;
	V->name = strdup(V->name);
	V->refs = 1;

	vlist->nvars++;
	vlist->vars = realloc(vlist->vars, sizeof(struct mrs_var*) * vlist->nvars);
	vlist->vars[vlist->nvars-1] = V;

	return V;
}

void	*parse_list(struct mrs_vlist	*vlist, char	**P, int	*N, void (*parse)(struct mrs_vlist *vlist, char **P, void *into))
{
	int			n = 0;
	void		**v = 0;
	char		*word;

	*N = 0;
	if(!(word=parse_word(P,1)) || strcmp(word, "<"))FAIL("list lacks initial '<'");
	while((word=parse_word(P,0)) && strcmp(word, ">"))
	{
		n++;
		v = realloc(v, sizeof(void*)*n);
		parse(vlist,P, &v[n-1]);
	}
	if(!(word=parse_word(P,1)) || strcmp(word, ">"))FAIL("list lacks closing '>'");
	*N = n;
	return v;
}

char	**parse_mrs_trigger(char	**P, int	*Ntriggers)
{
	void	parse_trigger(struct mrs_vlist *v, char **P, char **T) { *T = strdup(parse_word(P, 1)); }
	return (char**)parse_list(0, P, Ntriggers,
		(void(*)(struct mrs_vlist*,char**,void*))parse_trigger);
}

struct mrs_hcons	*parse_mrs_hcons(struct mrs_vlist	*vlist, char **P, int	*Nhcons)
{
	int					nhcons = 0;
	struct mrs_hcons	*hcons = 0;
	char				*word;

	*Nhcons = 0;
	if(!(word=parse_word(P,1)) || strcmp(word, "<"))FAIL("hcons list lacks initial '<'");
	while((word=parse_word(P,0)) && strcmp(word, ">"))
	{
		struct mrs_hcons	*hc;
		nhcons++;
		hcons = realloc(hcons, sizeof(struct mrs_hcons)*nhcons);
		hc = hcons+nhcons-1;
		hc->hi = parse_mrs_var(vlist, P);
		word = parse_word(P, 1);
		if(!word)FAIL("hcons lacks type");
		if(!strcmp(word, "qeq"))hc->type = hcons_qeq;
		else if(!strcmp(word, "leq"))hc->type = hcons_leq;
		else if(!strcmp(word, "geq"))hc->type = hcons_geq;
		else FAIL("hcons type unrecognized");
		hc->lo = parse_mrs_var(vlist, P);
	}
	if(!(word=parse_word(P,1)) || strcmp(word, ">"))FAIL("hcons list lacks closing '>'");
	*Nhcons = nhcons;
	return hcons;
}

struct mrs_ep	*parse_mrs_rels(struct mrs_vlist	*vlist, char **P, int	*Nrels)
{
	int				nrels = 0;
	struct mrs_ep	*eps = 0;
	char			*word;

	*Nrels = 0;
	if(!(word=parse_word(P,1)) || strcmp(word, "<"))FAIL("rels list lacks initial '<'");
	while(!strcmp(parse_word(P, 0), "["))
	{
		struct mrs_ep	*ep;
		parse_word(P, 1);	// [
		nrels++;
		eps = realloc(eps, sizeof(struct mrs_ep)*nrels);
		ep = eps+nrels-1;
		ep->pred = (word=parse_word(P,1))?strdup(word):0;
		ep->nargs = 0;
		ep->args = 0;
		ep->epnum = nrels-1;
		while((word=parse_word(P,1)) && strcmp(word,"]"))
		{
			if(!strcmp(word, "LBL:"))ep->lbl = parse_mrs_var(vlist, P);
			else
			{
				struct mrs_ep_role	*role;
				ep->nargs++;
				ep->args = realloc(ep->args, sizeof(struct mrs_ep_role)*ep->nargs);
				role = ep->args+ep->nargs-1;
				if(word[0]!=':' && word[strlen(word)-1]==':')
					word[strlen(word)-1] = 0;
				role->name = strdup(word);
				if(!strcmp(word, "CARG"))
					role->value = parse_mrs_const(P);
				else role->value = parse_mrs_var(vlist, P);
			}
		}
		if(!word)FAIL("ep lacks closing ']'");	// ]
	}
	if(!(word=parse_word(P,1)) || strcmp(word, ">"))FAIL("rels list lacks closing '>'");
	*Nrels = nrels;
	return eps;
}

struct mrs	*parse_mrs(char	*buffer)
{
	char	*p, *fname;
	struct mrs_vlist	vlist = {0,0};
	struct mrs			mrs, *M;

	bzero(&mrs, sizeof(mrs));
	p = buffer;
	while(*p)
	{
		fname = parse_word(&p, 1);
		if(!fname)break;
		if(!strcasecmp(fname, "LTOP:"))
			mrs.ltop = parse_mrs_var(&vlist, &p);
		if(!strcasecmp(fname, "INDEX:"))
			mrs.index = parse_mrs_var(&vlist, &p);
		if(!strcasecmp(fname, "XARG:"))
			mrs.xarg = parse_mrs_var(&vlist, &p);
		if(!strcasecmp(fname, "RELS:"))
			mrs.rels = parse_mrs_rels(&vlist, &p, &mrs.nrels);
		if(!strcasecmp(fname, "HCONS:"))
			mrs.hcons = parse_mrs_hcons(&vlist, &p, &mrs.nhcons);
		if(!strcasecmp(fname, "TRIGGER:"))
			mrs.triggers = parse_mrs_trigger(&p, &mrs.ntriggers);
	}

	M = malloc(sizeof(mrs));
	*M = mrs;
	M->vlist = vlist;
	return M;
}

struct mrs	*read_mrs(FILE	*f)
{
	char	*buffer = malloc(1024);
	int		abuf = 1024, lbuf = 0, balance = 0;
	struct mrs	*mrs;

	while(!feof(f) || balance || !lbuf)
	{
		int ch = fgetc(f);
		if(ch==' ' || ch=='\t' || ch=='\n' || ch=='\r')ch=' ';
		if(ch!='[' && lbuf==0)continue;
		if(lbuf && buffer[lbuf-1]==' ' && ch==' ')continue;
		if(lbuf+2 > abuf)
		{
			abuf *= 2;
			buffer = realloc(buffer, abuf);
		}
		buffer[lbuf++] = ch;
		if(ch=='[')balance++;
		if(ch==']')balance--;
	}

	buffer[lbuf-1] = 0;
	mrs = parse_mrs(buffer+1);
	free(buffer);
	return mrs;
}

void	print_mrs_var(struct mrs_var	*var)
{
	if(!var) { printf("(null)"); return; }
	printf("%s", var->name);
	if(var->nprops)
	{
		int	i;
		printf(" [");
		for(i=0;i<var->nprops;i++)
			{ printf(" %s: %s", var->props[i].name, var->props[i].value); }
		printf(" ]");
	}
}

void	print_mrs(struct mrs	*mrs)
{
	int	i;

	printf("[ LTOP: "); print_mrs_var(mrs->ltop);
	printf("\nINDEX: "); print_mrs_var(mrs->index);
	printf("\nXARG: "); print_mrs_var(mrs->xarg);
	printf("\nRELS: <");
	for(i=0;i<mrs->nrels;i++)
	{
		struct mrs_ep	*ep = mrs->rels+i;
		int				j;
		printf("	[ %s LBL: ", ep->pred); print_mrs_var(ep->lbl);
		for(j=0;j<ep->nargs;j++)
			{ printf(" %s: ", ep->args[j].name); print_mrs_var(ep->args[j].value); }
		printf(" ]");
		if(i+1 != mrs->nrels)printf("\n");
	}
	printf(" >");
	printf("\nHCONS: <");
	for(i=0;i<mrs->nhcons;i++)
	{
		struct mrs_hcons	*hc = mrs->hcons+i;
		printf(" "); print_mrs_var(hc->hi);
		switch(hc->type) {
			case hcons_qeq: printf(" qeq "); break;
			case hcons_leq: printf(" leq "); break;
			case hcons_geq: printf(" geq "); break; }
		print_mrs_var(hc->lo);
	}
	printf(" > ]\n");
}
