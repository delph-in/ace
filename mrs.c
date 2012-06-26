#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<stdarg.h>

#ifndef	NODAG
#include	"type.h"
#include	"dag.h"
#include	"chart.h"
#include	"freeze.h"
#endif

#include	"mrs.h"

#ifdef NODAG
#define slab_alloc(len)	malloc(len)
#define	slab_realloc(ptr,olen,len) realloc(ptr,len)
#define	slab_free(ptr)	free(ptr)
#else
#define	slab_free(ptr)
#endif

// mrs IO

void	free_mrs_var(struct mrs_var	*v)
{
#ifdef NODAG
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
#endif
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
#ifdef NODAG
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
#endif
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
	while(*p==' ')p++;
	ret = p;
	int inq = 0, esc = 0;
	while(*p && (inq || *p!=' '))
	{
		if(esc) { esc = 1; }
		else
		{
			if(*p=='\\')esc=1;
			if(*p=='"')inq = !inq;
		}
		p++;
	}
	if(*p==' '){*p=0;p++;}
	if(!advance)saved = ret;
	*P = p;
	if(inq) { fprintf(stderr, "MRS parsing error: bad word ended in quoted section\n"); return NULL; }
	//if(ret)printf("word `%s'\n", ret);
	return *ret?ret:0;
}

#define	FAIL(reason)	do { fprintf(stderr, "MRS parsing error: %s (near `%*s')\n", reason, 16, *P); return 0; } while(0)

static inline void	*slab_malloc(int l) { if(l%4)l += (4-(l%4)); return slab_alloc(l); }
static inline void	*slab_calloc(int a, int b) { int l = a*b; void *p = slab_malloc(l); bzero(p, l); return p; }

int	parse_mrs_int(char	**P)
{
	char	*c = parse_word(P, 1);
	return atoi(c);
}

struct mrs_var	*parse_mrs_const(struct mrs_vlist *vlist, char	**P)
{
	char	*c = parse_word(P, 1);
	struct mrs_var *var = slab_calloc(sizeof(struct mrs_var), 1);
	var->is_const = 1;
	var->type = strdup("c");
	var->name = strdup(c);

	var->vnum = vlist->nvars;
	vlist->nvars++;
	vlist->vars = slab_realloc(vlist->vars, sizeof(struct mrs_var*) * (vlist->nvars-1), sizeof(struct mrs_var*) * vlist->nvars);
	vlist->vars[vlist->nvars-1] = var;

	return var;
}

struct mrs_var	*parse_mrs_var(struct mrs_vlist	*vlist, char	**P)
{
	struct mrs_var	var, *V;
	int		defined = 0, i;
	char	*word, *var_name;

	var_name = parse_word(P, 0);
	if(!var_name)
	{
		FAIL("var lacks a name");
	}
	if(var_name[0]=='"')
	{
		// this is probably a const variable.
		return parse_mrs_const(vlist, P);
	}
	parse_word(P, 1);	// consume the name
	if((word=parse_word(P,0)) && !strcmp("[", word))
	{
		int		aprops = 8;
		char	*type;
		bzero(&var, sizeof(var));
		var.name = var_name;
		parse_word(P, 1);
		defined = 1;
		type = parse_word(P, 1);
		if(!type)FAIL("var lacks a type");
		var.type = strdup(type);
		var.props = slab_alloc(sizeof(struct mrs_var_property)*aprops);

		while((word = parse_word(P,1)) && strcmp(word,"]"))
		{
			struct mrs_var_property *prop;
			var.nprops++;
			if(var.nprops > aprops)
			{
				aprops += 4;
				var.props = slab_realloc(var.props, sizeof(struct mrs_var_property)*(aprops-4), sizeof(struct mrs_var_property)*aprops);
			}
			prop = var.props + var.nprops-1;
			// trim off pesky ':' at end of role name
			if(word[0]!=':' && word[strlen(word)-1]==':')
				word[strlen(word)-1] = 0;
			prop->name = strdup(word);
			prop->value = (word=parse_word(P,1))?strdup(word):0;
			//printf(" { prop `%s' value `%s' }\n", prop->name, prop->value);
		}
		if(var.nprops != aprops)
			var.props = slab_realloc(var.props, sizeof(struct mrs_var_property)*aprops, sizeof(struct mrs_var_property)*var.nprops);
	}

