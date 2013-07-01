#include	<stdio.h>
#include	<unistd.h>
#include	<stdlib.h>
#include	<string.h>
#include	<pthread.h>

#include	"chart.h"
#include	"dag.h"
#include	"rule.h"
#include	"lexicon.h"
#include	"mrs.h"
#include	"unpack.h"
#include	"lattice-mapping.h"
#include	"token.h"

void	lui_gen_chart_level2(int	chart_id, int	nedge_ids, int	*edge_ids);

extern int preexisting_lui_fd;
extern int input_from_lui;

static int lui_rfd = -1, lui_wfd;
FILE		*lui_write, *lui_read;
static int chart_id = 0;

void	browse_lui(int eid, char *what);

int waiting_for_lui = 0;
char	*sentence_to_parse_from_lui = NULL;

input_from_lui_mode()
{
	printf("ACE: reading input from LUI\n");
	waiting_for_lui = 1;
	while(lui_rfd != -1)
	{
		usleep(200000);
		if(sentence_to_parse_from_lui)
		{
			char	*sent = sentence_to_parse_from_lui;
			sentence_to_parse_from_lui = NULL;
			clear_slab();
			parse_line(sent);
		}
	}
	printf("ACE: exiting after LUI closed our connection\n");
	return 0;
}

void	parse_from_lui(char	*input)
{
	if(!input_from_lui)return;
	sentence_to_parse_from_lui = strdup(input);
}

void	*lui_listener(void *foo)
{
	while(!ferror(lui_read) && !feof(lui_read))
	{
		char	line[1024], *lp;
		if(fgets(line, 1023, lui_read))
		{
			int	len;
			lp = line; while(*lp==' ' || *lp=='\n' || *lp=='\r' || *lp=='' || *lp=='\t')lp++;
			len = strlen(lp);
			while(len && lp[len-1]=='\n' || lp[len-1]=='' || lp[len-1]==' ')len--;
			lp[len] = 0;
			if(!strncmp(lp, "browse ", 7))
			{
				int cid, eid;
				char what[256];
				sscanf(lp, "browse %d %d %s\n", &cid, &eid, what);
				if(cid != chart_id)
					fprintf(stderr, "LUI: out-of-date chart %d requested [%d is current]\n", cid, chart_id);
				else browse_lui(eid, what);
			}
			else if(!strncmp(lp, "version ", 8)) {}
			else if(strstr(lp, " forget")) {}
			else if(!strncmp(lp, "parse ", 6))parse_from_lui(lp+6);
			else if(!strncmp(lp, "genchart ", 9))
			{
				int cid, neid=0, *eid=NULL;
				int	offset = 0;
				char	*p;
				sscanf(lp, "genchart %d explore [%n", &cid, &offset);
				p = lp + offset;
				assert(p>lp);
				assert(p[-1]=='[');
				for(p=strtok(p," ");p;p=strtok(NULL," "))
				{
					neid++;
					eid = realloc(eid,sizeof(int)*neid);
					eid[neid-1] = atoi(p);
				}
				if(cid != chart_id)
				{
					fprintf(stderr, "LUI: out-of-date chart %d requested [%d is current]\n", cid, chart_id);
					continue;
				}
				fprintf(stderr, "NOTE: replying to LUI request to browse genchart level 2 on %d EIDs\n", neid);
				lui_gen_chart_level2(cid, neid, eid);
			}
			else if(!strcmp(lp, "")) {}
			else fprintf(stderr, "LUI: unknown `%s'\n", lp);
		}
	}
	if(!waiting_for_lui)
		fprintf(stderr, "LUI: exited\n");
	lui_rfd = -1;
	return 0;
}

void quit_lui()
{
	if(lui_write)
	{
		fprintf(lui_write, "quit 0\n");
		fclose(lui_write);
	}
}

void wait_for_lui()
{
	if(lui_write)
	{
		waiting_for_lui = 1;
		while(lui_rfd != -1)usleep(200000);
	}
}

