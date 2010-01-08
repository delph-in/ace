#include	<stdlib.h>
#include	<assert.h>
#include	<string.h>
#include	<time.h>

#include	"conf.h"
#include	"chart.h"
#include	"dag.h"
#include	"tdl.h"

#ifdef ERG
#define	HIDEOUS_HACK
#endif

/* essentially, QC extracts a skeleton type tree from a dag
 * and performs *tree*-unification on that skeleton before
 * the full dag-unifier is allowed to run */

/*
typedef enum
{
	_RECORD,
	_PUSH,
	_POP
} qccom;

#define	PUSH(str)	{_PUSH, str}
#define	POP		{_POP, 0}
#define	REC(idx)	{_RECORD, (void*)idx}

struct qcinst
{
	qccom	com;
	char	*arg;
	int	fnum;
}	qcprog[] = { 

REC(6), 
PUSH("INFLECTED"), REC(25), POP, 
PUSH("SYNSEM"), REC(5), 
	PUSH("LKEYS"), PUSH("KEYREL"), PUSH("PRED"), REC(22), POP, POP, POP, 
	PUSH("LOCAL"), REC(38), 
		PUSH("CAT"), PUSH("HEAD"), REC(0), 
				PUSH("AUX"), REC(33), POP, 
				PUSH("INV"), REC(16), POP, 
				PUSH("KEYS"), PUSH("ALTKEY"), REC(42), POP, 
					PUSH("KEY"), REC(7), POP, POP, 
				PUSH("MOD"), REC(4), 
					PUSH("FIRST"), REC(2), 
						PUSH("LOCAL"), REC(13), 
							PUSH("CAT"), PUSH("HEAD"), REC(23), 
									PUSH("KEYS"), PUSH("KEY"), REC(35), POP, POP, POP, POP, POP, POP, POP, 
				PUSH("PRD"), REC(15), POP, 
				PUSH("TAM"), PUSH("TENSE"), REC(28), POP, POP, 
				PUSH("VFORM"), REC(8), POP, POP, 
			PUSH("MC"), REC(18), POP, 
			PUSH("POSTHEAD"), REC(3), POP, 
			PUSH("VAL"), PUSH("COMPS"), REC(1), 
					PUSH("FIRST"), PUSH("LOCAL"), PUSH("CAT"), PUSH("HEAD"), REC(34), POP, POP, POP, 
						PUSH("OPT"), REC(27), POP, POP, POP, 
				PUSH("SPEC"), PUSH("FIRST"), REC(39), POP, POP, 
				PUSH("SPR"), REC(20), 
					PUSH("FIRST"), REC(12), 
						PUSH("LOCAL"), PUSH("CAT"), PUSH("HEAD"), PUSH("KEYS"), PUSH("KEY"), REC(30), POP, POP, POP, POP, POP, 
						PUSH("OPT"), REC(9), POP, POP, POP, 
				PUSH("SUBJ"), REC(10), 
					PUSH("FIRST"), REC(14), 
						PUSH("LOCAL"), PUSH("CAT"), PUSH("HEAD"), PUSH("CASE"), REC(29), POP, POP, POP, POP, 
						PUSH("NON-LOCAL"), PUSH("SLASH"), PUSH("LIST"), REC(36), POP, POP, POP, 
						PUSH("OPT"), REC(11), POP, POP, POP, POP, POP, 
		PUSH("CONJ"), REC(26), POP, 
		PUSH("CONT"), PUSH("HOOK"), PUSH("INDEX"), REC(19), 
					PUSH("E"), PUSH("TENSE"), REC(40), POP, POP, POP, 
				PUSH("XARG"), REC(21), POP, POP, 
			PUSH("MSG"), REC(17), 
				PUSH("PRED"), REC(43), POP, POP, POP, 
		PUSH("STEMHEAD"), REC(41), POP, POP, 
	PUSH("NON-LOCAL"), PUSH("QUE"), REC(44), POP, 
		PUSH("REL"), REC(37), POP, 
		PUSH("SLASH"), REC(32), 
			PUSH("LAST"), REC(24), POP, 
			PUSH("LIST"), REC(31), 
				PUSH("FIRST"), PUSH("CAT"), PUSH("HEAD"), REC(45), 
*/