	// done parsing, check for reentrancies
	for(i=0;i<vlist->nvars;i++)
		if(!strcmp(vlist->vars[i]->name, var_name))
		{
			if(defined)
			{
				var.name = vlist->vars[i]->name;
				var.vnum = i;
				if(vlist->vars[i]->type)slab_free(vlist->vars[i]->type);
				*vlist->vars[i] = var;
			}
			vlist->vars[i]->refs++;
			return vlist->vars[i];
		}

	if(!defined)
	{
		// assume name up to number is type
		bzero(&var, sizeof(var));
		var.name = var_name;
		for(i=0;var.name[i] && !isdigit(var.name[i]);i++) {}
		var.type = slab_malloc(i+1);
		memcpy(var.type, var.name, i);
		var.type[i] = 0;
	}

	V = slab_malloc(sizeof(var));
	*V = var;
	V->name = strdup(V->name);
	V->refs = 1;

	V->vnum = vlist->nvars;
	vlist->nvars++;
	vlist->vars = slab_realloc(vlist->vars, sizeof(struct mrs_var*) * (vlist->nvars-1), sizeof(struct mrs_var*) * vlist->nvars);
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
		v = slab_realloc(v, sizeof(void*)*(n-1), sizeof(void*)*n);
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
		hcons = slab_realloc(hcons, sizeof(struct mrs_hcons)*(nhcons-1), sizeof(struct mrs_hcons)*nhcons);
		hc = hcons+nhcons-1;
		hc->hi = parse_mrs_var(vlist, P);
		word = parse_word(P, 1);
		if(!word)FAIL("hcons lacks type");
		if(!strcasecmp(word, "qeq"))hc->type = hcons_qeq;
		else if(!strcasecmp(word, "leq"))hc->type = hcons_leq;
		else if(!strcasecmp(word, "geq"))hc->type = hcons_geq;
		else FAIL("hcons type unrecognized");
		hc->lo = parse_mrs_var(vlist, P);
	}
	if(!(word=parse_word(P,1)) || strcmp(word, ">"))FAIL("hcons list lacks closing '>'");
	*Nhcons = nhcons;
	return hcons;
}

char	*preddup(char	*pred)
{
	char	*b = strchr(pred, '<');
	if(!b)return strdup(pred);
	char	*d = malloc(b-pred+1);
	memcpy(d, pred, b-pred);
	d[b-pred]=0;
	return d;
}

struct mrs_ep	*parse_mrs_rels(struct mrs_vlist	*vlist, char **P, int	*Nrels)
{
	int				nrels = 0, arels = 8;
	struct mrs_ep	*eps = 0;
	char			*word;

	*Nrels = 0;
	if(!(word=parse_word(P,1)) || strcmp(word, "<"))FAIL("rels list lacks initial '<'");
	eps = slab_alloc(sizeof(struct mrs_ep)*arels);
	while(!strcmp(parse_word(P, 0), "["))
	{
		struct mrs_ep	*ep;
		parse_word(P, 1);	// [
		nrels++;
		if(nrels>arels)
		{
			arels *= 2;
			eps = slab_realloc(eps, sizeof(struct mrs_ep)*(arels/2), sizeof(struct mrs_ep)*arels);
		}
		ep = eps+nrels-1;
		bzero(ep,sizeof(*ep));
		ep->pred = (word=parse_word(P,1))?preddup(word):0;
		if(word && *word && word[strlen(word)-1]=='>' && strrchr(word, '<'))
		{
			char	*characterization = strrchr(word, '<');
			if(2 != sscanf(characterization, "<%d:%d>", &ep->cfrom, &ep->cto))
				FAIL("unparseable characterization");
		}
		ep->epnum = nrels-1;
		struct mrs_ep_role	args[20];
		while((word=parse_word(P,1)) && strcmp(word,"]"))
		{
			if(!strcmp(word, "LBL:"))ep->lbl = parse_mrs_var(vlist, P);
			else if(!strcmp(word, "CFROM:"))ep->cfrom = parse_mrs_int(P);
			else if(!strcmp(word, "CTO:"))ep->cto = parse_mrs_int(P);
			else
			{
				struct mrs_ep_role	*role;
				ep->nargs++;
				role = args+ep->nargs-1;
				if(!*word || word[strlen(word)-1]!=':')
				{
					fprintf(stderr, "mrs warning: role name `%s' does not end in a colon\n", word);
				}
				if(word[0]!=':' && word[strlen(word)-1]==':')
					word[strlen(word)-1] = 0;
				role->name = strdup(word);
				role->value = parse_mrs_var(vlist, P);
			}
		}
		if(!word)
		{
			FAIL("ep lacks closing ']'");	// ]
		}
		ep->args = slab_alloc(sizeof(struct mrs_ep_role)*ep->nargs);
		memcpy(ep->args, args, sizeof(struct mrs_ep_role)*ep->nargs);
	}
	if(nrels != arels)
		eps = slab_realloc(eps, sizeof(struct mrs_ep)*arels, sizeof(struct mrs_ep)*nrels);
	if(!(word=parse_word(P,1)) || strcmp(word, ">"))FAIL("rels list lacks closing '>'");
	*Nrels = nrels;
	return eps;
}