int lui_initialize()
{
	int rfd[2], wfd[2];
	if(lui_rfd != -1)return 0;

	if(preexisting_lui_fd != -1)
	{
		lui_wfd = preexisting_lui_fd;
		lui_rfd = preexisting_lui_fd;
		goto make_listener_thread;
	}

	int	pr1 = pipe(rfd);
	int	pr2 = pipe(wfd);
	assert((pr1 == 0 && pr2 == 0) || !"failed to create a pipe to talk to LUI");
	if(fork())
	{
		static pthread_t	pth;
		lui_wfd = wfd[1]; close(wfd[0]);
		lui_rfd = rfd[0]; close(rfd[1]);
		make_listener_thread:
		lui_write = fdopen(lui_wfd, "w");
		lui_read = fdopen(lui_rfd, "r");
		pthread_create(&pth, 0, lui_listener, 0);
		return 0;
	}
	else
	{
		close(0); close(1);
		close(wfd[1]); close(rfd[0]);
		dup2(wfd[0], 0); dup2(rfd[1], 1);
		execlp("yzlui", "yzlui", "-p", NULL);
		fprintf(stderr, "unable to launch yzlui!\n");
		exit(-1);
	}
}

void	output_lui_chart(struct lattice	*token_lattice)
{
	int	i, j, k;
	struct edge	*e, *esave;
	extern char	*current_sentence;

	lui_initialize();

	if(g_mode == GENERATING)
		chart_size++;
	fprintf(lui_write, "chart %d %d \"%s\"", ++chart_id, chart_size-1, current_sentence);
	char	covermap[chart_size];
	bzero(covermap, chart_size);
	//int	leid = -1;
	for(j=0;j<chart_size;j++)
	{
		if(covermap[j])continue;
		for(i=0;i<token_lattice->nedges;i++)
		{
			struct lattice_edge	*e = token_lattice->edges[i];
			if(e->from->id != j)continue;
			fprintf(lui_write, " #E[%d %d %d %d \"%s\" orth [ ]]", -(i+1), -(i+1), e->from->id, e->to->id, e->token->string);
			//leid--;
			break;
		}
	}
	//for(i=0;i<chart_size-1;i++)
		//fprintf(lui_write, " #E[%d %d %d %d \"%s\" orth [ ]]", -i-1, -i-1, i, i+1, words[i]);
	if(g_mode == GENERATING)
		chart_size--;

	void	lui_print_chart_edge(struct edge	*e)
	{
		if(e->unpack)return;
		e->unpack = (void*)0x1;
		if(e->rule)
		{
			struct dg *rn = dg_hop(e->rule->dg, lookup_fname("RNAME"));
			char *symb = strdup(rn?rn->xtype->name:e->rule->name);
			if(rn && *symb=='"') { symb++; if(strchr(symb, '"'))*strchr(symb, '"') = 0; }
			fprintf(lui_write, " #E[%d %d %d %d %s %s []", e->id, e->id, e->from, e->to, symb, e->rule->name);
			int l;
			for(l=0;l<e->have;l++)
				fprintf(lui_write, " %d", e->daughters[l]->id);
			fprintf(lui_write, "]");
		}
		else if(e->lex)
		{
			fprintf(lui_write, " #E[%d %d %d %d %s %s []", e->id, e->id, e->from, e->to, e->lex->word, e->lex->word);
			int k;
			for(k=0;k<e->lex->stemlen;k++)
			{
				struct token	*t = (struct token*)e->daughters[k];
				int j;
				for(j=0;j<token_lattice->nedges;j++)
					if(token_lattice->edges[j]->token == t)
						break;
				assert(j < token_lattice->nedges);
				fprintf(lui_write, " %d", -(j+1));
			}
			fprintf(lui_write, "]");
		}
		int i;
		for(i=0;i<e->have;i++)
			lui_print_chart_edge(e->daughters[i]);
		if(e->lex)
		{
			// show all lexical edges, including packed ones
			for(i=0;i<e->npack;i++)
				lui_print_chart_edge(e->pack[i]);
		}
	}

	reset_unpacking();
	for(i=0;i<chart_size;i++)
		for(j=0;j<chart_size;j++)
			for(k=0;k<cells[j*chart_size + i].p_nedges;k++)
			{
				e = cells[j*chart_size + i].p_edges[k];
				lui_print_chart_edge(e);
			}
	reset_unpacking();

/*
	for(i=0;i<chart_size;i++)
		for(j=0;j<chart_size;j++)
			for(k=0;k<cells[j*chart_size + i].p_nedges;k++)
			{
				e = cells[j*chart_size + i].p_edges[k];
				//if(e->id == 1155)esave = e;
				if(e->rule)
				{
					struct dg *rn = dg_hop(e->rule->dg, lookup_fname("RNAME"));
					char *symb = rn?strdup(rn->xtype->name):e->rule->name;
					if(rn) { symb++; if(strchr(symb, '"'))*strchr(symb, '"') = 0; }
					fprintf(lui_write, " #E[%d %d %d %d %s %s []", e->id, e->id, j, i, symb, e->rule->name);
					for(l=0;l<e->have;l++)
						fprintf(lui_write, " %d", e->daughters[l]->id);
					fprintf(lui_write, "]");
				}
				else if(e->lex)
				{
					fprintf(lui_write, " #E[%d %d %d %d %s %s [] %d]", e->id, e->id, j, j, e->lex->word, e->lex->word, -1-j);
				}
			}
			*/
	fprintf(lui_write, "\n");
	fflush(lui_write);
	//print_dg(esave->dg); printf("\n\n");
}

