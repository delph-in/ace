#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<limits.h>
#include	<wchar.h>
#include	<ctype.h>

#include	"conf.h"
#include	"rule.h"
#include	"chart.h"
#include	"morpho.h"
#include	"tdl.h"
#include	"lexicon.h"
#include	"freeze.h"
#ifdef POST
#include "post.h"
#endif
#include	"token.h"
#include	"type.h"

extern int trace;

char	*get_token_carg(struct token	*t)
{
	struct dg	*c = dg_hop(t->dg, lookup_fname("+CARG"));
	if(c && c->xtype->name[0]=='"')
	{
		char	buffer[1024];
		sprintf(buffer, "%.*s", (int)(strlen(c->xtype->name+1)-1), c->xtype->name+1);
		return freeze_string(buffer);
	}
	return NULL;
}

struct dg	*build_pos_tags_list(char	*tag)
{
	char	tag_str[32];
	sprintf(tag_str, "\"%s\"", tag);
	struct dg	*list = atomic_dg("*cons*");
	struct type	*tagtype = add_string(freeze_string(tag_str));
	struct dg	*tagdg = tagtype->dg?:atomic_dg("*top*");
	tagdg->xtype=tagdg->type=tagtype;
	list = add_dg_hop(list, 1, tagdg);	// FIRST
	list = add_dg_hop(list, 2, type_dg("*null*"));	// REST
	return list;
}

void	build_token_dag(struct token	*t)
{
	struct dg	*dg = type_dg(token_type);
	char	c_from_str[16], c_to_str[16];
	char	bigger_string[1024];

	sprintf(bigger_string, "\"%s\"", t->string);
	sprintf(c_from_str, "\"%d\"", t->cfrom);
	sprintf(c_to_str, "\"%d\"", t->cto);
	//print_dg(dg);
	dg = dg_path_add(dg, token_form_path, temporary_string_type(freeze_string(bigger_string))->dg);
	dg = dg_path_add(dg, token_from_path, temporary_string_type(freeze_string(c_from_str))->dg);
	dg = dg_path_add(dg, token_to_path, temporary_string_type(freeze_string(c_to_str))->dg);
	if(t->postag)dg = dg_path_add(dg, token_postags_path, build_pos_tags_list(t->postag));
	dg = wellform_dg(dg);
	t->dg = dg;
	//printf("token '%s' pos '%s'\n", t->string, t->postag);
}

struct lattice	*build_token_chart(int	nwords, char	**words, int	*cfrom, int	*cto)
{
	struct lattice	*tc = slab_alloc(sizeof(*tc));
	int	i;

	char	**tags;
	int		*unk;

	tags = malloc(sizeof(char*) * nwords);
	unk = malloc(sizeof(int) * nwords);
#ifdef	POST
	post_tag_sequence(nwords, words, tags, unk);
#else
	for(i=0;i<nwords;i++)
	{
		tags[i] = "NNP";
		unk[i] = 1;
	}
#endif
	if(trace)
	{
		for(i=0;i<nwords;i++)fprintf(stderr, "%s/%s%s ", words[i], tags[i], unk[i]?"?":"");
		fprintf(stderr, "\n");
	}

	bzero(tc, sizeof(*tc));
	for(i=0;i<=nwords;i++)new_lattice_vertex(tc);
	tc->nedges = nwords;
	tc->edges = slab_alloc(sizeof(struct lattice_edge*)*tc->nedges);
	for(i=0;i<tc->nedges;i++)
	{
		struct lattice_vertex	*vfrom = tc->vertices[i];
		struct lattice_vertex	*vto = tc->vertices[i+1];
		struct lattice_edge	*e = slab_alloc(sizeof(*e));
		e->from = vfrom;
		e->to = vto;
		e->token = slab_alloc(sizeof(struct token));
		tc->edges[i] = e;
		add_lattice_vertex_follower(vfrom, e);
		e->token->postag = tags[i];
		e->token->string = words[i];
		e->token->cfrom = cfrom[i];
		e->token->cto = cto[i];
		build_token_dag(e->token);
		e->token->oc = NULL;
	}
	tc->lattice_vertex_id_gen = 10000;
	return tc;
}

