#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<assert.h>

#include	"dag.h"
#include	"lexicon.h"
#include	"hash.h"
#include	"mrs.h"
#include	"freeze.h"

#define	VACUOUS	"vacuous"

struct semindex_pred_node
{
	// structure to keep track of all the info about a particular pred name
	char	*pred;

	// during chart population, keep track of which input EPs are compatible with this pred
	// this has to be a separate "cons" structure from the mrs_ep itself, since
	//   different semindex_pred_node's can have overlapping sets of compatible input EPs
	struct ep_list
	{
		struct mrs_ep	*ep;
		struct ep_list	*next;
	}	*activation;

	// complete list of semindex_pred_node's which are heirarchically related to this one,
	//   i.e. should be activated when this one is
	int							nrelatives;
	struct semindex_pred_node	**relatives;

	// list of lexemes' and rules' EPs that provide the predication in question
	// if an entity provides the prediction multiple times then it will show up
	//   in this list multiple times, with different 'pidx' values
	int	nentities, aentities;
	struct grammar_entity
	{
#define	LEXEME	1
#define RULE	2
		char what, nextra_preds, pidx, unused;
		union
		{
			struct lexeme	*lex;
			struct rule		*rule;
		};
		// if this grammar entity has multiple EPs,
		//   keep track of what the extra ones are
		struct semindex_pred_node	**extra_preds;
	}	*entities;
};

struct semindex_pred_node	*lookup_semindex(char *predn);
void						add_relative_to_spn(struct semindex_pred_node *rel, struct semindex_pred_node *node);

static struct hash	*sindex;

int							nstrpars;
struct semindex_pred_node	**strpars;

extern struct type *strtype;
static int predfnum;

void init_index()
{
	struct type	*predsort = lookup_type("predsort");
	int			i, j;

#define	GLB_TYPE(t)	(t->name[0]=='G' && t->name[1]=='(')	/* XXX crude hack to detect GLB types */

	predfnum = lookup_fname("PRED");
	sindex = hash_new("semantic index");
	for(i=0;i<predsort->ndescendents;i++)
	{
		struct type	*t = types[predsort->descendents[i]];
		if(GLB_TYPE(t))continue;
		struct semindex_pred_node *node = lookup_semindex(t->name);

		for(j=1;j<t->ndescendents;j++) if(!GLB_TYPE(types[t->descendents[j]]))
			add_relative_to_spn(node, lookup_semindex(types[t->descendents[j]]->name));

		if(descendent_of(strtype->tnum, t))
		{
			nstrpars++;
			strpars = realloc(strpars, sizeof(struct type*) * nstrpars);
			strpars[nstrpars-1] = lookup_semindex(t->name);
		}
	}
}

struct semindex_pred_node	*lookup_semindex(char *predn)
{
	struct semindex_pred_node	*node = hash_find(sindex, predn);
	if(!node)
	{
		node = calloc(sizeof(struct semindex_pred_node), 1);
		node->pred = predn;
		if(predn[0]=='"')
		{
			int i;
			// string predication; add it to the relation heirarchy
			for(i=0;i<nstrpars;i++)
				add_relative_to_spn(node, strpars[i]);
		}
		hash_add(sindex, predn, node);
	}
	return node;
}

void add_relative_to_spn(struct semindex_pred_node *rel, struct semindex_pred_node *node)
{
	node->relatives = realloc(node->relatives, sizeof(struct semindex_pred_node*)*++node->nrelatives);
	node->relatives[node->nrelatives-1] = rel;
	rel->relatives = realloc(rel->relatives, sizeof(struct semindex_pred_node*)*++rel->nrelatives);
	rel->relatives[rel->nrelatives-1] = node;
}

void	add_entity_to_spn(struct grammar_entity *e, struct semindex_pred_node *node)
{
}