void	output_lui_dg(struct dg	*dg, char *wtitle)
{
	static int dagid = 0;
	int	tmp = dup(1);
	lui_initialize();
	close(1);
	dup2(lui_wfd, 1);

	fprintf(lui_write, "avm %d ", ++dagid); fflush(lui_write);
	print_dg(dg); fflush(stdout);
	fprintf(lui_write, " \"%s\"\n", wtitle?wtitle:"ACE DAG"); fflush(lui_write);

	close(1);
	dup2(tmp, 1);
	close(tmp);

	//fprintf(stderr, "LUI: wrote out a dag `%p'\n", dg);
}

void lui_format_tree(struct hypothesis *h, char *surf)
{
	struct edge *e = h->edge;
	char	*sign;
	if(e->rule)
		sign = e->rule->name;
	else
	{
		// XXX switch here
		sign = e->lex->word;
		//sign = e->lex->lextype->name;
	}
	//char	*sign = e->rule?e->rule->name:e->lex->word;
	char	*label = label_dag(h->dg, sign);
	int arity = e->have, i;

	if(!surf && e->lex)surf = h->string;
	if(!arity)
		fprintf(lui_write, " #T[%d \"%s\" \"%s\" %d %s",
			e->id, label, surf, e->id, sign);
	else
	{
		fprintf(lui_write, " #T[%d \"%s\" %s %d %s",
			e->id, label, "nil", e->id, sign);
		for(i=0;i<arity;i++)
			lui_format_tree(h->rhslist[i], surf);
	}
	fprintf(lui_write, "]");
}

void	browse_lui(int eid, char *what)
{
	int i, j, k, l;
	struct edge *e, *E;

	if(!strstr(what, "gen-"))
	{
		find_edge:
		//fprintf(stderr, "LUI: browse edge %d's %s\n", eid, what);
		for(i=0;i<chart_size;i++)
			for(j=0;j<chart_size;j++)
				for(k=0;k<cells[j*chart_size + i].p_nedges;k++)
				{
					E = cells[j*chart_size + i].p_edges[k];
					for(l=-1;l<E->npack;l++)
					{
						e = (l==-1)?E:E->pack[l];
						if(e->id != eid)continue;
						if(!strcmp(what, "avm"))output_lui_dg(e->dg, "edge");
						else fprintf(stderr, "LUI: don't know how to browse edge %d's `%s'\n", eid, what);
					}
				}
	}
	else
	{
		char			*s;
		struct hypothesis	*h;
		struct mrs		*m;
		if(0 != get_nth_realization(eid, &s, &h, &m))
		{
			fprintf(stderr, "LUI: unable to find realization %d\n", eid);
			return;
		}
		if(!strcmp(what, "gen-tree"))
		{
			fprintf(lui_write, "tree %d ", chart_id);
			lui_format_tree(h, NULL);
			fprintf(lui_write, " \"Realization %d\"\n", eid);
			fflush(lui_write);
		}
	}
}

