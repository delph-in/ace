#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<sys/time.h>

#include	"tdl.h"
#include	"type.h"
#include	"freeze.h"

int		ntypes;
struct type	**types;
int		glb_type_count;

int		nstrings;
struct type	**strings;

struct type	*get_top() { return types[0]; }
struct type	*add_string(char	*name);

struct type	*strtype = 0;

#define	THSIZE	100000
static struct type	**typehash;

extern unsigned int generation;

void	th_init()
{
	typehash = calloc(sizeof(struct type*), THSIZE);
}

void	inval_thash()
{
	if(typehash)
	{
		free(typehash);
		typehash = 0;
	}
}

unsigned int	thash(char	*name)
{
	unsigned int	h = 0;

	while(*name)
	{
		h <<= 1;
		h ^= tolower(*name++);
	}
	return h;
}

void	th_add(char	*name, struct type	*t)
{
	unsigned int	h = thash(name), i;

	if(!typehash)th_init();

	for(i=0;i<THSIZE;i++)
		if(!typehash[(h+i)%THSIZE])
		{
			typehash[(h+i)%THSIZE] = t;
			return;
		}
	fprintf(stderr, "typehash: table full!\n");
}

void *freeze_typehash()
{
	int i;
	typehash = slab_alloc(sizeof(struct type*) * THSIZE);

	bzero(typehash, sizeof(struct type*)*THSIZE);
	for(i=0;i<ntypes;i++)th_add(types[i]->name, types[i]);
	return typehash;
}

void recover_typehash(void *frozen)
{
	typehash = frozen;
}

struct type	*th_look(char	*name)
{
	unsigned int	h = thash(name), i;

	if(!typehash)return 0;

	for(i=0;i<THSIZE;i++)
	{
		if(typehash[(h+i)%THSIZE] && !strcasecmp(typehash[(h+i)%THSIZE]->name, name))
			return typehash[(h+i)%THSIZE];
		if(!typehash[(h+i)%THSIZE])return 0;
	}
	return 0;
}

struct type	*lookup_type(char	*name)
{
	int	i;

	if(name[0]=='"' || name[0]=='\'')
	{
		if(!strcmp(name, "\"\\\"\""))strcpy(name, "\"\"\"");

		if(name[0]=='\'')
			name[0]='"';	// elsewhere in the code we assume strings start with ".
		// auto-add strings. this does a lookup too.
		return add_string(name);
	}

	if(typehash)return th_look(name);
	else for(i=0;i<ntypes;i++)
		if(!strcasecmp(types[i]->name, name))
			return types[i];
	return 0;
}

void	print_type(char	*name)
{
	struct type	*t = lookup_type(name);

	if(!t)
	{
		fprintf(stderr, "print_type: `%s' doesn't exist\n", name);
		return;
	}
	printf("type `%s': ", name);
	print_dg(t->dg);
	printf("\n\n");
}

int	descendent_of(int	dec, struct type	*of)
{
	int	i, j, k, x;

	if(!of->ndescendents)return 0;
	if(dec==ntypes-1)return dec==of->descendents[of->ndescendents-1];
	i = 0, j = of->ndescendents-1;
	while(i<j)
	{
		k = (i+j)/2;
		x = of->descendents[k];
		if(x==dec)return 1;
		else if(x<dec)i=k+1;
		else j=k-1;
	}
	return dec == of->descendents[i];
}

void	add_descendent(struct type	*anc, int	dec)
{
	int	i;

	if(dec==ntypes-1)i=anc->ndescendents;
	else
	{
		for(i=0;i<anc->ndescendents;i++)
			if(dec < anc->descendents[i])break;
	}
	anc->ndescendents++;
	anc->descendents = realloc(anc->descendents, sizeof(short)*anc->ndescendents);
	if(i!=anc->ndescendents-1)
		memmove(anc->descendents+i+1, anc->descendents+i, sizeof(short) * (anc->ndescendents-1-i));
	anc->descendents[i] = dec;
}

struct type	*add_string(char	*name)
{
	struct type	*t;
	int		i, j, k, r;

	// binary search for existing string
	for(i=0,j=nstrings-1;i<=j;)
	{
		k = (i+j)/2;
		r = strcmp(strings[k]->name, name);
		if(r==0)return strings[k];

		if(r<0)i = k+1;
		else j = k-1;
	}
	// i is (conveniently) the right place to insert

	t = calloc(sizeof(struct type), 1);
	nstrings++;

	if(FROZEN(strings))
	{
		struct type **str = malloc(sizeof(struct type*)*nstrings);
		memcpy(str, strings, sizeof(struct type*)*(nstrings-1));
		strings = str;
	}
	else strings = realloc(strings, sizeof(struct type*) * nstrings);

