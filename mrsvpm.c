#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

#include	"freeze.h"
#include	"mrs.h"
#include	"dag.h"

struct vpm
{
	char	*lefth, *righth;	// type hierarchy names
	char	*filename;
	struct vpm_context
	{
		int		nleft, nright;
		char	**left, **right;
		struct vpm_rule
		{
			char			**left, **right;
			#define	VPM_TYPE_EQUALITY	0x1
			#define	VPM_TYPE_VACANCY	0x2
			unsigned char	*leftprops, *rightprops;
			#define	VPM_LEFT_TO_RIGHT	0x1
			#define	VPM_RIGHT_TO_LEFT	0x2
			#define	VPM_BIDIRECTIONAL	(VPM_LEFT_TO_RIGHT | VPM_RIGHT_TO_LEFT)
			unsigned		direction;
		}	*rules;
		int	nrules;
	}		*contexts;
	int		ncontexts;
};

static struct vpm *v, *vinv;

struct vpm	*invert_vpm(struct vpm	*v)
{
	struct vpm	*vpm = malloc(sizeof(*v));
	struct vpm_context *ctx, *fctx;
	struct vpm_rule	*r, *fr;
	int			i, j, k;

	vpm->ncontexts = v->ncontexts;
	vpm->contexts = malloc(sizeof(struct vpm_context)*vpm->ncontexts);
	vpm->lefth = v->righth;
	vpm->righth = v->lefth;
	for(i=0;i<vpm->ncontexts;i++)
	{
		ctx = v->contexts+i;
		fctx = vpm->contexts+i;
		*fctx = *ctx;
		fctx->nleft = ctx->nright;
		fctx->nright = ctx->nleft;
		fctx->right = ctx->left;
		fctx->left = ctx->right;
		fctx->rules = malloc(sizeof(struct vpm_rule)*ctx->nrules);
		fctx->nrules = ctx->nrules;
		for(j=0;j<ctx->nrules;j++)
		{
			r = ctx->rules+j;
			fr = fctx->rules+j;
			fr->direction = (r->direction==VPM_BIDIRECTIONAL)?r->direction:(r->direction^VPM_BIDIRECTIONAL);
			fr->right = r->left;
			fr->left = r->right;
			fr->rightprops = r->leftprops;
			fr->leftprops = r->rightprops;
		}
	}
	return vpm;
}