struct mrs	*parse_mrs(char	*buffer)
{
	char	*p, *fname;
	struct mrs_vlist	vlist = {0,0};
	struct mrs			mrs, *M;

	// trim off outer []'s
	while(*buffer && *buffer==' ' || *buffer=='\t' || *buffer=='\n')buffer++;
	p = buffer+strlen(buffer)-1;
	while(p>buffer && *p!=']')p--;
	if(p<=buffer)return 0;
	if(*p!=']' || *buffer!='[')
	{
		fprintf(stderr, "no []'s around mrs");
		return 0;
	}
	*p = 0;
	buffer++;
	for(p=buffer;*p;p++)if(*p=='\t' || *p=='\n')*p=' ';

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
		if(!strcasecmp(fname, "TRIGGER:") || !strcasecmp(fname, "VCS:"))
			mrs.triggers = parse_mrs_trigger(&p, &mrs.ntriggers);
	}

	M = slab_malloc(sizeof(mrs));
	*M = mrs;
	M->vlist = vlist;
	return M;
}

struct mrs	*read_mrs(FILE	*f)
{
	int		abuf = 1024, lbuf = 0, balance = 0;
	char	*buffer = malloc(abuf);
	struct mrs	*mrs;

	while(!feof(f) && (balance || !lbuf))
	{
		int ch = fgetc(f);
		if(ch==' ' || ch=='\t' || ch=='\n' || ch=='\r')ch=' ';
		if(ch!='[' && lbuf==0)continue;
		//if(lbuf && buffer[lbuf-1]==' ' && ch==' ')continue;
		if(lbuf+2 > abuf)
		{
			abuf *= 2;
			buffer = realloc(buffer, abuf);
		}
		buffer[lbuf++] = ch;
		if(ch=='[')balance++;
		if(ch==']')balance--;
	}

	if(balance || !lbuf)
	{
		if(balance)
		{
			fprintf(stderr, "NOTE: couldn't read a complete mrs\n");
			buffer[lbuf] = 0;
			fprintf(stderr, "%s\n", buffer);
		}
		free(buffer);
		return 0;
	}
	buffer[lbuf] = 0;
	mrs = parse_mrs(buffer);
	free(buffer);
	return mrs;
}

struct mrs_var	*mrs_find_ep_role(struct mrs_ep	*ep, char	*role)
{
	int i;

	for(i=0;i<ep->nargs;i++)
		if(!strcmp(ep->args[i].name, role))
			return ep->args[i].value;
	return 0;
}

void	mrs_crange_for_x(struct mrs	*m, struct mrs_var	*x, int	quantified, int	*From, int	*To)
{
	int	i;
	*From = *To = -1;
	//printf("mrs finding crange for x%d\n", x->vnum);
	for(i=0;i<m->nrels;i++)
	{
		struct mrs_ep	*ep = m->rels+i;
		if(mrs_find_ep_role(ep, "ARG0") != x)continue;
		if(!quantified && strstr(ep->pred, "_q_"))continue;
		//printf(" ... pred '%s' %d-%d\n", ep->pred, ep->cfrom, ep->cto);
		if(*From==-1 || *From>ep->cfrom)*From = ep->cfrom;
		if(*To==-1 || *To<ep->cto)*To = ep->cto;
	}
}