/*
REC(16),
PUSH("C-CONT"), PUSH("HOOK"), PUSH("INDEX"), REC(93), POP, POP, POP, 
PUSH("INFLECTED"), REC(2), POP, 
PUSH("SYNSEM"), REC(12), 
PUSH("--SEMIND"), PUSH("PNG"), PUSH("GEN"), REC(66), POP, 
PUSH("PN"), REC(79), POP, POP, POP, 
PUSH("LEX"), REC(56), POP, 
PUSH("LOCAL"), PUSH("AGR"), PUSH("DIVISIBLE"), REC(38), POP, 
PUSH("PNG"), PUSH("PN"), REC(32), POP, POP, POP, 
PUSH("CAT"), REC(62), 
PUSH("HEAD"), REC(0), 
//PUSH("AUX"), REC(16), POP, 
PUSH("CASE"), REC(78), POP, 
PUSH("INV"), REC(28), POP, 
PUSH("MOD"), REC(10), 
PUSH("FIRST"), REC(59), 
PUSH("LOCAL"), REC(11), 
PUSH("CAT"), PUSH("HEAD"), REC(33), POP, POP, 
PUSH("KEYS"), PUSH("KEY"), REC(98), POP, POP, POP, POP, POP, 
PUSH("POSS"), REC(94), POP, 
PUSH("PRD"), REC(7), POP, 
PUSH("TAM"), PUSH("ASPECT"), REC(80), POP, 
PUSH("MOOD"), REC(27), POP, 
PUSH("TENSE"), REC(100), POP, POP, 
PUSH("VFORM"), REC(4), POP, POP, 
PUSH("MC"), REC(3), POP, 
PUSH("POSTHEAD"), REC(14), POP, 
PUSH("VAL"), PUSH("COMPS"), REC(6), 
PUSH("FIRST"), REC(53), 
PUSH("--SEMIND"), REC(101), POP, 
PUSH("LOCAL"), PUSH("CAT"), PUSH("HEAD"), REC(29), POP, POP, 
PUSH("KEYS"), PUSH("KEY"), REC(35), POP, POP, POP, 
PUSH("NON-LOCAL"), PUSH("SLASH"), PUSH("LAST"), REC(77), POP, 
PUSH("LIST"), REC(46), POP, POP, POP, 
PUSH("OPT"), REC(9), POP, POP, 
PUSH("REST"), REC(25), 
PUSH("FIRST"), PUSH("LOCAL"), PUSH("CAT"), REC(49), 
PUSH("HEAD"), REC(37), 
PUSH("TAM"), PUSH("TENSE"), REC(65), POP, POP, POP, POP, POP, 
PUSH("OPT"), REC(55), POP, POP, 
PUSH("REST"), REC(43), POP, POP, POP, 
PUSH("SPEC"), PUSH("FIRST"), PUSH("LOCAL"), PUSH("CAT"), PUSH("HEAD"), REC(73), 
PUSH("PRD"), REC(81), POP, POP, POP, POP, 
PUSH("MODIFIED"), REC(70), POP, POP, POP, 
PUSH("SPR"), REC(24), 
PUSH("FIRST"), REC(13), 
PUSH("LOCAL"), PUSH("AGR"), PUSH("DIVISIBLE"), REC(71), POP, POP, 
PUSH("KEYS"), PUSH("KEY"), REC(52), POP, POP, POP, 
PUSH("OPT"), REC(15), POP, POP, POP, 
PUSH("SUBJ"), REC(41), 
PUSH("FIRST"), REC(18), 
PUSH("--SEMIND"), PUSH("PNG"), PUSH("GEN"), REC(39), POP, POP, POP, 
PUSH("LOCAL"), PUSH("CAT"), PUSH("HEAD"), PUSH("CASE"), REC(20), POP, POP, POP, POP, 
PUSH("NON-LOCAL"), PUSH("QUE"), PUSH("LAST"), REC(76), POP, 
PUSH("LIST"), REC(75), POP, POP, 
PUSH("REL"), PUSH("LAST"), REC(50), POP, 
PUSH("LIST"), REC(45), POP, POP, 
PUSH("SLASH"), PUSH("LAST"), REC(63), POP, 
PUSH("LIST"), REC(26), POP, POP, POP, 
PUSH("OPT"), REC(42), POP, POP, POP, POP, POP, 
PUSH("CONJ"), REC(44), 
PUSH("CHEAD"), PUSH("LEFT"), PUSH("REST"), REC(72), POP, POP, POP, POP, 
PUSH("CONT"), PUSH("HOOK"), PUSH("E-INDEX"), PUSH("E"), PUSH("MOOD"), REC(68), POP, POP, POP, 
PUSH("INDEX"), REC(23), 
PUSH("E"), PUSH("MOOD"), REC(97), POP, 
PUSH("TENSE"), REC(89), POP, POP, 
PUSH("PNG"), PUSH("PN"), REC(64), POP, POP, 
PUSH("PRONTYPE"), REC(54), POP, 
PUSH("SORT"), REC(69), POP, POP, 
PUSH("XARG"), REC(61), POP, POP, 
PUSH("RELS"), PUSH("LIST"), PUSH("FIRST"), REC(88), POP, POP, POP, POP, 
PUSH("CTXT"), PUSH("ACTIVATED"), REC(92), POP, POP, 
PUSH("KEYS"), PUSH("ALT2KEY"), REC(85), POP, 
PUSH("ALTKEY"), REC(34), POP, 
PUSH("KEY"), REC(1), 
PUSH("ARG0"), PUSH("E"), PUSH("TENSE"), REC(102), POP, POP, POP, POP, 
PUSH("MESSAGE"), REC(47), 
PUSH("LAST"), REC(36), POP, POP, POP, 
PUSH("STEMHEAD"), REC(19), POP, POP, 
PUSH("MODIFIED"), REC(57), 
PUSH("LPERIPH"), REC(48), POP, 
PUSH("RPERIPH"), REC(91), POP, POP, 
PUSH("NON-LOCAL"), PUSH("QUE"), REC(8), 
PUSH("LAST"), REC(40), POP, 
PUSH("LIST"), REC(83), POP, POP, 
PUSH("REL"), REC(17), 
PUSH("LAST"), REC(51), POP, 
PUSH("LIST"), REC(60), POP, POP, 
PUSH("SLASH"), PUSH("LAST"), REC(5), POP, 
PUSH("LIST"), REC(21), 
PUSH("FIRST"), PUSH("CAT"), PUSH("HEAD"), REC(31), 
PUSH("CASE"), REC(82), POP, 
PUSH("MOD"), REC(30), 
PUSH("FIRST"), PUSH("LOCAL"), PUSH("CAT"), PUSH("HEAD"), REC(87), POP, 
PUSH("MC"), REC(95), POP, 
PUSH("VAL"), PUSH("SUBJ"), PUSH("FIRST"), REC(74), 
PUSH("LOCAL"), PUSH("CONT"), PUSH("RELS"), PUSH("LIST"), PUSH("REST"), PUSH("FIRST"), PUSH("CARG"), REC(90), POP, POP, POP, POP, POP, POP, POP, POP, POP, POP, POP, POP, POP, POP, 
PUSH("TAM"), PUSH("TENSE"), REC(84), POP, POP, POP, 
PUSH("POSTHEAD"), REC(96), POP, 
PUSH("VAL"), PUSH("COMPS"), REC(58), POP, POP, POP, 
PUSH("CONT"), PUSH("HOOK"), PUSH("INDEX"), PUSH("SORT"), REC(67), POP, POP, 
PUSH("XARG"), REC(99), POP, POP, POP, 
PUSH("KEYS"), PUSH("KEY"), REC(22), POP, POP, POP, POP, POP, POP, 
PUSH("ROOT"), REC(86),
*/

