#include	<stdio.h>
#include	<stdlib.h>
#include	<stdio.h>
#include	<string.h>
#include	<assert.h>

#include	"conf.h"
#include	"dag.h"
#include	"chart.h"
#include	"unpack.h"
#include	"rule.h"
#include	"type.h"
#include	"lexicon.h"
#include	"dag-provenance.h"

print_prov(struct dg_provenance	*p)
{
	switch(p->kind)
	{
		case	FROM_LUI_TREE:	printf("lui tree %s", p->arg0id);	break;
		case	FROM_LUI_EDGE:	printf("lui edge %s", p->arg0id);	break;
		case	FROM_TREE:	printf("tree %p / #%d", p->arg0ptr, ((struct hypothesis*)p->arg0ptr)->edge->id);	break;
		case	FROM_EDGE:	printf("edge #%d", ((struct edge*)p->arg0ptr)->id);	break;
		case	FROM_TYPE:	printf("type %s", ((struct type*)p->arg0ptr)->name);	break;
		case	FROM_LEX:	printf("lexeme %s", ((struct lexeme*)p->arg0ptr)->word);	break;
		case	FROM_LEX_LOCAL:	printf("lexeme %s local constraints", ((struct lexeme*)p->arg0ptr)->word);	break;
		case	FROM_RULE:	printf("rule %s", ((struct rule*)p->arg0ptr)->name);	break;
		case	FROM_UNIFY:	printf("interactive unification");	break;
	}
}

show_provenance(struct path	path, struct dg_provenance	*p)
{
	printf("computing provenance of path ");
	print_path(path);
	printf(" in a dag\n");
	struct edge	*e = NULL;
	struct hypothesis	*h = NULL;
	switch(p->kind)
	{
		case	FROM_LUI_TREE:
			printf("that dag comes from tree LUI#=%s\n", p->arg0id);
			h = lui_find_tree(p->arg0id);
			assert(h);
			e = h->edge;
			goto got_edge;
		case	FROM_LUI_EDGE:
			printf("that dag comes from edge LUI#=%s\n", p->arg0id);
			e = lui_find_edge(p->arg0id);
			goto got_edge;
		case	FROM_TREE:
			h = p->arg0ptr;
			printf("that dag comes from hypothesis %p\n", h);
			e = h->edge;
			goto got_edge;
		case	FROM_EDGE:
			e = p->arg0ptr;
			printf("that comes from edge %p\n", e);
			got_edge:
			assert(e);
			printf(" ... which is edge #%d\n", e->id);
			if(e->rule)
			{
				printf(" = rule %s\n", e->rule->name);
				assert(e->have==e->need);
				struct dg_provenance	rp;
				rp.kind = FROM_RULE;
				rp.arg0ptr = e->rule;
				int i;
				int	arglist[e->have];
				struct dg		*dglist[e->have];
				struct dg_provenance	provlist[e->have];
				struct dg_provenance	*provptrlist[e->have];
				for(i=0;i<e->have;i++)
				{
					printf("  + edge #%d\n", e->daughters[i]->id);
					arglist[i] = i;
					if(p->kind == FROM_EDGE || p->kind == FROM_LUI_EDGE)
					{
						provlist[i].kind = FROM_EDGE;
						provlist[i].arg0ptr = e->daughters[i];
						dglist[i] = e->daughters[i]->dg;
					}
					else
					{
						assert(h != NULL);
						provlist[i].kind = FROM_TREE;
						provlist[i].arg0ptr = h->rhslist[i];
						dglist[i] = h->rhslist[i]->dg;
					}
					provptrlist[i] = &provlist[i];
				}
				show_provenance_from_unification(path, e->rule->dg, &rp, e->have, dglist, provptrlist, arglist);
			}
			else
			{
				printf(" = lex %s\n", e->lex->word);
				printf("which is the unification of lextype %s with its lexent constraints\n", e->lex->lextype->name);
				struct dg_provenance	ltp, localp;
				bzero(&ltp,sizeof(ltp));
				bzero(&localp,sizeof(localp));
				ltp.kind = FROM_TYPE;
				ltp.arg0ptr = e->lex->lextype;
				localp.kind = FROM_LEX_LOCAL;
				localp.arg0ptr = e->lex;
				struct dg	*local = e->lex->dg;
				extern int enable_simple_form_lexicon;
				if(enable_simple_form_lexicon)local = expand_lexeme(e->lex);
				int	arg = -1;
				struct dg_provenance	*provlist = &localp;
				show_provenance_from_unification(path, e->lex->lextype->dg, &ltp, 1, &local, &provlist, &arg);
			}
			break;
		case	FROM_LEX_LOCAL:;
			struct lexeme	*lex = p->arg0ptr;
			printf("that comes from the immediate constraints on lexeme %s\n", lex->word);
			break;
		case	FROM_TYPE:;
			struct type	*type = p->arg0ptr;
			printf("that comes from constraints on type %s\n", type->name);
			break;
		case	FROM_RULE:;
			struct rule	*r = p->arg0ptr;
			printf("that comes from constraints on rule %s\n", r->name);
			break;
		default:
			printf("that comes from provenance type %d\n", p->kind);
			break;
	}
}

