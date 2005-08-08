#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

#include	"dag.h"
#include	"lexicon.h"
#include	"hash.h"

static struct hash	*sindex;
static struct hash	*ruindex;

int			npsorts;
struct type	**psorts;

void init_indexes()
{
	struct type	*predsort = lookup_type("predsort");
	int			i;

	sindex = hash_new("semantic lexicon index");
	ruindex = hash_new("semantic rule index");
	for(i=0;i<predsort->ndescendents;i++)
	{
		struct type	*t = types[predsort->descendents[i]];
		if(t->ndescendents == 1)continue;
		// XXX crude hack to detect GLB types
		if(t->name[0]=='G' && t->name[1]=='(')
			continue;
		//printf("`%s' is a predsort\n", t->name);
		npsorts++;
		psorts = realloc(psorts, sizeof(struct type*) * npsorts);
		psorts[npsorts-1] = t;
	}
	//printf("%d predsorts\n", npsorts);
}

void	add_semantic_index(struct lexeme *l, struct dg *full, int which)
{
	// walk through the lexeme's full dag's MRS, hash-add this lexeme for each relation name mentioned
	static int	savef[10] = {-1,-1,-1,-1,-1,-1,-1,-1};
	struct dg	*rels = walk_dg(full, savef, "SYNSEM", "LOCAL", "CONT", "RELS", "LIST", 0);
	struct hash	*ind;

	if(!sindex)init_indexes();

	if(which)ind = ruindex;
	else ind = sindex;
	if(rels && dg_hop(rels, 1))
	{
		for(;rels;rels = dg_hop(rels, 2 /* REST */ ))
		{
			struct dg	*rel = dg_hop(rels, 1 /* FIRST */);
			struct dg	*pred;
			char		*predn;
			int			i;
			static int	predf = 0;
			if(!rel)break;
			//printf(" rel "); print_dg(rel); printf("\n");
			if(!predf)predf = lookup_fname("PRED");
			pred = dg_hop(rel, predf);
			if(!pred)continue;
			predn = pred->xtype->name;
			//printf("pred %s\n", predn);
			hash_add(ind, predn, l);
			for(i=0;i<npsorts;i++)
				if(descendent_of(pred->xtype->tnum, psorts[i]))
					hash_add(ind, psorts[i]->name, l);
			for(i=1;i<pred->xtype->ndescendents;i++)
				hash_add(ind, types[pred->xtype->descendents[i]]->name, l);
		}
	}
	else hash_add(ind, "vacuous", l);
}

int	add_lexemes_for_relation(int	x, char	*rel, int (*skolemize)(struct edge	*e, struct lexeme	*lex))
{
	int	i, count = 0;

	void visitter(struct lexeme *l)
	{
		struct edge	*e;
		struct slab_state tmpstate;
		
		get_slabstate(&tmpstate);
		e = lexical_edge(l, x, 1);
		if(!skolemize(e, l))
		{
			add_agenda(e);
			count++;
		}
		else set_slabstate(&tmpstate);
	}

	hash_visit_key(sindex, rel, (void (*)(void*))visitter);
	return count;
}

int	add_rules_for_relation(char	*rel, int (*skolemize)(struct edge	*e, struct rule	*rule))
{
	int	i, count = 0;

	void visitter(struct rule *r)
	{
		struct edge	*e;
		struct slab_state tmpstate;
		
		get_slabstate(&tmpstate);
		e = epsilon_edge(r, 0, r->rtl);
		if(!skolemize(e, r))
		{
			add_agenda(e);
			count++;
		}
		else set_slabstate(&tmpstate);
	}

	hash_visit_key(ruindex, rel, (void (*)(void*))visitter);
	return count;
}

struct frozen_semindex
{
	struct hash	*sindex;
	struct hash	*ruindex;
};

void	*freeze_semantic_index()
{
	int	i;
	struct hash_bucket *walk;
	struct frozen_semindex *frozen = slab_alloc(sizeof(*frozen));
	// NOTE: this gets called shortly after the lexicon is frozen.
	// The old lexicon memory still exists and our hash table points to it.
	// We arranged to leave forwarding addresses in lex->dg slots.
	for(i=0;i<sindex->size;i++)
		for(walk=sindex->buckets[i];walk;walk=walk->next)
			walk->value = ((struct lexeme*)walk->value)->dg;
	frozen->sindex = freeze_hash(sindex);
	for(i=0;i<ruindex->size;i++)
		for(walk=ruindex->buckets[i];walk;walk=walk->next)
			walk->value = ((struct rule*)walk->value)->dg;
	frozen->ruindex = freeze_hash(ruindex);
	return frozen;
}

void	recover_semantic_index(void *_frozen)
{
	struct frozen_semindex *frozen = _frozen;
	sindex = frozen->sindex;
	ruindex = frozen->ruindex;
	printf("NOTE: semantic lexicon index hash contains %d entries in %d slots\n", sindex->entries, sindex->size);
	printf("NOTE: semantic rule index hash contains %d entries in %d slots\n", ruindex->entries, ruindex->size);
}