int	output_lui_trees(struct chart_cell	*c, char *sent, int best_only, struct mrs	*require_mrs)
{
	struct hypothesis	**hyps = NULL;
	struct mrs			**mrss = NULL;
	int	count = 0;

	lui_initialize();
	int iter(struct hypothesis	*hyp, struct mrs	*mrs, double	prob)
	{
		if(!require_mrs || mrs_subsumes(require_mrs, mrs))
		{
			count++;
			hyps = realloc(hyps, sizeof(struct hypothesis*) * count);
			mrss = realloc(mrss, sizeof(struct mrs*) * count);
			hyps[count-1] = hyp;
			mrss[count-1] = mrs;
			return 1;
		}
		else return 0;
	}
	count = iterate_cell_root_hypotheses(c, iter, best_only);
	//printf("got %d results\n", count);
	fprintf(lui_write, "group %d \"%s\"\n", count, sent);

	int show_tree(struct hypothesis	*hyp, struct mrs	*mrs)
	{
		if(require_mrs && !mrs_subsumes(require_mrs, mrs))return 0;
		fprintf(lui_write, "tree %d", chart_id);
		lui_format_tree(hyp, NULL);
		fprintf(lui_write, " \"%s\"\n", sent);
		//printf("lui wrote a tree\n");
		return 1;
	}

	chart_id++;
	int i;
	for(i=0;i<count;i++)
		show_tree(hyps[i], mrss[i]);

	fflush(lui_write);

	return count;
}

int	xtext_id_gen = 1, ngenresults = 0;

void	lui_begin_gen_results(int	n)
{
	lui_initialize();
	ngenresults = n;
	fprintf(lui_write, "text %d #X[\"Generation Results (%d total):\" newline\n", ++chart_id, n);
}

void	lui_gen_result(int	idx, char	*surface)
{
	fprintf(lui_write, " #X[%d \"%s\"] newline\n", idx, surface);
}

void	lui_end_gen_results()
{
	fprintf(lui_write, "] #M[ \"Tree\" \"browse %d %%d gen-tree\"", chart_id);
	int i;
	for(i=0;i<ngenresults;i++)
		fprintf(lui_write, " %d", i);
	fprintf(lui_write, "] \"Generation Results\"\n");
	fflush(lui_write);
}

void	lui_text(char	*message)
{
	lui_initialize();
	fprintf(lui_write, "text %d #X[\"%s\"] \"ACE message\"\n", ++chart_id, message);
	fflush(lui_write);
}

char	*slab_orth_leaves(struct edge	*e)
{
	int		i;
	char	*sub, *ret;

	if(!e->have)
	{
		if(e->lex)
		{
			int len = 0, i;
			for(i=0;i<e->lex->stemlen;i++)len += strlen(e->lex->stem[i]->name) + 1 - 2;
			ret = slab_alloc_unaligned(len+4); *ret = 0;
			for(i=0;i<e->lex->stemlen;i++)
			{
				if(i)strcat(ret, " ");
				strcat(ret, e->lex->stem[i]->name+1);
				ret[strlen(ret)-1] = 0;
			}
			return ret;
		}
		return "[null]";
	}
	if(!e->daughters)return "*INVENTED*";
	if(e->have==1)
	{
		sub = slab_orth_leaves(e->daughters[0]);
		if(e->rule->orth)
		{
			//printf("applying orth rule `%s' to `%s'\n", e->rule->name, sub);
			ret = inflect(e->rule->orth, sub);
			return ret;
		}
		else return sub;
	}
	else
	{
		int		l = e->have-1;
		char	*sub[e->have];
		for(i=0;i<e->have;i++)
		{
			sub[i] = slab_orth_leaves(e->daughters[i]);
			l += strlen(sub[i]);
		}
		ret = slab_alloc_unaligned(l + 1);
		*ret = 0;
		for(i=0;i<e->have;i++)
		{
			if(i)strcat(ret, " ");
			strcat(ret, sub[i]);
		}
		return ret;
	}
	assert(!"not reached");
}