void	collect_paths(struct dg	*from, struct dg	*to, int	*N, struct path	**P, int	len, int	*path)
{
	from = dereference_dg(from);
	if(from==to)
	{
		struct path	p;
		p.count = len;
		p.fnums = malloc(sizeof(int)*len);
		memcpy(p.fnums, path, sizeof(int)*len);
		(*N)++;
		*P = realloc(*P,sizeof(struct path)*(*N));
		(*P)[(*N)-1] = p;
	}
	if(len>=1000)
	{
		fprintf(stderr, "surprisingly deep dag (1000 levels); terminating search\n");
		return;
	}
	int	i;
	struct dg	**darcs = DARCS(from);
	for(i=0;i<from->narcs;i++)
	{
		path[len] = from->label[i];
		collect_paths(darcs[i], to, N, P, len+1, path);
	}
	extern unsigned int generation;
	if(from->gen != generation)return;
	for(i=0;i<from->ncarcs;i++)
	{
		path[len] = from->carcs[i].label;
		collect_paths(from->carcs[i].dest, to, N, P, len+1, path);
	}
}

struct path	arg_subpath(struct path	p, int	arg)
{
	struct path	p2 = p;
	if(arg==-1)return p;
	// get a shortened path, making sure it's actually inside the right arg
	if(!p2.count-- || *p2.fnums++!=lookup_fname("ARGS"))assert(!"no such arg subpath");
	while(arg-->0)
		if(!p2.count-- || *p2.fnums++!=lookup_fname("REST"))assert(!"no such arg subpath");
	if(!p2.count-- || *p2.fnums++!=lookup_fname("FIRST"))assert(!"no such arg subpath");
	return p2;
}

struct dg	*dg_path_in_arg(struct dg	*from, struct path	p, int	arg)
{
	struct path	p2 = p;
	if(arg==-1)return dg_path(from, p);
	// get a shortened path, making sure it's actually inside the right arg
	if(!p2.count-- || *p2.fnums++!=lookup_fname("ARGS"))return NULL;
	while(arg-->0)
		if(!p2.count-- || *p2.fnums++!=lookup_fname("REST"))return NULL;
	if(!p2.count-- || *p2.fnums++!=lookup_fname("FIRST"))return NULL;
	return dg_path(from, p2);
}