void	entity_add_semantic_index(short what, struct dg *indpred, void *entity, struct dg *rels, int pidx)
{
	int							nextras = 0, i;
	struct semindex_pred_node	*node, **extras = 0;
	struct grammar_entity		*e;

	for(;rels;rels = dg_hop(rels, 2 /* REST */ ))
	{
		struct dg	*pred, *rel = dg_hop(rels, 1 /* FIRST */);
		if(!rel)break;
		pred = dg_hop(rel, predfnum);
		if(!pred || pred==indpred)continue;
		extras = realloc(extras, sizeof(char*)*(nextras+1));
		extras[nextras++] = lookup_semindex(pred->xtype->name);
	}

	node = lookup_semindex(indpred?indpred->xtype->name:VACUOUS);
	// 'e' only gets added to 'node', not to 'node's relatives.
	if(node->nentities+1 >= node->aentities)
	{
		if(node->aentities==0)node->aentities = 1;
		else node->aentities *= 2;
		node->entities = realloc(node->entities, sizeof(struct grammar_entity)*node->aentities);
	}
	e = node->entities + node->nentities++;

	e->nextra_preds = nextras;
	e->extra_preds = extras;
	e->what = what;
	e->lex = entity;
	e->pidx = pidx;
}

static int	savef[10] = {-1,-1,-1,-1,-1,-1,-1,-1};
static int	rsavef[10] = {-1,-1,-1,-1,-1,-1,-1,-1};

void	add_semantic_index(struct lexeme *l, struct dg *full, int which)
{
	// walk through the lexeme's full dag's MRS, hash-add this lexeme for each relation name mentioned
	int			pidx = 0;
	struct dg	*rels, *Rels;

	if(!which)rels = walk_dg(full, savef, "SYNSEM", "LOCAL", "CONT", "RELS", "LIST", NULL);
	else rels = walk_dg(full, rsavef, "C-CONT", "RELS", "LIST", NULL);

	Rels = rels;
	if(!sindex)init_index();

	if(rels && dg_hop(rels, 1))
	{
		for(;rels;rels = dg_hop(rels, 2 /* REST */ ))
		{
			struct dg	*pred, *rel = dg_hop(rels, 1 /* FIRST */);
			char		*predn;
			int			i;
			if(!rel)break;
			pred = dg_hop(rel, predfnum);
			if(!pred)continue;
			predn = pred->xtype->name;
			entity_add_semantic_index(which?RULE:LEXEME, pred, l, Rels, pidx++);
			break;
		}
	}
	else entity_add_semantic_index(which?RULE:LEXEME, 0, l, 0, 0);
}

void	freeze_ge(struct grammar_entity *n, struct grammar_entity	*ge)
{
	int						i;
	*n = *ge;
	n->extra_preds = slab_alloc(sizeof(struct semindex_pred_node*) * n->nextra_preds);
	for(i=0;i<n->nextra_preds;i++)
		n->extra_preds[i] = (struct semindex_pred_node*)ge->extra_preds[i]->activation;
	if(n->what == LEXEME)
		n->lex = (struct lexeme*)ge->lex->dg;	// funny freezer forwarding addresses
	if(n->what == RULE)
		n->rule = (struct rule*)ge->rule->dg;	// funny freezer forwarding addresses
}

struct semindex_pred_node	*freeze_spn(struct semindex_pred_node	*old)
{
	struct semindex_pred_node	*node = (struct semindex_pred_node*)old->activation;
	int							i;

	node->pred = freeze_string(old->pred);
	node->activation = 0;

	node->nrelatives = old->nrelatives;
	node->relatives = slab_alloc(sizeof(struct semindex_pred_node*) * node->nrelatives);
	for(i=0;i<node->nrelatives;i++)
		node->relatives[i] = (struct semindex_pred_node*)old->relatives[i]->activation;

	node->aentities = node->nentities = old->nentities;
	node->entities = slab_alloc(sizeof(struct grammar_entity) * node->aentities);
	for(i=0;i<node->nentities;i++)
		 freeze_ge(node->entities+i, old->entities+i);

	return node;
}