//};

//#define	nqcprog	(sizeof(qcprog)/(sizeof(struct qcinst)))

//unsigned int	qc_size = 46;
//unsigned int	qc_size = 71;

#define	QC_SIZE(x) unsigned int	qc_size = x;
#define PUSH(fname)
#define POP
#define REC(x)
#include	"qccode.c"
#undef PUSH
#undef POP
#undef REC
#undef QC_SIZE
#define	QC_SIZE(x)

struct qc
{
	struct type	*entries[1];
};

int	__qc_use_heap = 0;

static struct type	*olist, *minus, *_0dlist;
extern int gen_qc;

IFCLOCK(unsigned long long extract_clock;)
IFCLOCK(unsigned long long extract_count;)

struct qc	*extract_qc(struct dg	*d)
{
	static int	init = 0;
	struct qc	*q;
	long		i, iter, sp = 0, dst, fn, onf;
	extern int	nfnames;
	int			*lp;
	extern unsigned int	generation;
	struct dg	*stack[64];
	static int	labels[1024];

	if(gen_qc)return NULL;

	if(!init)
	{
		init = 1;
		lp = labels;
#define	REC(dst)
#define	POP
#define	PUSH(fstr)			onf = nfnames ; *lp++ = fn = lookup_fname(#fstr); if(fn>=onf)fprintf(stderr, "QC: feature %s does not exist\n", #fstr);
#include	"qccode.c"
#undef	PUSH
#undef	POP
#undef	REC
		/*for(i=0;i<nqcprog;i++)
			if(qcprog[i].com==_PUSH)
				qcprog[i].fnum = lookup_fname(qcprog[i].arg);*/

#ifdef	HIDEOUS_HACK
		olist = lookup_type("*olist*");
		assert(olist);
		minus = lookup_type("-");
		assert(minus);
		_0dlist = lookup_type("0-dlist");
		assert(_0dlist);
#endif
	}

