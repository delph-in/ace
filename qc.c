#include	<stdlib.h>

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

/*
REC(6), 
PUSH("SYNSEM"), REC(4), 
	PUSH("LOCAL"), PUSH("CAT"),
		PUSH("HEAD"), REC(0),
			PUSH("INV"), REC(15), POP, 
			PUSH("KEYS"), PUSH("KEY"), REC(7), POP, POP, 
			PUSH("MOD"), REC(5), 
				PUSH("FIRST"), REC(2), 
					PUSH("LOCAL"), REC(13), POP, POP, POP, 
			PUSH("PRD"), REC(14), POP, 
			PUSH("VFORM"), REC(10), POP, POP, 
		PUSH("POSTHEAD"), REC(3), POP, 
		PUSH("VAL"),
			PUSH("COMPS"), REC(1), POP, 
			PUSH("SPR"), PUSH("FIRST"), REC(12), 
				PUSH("OPT"), REC(8), POP, POP, POP, 
			PUSH("SUBJ"), REC(9), 
				PUSH("FIRST"), REC(16), 
					PUSH("OPT"), REC(11), 
*/


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


};

//#define	QC_SIZE	17
#define	QC_SIZE	46

#define	nqcprog	(sizeof(qcprog)/(sizeof(struct qcinst)))

struct qc
{
	struct type	*entries[QC_SIZE];
};

int	__qc_use_heap = 0;

struct qc	*extract_qc(struct dg	*d)
{
	static int	init = 0;
	struct qc	*q;
	int		i, sp = 0;
	extern int	generation;
	struct dg	*stack[32];

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
				//printf("on push %s have %p\n", qcprog[i].arg, stack[sp]);
				break;
			case	_POP:	--sp;	break;
			case	_RECORD:
				if(stack[sp])
					q->entries[(int)qcprog[i].arg] = (stack[sp]->gen==generation)?stack[sp]->type:stack[sp]->xtype;
				else q->entries[(int)qcprog[i].arg] = 0;
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

	for(i=0;i<QC_SIZE;i++)
		if(a->entries[i]!=b->entries[i])return -1;
	return 0;
}