int	load_vpm(char	*path)
{
	FILE	*f = fopen(path, "r");
	char	line[1024], *args[32], *p;
	int		nargs, iop, i, lnum=0;
	struct vpm_context *ctx;
	struct vpm			vpm = {.lefth = 0, .righth = 0, .ncontexts = 0, .contexts = 0};

	if(!f)
	{
		perror(path);
		return -1;
	}
	vpm.filename = freeze_string(path);
	vpm.ncontexts++;
	ctx = vpm.contexts = slab_alloc(sizeof(*ctx));
	bzero(ctx, sizeof(*ctx));
	ctx->left = slab_alloc(sizeof(char*));	ctx->left[0]=freeze_string(""); ctx->nleft = 1;
	ctx->right = slab_alloc(sizeof(char*));	ctx->right[0]=freeze_string(""); ctx->nright = 1;
	while(!feof(f) && (p = fgets(line, 1023, f)) != NULL)
	{
		++lnum;
		nargs = 0; args[0] = 0;
		while(*p)
		{
			if(*p==';' || *p=='\n'){*p=0; break;}
			if(*p==' ' || *p=='\t'){*p=0; if(args[nargs])args[++nargs]=0; }
			else if(!args[nargs])args[nargs] = p;
			p++;
		}
		if(args[nargs])nargs++;
		else if(!nargs)continue;
		else if(nargs<3)
		{
			syntax:
			fprintf(stderr, "vpm: syntax error on line %d, LHS op RHS expected\n", lnum);
			return -1;
		}

		for(iop=1;iop<nargs-1;iop++)
			if(!args[iop][1] && strchr("=<>:#", args[iop][0]))break;
		if(iop==nargs-1)goto syntax;

		if(args[iop][0]==':')
		{
			// set context
			vpm.ncontexts++;
			vpm.contexts = slab_realloc(vpm.contexts,
				sizeof(struct vpm_context)*(vpm.ncontexts-1),
				sizeof(struct vpm_context)*vpm.ncontexts);
			ctx = vpm.contexts + vpm.ncontexts-1;
			ctx->nleft = iop; ctx->nright = nargs-iop-1;
			ctx->left = slab_alloc(sizeof(char*)*ctx->nleft);
			ctx->right = slab_alloc(sizeof(char*)*ctx->nright);
			for(i=0;i<ctx->nleft;i++)ctx->left[i] = freeze_string(args[i]);
			for(i=0;i<ctx->nright;i++)ctx->right[i] = freeze_string(args[iop+1+i]);
			ctx->rules = 0;
			ctx->nrules = 0;
		}
		else if(args[iop][0]=='#')
		{
			// type hierarchy specifiers
			if(nargs != 3)
			{
				fprintf(stderr, "vpm: syntax error on line %d\n", lnum);
				return -1;
			}
			vpm.lefth = freeze_string(args[0]);
			vpm.righth = freeze_string(args[2]);
		}
		else
		{
			struct vpm_rule r;
			if(iop != ctx->nleft || nargs-iop-1 != ctx->nright)
			{
				fprintf(stderr, "vpm: syntax error on line %d, %d/%d context but %d/%d rule\n",
					lnum, ctx->nleft, ctx->nright, iop, nargs-iop-1);
				return -1;
			}
			r.direction = (args[iop][0]=='=')?VPM_BIDIRECTIONAL:
							((args[iop][0]=='<')?VPM_RIGHT_TO_LEFT:
								VPM_LEFT_TO_RIGHT);
			r.left = slab_alloc(sizeof(char*)*ctx->nleft);
			r.right = slab_alloc(sizeof(char*)*ctx->nright);
			r.leftprops = slab_alloc(sizeof(char)*ctx->nleft);
			r.rightprops = slab_alloc(sizeof(char)*ctx->nright);
#define VPMPROP(type)	((type[0]=='!' || type[0]=='=')?((*type++=='!')?VPM_TYPE_VACANCY:VPM_TYPE_EQUALITY):0)
			for(i=0;i<ctx->nleft;i++)
			{
				r.left[i] = freeze_string(args[i]);
				r.leftprops[i] = VPMPROP(r.left[i]);
			}
			for(i=0;i<ctx->nright;i++)
			{
				r.right[i] = freeze_string(args[iop+1+i]);
				r.rightprops[i] = VPMPROP(r.right[i]);
			}
			ctx->nrules++;
			ctx->rules = slab_realloc(ctx->rules,
				sizeof(struct vpm_rule)*(ctx->nrules-1),
				sizeof(struct vpm_rule)*ctx->nrules);
			ctx->rules[ctx->nrules-1] = r;
		}
	}
	if(!vpm.lefth || !vpm.righth)
	{
		fprintf(stderr, "vpm: no left/right hierarchy specified\n");
		return -1;
	}
	v = slab_alloc(sizeof(vpm));
	*v = vpm;

	/*printf("vpm [%s # %s] has %d contexts:\n", v->lefth, v->righth, v->ncontexts);
	for(i=0;i<v->ncontexts;i++)
	{
		int j;
		ctx = v->contexts+i;
		printf("left context:"); for(j=0;j<ctx->nleft;j++)printf("	%s", ctx->left[j]); printf("\n");
		printf("right context:"); for(j=0;j<ctx->nright;j++)printf("	%s", ctx->right[j]); printf("\n");
		for(j=0;j<ctx->nrules;j++)
		{
			struct vpm_rule *r = ctx->rules+j;
			int k;
			printf("  left types:"); for(k=0;k<ctx->nleft;k++)printf("	%s", r->left[k]); printf("\n");
			printf("  right types:"); for(k=0;k<ctx->nright;k++)printf("	%s", r->right[k]); printf("\n");
		}
	}*/
	return 0;
}

void	*freeze_vpm()
{
	struct vpm	*vpm = freeze_block(v, sizeof(*v));
	struct vpm_context *ctx, *fctx;
	struct vpm_rule	*r, *fr;
	int			i, j, k;

	vpm->contexts = slab_alloc(sizeof(struct vpm_context)*vpm->ncontexts);
	vpm->ncontexts = v->ncontexts;
	vpm->lefth = freeze_string(v->lefth);
	vpm->righth = freeze_string(v->righth);
	for(i=0;i<vpm->ncontexts;i++)
	{
		ctx = v->contexts+i;
		fctx = vpm->contexts+i;
		*fctx = *ctx;
		fctx->left = slab_alloc(sizeof(char*)*ctx->nleft);
		fctx->right = slab_alloc(sizeof(char*)*ctx->nright);
		for(j=0;j<ctx->nleft;j++)
			fctx->left[j] = freeze_string(ctx->left[j]);
		for(j=0;j<ctx->nright;j++)
			fctx->right[j] = freeze_string(ctx->right[j]);
		fctx->rules = slab_alloc(sizeof(struct vpm_rule)*ctx->nrules);
		fctx->nrules = ctx->nrules;
		for(j=0;j<ctx->nrules;j++)
		{
			r = ctx->rules+j;
			fr = fctx->rules+j;
			fr->direction = r->direction;
			fr->left = slab_alloc(sizeof(char*)*ctx->nleft);
			fr->right = slab_alloc(sizeof(char*)*ctx->nright);
			fr->leftprops = slab_alloc(sizeof(char)*ctx->nleft);
			fr->rightprops = slab_alloc(sizeof(char)*ctx->nright);
			for(k=0;k<ctx->nleft;k++)
			{
				fr->left[k] = freeze_string(r->left[k]);
				fr->leftprops = freeze_block(r->leftprops, ctx->nleft);
			}
			for(k=0;k<ctx->nright;k++)
			{
				fr->right[k] = freeze_string(r->right[k]);
				fr->rightprops = freeze_block(r->rightprops, ctx->nright);
			}
		}
	}
	return vpm;
}