	if(!__qc_use_heap)
		q = slab_alloc(sizeof(struct type*)*qc_size);
	else q = malloc(sizeof(struct type*)*qc_size);

IFCLOCK(	clock_t	T = clock();for(iter=0;iter<100;iter++)	)
	{
	sp = 0;
	lp = labels;
	stack[sp] = d;


#define	REC(dst)	if(stack[sp]) \
						q->entries[dst] = (stack[sp]->gen==generation)?stack[sp]->type:stack[sp]->xtype; \
					else q->entries[dst] = 0;

#define	POP			--sp;

#ifdef	CLOCKCOUNT
#define	EC		extract_count++
#else
#define	EC
#endif
#define	PUSH(fname)	if(stack[sp])stack[sp+1] = dg_hop(stack[sp], *lp);	\
					else stack[sp+1] = 0;	\
					sp++; lp++; EC;

#include	"qccode.c"
	}
IFCLOCK(	extract_clock += clock() - T; )

/*	for(i=0;i<nqcprog;i++)
	{
		switch(qcprog[i].com)
		{
			case	_PUSH:
				if(stack[sp])stack[sp+1] = dg_hop(stack[sp], qcprog[i].fnum);
				else stack[sp+1] = 0;
				sp++;
				assert(sp<32);
				//printf("on push %s have %p\n", qcprog[i].arg, stack[sp]);
				break;
			case	_POP:	--sp;	break;
			case	_RECORD:
				dst = (long)qcprog[i].arg;
				if(stack[sp])
					q->entries[dst] = (stack[sp]->gen==generation)?stack[sp]->type:stack[sp]->xtype;
				else q->entries[dst] = 0;
				break;
		}
	}	*/

//	printf("QUE %s LIST %s LAST %s\n", q->entries[29]?q->entries[29]->name:"0",
//		q->entries[46]?q->entries[46]->name:"0", q->entries[12]?q->entries[12]->name:"0");


	return q;
}

struct qc	*extract_qc_arg(struct dg	*d, int	arg)
{
	struct dg	*to;
	int		i = 0;

	if(arg>=0)
	{
		to = dg_hop(d, 0);	// 0 is guaranteed to be ARGS
		while(to && i<arg)
		{
			to = dg_hop(to, 2);	// REST
			i++;
		}
		if(to)to = dg_hop(to, 1);	// FIRST
		if(!to)
		{
			fprintf(stderr, "extract_qc_arg: couldn't get arg %d position in %s\n", arg, d->xtype->name);
			exit(-1);
		}
	}
	else to = d;