void	*freeze_semantic_index()
{
	int	i;
	struct hash_bucket *walk;
	// NOTE: this gets called shortly after the lexicon is frozen.
	// The old lexicon memory still exists and our hash table points to it.
	// We arranged to leave forwarding addresses in lex->dg slots.

	// tricky process.  first make slab copies of all the SPNs, with forwarding addresses in the ->activation slots.
	for(i=0;i<sindex->size;i++)
		for(walk=sindex->buckets[i];walk;walk=walk->next)
		{
			struct semindex_pred_node *n = walk->value;
			n->activation = slab_alloc(sizeof(struct semindex_pred_node));
		}
	// next migrate all the data to the slab copies
	for(i=0;i<sindex->size;i++)
		for(walk=sindex->buckets[i];walk;walk=walk->next)
			walk->value = freeze_spn(walk->value);
	// finally, freeze the hash structure
	return freeze_hash(sindex);
}

void	recover_semantic_index(void *frozen)
{
	extern int debug_level;
	sindex = frozen;
	if(debug_level)
		printf("NOTE: semantic index hash contains %d entries in %d slots\n",
					sindex->entries, sindex->size);
}


/* semantic lookup at generation time: efficient two-phase process
	- activation: for each predication in the input mrs, 'activate' the corresponding index nodes and their relatives
	- enumeration: traverse the activated nodes, filtering those which require inactive preds, and finding all alignments
		- constraints: we call back to generate.c to implement the constraints specified in the alignmnts

	- as an after-thought we add semantically vacuous rules and those lexemes which are licensed
 */

// activation

struct spn_list
{
	struct semindex_pred_node	*node;
	struct spn_list				*next;
};

void print_spn_activation(struct semindex_pred_node	*node)
{
	struct ep_list *epl;
	printf(" active SPN: `%s' [%d entities]", node->pred, node->nentities);
	for(epl=node->activation;epl;epl=epl->next)
		printf(" /%p %s", epl->ep, epl->ep->pred);
	printf("\n");
}

struct spn_list	*activate_semindex_node(struct semindex_pred_node *node, struct mrs_ep *ep, struct spn_list	*rest)
{
	struct ep_list				*epl;
	struct spn_list				*spnl;

	// if this node hasn't been activated yet, record it in our list of activated nodes
	if(node->activation)spnl = rest;
	else
	{
		spnl = slab_alloc(sizeof(struct spn_list));
		spnl->node = node;
		spnl->next = rest;
	}

	// add the relevant ep to the activation record
	for(epl=node->activation;epl;epl=epl->next)
		if(epl->ep == ep)break;
	if(!epl)
	{
		epl = slab_alloc(sizeof(struct ep_list));
		epl->ep = ep;
		epl->next = node->activation;
		node->activation = epl;
	}

	return spnl;
}

struct spn_list	*activate_semindex(struct mrs_ep *ep, struct spn_list	*rest)
{
	struct semindex_pred_node	*node = lookup_semindex(ep->pred);
	int							i;

	ep->active = 0;
	for(i=0;i<node->nrelatives;i++)
		rest = activate_semindex_node(node->relatives[i], ep, rest);
	return activate_semindex_node(node, ep, rest);
}

// enumeration

int	enumerate_eps(struct grammar_entity	*e, struct semindex_pred_node *node, int ai, int ei, struct mrs_ep	*align[]);
//int constrain_gen_edge(struct edge *e, struct dg	*cont, int	neps, struct mrs_ep	*eps[]);
struct dg	*dagify_rels_from_eps(int neps, struct mrs_ep	*eps[]);