	memmove(strings+i+1, strings+i, sizeof(struct type*) * (nstrings-1-i));
	strings[i] = t;

	t->name = strdup(name);
	t->ndescendents = 0;
	t->descendents = 0;
	t->dg = 0;
	t->tnum = -1;//nstrings-1;

	return t;
}

void	number_strings()
{
	int	i;

	for(i=0;i<nstrings;i++)strings[i]->tnum = i;
}

int	add_type_only(char	*name, int	npar, struct type	**par)
{
	struct type	*t;
	int		i, pari;

	if(th_look(name))
	/*for(i=0;i<ntypes;i++)
		if(!strcasecmp(types[i]->name, name))*/
		{
			fprintf(stderr, "type: `%s' already declared, go away!\n", name);
			return -1;
		}

	/*fprintf(stderr, "new type `%s' inherits from:", name);
	for(i=0;i<npar;i++)
	{
		fprintf(stderr, " %s", par[i]->name);
	}
	fprintf(stderr, "\n");*/

	t = calloc(sizeof(struct type), 1);
	ntypes++;
	types = realloc(types, sizeof(struct type*) * ntypes);
	types[ntypes-1] = t;
	t->name = strdup(name);
	t->ndescendents = 0;
	t->descendents = 0;
	t->dg = 0;
	if(!strcasecmp(name, "string"))strtype = t;

	//fprintf(stderr, " - linking\n");
	t->tnum = ntypes-1;
	for(i=0;i<ntypes-1;i++)
	{
		for(pari=0;pari<npar;pari++)
			if(descendent_of(par[pari]->tnum, types[i]))
			{
				if(!descendent_of(ntypes-1, types[i]))
					add_descendent(types[i], ntypes-1);
				break;
			}
	}
	add_descendent(t, ntypes-1);
	//fprintf(stderr, " - done adding\n");

	th_add(name, t);

	return 0;
}

extern int		g_loaded;

int	intcomp(const void	*a, const void	*b) { return *(int*)a - *(int*)b; }

int	constrain_type(char	*name, struct dg	*dg)
{
	struct type	*type = lookup_type(name);

	if(!type)
	{
		fprintf(stderr, "type: tried to constrain type `%s' which doesn't exist yet\n", name);
		return -1;
	}
	if(!dg)type->dg = atomic_dg(name);
	else type->dg = dg;

	type->dg->type = type->dg->xtype = type;

	/*printf("\ntype: %s	definition: ", type->name);
	print_dg(type->dg); printf("\n");*/
	return 0;
}

static struct glb_fix
{
	struct type	*para, *parb, *glb;
}		*fixes;
static int	nfixes;

int	constrain_glb_type(struct type	*g)
{
	int		i;
	struct glb_fix	*f;

	if(g->dg)return 0;
	for(i=0;i<nfixes;i++)
		if(fixes[i].glb==g)break;
	if(i>=nfixes)
	{
		fprintf(stderr, "glb-fix: no glb type `%p' (%s) was recorded\n", g, g->name);
		exit(-1);
	}
	f = fixes+i;

	if(!f->para->dg)constrain_glb_type(f->para);
	if(!f->parb->dg)constrain_glb_type(f->parb);
	g->dg = unify_dg(f->para->dg, f->parb->dg, -1);
	if(!g->dg)
	{
		fprintf(stderr, "glb-fix: unexpected error! glb type `%s' is inconsistent.\n", g->name);
#ifdef	UNIFY_PATHS
		unify_backtrace();
#endif
		exit(-1);
	}
	return 0;
}

void	fix_glbs()
{
	int	i;

	for(i=0;i<nfixes;i++)
		constrain_glb_type(fixes[i].glb);
	nfixes = 0;
	if(fixes)free(fixes);
}

#define	CSTSIZE	(16384*2-1)
//static char	csthashres[CSTSIZE];
static struct type	*csthasha[CSTSIZE];
static struct type	*csthashb[CSTSIZE];
int					ncsthash_collisions = 0, ncsthash_requests = 0;