/*
int	lattice_vertex_follows(struct lattice	*lat, struct lattice_vertex	*a, struct lattice_vertex	*b)
{
	int i;
	for(i=0;i<b->nfollowers;i++)
	{
		struct lattice_edge	*f = b->followers[i];
		if(a == f->to)return 1;
		if(lattice_vertex_follows(lat, a, f->to))return 1;
	}
	return 0;
}
*/

static void _mark_followers(struct lattice_vertex	*v)
{
	int i;
	if(v->mark)return;
	v->mark = 1;
	for(i=0;i<v->nfollowers;i++)
		_mark_followers(v->followers[i]->to);
}

int	lattice_vertex_follows(struct lattice	*lat, struct lattice_vertex	*a, struct lattice_vertex	*b)
{
	int i;
	if(a==b)return 0;
	for(i=0;i<lat->nvertices;i++)lat->vertices[i]->mark = 0;
	_mark_followers(b);
	return a->mark;
}

void	sort_lattice(struct lattice	*tc)
{
	/* assign consecutive numbers to each vertex,
	 * consistent with the order implied by the edges in the chart.
	 * these become the numbered vertices of the parse chart...
	 */

	/* we do this by inventing a total ordering:
	 *  - if the edges imply an order, use that;
	 *  - otherwise, use the original numeric order of the vertex ids
	 */
	int	vertex_compare(struct lattice_vertex	**va, struct lattice_vertex	**vb)
	{
		// return 1 if va should come after vb; -1 if va should come before vb
		if(*va == *vb)return 0;
		if(lattice_vertex_follows(tc, *va, *vb))return 1;
		if(lattice_vertex_follows(tc, *vb, *va))return -1;
		if((*va)->id > (*vb)->id)return 1;
		if((*va)->id < (*vb)->id)return -1;
		return 0;
	}
	qsort(tc->vertices, tc->nvertices, sizeof(struct lattice_vertex*), (int(*)(const void*,const void*))vertex_compare);

	/* tc->vertices is now ordered according to the new order;
	 * go through and set new ->id values. */
	int i;
	tc->lattice_vertex_id_gen = 0;
	for(i=0;i<tc->nvertices;i++)
		tc->vertices[i]->id = tc->lattice_vertex_id_gen++;
}

void	print_token_chart(struct lattice	*tc)
{
	int	i;
	for(i=0;i<tc->nedges;i++)
	{
		struct lattice_edge	*e = tc->edges[i];
		struct token	*t = e->token;
		printf("token le %p vtx [%d-%d] char [%d-%d] string '%s' dag ",
			e, e->from->id, e->to->id, t->cfrom, t->cto, t->string);
		print_dg(t->dg);
		printf("\n");
	}
}

int	test_first_token_in_lex_dg(struct dg	*le_dg, struct token	*tok)
{
	struct dg	*toklist = dg_path(le_dg, lex_tokens_list_path);
	//printf(" .. token %s\n", tokens[i]->string);
	assert(toklist);
	struct dg	*lex_tok_dg = dg_hop(toklist, 1);	// FIRST
	if(lex_tok_dg)
	{
		//printf("candidate token site: "); print_dg(lex_tok_dg); printf("\n");
		int res = unify_dg_tmp(lex_tok_dg, tok->dg, -1);
		forget_tmp();
		if(res != 0)return -1;
	}
	return 0;
}

struct dg	*build_token_list(int	n, struct token	**toks, struct dg	**last_token_dg)
{
	if(n<=0)return atomic_dg(g_null_type);
	struct dg	*list = atomic_dg(g_cons_type);
	struct dg	*first = copy_dg(toks[0]->dg);
	struct dg	*rest = build_token_list(n-1, toks+1, last_token_dg);
	if(n==1)*last_token_dg = first;
	list = add_dg_feature(list, "FIRST", first);
	list = add_dg_feature(list, "REST", rest);
	return list;
}