int enumerate_alignments(struct grammar_entity	*e, int ei, struct mrs_ep	*align[])
{
	if(ei == e->nextra_preds)
	{
		// we have produced a complete alignment of 'e' against the input semantics.
		struct edge	*edge = 0;
		struct dg	*rels = dagify_rels_from_eps(1+e->nextra_preds, align);
		int			result, i;
		struct slab_state slab;

		get_slabstate(&slab);
		if(e->what == LEXEME)
		{
			//printf("aligned `%s' to eps ", e->lex->word);
			//for(i=0;i<=e->nextra_preds;i++)printf(" %d", align[i]->epnum); printf("\n");
			edge = lexical_edge(e->lex, 0, 0, rels);
			//cont = walk_dg(edge->dg, savef, "SYNSEM", "LOCAL", "CONT", NULL);
		}
		if(e->what == RULE)
		{
			//printf("aligned `%s' to eps ", e->rule->name);
			//for(i=0;i<=e->nextra_preds;i++)printf(" %d", align[i]->epnum); printf("\n");
			edge = epsilon_edge(e->rule, 0, e->rule->rtl, rels);
			//cont = walk_dg(edge->dg, rsavef, "C-CONT", NULL);
		}
		if(!edge)set_slabstate(&slab);
		else
		{
			int i;
			for(i=0;i<=e->nextra_preds;i++)
				edge->ep_span |= 1<<align[i]->epnum;
			add_agenda(edge);
		}
		return edge?1:0;
	}
	else
	{
		// we've finished aligning up to but not including extra_preds[ei].
		return enumerate_eps(e, e->extra_preds[ei], (ei<e->pidx)?(ei):(ei+1), ei+1, align);
	}
}

int enumerate_eps(struct grammar_entity	*e, struct semindex_pred_node *node, int ai, int ei, struct mrs_ep	*align[])
{
	struct ep_list *epl;
	int count = 0;

	for(epl=node->activation;epl;epl=epl->next)
	{
		if(epl->ep->active)continue;
		align[ai] = epl->ep;
		epl->ep->active = 1;
		count += enumerate_alignments(e, ei, align);
		epl->ep->active = 0;
	}
	return count;
}

int enumerate_semindex(struct semindex_pred_node *node)
{
	int i, j, count = 0;
	struct ep_list	*epl;
	struct mrs_ep	*align[32];

	//printf("active: "); print_spn_activation(node);

	// inspect each grammar entity representing this predication;
	//   for those of which all required predications are activated,
	//   investigate all possible alignments against the input mrs.
	for(i=0;i<node->nentities;i++)
	{
		struct grammar_entity	*e = node->entities+i;
		for(j=0;j<e->nextra_preds;j++)
			if(e->extra_preds[j]->activation == 0)
				break;
		if(j < e->nextra_preds)continue;
		assert(e->nextra_preds+1 <= 32);
		//printf("consider grammar entity `%s'\n", (e->what==RULE)?e->rule->name:e->lex->word);
		count += enumerate_eps(e, node, e->pidx, 0, align);
	}
	return count;
}

int evacuate(int nvaclex, char **vaclex)
{
	struct semindex_pred_node	*vac = lookup_semindex(VACUOUS);
	struct grammar_entity		*e;
	int							i, count = 0;

	for(i=0;i<vac->nentities;i++)
	{
		e = vac->entities+i;
		if(e->what == RULE)
		{
			add_agenda(epsilon_edge(e->rule, 0, e->rule->rtl, 0));
			count++;
		}
		if(e->what == LEXEME)
		{
			int j;
			for(j=0;j<nvaclex;j++)
				if(!strcmp(vaclex[j], e->lex->word))break;
			if(j==nvaclex)continue;
			add_agenda(lexical_edge(e->lex, 0, 0, 0));
			count++;
		}
	}
	return count;
}

// driver

void semantic_lookup(struct mrs	*mrs)
{
	extern int debug_level;
	struct spn_list	*active = 0, *walk;
	int				i, count = 0;

	// activate index nodes
	for(i=0;i<mrs->nrels;i++)
		active = activate_semindex(mrs->rels+i, active);

	// enumerate grammar entity alignments
	for(walk=active;walk;walk=walk->next)
		count += enumerate_semindex(walk->node);

	// clear activation records
	while(active) { active->node->activation = 0; active = active->next; }

	// finally, add vacuous entities
	count += evacuate(mrs->ntriggers, mrs->triggers);

	if(debug_level)printf("added %d grammar entities\n", count);
}