	return extract_qc(to);
}

IFCLOCK(unsigned long long glb_clock;)
IFCLOCK(unsigned long long glb_count;)

int	quickcheck(struct qc	*a, struct qc	*b)
{
	int	i, qcs = qc_size;
	struct type	**ae = a->entries, **be = b->entries;

	for(i=0;i<qcs;i++)
	{
		if(ae[i] && be[i] && (ae[i] != be[i]) &&
			!glb(ae[i], be[i]))
		{
			//printf("QC reject %d\n", i);
			break;
		}
	}

	if(i<qcs)return -1;

#ifdef	HIDEOUS_HACK
	struct dlist_hack { int dlist, list, last; }	hacks[1] = { {30,21,15} };
	struct olist_hack { int list, opt; }			ohacks[1] = { {28,17} };
	int					nhacks=1, nohacks=1;
//	if(a->entries[20] == olist && b->entries[9]==minus)return -1;
//	if(b->entries[20] == olist && a->entries[9]==minus)return -1;
	for(i=0;i<nhacks;i++)
	{
		struct dlist_hack	*h = hacks+i;
		if(b->entries[h->dlist]==_0dlist)
			if(a->entries[h->list] && a->entries[h->last] && !glb(a->entries[h->list], a->entries[h->last]))
				return -1;
		if(a->entries[h->dlist]==_0dlist)
			if(b->entries[h->list] && b->entries[h->last] && !glb(b->entries[h->list], b->entries[h->last]))
				return -1;
	}
	for(i=0;i<nohacks;i++)
	{
		struct olist_hack	*h = ohacks+i;
		if(a->entries[h->list] == olist && b->entries[h->opt]==minus)return -1;
		if(b->entries[h->list] == olist && a->entries[h->opt]==minus)return -1;
	}
//	if(a->entries[10] == olist && b->entries[11]==minus)return -1;
//	if(b->entries[10] == olist && a->entries[11]==minus)return -1;
#endif

	return 0;
}

int	equiv_quickcheck(struct qc	*a, struct qc	*b, int *Fw, int *Bk)
{
	int	i;
	struct type	*ta, *tb, *g;
	int	fw = 1, bw = 1;

	for(i=0;i<qc_size;i++)
	{
		ta = a->entries[i];
		tb = b->entries[i];

		if(ta && tb && (ta!=tb))
		{
			g = glb(ta, tb);
			if(g!=tb)fw = 0;
			if(g!=ta)bw = 0;
		}
		if(ta && !tb)fw = 0;
		if(tb && !ta)bw = 0;
		if(!fw && !bw) { *Fw = fw; *Bk = bw; return -1; }
	}
	*Fw = fw; *Bk = bw;
	return 0;
}

struct qc	*copy_qc(struct qc	*in)
{
	if(!in)return 0;
	struct qc	*out = slab_alloc(sizeof(struct type*)*qc_size);
	return memcpy(out, in, sizeof(struct type*)*qc_size);
}

// qc generator

static struct qc_path
{
	struct path			path;
	unsigned long long	failures;
	int					rank;
}			*paths;
static int	npaths;

extern int			upathgl;
extern struct type	*uglb;

void	gqc_count_failure(struct path	path)
{
	int	i;

	for(i=0;i<npaths;i++)
		if(!pathcmp(&path, &paths[i].path))
		{
			paths[i].failures++;
			return;
		}
	npaths++;
	paths = realloc(paths, sizeof(struct qc_path)*npaths);
	paths[npaths-1].path = pathdup(path);
	paths[npaths-1].failures = 1;
}

extern unsigned long long unify_attempts_total, unify_attempts_pass_rf, unify_attempts_pass_qc, unify_attempts_success, unify_attempts_bad_orth;

const double	k_glb = 0.0000022, k_unify = 0.0011, k_hop = 0.0000024;