int	install_tokens_in_le(struct edge	*le, struct token	**tokens)
{
/*
	struct dg	*toklist = dg_path(le->dg, lex_tokens_list_path), *last_token_dg = NULL;
	int i;
	printf("installing tokens in lexeme '%s'\n", le->lex->word);
	for(i=0;i<le->lex->stemlen;i++)
	{
		printf(" .. token %s %p\n", tokens[i]->string, tokens[i]);
		assert(toklist);
		struct dg	*lex_tok_dg = dg_hop(toklist, 1);	// FIRST
		if(lex_tok_dg)
		{
			struct dg	*tok_dg = copy_dg(tokens[i]->dg);
			printf("candidate token site: "); print_dg(lex_tok_dg); printf("\n");
			if(0 != unify_dg_infrom(lex_tok_dg, tok_dg))
			{
				fprintf(stderr, " failed to unify token with lexeme!\n");
				printf("trying to unify in token dag:\n");
				print_dg(tokens[i]->dg);
				printf("\n");
#ifdef	UNIFY_PATHS
			unify_backtrace();
#endif
				return -1;
			}
			else printf("SUCESSFULLY unified in token dag\n");
			last_token_dg = lex_tok_dg;
		}
		else
		{
			last_token_dg = copy_dg(tokens[i]->dg);
			toklist = add_dg_hop(toklist, 1, last_token_dg);	// FIRST
		}
		if(i+1 < le->lex->stemlen)
		{
			struct dg	*rest = dg_hop(toklist, 2);	// REST
			if(!rest)
			{
				rest = type_dg(g_cons_type);
				toklist = add_dg_hop(toklist, 2, rest);	// REST
			}
			toklist = rest;
		}
	}
	struct dg	*toklast = dg_path(le->dg, lex_tokens_last_path);
	assert(toklast);
	if(0 != unify_dg_infrom(toklast, last_token_dg))
	{
		fprintf(stderr, " failed to unify tokens LAST pointer\n");
		return -1;
	}
*/

	struct dg	*le_toklist = dg_path(le->dg, lex_tokens_list_path);
	struct dg	*le_toklast = dg_path(le->dg, lex_tokens_last_path);
	struct dg	*in_toklist, *in_toklast;
	if(!le_toklist || !le_toklast)
	{
		fprintf(stderr, "ERROR: toklist or toklast missing on a token\n");
		return -1;
	}
	in_toklist = build_token_list(le->lex->stemlen, tokens, &in_toklast);
	//printf(" ... in_toklist.first = %p ; in_toklast = %p\n", dg_hop(in_toklist,lookup_fname("FIRST")), in_toklast);
	assert(le_toklist && le_toklast && in_toklist && in_toklast);

	if(0 != unify_dg_infrom(le_toklist, in_toklist))
	{
		if(trace) { fprintf(stderr, "failed to unfiy tokens LIST\n"); unify_backtrace(); }
		return -1;
	}
	if(0 != unify_dg_infrom(le_toklast, in_toklast))
	{
		if(trace) { fprintf(stderr, "failed to unify tokens LAST\n"); unify_backtrace(); }
		return -1;
	}

	assert(le->daughters == NULL);
	le->daughters = slab_alloc(sizeof(struct token*) * le->lex->stemlen);
	int	i;
	for(i=0;i<le->lex->stemlen;i++)
		le->daughters[i] = (struct edge*)tokens[i];

	//printf("after unify, le_toklast:\n");print_dg(le_toklast);printf("\n");
	//printf("after unify, in_toklast:\n");print_dg(in_toklast);printf("\n");

	//printf("lexeme dag:\n");
	//print_dg(le->dg);
	//printf("\n");

	return 0;
}