int	safe_snprintf(char	*str, int	len, char	*fmt, ...)
{
	va_list	v;
	va_start(v, fmt);
	if(len <= 0) { len = 0; str = NULL; }
	int	r = vsnprintf(str, len, fmt, v);
	va_end(v);
	return r;
}

int	snprint_mrs_var_props(char	*str, int	len, struct mrs_var	*var)
{
	int	i, l=0;
	l += safe_snprintf(str+l, len-l, "[ %s", var->type);
	for(i=0;i<var->nprops;i++)
		{ l += safe_snprintf(str+l, len-l, " %s: %s", var->props[i].name, var->props[i].value); }
	l += safe_snprintf(str+l, len-l, " ]");
	return l;
}

int	snprint_mrs_var(char	*str, int	len, struct mrs_var	*var)
{
	int	i, l=0;
	if(!var) return safe_snprintf(str, len, "(null)");
	if(var->is_const)return safe_snprintf(str, len, "%s", var->name);
	else
	{
		l += safe_snprintf(str+l, len-l, "%s%d", var->type, var->vnum);
		if(var->nprops > 0)	// berthold crysmann noticed that the LKB fails to parse MRSes with a properties-clause on handles in QEQs... a rare condition indeed. (23-nov-2011)
		{
			l += safe_snprintf(str+l, len-l, " ");
			l += snprint_mrs_var_props(str+l, len-l, var);
		}
		return l;
	}
}

void	print_mrs_var_props(struct mrs_var	*var)
{
	char text[1024];
	snprint_mrs_var_props(text, 1024, var);
	printf("%s", text);
}

void	print_mrs_var(struct mrs_var	*var)
{
	char	buffer[1024];
	snprint_mrs_var(buffer, 1024, var);
	printf("%s", buffer);
}

int	snprint_mrs_var_marked(char	*str, int	len, struct mrs_var	*var, char	*marked)
{
	if(marked && marked[var->vnum])
		return safe_snprintf(str, len, "%s%d", var->type, var->vnum);
	if(marked)marked[var->vnum] = 1;
	return snprint_mrs_var(str, len, var);
}

char	mrs_line_breaks = ' ', mrs_tab = ' ';
char	*mrs_end = "\n";

int	snprint_mrs(char	*str, int	len, struct mrs	*mrs)
{
	int	i;
	static int	timer = -1;
	if(timer==-1)timer = new_timer("mrs printing");
	start_timer(timer);

	make_reliable_vnums(mrs);

	char	*marked = calloc(sizeof(char), mrs->vlist.nvars);
	int	l = 0;

	l += safe_snprintf(str+l, len-l, "[ LTOP: ");
	l += snprint_mrs_var_marked(str+l, len-l, mrs->ltop, marked);
	l += safe_snprintf(str+l, len-l, "%cINDEX: ", mrs_line_breaks);
	l += snprint_mrs_var(str+l, len-l, mrs->index);

	l += safe_snprintf(str+l, len-l, "%cRELS: <", mrs_line_breaks);
	for(i=0;i<mrs->nrels;i++)
	{
		struct mrs_ep	*ep = mrs->rels+i;
		int				j;
		l += safe_snprintf(str+l, len-l, "%c[ %s<%d:%d> LBL: ", mrs_tab, ep->pred, ep->cfrom, ep->cto);
		l += snprint_mrs_var_marked(str+l, len-l, ep->lbl, marked);
		for(j=0;j<ep->nargs;j++)
		{
			l += safe_snprintf(str+l, len-l, " %s: ", ep->args[j].name);
			if(ep->args[j].value)
				l += snprint_mrs_var_marked(str+l, len-l, ep->args[j].value, marked);
			else l += safe_snprintf(str+l, len-l, " (null)");
		}
		l += safe_snprintf(str+l, len-l, " ]");
		if(i+1 != mrs->nrels)
			l += safe_snprintf(str+l, len-l, "%c", mrs_line_breaks);
	}
	l += safe_snprintf(str+l, len-l, " >");
	l += safe_snprintf(str+l, len-l, "%cHCONS: <", mrs_line_breaks);
	for(i=0;i<mrs->nhcons;i++)
	{
		struct mrs_hcons	*hc = mrs->hcons+i;
		l += safe_snprintf(str+l, len-l, " ");
		l += snprint_mrs_var_marked(str+l, len-l, hc->hi, marked);
		switch(hc->type) {
			case hcons_qeq: l += safe_snprintf(str+l, len-l, " qeq "); break;
			case hcons_leq: l += safe_snprintf(str+l, len-l, " leq "); break;
			case hcons_geq: l += safe_snprintf(str+l, len-l, " geq "); break;
			default:	restore_stderr(); fprintf(stderr, "ERROR: unknown HCONS type %d\n", hc->type); exit(-1);
		}
		l += snprint_mrs_var_marked(str+l, len-l, hc->lo, marked);
	}
	l += safe_snprintf(str+l, len-l, " > ]%s", mrs_end);

	free(marked);
	stop_timer(timer, 1);
	return l;
}