int	find_common_subtypes(struct type	*a, struct type	*b, unsigned short	*_glb_common)
{
	int	_glb_ncommon = 0, ia, ib, and, bnd;
	unsigned short *ad, *bd;
	unsigned long	h = (((unsigned long)a<<5) ^ ((unsigned long)b>>5)) % (CSTSIZE);

	ncsthash_requests++;
	for(ia=0;ia<4;ia++)
		if(csthasha[(h+17*ia)%CSTSIZE]==a && csthashb[(h+17*ia)%CSTSIZE]==b)return 0;// && !csthashres[h])return 0;

	ia = ib = 0;
	and = a->ndescendents;
	bnd = b->ndescendents;
	ad = a->descendents;
	bd = b->descendents;
	while(ia < and && ib < bnd)
	{
		if(*ad == *bd)
		{
			//if(ia==0 || ib==0)return -1;	// one type is a descendent of the other
			if((_glb_ncommon+1) >= 10000)
			{
				fprintf(stderr, "too many common types for our buffer!\n");
				exit(-1);
			}
			_glb_common[_glb_ncommon++] = *ad;
			ia++;ad++;
			ib++;bd++;
		}
		else if(*ad < *bd){ia++;ad++;}
		else {ib++;bd++;}
	}

	// try to find a free hash slot
	for(ia=0;ia<4;ia++)
		if(!csthasha[(h+17*ia)%CSTSIZE])break;
	if(ia==4)ia=0;
	h = (h+17*ia)%CSTSIZE;
	if(csthasha[h])ncsthash_collisions++;
	csthasha[h] = a;
	csthashb[h] = b;
	//csthashres[h] = _glb_ncommon?1:0;
	return _glb_ncommon;
}

int	check_for_glb(struct type	*a, struct type	*b)
{
	unsigned short	_glb_common[10000];
	int				_glb_ncommon;
	int	ia, i, j, mi, resort, resort_key=-1;
	struct type	*ar[2] = {a, b};
	char	name[1024];
	struct type	*g;

	_glb_ncommon = find_common_subtypes(a, b, _glb_common);
	//if(_glb_ncommon==-1)return 0;	// one type is a descendent of the other

	if(_glb_ncommon==0)return 0;	// no types in common, no glb
	if(_glb_ncommon==1)return 0;	// one type in common, it must be the glb

	// check first common descendent to see if it's the glb
	g = types[_glb_common[0]];
	if(g->ndescendents == _glb_ncommon)
	{
		for(j=0;j<_glb_ncommon;j++)
			if(g->descendents[j] != _glb_common[j])
				break;
		if(j==_glb_ncommon)return 0;	// this is the glb
	}

	// no glb existed; make one!
	sprintf(name, "G(%s,%s)", a->name, b->name);
	add_type_only(name, 2, ar);
	//printf("add `%s'\n", name);

	_glb_common[_glb_ncommon++] = ntypes-1;	// a,b have a new common descendent

	g = types[ntypes-1];
	g->ndescendents = _glb_ncommon;
	g->descendents = realloc(g->descendents, sizeof(short)*_glb_ncommon);
	memcpy(g->descendents, _glb_common, sizeof(short)*_glb_ncommon);

	nfixes++;
	fixes = realloc(fixes, sizeof(struct glb_fix)*nfixes);
	fixes[nfixes-1].glb = g;
	fixes[nfixes-1].para = a;
	fixes[nfixes-1].parb = b;

	// compute where to put the new type wrt the partial order 'descendent_of'
	mi=0;
	for(i=0;i<ntypes-1;i++)
		if(descendent_of(ntypes-1, types[i]))mi = i;
	mi++;
	//printf("  *** goes at %d / %d\n", mi, ntypes-1);
	//printf("  after which: ");
	//for(i=mi;i<ntypes-1;i++)printf("%s ", types[i]->name);
	//printf("\n");

	if(mi != ntypes-1)
	{
		g = types[ntypes-1];
		memmove(types+mi+1, types+mi, sizeof(struct type*)*(ntypes-1-mi));
		types[mi] = g;
		g->tnum = mi;
		// relocate 'descendents' of everybody who pointed at someone who got moved
		for(i=0;i<ntypes;i++)
		{
			unsigned short ndesc, ntm1, *desc;
			g = types[i];
			if(i>mi)g->tnum++;
			resort = 0;
			desc = g->descendents;
			ndesc = g->ndescendents;
			ntm1 = ntypes-1;
			for(ia=0;ia<ndesc;ia++,desc++)
			{
				if(*desc==ntm1){resort=1;resort_key = ia;*desc=mi;}
				else if(*desc>=mi)(*desc)++;
			}
			if(resort && g->ndescendents>1)
			{
				// item "resort_key" is in the wrong place; fix it!
				if(resort_key != g->ndescendents-1)
				{
					fprintf(stderr, "WRONG: resort_key = %d / %d\n", resort_key, g->ndescendents);
					qsort(g->descendents, g->ndescendents, sizeof(short), intcomp);
				}
				else
				{
					// find out where 'mi' belongs and put it there
					for(ia=0;ia<g->ndescendents-1;ia++)
						if(mi < g->descendents[ia])break;
					memmove(g->descendents+ia+1, g->descendents+ia, sizeof(short)*(g->ndescendents-1-ia));
					g->descendents[ia] = mi;
				}
			}
		}
	}
	glb_type_count++;
	return 1;
}