void	lui_gen_chart(struct mrs	*m)
{
	int i, j;
	lui_initialize();
	// reuse current chart_id, since we are in -l mode, and the realizations window will have already allocated one.
	fprintf(lui_write, "genchart %d [", chart_id);
	for(i=0;i<m->nrels;i++)fprintf(lui_write, " \"%s\"", slab_escape(m->rels[i].pred));
	fprintf(lui_write, "] [");
	struct chart_cell	*c = cells;
	for(i=0;i<c->p_nedges;i++)
	{
		struct edge	*e = c->p_edges[i];
		int r = ep_span_is_root(e->ep_span_bv, e->neps) && is_root(e->dg);
		char	*yield = slab_orth_leaves(e);
		char	*esc = slab_escape(yield);
		fprintf(lui_write, " #G[%d %d [", e->id, r);
		for(j=0;j<m->nrels;j++)
			if(bitvec_test(e->ep_span_bv, j))
				fprintf(lui_write, " %d", j);
		fprintf(lui_write, "] \"%s\"]", esc);
	}
	fprintf(lui_write, "] \"Generation Chart\"\n");
	fflush(lui_write);
}

struct edge	*find_gen_edge(int	id)
{
	struct chart_cell	*c = cells;
	int i;
	for(i=0;i<c->p_nedges;i++)
		if(c->p_edges[i]->id == id)
			return c->p_edges[i];
	return NULL;
}

int	set_gen_fromto(struct edge	*e, int	left)
{
	int i;
	e->from = left;
	for(i=0;i<e->have;i++)
	{
		set_gen_fromto(e->daughters[i], left);
		left = e->daughters[i]->to;
	}
	if(!e->have)left++;
	e->to = left;
	return left;
}

struct edge	*first_lexical_daughter(struct edge	*e)
{
	if(e->lex || e->rule->lexical)return e;
	assert(e->have);
	return first_lexical_daughter(e->daughters[0]);
}


/* the following is a copy of lui_show_chart() that has not been fully adapted yet.
 * I started on it, and then realized I'm not sure what constitute the vertices. */
void	lui_gen_chart_level2(int	chart_id, int	nedge_ids, int	*edge_ids)
{
	int	i;

extern int inhibit_pack;
	assert(inhibit_pack);	// this guarantees that an edge has a unique yield

	if(nedge_ids <= 0)
	{
		fprintf(stderr, "cannot display a chart of %d edges\n", nedge_ids);
		return;
	}
	struct edge	*canonical, *e;
	canonical = find_gen_edge(edge_ids[0]);
	assert(canonical != NULL);
	int	canonical_count = set_gen_fromto(canonical, 0);
	for(i=1;i<nedge_ids;i++)
	{
		e = find_gen_edge(edge_ids[i]);
		assert(e != NULL);
		int	count = set_gen_fromto(e, 0);
		assert(count == canonical_count);
	}

	fprintf(lui_write, "chart %d %d \"%s\"", chart_id, canonical_count, "Generation Chart Level 2");
	char	covermap[canonical_count];
	bzero(covermap, canonical_count);

	void	lui_print_chart_edge(struct edge	*e)
	{
		if(e->unpack)return;
		e->unpack = (void*)0x1;
		if(!covermap[e->from])
		{
			struct edge	*firstlex = first_lexical_daughter(e);
			char	*yield = slab_orth_leaves(firstlex);
			fprintf(lui_write, " #E[%d %d %d %d \"%s\" orth [ ]]", -(e->from+1), -(e->from+1), e->from, e->to, slab_escape(yield));
			covermap[e->from] = 1;
		}
		if(e->rule)
		{
			struct dg *rn = dg_hop(e->rule->dg, lookup_fname("RNAME"));
			char *symb = strdup(rn?rn->xtype->name:e->rule->name);
			if(rn && *symb=='"') { symb++; if(strchr(symb, '"'))*strchr(symb, '"') = 0; }
			fprintf(lui_write, " #E[%d %d %d %d %s %s []", e->id, e->id, e->from, e->to, symb, e->rule->name);
			int l;
			for(l=0;l<e->have;l++)
				fprintf(lui_write, " %d", e->daughters[l]->id);
			fprintf(lui_write, "]");
		}
		else if(e->lex)
		{
			fprintf(lui_write, " #E[%d %d %d %d %s %s []", e->id, e->id, e->from, e->to, e->lex->word, e->lex->word);
			fprintf(lui_write, " %d]", -(e->from+1));
		}
		int i;
		for(i=0;i<e->have;i++)
			lui_print_chart_edge(e->daughters[i]);
	}

	reset_unpacking();
	for(i=0;i<nedge_ids;i++)
		lui_print_chart_edge(find_gen_edge(edge_ids[i]));
	reset_unpacking();

	fprintf(lui_write, "\n");
	fflush(lui_write);
}