show_provenance_from_unification(struct path	path, struct dg	*left, struct dg_provenance	*leftp, int	nright, struct dg	**rightlist, struct dg_provenance	**rightplist, int	*rightarglist)
{
	// make copies so we don't see any structure sharing
	//printf("right0 on input:\n"); print_dg(rightlist[0]);printf("\n");
	enable_packing(0);
	left = copy_dg(left);
	int	i,s;
	for(i=0;i<nright;i++)
		rightlist[i] = copy_dg(rightlist[i]);
	for(i=0;i<nright;i++)
	{
		int	result = unify_dg_tmp(rightlist[i], left, rightarglist[i]);
		if(result)
		{
			unify_backtrace();
			assert(result==0);
		}
	}
	struct dg	*node = dg_path(left, path);
	if(!node)
	{
		printf("SURPRISE: that path is missing.  weird.\n");
		printf("wanted path: "); print_path(path); printf("\n");
		print_within_generation(left);
		printf("\n");
		assert(node != NULL);
	}
	extern unsigned int	generation;
	struct type	*t = (node->gen==generation)?node->type:node->xtype;
	printf("unified value is %s\n", t->name);
	int		nupaths = 0;
	struct path	*upaths = NULL;
	int		pathstack[1024];
	collect_paths(left, node, &nupaths, &upaths, 0, pathstack);
	forget_tmp();
	struct type	*g = get_top();
	int		ndghit = 0;
	struct dg	**dghit = NULL;
	struct dg_provenance	**dghitprov = NULL;
	int			*dghitpath = NULL, *dghitarg = NULL;
	int	sufficienti=-1, sufficientd;
	for(i=0;i<nupaths;i++)
	{
		printf("that node can be reached at: "); print_path(upaths[i]); printf("\n");
		for(s=-1;s<nright;s++)
		{
			struct dg	*side;
			struct dg_provenance	*sidep;
			if(s==-1)
			{
				side = dg_path(left, upaths[i]);
				sidep = leftp;
			}
			else
			{
				side = dg_path_in_arg(rightlist[s], upaths[i], rightarglist[s]);
				sidep = rightplist[s];
			}
			//struct dg	*side = dg_path(s?left:right, upaths[i]);
			//struct dg_provenance	*sidep = s?leftp:rightp;
			if(!side)
			{
				printf("	");
				print_prov(sidep);
				printf(" -- no value there at all\n");
				continue;
			}
			int	j;
			for(j=0;j<ndghit;j++)
				if(dghit[j]==side)break;
			if(j<ndghit)
			{
				printf("	");
				print_prov(sidep);
				printf(" -- node was already seen at a different path\n");
				continue;
			}
			ndghit++;
			dghit = realloc(dghit,sizeof(struct dg*)*ndghit);
			dghitprov = realloc(dghitprov,sizeof(struct dg_provenance*)*ndghit);
			dghitpath = realloc(dghitpath,sizeof(int)*ndghit);
			dghitarg = realloc(dghitarg,sizeof(int)*ndghit);
			dghit[ndghit-1] = side;
			dghitprov[ndghit-1] = sidep;
			dghitpath[ndghit-1] = i;
			dghitarg[ndghit-1] = (s==-1)?-1:rightarglist[s];
			struct type	*stype = side->xtype;
			printf("	");
			print_prov(sidep);
			printf(" -- value before was %s\n", stype->name);
			if(stype==t)
			{
				sufficienti = i;
				sufficientd = ndghit-1;
			}
			struct type	*g2 = glb(g, stype);
			if(g!=g2)
			{
				if(g != get_top())
				{
					printf("	which lowers the GLB to %s\n", g2->name);
				}
				g = g2;
			}
		}
	}
	if(g == t)
	{
		printf("and that adequately explains the value '%s'\n", t->name);
	}
	else
	{
		printf("but that is not enough to explain '%s'; there must be type lowering higher in the AVM.\n", t->name);
	}
	if(sufficienti>=0)
	{
		printf("recursing on sufficient provenance from: ");
		print_prov(dghitprov[sufficientd]); printf("\n");
		struct path	subpath = arg_subpath(upaths[sufficienti], dghitarg[sufficientd]);
		show_provenance(subpath, dghitprov[sufficientd]);
	}
	else
	{
		printf("recursing on all %d sources:\n", ndghit);
		for(i=0;i<ndghit;i++)
		{
			print_prov(dghitprov[i]); printf("\n");
			struct path	subpath = arg_subpath(upaths[dghitpath[i]], dghitarg[i]);
			show_provenance(subpath, dghitprov[i]);
		}
	}
	if(upaths)free(upaths);
}