void	gqc_treeify(int	count, int *hops)
{
	struct qc_path	pth[count];
	int				i;

	memcpy(pth, paths, sizeof(struct qc_path)*count);
	int	lexcmp(const struct qc_path	*a, const struct qc_path	*b)
	{
		int i;
		for(i=0;i<a->path.count && i<b->path.count;i++)
			if(a->path.fnums[i] != b->path.fnums[i])
				return a->path.fnums[i] - b->path.fnums[i];
		return a->path.count - b->path.count;
	}
	qsort(pth, count, sizeof(struct qc_path), (int(*)(const void*,const void*))lexcmp);

	if(hops)
	{
		int i, hc = pth[0].path.count;
		// just count hops
		for(i=1;i<count;i++)
		{
			// find common prefix
			int j;
			for(j=0;j<pth[i].path.count && j<pth[i-1].path.count;j++)
				if(pth[i].path.fnums[j] != pth[i-1].path.fnums[j])break;
			hc += pth[i].path.count - j;
		}
		*hops = hc;
	}
	else
	{
		// actually build program
		extern char	**fnames;
		int i, hc = pth[0].path.count;
		for(i=0;i<pth[0].path.count;i++)
			printf(" PUSH(%s)", fnames[pth[0].path.fnums[i]]);
		printf(" REC(%d)", pth[0].rank);
		for(i=1;i<count;i++)
		{
			// find common prefix
			int j, k;
			for(j=0;j<pth[i].path.count && j<pth[i-1].path.count;j++)
				if(pth[i].path.fnums[j] != pth[i-1].path.fnums[j])break;
			for(k=j;k<pth[i-1].path.count;k++)printf(" POP");
			printf("\n"); for(k=0;k<j;k++)printf("    ");
			for(k=j;k<pth[i].path.count;k++)printf(" PUSH(%s)", fnames[pth[i].path.fnums[k]]);
			printf(" REC(%d)", pth[i].rank);
		}
		printf("\n");
	}
}

void	gqc_result()
{
	int i, nhops[npaths], cnh = 0, cheapest = 0;
	unsigned long long total = 0, cum = 0;
	double	filtration[npaths], least_cost;
	int	qcpcmp(const struct qc_path	*a, const struct qc_path	*b) { return b->failures - a->failures; }
	qsort(paths, npaths, sizeof(struct qc_path), (int(*)(const void*,const void*))qcpcmp);

	for(i=0;i<npaths;i++)
	{
		total += paths[i].failures;
		paths[i].rank = i;
	}

	for(i=0;i<npaths;i++)
	{
		cnh += paths[i].path.count;
		cum += paths[i].failures;

		filtration[i] = (double)cum / unify_attempts_pass_rf;//total;
		gqc_treeify(i+1, &nhops[i]);
	}

	for(i=0;i<=npaths;i++)
	{
		int j;
		double	cost, c_check = 0, c_unify, c_extract;
		for(j=0;j<i;j++)
			c_check += k_glb * unify_attempts_pass_rf * ((j>0)?(1-filtration[j-1]):1.0);
		c_unify = k_unify * unify_attempts_pass_rf * ((i>0)?(1-filtration[i-1]):1.0);
		c_extract = k_hop * ((i>0)?nhops[i-1]:0) * (unify_attempts_success - unify_attempts_bad_orth);
		cost = c_check + c_unify + c_extract;
		/*printf("%3d paths:	%10f = %10f + %10f + %10f\n", i, cost, c_check, c_unify, c_extract);
		if(i<npaths)
		{
			printf("%16lld (%.1f%%, %d hops): ", paths[i].failures, 100*filtration[i], nhops[i]);
			print_path(paths[i].path);
			printf("\n");
		}*/
		if(i==0)least_cost = cost;
		else if(cost<least_cost) { least_cost = cost; cheapest = i; }
	}

	printf("QC_SIZE(%d)\n", cheapest);
	printf("/* GQC: using %d paths, %.1f%% of failures, %d extraction hops, expected cost %f */\n",
		cheapest, 100 * (filtration[cheapest-1] / filtration[npaths-1]), nhops[cheapest-1], least_cost);
	gqc_treeify(cheapest, 0);
}
