#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>

#include	"tdl.h"
#include	"type.h"

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
		h ^= *name++;
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

struct type	*th_look(char	*name)
{
	unsigned int	h = thash(name), i;

	if(!typehash)return 0;

	for(i=0;i<THSIZE;i++)
		if(typehash[(h+i)%THSIZE] && !strcasecmp(typehash[(h+i)%THSIZE]->name, name))
			return typehash[(h+i)%THSIZE];
	return 0;
}

struct type	*lookup_type(char	*name)
{
	int	i;

	if(name[0]=='"' && name[strlen(name)-1]=='"')
	{
		if(!strcmp(name, "\"\\\"\""))strcpy(name, "\"\"\"");
		/*for(i=nstrings-1;i>=0;i--)
			if(!strcasecmp(strings[i]->name, name))
				return strings[i];*/

		// auto-add strings. this does a lookup too.
		return add_string(name);
	}

	if(typehash)return th_look(name);
	else for(i=0;i<ntypes;i++)
		if(!strcasecmp(types[i]->name, name))
			return types[i];
	return 0;
}

print_type(char	*name)
{
	struct type	*t = lookup_type(name);

	if(!t)
	{
		fprintf(stderr, "print_type: `%s' doesn't exist\n", name);
		return -1;
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

int	add_descendent(struct type	*anc, int	dec)
{
	int	i;

	if(dec==ntypes-1)i=anc->ndescendents;
	else
	{
		for(i=0;i<anc->ndescendents;i++)
			if(dec < anc->descendents[i])break;
	}
	anc->ndescendents++;
	anc->descendents = realloc(anc->descendents, sizeof(int)*anc->ndescendents);
	if(i!=anc->ndescendents-1)
		memmove(anc->descendents+i+1, anc->descendents+i, sizeof(int) * (anc->ndescendents-1-i));
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

	strings = realloc(strings, sizeof(struct type*) * nstrings);

	memmove(strings+i+1, strings+i, sizeof(struct type*) * (nstrings-1-i));
	strings[i] = t;

	t->name = strdup(name);
	t->ndescendents = 0;
	t->descendents = 0;
	t->dg = 0;
	t->tnum = -1;//nstrings-1;
	return t;
}

number_strings()
{
	int	i;

	for(i=0;i<nstrings;i++)strings[i]->tnum = i;
}

int	add_type_only(char	*name, int	npar, struct type	**par)
{
	struct type	*t, *g;
	int		i, pari;

	for(i=0;i<ntypes;i++)
		if(!strcasecmp(types[i]->name, name))
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
		for(pari=0;pari<npar;pari++)
			if(descendent_of(par[pari]->tnum, types[i]) && !descendent_of(ntypes-1, types[i]))
				add_descendent(types[i], ntypes-1);
	add_descendent(t, ntypes-1);
	//fprintf(stderr, " - done adding\n");

	th_add(name, t);

	return 0;
}

short		**glb_lut;
extern int		g_loaded;

setup_glb_lut()
{
	int	i, j, ind = 0;
	short	*data;

	glb_lut = calloc(sizeof(short*), ntypes);
	data = malloc(sizeof(short) * ntypes*(ntypes-1)/2);
	memset(data, -1, sizeof(short) * ntypes*(ntypes-1)/2);
	for(i=0;i<ntypes;i++)
	{
		glb_lut[i] = data + ind;
		ind += i;
	}
}

struct type	*glb(struct type	*a, struct type	*b)
{
	int		ia, ib, x, y;

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

	//fprintf(stderr, "glb %s, %s?\n", a->name, b->name);
	ia = ib = 0;
	while(ia < a->ndescendents && ib < b->ndescendents)
	{
		if(a->descendents[ia] == b->descendents[ib])
		{
			// must be the glb, since types are in descending order
			if(g_loaded)glb_lut[y][x] = a->descendents[ia];
			return types[a->descendents[ia]];
		}
		else if(a->descendents[ia] < b->descendents[ib])ia++;
		else ib++;
	}
	//fprintf(stderr, "no glb for %s, %s\n", a->name, b->name);
	return 0;
}

int	intcomp(const void	*a, const void	*b) { return *(int*)a - *(int*)b; }

int	constrain_type(char	*name, struct dg	*dg)
{
	struct type	*type = lookup_type(name);
	int		i;

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

constrain_glb_type(struct type	*g)
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
		unify_backtrace();
		exit(-1);
	}
}

fix_glbs()
{
	int	i;

	for(i=0;i<nfixes;i++)
		constrain_glb_type(fixes[i].glb);
	nfixes = 0;
	if(fixes)free(fixes);
}

int	find_common_subtypes(struct type	*a, struct type	*b, int	*_glb_common)
{
	int	_glb_ncommon = 0, ia, ib;

	ia = ib = 0;
	while(ia < a->ndescendents && ib < b->ndescendents)
	{
		if(a->descendents[ia] == b->descendents[ib])
		{
			if((_glb_ncommon+1) >= 10000)
			{
				fprintf(stderr, "too many common types for our buffer!\n");
				exit(-1);
			}
			_glb_common[_glb_ncommon++] = a->descendents[ia];
			ia++;
			ib++;
		}
		else if(a->descendents[ia] < b->descendents[ib])ia++;
		else ib++;
	}
	return _glb_ncommon;
}

check_for_glb(struct type	*a, struct type	*b)
{
	int	_glb_common[10000], _glb_ncommon;
	int	ia, ib, i, j, mi, resort, resort_key;
	struct type	*ar[2] = {a, b};
	char	name[1024];
	struct type	*g;

	_glb_ncommon = find_common_subtypes(a, b, _glb_common);

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
	g->descendents = realloc(g->descendents, sizeof(int)*_glb_ncommon);
	memcpy(g->descendents, _glb_common, sizeof(int)*_glb_ncommon);

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
			g = types[i];
			if(i>mi)g->tnum++;
			resort = 0;
			for(ia=0;ia<g->ndescendents;ia++)
			{
				if(g->descendents[ia]==ntypes-1){resort=1;resort_key = ia;g->descendents[ia]=mi;}
				else if(g->descendents[ia]>=mi)g->descendents[ia]++;
			}
			if(resort && g->ndescendents>1)
			{
				// item "resort_key" is in the wrong place; fix it!
				if(resort_key != g->ndescendents-1)
				{
					fprintf(stderr, "WRONG: resort_key = %d / %d\n", resort_key, g->ndescendents);
					qsort(g->descendents, g->ndescendents, sizeof(int), intcomp);
				}
				else
				{
					// find out where 'mi' belongs and put it there
					for(ia=0;ia<g->ndescendents-1;ia++)
						if(mi < g->descendents[ia])break;
					memmove(g->descendents+ia+1, g->descendents+ia, sizeof(int)*(g->ndescendents-1-ia));
					g->descendents[ia] = mi;
				}
			}
		}
	}
	glb_type_count++;
	return 1;
}