void	print_mrs(struct mrs	*m)
{
	int	blen = 1024 + 1024 * m->nrels;
	char	buffer[blen];
	int	l = snprint_mrs(buffer, blen, m);
	if(l >= blen)
	{
		blen = l + 1;
		char	*b2 = malloc(blen);
		snprint_mrs(b2, blen, m);
		fwrite(b2, l, 1, stdout);
		free(b2);
	}
	else fwrite(buffer, l, 1, stdout);
}

#ifndef	NOTESTING

// mrs testing

int	mrs_no_extra = 0;

int var_subsume(struct mrs_var *in, struct mrs_var *out, int *vmap, int nv);

//#define	DEBUG	printf
#define	DEBUG(x...)	do {} while(0)

int	g_mrs_test_demand_equality = 0;

int mrs_subsumes(struct mrs	*in, struct mrs	*out)
{
	int	i;
	int	vmap[in->vlist.nvars];
	char used[out->nrels];

	make_reliable_vnums(in);
	make_reliable_vnums(out);

	if(mrs_no_extra || g_mrs_test_demand_equality)
	{
		if(in->nrels != out->nrels)return 0;
		if(in->nhcons != out->nhcons)return 0;
		if(g_mrs_test_demand_equality && in->vlist.nvars != out->vlist.nvars)return 0;
	}
	else
	{
		if(in->nrels > out->nrels)return 0;
		if(in->nhcons > out->nhcons)return 0;
	}
	DEBUG("mrs subsumption passed numerical checks\n");
	for(i=0;i<in->vlist.nvars;i++)vmap[i] = -1;
	bzero(used, out->nrels);
	if(in->ltop && out->ltop && !var_subsume(in->ltop, out->ltop, vmap, in->vlist.nvars))return 0;
	DEBUG("mrs subsumption passed ltop checks\n");
	if(in->index && out->index && !var_subsume(in->index, out->index, vmap, in->vlist.nvars))return 0;
	DEBUG("mrs subsumption passed index checks\n");
	if(in->xarg && out->xarg && !var_subsume(in->xarg, out->xarg, vmap, in->vlist.nvars))return 0;
	DEBUG("mrs subsumption passed xarg checks\n");
	return mrs_subsumes0(in, out, 0, vmap, used);
}

int pred_subsumes(char *in, char *out)
{
	// XXX when SEM-I import works, this is where we make our move...
	// more on that: this function is called by:
	//   1. mrs subsumption testing
	//   2. semantic index lookup
	//   3. vpm (via prop_subsumes)
	// (1) can be in either type hierarchy.
	// (2) will only be in the internal hierarchy.
	// (3) can be in either type hierarchy.
	// *so*, we need a way to specify which hierarchy we want to use!
	// answer: default_type_hierarchy()
	// leave it up to the caller to set the right hierarchy.
	struct type_hierarchy	*th = default_type_hierarchy();
	struct type *tin, *tout;
	if(!strcasecmp(in, out))return 1;
	if(g_mrs_test_demand_equality)return 0;
	if(in[0]=='"')return 0;	// strings don't subsume anything but themself
	tin = lookup_type(in);
	if(!tin)
	{
		// XXX ignoring this is wrong; find out where these types are coming from.
		// the VPM can introduce some types that aren't in the heirarchy;
		// apparently the correct behavior is to treat them as having no inheritance
		//fprintf(stderr, "MRS: no such predicate type `%s' (when subsume(%s,%s) called)\n", in, in, out);
		return 0;
		exit(-1);
	}
	if(out[0]=='"')return descendent_of(th->strtype->tnum, tin);
	tout = lookup_type(out);
	if(!tout)
	{
		// XXX where are these coming from?
		//fprintf(stderr, "MRS: no such predicate type `%s' (when subsume(%s,%s) called)\n", out, in, out);
		return 0;
		exit(-1);
	}
	return descendent_of(tout->tnum, tin);
}