int	make_glbs()
{
	int	i, j;

	// don't do 0; finding glb's of *top* with things is a big waste of time!
	for(i=ntypes-1;i>0;i--)
	{
		more:
		if(types[i]->ndescendents == 1)continue;
		for(j=ntypes-1;j>i;j--)
		{
			if(types[j]->ndescendents == 1)continue;
			if(check_for_glb(types[i], types[j]))
			{
				// check the newly created type against everything
				//i = ntypes-1;
				i++;
				//printf(" +%d\n", ntypes);
				goto more;
			}
		}
	}
	//printf("%d cst hash collisions / %d R  ", ncsthash_collisions, ncsthash_requests);
	return 0;
}

struct type	*most_general_type_for_dg(struct dg	*d);
int		wfpath[1024], wfpl = 0;

long top_unifies_wellform;

// make children well-formed
void	wellform(struct dg	*d, char	*typename)
{
	int		i;
	struct type	*t;
	//extern char	**fnames;
	struct darc	*arcs = DARCS(d);

	for(i=0;i<d->narcs;i++)
	{
		arcs[i].dest = (struct dg*)dereference_dg(arcs[i].dest);
		wfpath[wfpl++] = arcs[i].label;
		wellform(arcs[i].dest, typename);
		t = most_general_type_for_dg(arcs[i].dest);
		if(!t)
		{
			fprintf(stderr, "well-formedness: no type is compatible with features and type of `%s'\n", arcs[i].dest->type->name);
			exit(-1);
		}
		if(t != arcs[i].dest->type)
		{
			//fprintf(stderr, "well-formedness forces dag `%s' -> `%s'  (inside constraint for type %s)\n", arcs[i].dest->type->name, t->name, typename);
			//for(j=0;j<wfpl;j++)
				//fprintf(stderr, "%s ", fnames[wfpath[j]]);
			//fprintf(stderr, "\n");
			if(!descendent_of(t->tnum, arcs[i].dest->type))
			{
				fprintf(stderr, "unexpected well-formedness constraint forces dag `%s' to become more general type `%s'!\n", arcs[i].dest->type->name, t->name);
				exit(-1);
			}
			top_unifies_wellform++;
			if(unify_dg_infrom(arcs[i].dest, copy_dg(t->dg)))
			{
				fprintf(stderr, "failed well-formedness constraint!\n");
				exit(-1);
			}
			//print_dg(arcs[i].dest);printf("\n");
		}
		wfpl--;
	}
}

int	init_top_type()
{
	types = calloc(sizeof(struct type*), 1);

	if(add_type_only("*top*", 0, 0)) return -1;
	types[0]->dg = atomic_dg("*top*");
	return 0;
}

int	constrain_glbs_and_wellformedness()
{
	int	i, j, descbytes = 0;

	fix_glbs();

	for(i=0;i<ntypes;i++)
	{
		// introduce top-level features for this type, it might be the most general type for them
		//printf("pre-welformed dag for `%s':\n", types[i]->name);
		//print_dg(types[i]->dg); printf("\n\n");
		for(j=0;j<types[i]->dg->narcs;j++)
			introduce_feature(DARCS(types[i]->dg)[j].label, i);
	}
	number_strings();	// just in case

	// XXX hack!
	generation = 11;
	// XXX hack!

	for(i=0;i<ntypes;i++)
	{
		//printf("well-forming `%s', current dag:\n", types[i]->name);
		//print_dg(types[i]->dg); printf("\n");
		wellform(types[i]->dg, types[i]->name);
		descbytes += types[i]->ndescendents * sizeof(short);
	}
	//fprintf(stderr, "%d bytes were needed for descendent arrays\n", descbytes);

	return 0;
}

int	load_types()
{
extern int enable_out_of_order;
int	typify_tdl_line(char	*lhs, char	*rhs, void	*ref);

	if(init_top_type())return -1;

	enable_out_of_order = 1;
	process_tdl_status("type", typify_tdl_line, 0);

	run_task("checking for glbs...", make_glbs);

	process_tdl_dgs_status("type", constrain_type);
	enable_out_of_order = 0;

	run_task("processing constraints...", constrain_glbs_and_wellformedness);
	return 0;
}