make_glbs()
{
	int	i, j;

	//for(i=0;i<ntypes;i++)
		//for(j=0;j<i;j++)
	for(i=ntypes-1;i>=0;i--)
	{
		more:
		for(j=ntypes-1;j>i;j--)
			if(check_for_glb(types[i], types[j]))
			{
				i++;
				goto more;
			}
	}
}

struct type	*most_general_type_for_dg(struct dg	*d);
int		wfpath[1024], wfpl = 0;

// make children well-formed
wellform(struct dg	*d, char	*typename)
{
	int		i, j;
	struct type	*t;
	extern char	**fnames;

	for(i=0;i<d->narcs;i++)
	{
		d->arcs[i].dest = (struct dg*)dereference_dg(d->arcs[i].dest);
		wfpath[wfpl++] = d->arcs[i].label;
		wellform(d->arcs[i].dest, typename);
		t = most_general_type_for_dg(d->arcs[i].dest);
		if(!t)
		{
			fprintf(stderr, "well-formedness: no type is compatible with features and type of `%s'\n", d->arcs[i].dest->type->name);
			exit(-1);
		}
		if(t != d->arcs[i].dest->type)
		{
			//fprintf(stderr, "well-formedness forces dag `%s' -> `%s'  (inside constraint for type %s)\n", d->arcs[i].dest->type->name, t->name, typename);
			//for(j=0;j<wfpl;j++)
				//fprintf(stderr, "%s ", fnames[wfpath[j]]);
			//fprintf(stderr, "\n");
			if(!descendent_of(t->tnum, d->arcs[i].dest->type))
			{
				fprintf(stderr, "unexpected well-formedness constraint forces dag `%s' to become more general type `%s'!\n", d->arcs[i].dest->type->name, t->name);
				exit(-1);
			}
			if(unify_dg_infrom(d->arcs[i].dest, copy_dg(t->dg)))
			{
				fprintf(stderr, "failed well-formedness constraint!\n");
				exit(-1);
			}
			//print_da(d->arcs[i].dest);printf("\n");
		}
		wfpl--;
	}
}

int	load_types()
{
#ifdef	ERG
	char	*files[] = {	"../erg/fundamentals.tdl",
				"../erg/lextypes.tdl",
				"../erg/syntax.tdl",
				"../erg/lexrules.tdl",
				"../erg/auxverbs.tdl",
				"../erg/letypes.tdl"	};
#else
	char	*files[] = {	"../smallgrammars/esslli2k-grammar9/types.tdl" };
#endif
	int	i, j;
	struct timeval	tv1, tv2;

	types = calloc(sizeof(struct type*), 1);

	if(add_type_only("*top*", 0, 0)) return -1;
	types[0]->dg = atomic_dg("*top*");

	for(i=0;i<sizeof(files)/sizeof(char*);i++)
	{
		fprintf(stderr, "gleaning types from `%s'\n", files[i]);
		if(iterate_tdl_types(files[i]))
		{
			fprintf(stderr, "types: unable to load `%s' types, bailing out!\n", files[i]);
			exit(-1);
		}
	}
	fprintf(stderr, "checking for glbs... ");
	gettimeofday(&tv1, 0);
	make_glbs();
	gettimeofday(&tv2, 0);
	tv2.tv_sec -= tv1.tv_sec;
	tv2.tv_usec -= tv1.tv_usec;
	fprintf(stderr, "%.2fs\n", (float)tv2.tv_sec + (float)tv2.tv_usec / 1000000);

	for(i=0;i<sizeof(files)/sizeof(char*);i++)
	{
		fprintf(stderr, "constraining types from `%s'\n", files[i]);
		if(iterate_tdl_dgs(files[i], constrain_type))
		{
			fprintf(stderr, "types: unable to load `%s' constraints, bailing out!\n", files[i]);
			exit(-1);
		}
	}
	fix_glbs();

	for(i=0;i<ntypes;i++)
	{
		// introduce top-level features for this type, it might be the most general type for them
		for(j=0;j<types[i]->dg->narcs;j++)
			introduce_feature(types[i]->dg->arcs[j].label, i);
	}
	number_strings();	// just in case

	fprintf(stderr, "enforcing well-formedness...\n");
	for(i=0;i<ntypes;i++)
	{
		//printf("well-forming `%s', current dag:\n", types[i]->name);
		//print_dg(types[i]->dg); printf("\n");
		wellform(types[i]->dg, types[i]->name);
	}

	return 0;
}