int prop_subsumes(char *in, char *out)
{
	if(!strcasecmp(in, out))return 1;
	return pred_subsumes(in, out);
}

int var_subsume(struct mrs_var *in, struct mrs_var *out, int *vmap, int nv)
{
	int vnin = in->vnum, i, j;

	if(in->is_const)
	{
		if(!out->is_const)return 0;
		return !strcmp(in->name, out->name);
	}
	else if(out->is_const)return 0;

	//printf(" subsume? %s <=> %s  ;; in.%d->%d and out=%d\n", in->name, out->name, vnin, vmap[vnin], out->vnum);
	if(vmap[vnin]!=-1)
		return vmap[vnin] == out->vnum;
	// make sure no input vars are already mapped to this output var;
	//   we don't allow accidental correferencing.
	// skolemization should block this, but just in case something funny happened
	//   (e.g. the grammar didn't do its job right with packing turned on)
	for(i=0;i<nv;i++) if(vmap[i] == out->vnum)return 0;

	DEBUG("passed pre-hierarchical var subsume test [ '%s' vs '%s' ]\n", in->type, out->type);

	// make sure variable type is subsumed
	if(!pred_subsumes(in->type, out->type))return 0;
	/*switch(in->type[0])
	{
		case	'u':	break;
		case	'i':	if(out->type[0]=='h' || out->type[0]=='u')return 0;	break;
		default:		if(out->type[0]!=in->type[0])return 0;	break;
	}*/

	DEBUG("passed type var subsume test\n");

	// make sure variable properties are subsumed
	for(i=0;i<in->nprops;i++)
	{
		for(j=0;j<out->nprops;j++)
			if(!strcasecmp(in->props[i].name, out->props[j].name))break;
		if(j==out->nprops)return 0;
		if(!prop_subsumes(in->props[i].value, out->props[j].value))return 0;
	}
	if(g_mrs_test_demand_equality && in->nprops != out->nprops)
		return 0;

	// compatible.  establish a mapping and succeed.
	vmap[vnin] = out->vnum;
	return 1;
}

int mrs_subsumes0(struct mrs *in, struct mrs *out, int epi, int *vmap, char *used)
{
	// try to match epi of in with some ep in out, and recurse
	struct mrs_ep	*ep = in->rels+epi, *op;
	int				vm[in->vlist.nvars];
	int				i, j, k;

	if(epi == in->nrels)
	{
		char	hused[in->nhcons];
		bzero(hused, in->nhcons);
		//printf("checking HCONS subsumption\n");
		DEBUG("mrs subsumption passed RELS checks\n");
		return mrs_subsumes1(in, out, 0, vmap, hused);
	}

	//printf("checking input pred '%s' subsumption\n", ep->pred);
	for(i=0;i<out->nrels;i++) if(!used[i])
	{
		op = out->rels+i;
		if(!pred_subsumes(ep->pred, op->pred))continue;
		DEBUG(" ... might match output pred '%s'\n", op->pred);
		memcpy(vm, vmap, sizeof(int)*in->vlist.nvars);
		if(ep->lbl && !var_subsume(ep->lbl, op->lbl, vm, in->vlist.nvars))continue;
		//printf(" ... label ok\n");
		for(j=0;j<ep->nargs;j++)
		{
			for(k=0;k<op->nargs;k++)
				if(!strcasecmp(ep->args[j].name, op->args[k].name))break;
			if(k==op->nargs)
			{
				DEBUG("no `%s'\n", ep->args[j].name);
				break;
			}
			struct mrs_var	*iv = ep->args[j].value, *ov = op->args[k].value;
			if(!var_subsume(iv, ov, vm, in->vlist.nvars))
			{
				if(iv->is_const)DEBUG("in var is  const %s\n", iv->name);
				if(ov->is_const)DEBUG("out var is const %s\n", ov->name);
				DEBUG("in var %s%d incompatible with out var %s%d\n",
					iv->type, iv->vnum, ov->type, ov->vnum);
				break;
			}
		}
		if(j<ep->nargs)continue;
		if(g_mrs_test_demand_equality && ep->nargs != op->nargs)continue;

		DEBUG("mrs subsumption: in %d <=> out %d\n", epi, i);

		// we found an alignment for epi; recurse.
		used[i] = 1;
		if(mrs_subsumes0(in, out, epi+1, vm, used))
		{
			used[i] = 0;
			return 1;
		}
		used[i] = 0;
	}
	DEBUG("mrs subsumption: no match for %s\n", ep->pred);
	return 0;
}

