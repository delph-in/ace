#include	<stdlib.h>

#include	"dag.h"

/* essentially, QC extracts a skeleton type tree from a dag
 * and performs *tree*-unification on that skeleton before
 * the full dag-unifier is allowed to run */

/*
  43563 unify failed at path: SYNSEM LOCAL CAT HEAD VFORM 
  45221 unify failed at path: SYNSEM LOCAL CAT HEAD KEYS KEY 
  59664 unify failed at path: SYNSEM 
  75342 unify failed at path: SYNSEM LOCAL CAT HEAD MOD 
 103161 unify failed at path: SYNSEM LOCAL CAT POSTHEAD 
 113365 unify failed at path: SYNSEM LOCAL CAT HEAD MOD FIRST 
 225816 unify failed at path: SYNSEM LOCAL CAT VAL COMPS 
 624508 unify failed at path: SYNSEM LOCAL CAT HEAD 
 */

/*
  10549 unify failed at path: SYNSEM NON-LOCAL SLASH LAST 
  11250 unify failed at path: SYNSEM LOCAL CAT HEAD INV 
  11268 unify failed at path: SYNSEM LOCAL CONT MSG 
  12404 unify failed at path: SYNSEM LOCAL CAT VAL SPR FIRST 
  13457 unify failed at path: SYNSEM LOCAL CAT HEAD MOD FIRST LOCAL 
  13698 unify failed at path: SYNSEM LOCAL CAT MC 
  13886 unify failed at path: SYNSEM LOCAL CAT VAL SUBJ FIRST OPT 
*/

#define	QC_SIZE	16

typedef enum
{
	_RECORD,
	_PUSH,
	_POP
} qccom;

#define	PUSH(str)	{_PUSH, str}
#define	POP		{_POP, 0}
#define	REC		{_RECORD, 0}

struct qcinst
{
	qccom	com;
	char	*arg;
	int	fnum;
}	qcprog[] = { REC,
PUSH("SYNSEM"), REC,
	PUSH("LOCAL"),
		PUSH("CAT"),
			PUSH("HEAD"), REC,
				PUSH("MOD"), REC,
					PUSH("FIRST"),REC,
						PUSH("LOCAL"), REC, POP,
						POP,
					POP,
				PUSH("KEYS"), PUSH("KEY"), REC, POP, POP,
				PUSH("VFORM"), REC, POP,
				PUSH("INV"), REC, POP,
				POP,
			PUSH("VAL"),
				PUSH("COMPS"), REC, POP,
				PUSH("SUBJ"), PUSH("FIRST"), PUSH("OPT"), REC, POP, POP, POP,
				PUSH("SPR"), PUSH("FIRST"), REC, POP, POP,
				POP,
			PUSH("POSTHEAD"), REC, POP,
			PUSH("MC"), REC, POP,
			POP,
		POP,
		PUSH("CONT"), PUSH("MSG"), REC, POP, POP,
	POP,
	PUSH("NON-LOCAL"), PUSH("SLASH"), PUSH("LAST"), REC, POP, POP, POP,
	};
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
	int		i, sp = 0, ne = 0;
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
				{
					q->entries[ne++] = (stack[sp]->gen==generation)?stack[sp]->type:stack[sp]->xtype;
					//printf("rec[%d] [sp%d] = %s\n", ne-1, sp, q->entries[ne-1]->name);
				}
				else q->entries[ne++] = 0;
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
