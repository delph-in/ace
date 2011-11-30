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

static int lui_rfd = -1, lui_wfd;
FILE		*lui_write, *lui_read;
static int chart_id = 0;

void	browse_lui(int eid, char *what);

int waiting_for_lui = 0;

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
					fprintf(stderr, "LUI: out-of-date chart %d requested\n", cid);
				else browse_lui(eid, what);
			}
			else if(!strncmp(lp, "version ", 8)) {}
			else if(strstr(lp, " forget")) {}
			else if(!strcmp(lp, "")) {}
			else fprintf(stderr, "LUI: unknown `%s'\n", lp);
		}
	}
	if(!waiting_for_lui)
		fprintf(stderr, "LUI: exited\n");
	lui_rfd = -1;
	return 0;
}

void wait_for_lui()
{
	if(lui_write)
	{
		waiting_for_lui = 1;
		fprintf(lui_write, "quit 0\n");
		fclose(lui_write);
		while(lui_rfd != -1)usleep(200000);
	}
}

int lui_initialize()
{
	int rfd[2], wfd[2];
	if(lui_rfd != -1)return 0;

	int	pr1 = pipe(rfd);
	int	pr2 = pipe(wfd);
	assert((pr1 == 0 && pr2 == 0) || !"failed to create a pipe to talk to LUI");
	if(fork())
	{
		static pthread_t	pth;
		lui_wfd = wfd[1]; close(wfd[0]);
		lui_rfd = rfd[0]; close(rfd[1]);
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
	int	i, j, k, l;
	struct edge	*e, *esave;
	extern char	*current_sentence;

	lui_initialize();

	if(g_mode == GENERATING)
		chart_size++;
	fprintf(lui_write, "chart %d %d \"%s\"", ++chart_id, chart_size-1, current_sentence);
	char	covermap[chart_size];
	bzero(covermap, chart_size);
	int	leid = -1;
	for(j=0;j<chart_size;j++)
	{
		if(covermap[j])continue;
		for(i=0;i<token_lattice->nedges;i++)
		{
			struct lattice_edge	*e = token_lattice->edges[i];
			if(e->from->id != j)continue;
			fprintf(lui_write, " #E[%d %d %d %d \"%s\" orth [ ]]", leid, leid, e->from->id, e->to->id, e->token->string);
			leid--;
			break;
		}
	}
	//for(i=0;i<chart_size-1;i++)
		//fprintf(lui_write, " #E[%d %d %d %d \"%s\" orth [ ]]", -i-1, -i-1, i, i+1, words[i]);
	if(g_mode == GENERATING)
		chart_size--;

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

void	browse_lui(int eid, char *what)
{
	int i, j, k, l;
	struct edge *e, *E;

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

int	output_lui_trees(struct chart_cell	*c, char *sent, int best_only, struct mrs	*require_mrs)
{
	struct hypothesis	**hyps = NULL;
	struct mrs			**mrss = NULL;
	int	count = 0;

	lui_initialize();
	int iter(struct hypothesis	*hyp, struct mrs	*mrs)
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
		void rtree(struct hypothesis *h, char *surf)
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
					rtree(h->rhslist[i], surf);
			}
			fprintf(lui_write, "]");
		}
		if(require_mrs && !mrs_subsumes(require_mrs, mrs))return 0;
		fprintf(lui_write, "tree %d", chart_id);
		rtree(hyp, 0);
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