int mrs_subsumes1(struct mrs *in, struct mrs *out, int hci, int *vmap, char *used)
{
	// try to match hci of in with some hc in out, and recurse
	struct mrs_hcons	*hc = in->hcons+hci, *oh;
	int				vm[in->vlist.nvars], i;

	if(hci == in->nhcons)
		return 1;

	for(i=0;i<out->nhcons;i++) if(!used[i])
	{
		oh = out->hcons+i;
		if(hc->type != oh->type)return 0;
		memcpy(vm, vmap, sizeof(int)*in->vlist.nvars);
		if(!var_subsume(hc->hi, oh->hi, vm, in->vlist.nvars))continue;
		if(!var_subsume(hc->lo, oh->lo, vm, in->vlist.nvars))continue;

		//printf("in hc %d <=> out hc %d\n", hci, i);
		// we found an alignment for hci; recurse.
		used[i] = 1;
		if(mrs_subsumes1(in, out, hci+1, vm, used))
		{
			used[i] = 0;
			return 1;
		}
		used[i] = 0;
	}
	return 0;
}
#endif

// produce a copy of 'min' entirely on the heap, for posterity
struct mrs	*copy_mrs(struct mrs	*min)
{
	struct mrs	*mout = calloc(sizeof(*mout),1);
	int	i, j;

	make_reliable_vnums(min);

	mout->vlist.nvars = min->vlist.nvars;
	mout->vlist.vars = malloc(sizeof(struct mrs_var*) * mout->vlist.nvars);
	for(i=0;i<mout->vlist.nvars;i++)
	{
		mout->vlist.vars[i] = calloc(sizeof(struct mrs_var),1);
		struct mrs_var	*vin = min->vlist.vars[i];
		struct mrs_var	*vout = mout->vlist.vars[i];
		vout->name = strdup(vin->name);
		vout->type = strdup(vin->type);
		vout->is_const = vin->is_const;
		vout->refs = vin->refs;
		vout->vnum = vin->vnum;
		vout->nprops = vin->nprops;
		vout->props = malloc(sizeof(struct mrs_var_property) * vout->nprops);
		for(j=0;j<vout->nprops;j++)
		{
			vout->props[j].name = strdup(vin->props[j].name);
			vout->props[j].value = strdup(vin->props[j].value);
		}
		vout->dg = vin->dg;
		vout->semi_dg = vin->semi_dg;
	}

	mout->nhcons = min->nhcons;
	mout->hcons = malloc(sizeof(struct mrs_hcons) * mout->nhcons);
	for(i=0;i<mout->nhcons;i++)
	{
		struct mrs_hcons	*hin = min->hcons+i;
		struct mrs_hcons	*hout = mout->hcons+i;
		hout->type = hin->type;
		hout->hi = mout->vlist.vars[hin->hi->vnum];
		hout->lo = mout->vlist.vars[hin->lo->vnum];
	}

	if(min->ltop)mout->ltop = mout->vlist.vars[min->ltop->vnum];
	if(min->index)mout->index = mout->vlist.vars[min->index->vnum];
	if(min->xarg)mout->xarg = mout->vlist.vars[min->xarg->vnum];