void recover_vpm(void *vpm)
{
	v = vpm;
}

// XXX need to support VPM_TYPE_VACANCY

int vpm_test(char *value, char *against, unsigned char flags)
{
extern int prop_subsumes(char*,char*);
	int res;
	//printf("compare `%s' vs `%s' flags %d\n", against, value, flags);
	if(flags & VPM_TYPE_EQUALITY)return strcasecmp(against, value);
	res = !prop_subsumes(against, value);
	return res;
}

char	*vpm_map(char	**right, struct vpm_context	*ctx, int	left)
{
	int	i, j;

	for(i=0;i<ctx->nrules;i++)
	{
		struct vpm_rule	*r = ctx->rules+i;
		for(j=0;j<ctx->nright;j++)
			if(vpm_test(right[j], r->right[j], r->rightprops[j]) != 0)
				break;
		if(j<ctx->nright)continue;
		// else the rule applies.
		return strdup(r->left[left]);
	}
	return 0;
}

struct mrs_var	*convert_mrs_var(struct vpm *v, struct mrs_var	*vin)
{
	struct mrs_var	*vout = malloc(sizeof(*vout));
	char			inused[vin->nprops];
	int				i, j, k;

	*vout = *vin;
	vout->name = strdup(vin->name);
	vout->type = 0;
	bzero(inused, vin->nprops);
	vout->nprops = 0;
	vout->props = 0;

	for(i=0;i<v->ncontexts;i++)
	{
		struct vpm_context *ctx = v->contexts+i;
		if(ctx->nleft==1 && *ctx->left[0]==0 && ctx->nright==1 && *ctx->right[0]==0)
		{
			// context matches root level
			vout->type = vpm_map(&vin->type, ctx, 0);
		}
		else
		{
			char	*values[ctx->nright];
			int		props[ctx->nright];
			// try to align context to vin's properties
			for(j=0;j<ctx->nright;j++)values[j] = 0;
			for(j=0;j<ctx->nright;j++)
			{
				for(k=0;k<vin->nprops;k++) if(!inused[k])
					if(!strcasecmp(vin->props[k].name, ctx->right[j]))
					{
						values[j] = vin->props[k].value;
						props[j] = k;
						break;
					}
				if(k==vin->nprops)break;
			}
			if(j<ctx->nright)continue;
			for(j=0;j<ctx->nleft;j++)
			{
				char	*nval = vpm_map(values, ctx, j);
				if(!nval)break; // should only ever fail the first iteration
				vout->nprops++;
				vout->props = realloc(vout->props, sizeof(struct mrs_var_property)*vout->nprops);
				vout->props[vout->nprops-1].name = strdup(ctx->left[j]);
				vout->props[vout->nprops-1].value = nval;
			}
			if(j==ctx->nleft) for(j=0;j<ctx->nright;j++)inused[props[j]] = 1;
		}
	}
	if(!vout->type)vout->type = strdup(vin->type);
	for(i=0;i<vin->nprops;i++) if(!inused[i])
	{
		vout->nprops++;
		vout->props = realloc(vout->props, sizeof(struct mrs_var_property)*vout->nprops);
		vout->props[vout->nprops-1].name = strdup(vin->props[i].name);
		vout->props[vout->nprops-1].value = strdup(vin->props[i].value);
	}
	//printf("converted: ");print_mrs_var(vout);printf("\n");
	return vout;
}

struct mrs_var	*internal_to_external_mrs_var(struct mrs_var	*vin)
{
	return convert_mrs_var(v, vin);
}

struct mrs_var	*external_to_internal_mrs_var(struct mrs_var	*vin)
{
	if(!vinv)vinv = invert_vpm(v);
	return convert_mrs_var(vinv, vin);
}
