#include	<stdlib.h>
#include	<assert.h>

#include	"dag.h"

/* essentially, QC extracts a skeleton type tree from a dag
 * and performs *tree*-unification on that skeleton before
 * the full dag-unifier is allowed to run */

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

};

#define	QC_SIZE	46
//#define	QC_SIZE	103

#define	nqcprog	(sizeof(qcprog)/(sizeof(struct qcinst)))

struct qc
{
	struct type	*entries[QC_SIZE];
};

int	__qc_use_heap = 0;

const int	qcbytes = sizeof(struct type*)*QC_SIZE;

struct qc	*extract_qc(struct dg	*d)
{
	static int	init = 0;
	struct qc	*q;
	int		i, sp = 0;
	extern unsigned int	generation;
	struct dg	*stack[64];

	if(!init)
	{
		init = 1;
		for(i=0;i<nqcprog;i++)
			if(qcprog[i].com==_PUSH)
				qcprog[i].fnum = lookup_fname(qcprog[i].arg);
	}

	if(!__qc_use_heap)
		q = slab_alloc(sizeof(struct qc));
	else q = malloc(sizeof(struct qc));

	stack[sp] = d;
	for(i=0;i<nqcprog;i++)
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
				if(stack[sp])
					q->entries[(long)qcprog[i].arg] = (stack[sp]->gen==generation)?stack[sp]->type:stack[sp]->xtype;
				else q->entries[(long)qcprog[i].arg] = 0;
				break;
		}
	}

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

int	quickcheck(struct qc	*a, struct qc	*b)
{
	int	i;

	for(i=0;i<QC_SIZE;i++)
	{
		if(a->entries[i] && b->entries[i] &&
			!glb(a->entries[i], b->entries[i]))
		{
			//printf("QC reject %d\n", i);
			return -1;
		}
	}
	return 0;
}

int	equiv_quickcheck(struct qc	*a, struct qc	*b)
{
	int	i;
	struct type	*ta, *tb;

	for(i=0;i<QC_SIZE;i++)
	{
		ta = a->entries[i];
		tb = b->entries[i];
		//if(ta!=tb)return -1;
		if((ta&&!tb)||(tb&&!ta))return -1;
		if(ta && tb && !descendent_of(tb->tnum, ta))return -1;
	}
	return 0;
}

struct qc	*copy_qc(struct qc	*in)
{
	struct qc	*out = slab_alloc(sizeof(struct qc));
	*out = *in;
	return out;
}