	mout->nrels = min->nrels;
	mout->rels = calloc(sizeof(struct mrs_ep), mout->nrels);
	for(i=0;i<mout->nrels;i++)
	{
		struct mrs_ep	*ein = min->rels+i;
		struct mrs_ep	*eout = mout->rels+i;
		eout->pred = strdup(ein->pred);
		eout->lbl = mout->vlist.vars[ein->lbl->vnum];
		eout->epnum = ein->epnum;
		eout->cfrom = ein->cfrom;
		eout->cto = ein->cto;
		eout->nargs = ein->nargs;
		eout->args = malloc(sizeof(struct mrs_ep_role) * eout->nargs);
		for(j=0;j<eout->nargs;j++)
		{
			eout->args[j].name = strdup(ein->args[j].name);
			eout->args[j].value = mout->vlist.vars[ein->args[j].value->vnum];
		}
	}

	return mout;
}

// produce a copy of 'min' entirely on the slab
struct mrs	*slab_copy_mrs(struct mrs	*min, int share_strings)
{
	struct mrs	*mout = slab_calloc(sizeof(*mout),1);
	int	i, j;

	make_reliable_vnums(min);

	mout->vlist.nvars = min->vlist.nvars;
	mout->vlist.vars = slab_malloc(sizeof(struct mrs_var*) * mout->vlist.nvars);
	for(i=0;i<mout->vlist.nvars;i++)
	{
		mout->vlist.vars[i] = slab_calloc(sizeof(struct mrs_var),1);
		struct mrs_var	*vin = min->vlist.vars[i];
		struct mrs_var	*vout = mout->vlist.vars[i];
		vout->name = share_strings?vin->name:freeze_string(vin->name);
		vout->type = share_strings?vin->type:freeze_string(vin->type);
		vout->is_const = vin->is_const;
		vout->refs = vin->refs;
		vout->vnum = vin->vnum;
		vout->nprops = vin->nprops;
		vout->props = slab_malloc(sizeof(struct mrs_var_property) * vout->nprops);
		for(j=0;j<vout->nprops;j++)
		{
			vout->props[j].name = share_strings?vin->props[j].name:freeze_string(vin->props[j].name);
			vout->props[j].value = share_strings?vin->props[j].value:freeze_string(vin->props[j].value);
		}
		vout->dg = vin->dg;
		vout->semi_dg = vin->semi_dg;
	}

	mout->nhcons = min->nhcons;
	mout->hcons = slab_malloc(sizeof(struct mrs_hcons) * mout->nhcons);
	for(i=0;i<mout->nhcons;i++)
	{
		struct mrs_hcons	*hin = min->hcons+i;
		struct mrs_hcons	*hout = mout->hcons+i;
		hout->type = hin->type;
		hout->hi = mout->vlist.vars[hin->hi->vnum];
		hout->lo = mout->vlist.vars[hin->lo->vnum];
	}

	if(min->ltop)mout->ltop = mout->vlist.vars[min->ltop->vnum];
	if(min->index)mout->index = mout->vlist.vars[min->index->vnum];
	if(min->xarg)mout->xarg = mout->vlist.vars[min->xarg->vnum];

	mout->nrels = min->nrels;
	mout->rels = slab_calloc(sizeof(struct mrs_ep), mout->nrels);
	for(i=0;i<mout->nrels;i++)
	{
		struct mrs_ep	*ein = min->rels+i;
		struct mrs_ep	*eout = mout->rels+i;
		eout->pred = share_strings?ein->pred:freeze_string(ein->pred);
		eout->lbl = mout->vlist.vars[ein->lbl->vnum];
		eout->epnum = ein->epnum;
		eout->cfrom = ein->cfrom;
		eout->cto = ein->cto;
		eout->nargs = ein->nargs;
		eout->args = slab_malloc(sizeof(struct mrs_ep_role) * eout->nargs);
		eout->dg = ein->dg;
		for(j=0;j<eout->nargs;j++)
		{
			eout->args[j].name = share_strings?ein->args[j].name:freeze_string(ein->args[j].name);
			eout->args[j].value = mout->vlist.vars[ein->args[j].value->vnum];
		}
	}

	return mout;
}
