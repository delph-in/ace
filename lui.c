#include	<stdio.h>
#include	<unistd.h>
#include	<stdlib.h>
#include	<string.h>
#include	<pthread.h>

#include	"conf.h"
#include	"chart.h"
#include	"dag.h"
#include	"rule.h"
#include	"lexicon.h"
#include	"mrs.h"
#include	"unpack.h"
#include	"lattice-mapping.h"
#include	"token.h"
#include	"hash.h"

void	lui_gen_chart_level2(int	chart_id, int	nedge_ids, int	*edge_ids);
void interactive_unify(char	*command);
void	output_lui_dg_with_failure(struct dg	*dg, char *wtitle, struct path	relative_to);
void	lui_text(char	*message);

extern int preexisting_lui_fd;
extern int input_from_lui;

static int lui_rfd = -1, lui_wfd;
FILE		*lui_write, *lui_read;
static int chart_id = 0;
int	xtext_id_gen = 1;

void	browse_lui(int eid, char *what, char	*what2);

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
				char what[256], what2[256] = "";
				int fields = sscanf(lp, "browse %d %d %s %s\n", &cid, &eid, what, what2);
				if(fields < 3)
					fprintf(stderr, "LUI: bad 'browse' command\n");
				else if(cid != chart_id)
					fprintf(stderr, "LUI: out-of-date chart %d requested [%d is current]\n", cid, chart_id);
				else browse_lui(eid, what, what2);
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
					if(*p==']')break;
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
			else if(!strncmp(lp, "type ", 5))
			{
				char type[256], command[256] = "";
				int fields = sscanf(lp, "type %s %s\n", type, command);
				if(fields < 2) { fprintf(stderr, "LUI: bad 'type' command\n"); }
				else if(!strcmp(command, "expansion"))
					lui_type(type);
				else fprintf(stderr, "LUI: '%s' viewer for types not implemented\n", command);
			}
			else if(!strcmp(lp, "")) {}
			else if(!strncmp(lp, "unify ", 6))interactive_unify(lp);
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
		fprintf(lui_write, "parameter list-type %s\n", g_list_type);
		fprintf(lui_write, "parameter empty-list-type %s\n", g_null_type);
		fprintf(lui_write, "parameter non-empty-list-type %s\n", g_cons_type);
		fprintf(lui_write, "parameter avm-collapsed-types [u i p e x h]\n");
		fflush(lui_write);
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
	extern char	current_sentence[];

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
			fprintf(lui_write, " #E[%d %d %d %d %s %s []", lui_register_edge(e), e->id, e->from, e->to, symb, e->rule->name);
			int l;
			for(l=0;l<e->have;l++)
				fprintf(lui_write, " %d", e->daughters[l]->id);
			fprintf(lui_write, "]");
		}
		else if(e->lex)
		{
			fprintf(lui_write, " #E[%d %d %d %d %s %s []", lui_register_edge(e), e->id, e->from, e->to, e->lex->word, e->lex->word);
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

struct hash	*lui_dgs = NULL;
struct lui_obj
{
	enum { LUI_DG, LUI_EDGE, LUI_TREE, LUI_MRS }	what;
	void	*ptr;
};
int	lui_id_allocator = 1;

int	lui_register(int	what, void	*ptr)
{
	if(!lui_dgs)lui_dgs = hash_new("lui dgs");
	char	str[32];
	int	id = lui_id_allocator++;
	sprintf(str, "%d", id);
	struct lui_obj	*o = calloc(sizeof(*o),1);
	o->what = what;
	o->ptr = ptr;
	hash_add(lui_dgs, strdup(str), o);
	return id;
}

void	*fail_to_find(int what, char	*id)
{
	fprintf(stderr, "LUI asked for object %s, but couldn't find it\n", id);
	return NULL;
}

void *lui_find(int	what, char	*id, int	complain)
{
	if(!lui_dgs)
	{
		if(!complain)return NULL;
		return fail_to_find(what, id);
	}
	struct lui_obj	*o = hash_find(lui_dgs, id);
	if(o->what != what)
	{
		if(!complain)return NULL;
		fprintf(stderr, "wrong type:\n");
		return fail_to_find(what, id);
	}
	return o->ptr;
}

int	lui_register_dg(struct dg	*dg)
{
	return lui_register(LUI_DG, dg);
}

int	lui_register_edge(struct edge	*e)
{
	return lui_register(LUI_EDGE, e);
}

int	lui_register_tree(struct hypothesis	*h)
{
	return lui_register(LUI_TREE, h);
}

struct edge	*lui_find_edge(char	*id)
{
	return (struct edge*)lui_find(LUI_EDGE, id, 1);
}

struct hypothesis	*lui_find_tree(char	*id)
{
	return (struct hypothesis*)lui_find(LUI_TREE, id, 1);
}

struct dg	*lui_find_dg(char	*id)
{
	struct dg	*d = (struct dg*)lui_find(LUI_DG, id, 0);
	if(d)return d;
	struct edge	*e = (struct edge*)lui_find(LUI_EDGE, id, 0);
	if(e)return e->dg;
	struct hypothesis	*h = lui_find_tree(id);
	if(h)return h->dg;
	return NULL;
}

void interactive_unify(char	*command)
{
	char	left_id[32], right_id[32];
	int		offset;
	sscanf(command, "unify %s [%n", left_id, &offset);
	char	*left_path_str = command + offset;
	char	*end = strchr(left_path_str, ']');
	assert(end != NULL);
	*end++ = 0;
	sscanf(end, " %s [%n", right_id, &offset);
	char	*right_path_str = end + offset;
	end = strchr(right_path_str, ']');
	assert(end != NULL);
	*end++ = 0;

	//printf("should unify part of %s with part of %s\n", left_id, right_id);
	struct dg	*left_dg = lui_find_dg(left_id);
	struct dg	*right_dg = lui_find_dg(right_id);
	struct dg	*original_right_dg = right_dg;
	if(!left_dg || !right_dg)return;

	struct path	left_path = string_to_path(left_path_str);
	struct path	right_path = string_to_path(right_path_str);

	left_dg = dg_path(left_dg, left_path);
	if(!left_dg) { fprintf(stderr, "no such path in left DG\n"); return; }
	right_dg = dg_path(right_dg, right_path);
	if(!right_dg) { fprintf(stderr, "no such path in right DG\n"); return; }

	//output_lui_dg(left_dg, "left");
	//output_lui_dg(right_dg, "right");

	int	result = unify_dg_tmp(left_dg, right_dg, -1);
	struct dg	*result_dg = finalize_tmp(original_right_dg, 0);
	if(!result_dg)
	{
		lui_text("cycle failure. sorry, I'm too lazy to tell you where.");
	}
	else if(result != 0)
	{
		output_lui_dg_with_failure(result_dg, "unification failure", right_path);
	}
	else
	{
		output_lui_dg(result_dg, "unification result");
	}
}

void	output_lui_dg_id(struct dg	*dg, char *wtitle, int	id)
{
	static int dagid = 0;
	int	tmp = dup(1);
	lui_initialize();
	close(1);
	dup2(lui_wfd, 1);

	fprintf(lui_write, "avm %d ", id); fflush(lui_write);
	extern int quoted_number_types;
	quoted_number_types = 1;
	print_dg(dg); fflush(stdout);
	quoted_number_types = 0;
	fprintf(lui_write, " \"%s\"\n", wtitle?wtitle:"ACE DAG"); fflush(lui_write);

	close(1);
	dup2(tmp, 1);
	close(tmp);

	//fprintf(stderr, "LUI: wrote out a dag `%p'\n", dg);
}

void	output_lui_dg_id_with_failure(struct dg	*dg, char *wtitle, int	id, struct path	relative_to)
{
	static int dagid = 0;
	int	tmp = dup(1);
	lui_initialize();
	close(1);
	dup2(lui_wfd, 1);

	fprintf(lui_write, "avm %d ", id); fflush(lui_write);
	print_dg(dg); fflush(stdout);
	extern int upathl, upath[];
	extern struct type	*ut1, *ut2, *uglb;
	fprintf(lui_write, " \"%s\" [ #U[%s 1 [", wtitle?wtitle:"ACE DAG", uglb?"constraint":"type");
	int i, n=0;
	extern char	**fnames;
	for(i=0;i<relative_to.count;i++)
		fprintf(lui_write, "%s%s", (n++)?" ":"", fnames[relative_to.fnums[i]]);
	for(i=0;i<upathl;i++)fprintf(lui_write, "%s%s", (n++)?" ":"", fnames[upath[i]]);
	if(uglb)fprintf(lui_write, "] %s %s %s -1]\n", ut1->name, ut2->name, uglb->name);
	else    fprintf(lui_write, "] %s %s -1] ]\n", ut1->name, ut2->name);
	fflush(lui_write);

	close(1);
	dup2(tmp, 1);
	close(tmp);

	//fprintf(stderr, "LUI: wrote out a dag `%p'\n", dg);
}

void	output_lui_dg(struct dg	*dg, char *wtitle)
{
	output_lui_dg_id(dg, wtitle, lui_register_dg(dg));
}

void	output_lui_dg_with_failure(struct dg	*dg, char *wtitle, struct path	relative_to)
{
	output_lui_dg_id_with_failure(dg, wtitle, lui_register_dg(dg), relative_to);
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
			lui_register_tree(h), label, surf, h->eid, sign);
	else
	{
		fprintf(lui_write, " #T[%d \"%s\" %s %d %s",
			lui_register_tree(h), label, "nil", h->eid, sign);
		for(i=0;i<arity;i++)
			lui_format_tree(h->rhslist[i], surf);
	}
	fprintf(lui_write, "]");
}

void	output_lui_edge_mrs(struct hypothesis	*h, char	*type)
{
	struct mrs	*m = extract_mrs(h->dg);
	if(!strcasecmp(type, "simple"))
	{
		int	id = lui_register(LUI_MRS, m);
		fprintf(lui_write, "avm %d #D[mrs", id);
		int i, j, k;

		make_reliable_vnums(m);

		void	output_lui_mrs_var(struct mrs_var	*v)
		{
			if(!v->is_const)
			{
				fprintf(lui_write, "<%d>=#D[%s", v->vnum, v->type);
				int k;
				for(k=0;k<v->nprops;k++)
				{
					fprintf(lui_write, " %s: \"%s\"", v->props[k].name, v->props[k].value);
				}
				fprintf(lui_write, "]");
			}
			else
			{
				char	*esc = slab_escape(v->name);
				fprintf(lui_write, "\"%s\"", esc);
			}
		}

		fprintf(lui_write, " TOP: ");
		output_lui_mrs_var(m->ltop);
		fprintf(lui_write, " INDEX: ");
		output_lui_mrs_var(m->index);

		fprintf(lui_write, " RELS: ");
		for(i=0;i<m->nrels;i++)
		{
			struct mrs_ep	*e = &m->rels[i];
			fprintf(lui_write, "#D[%s FIRST: #D[%s", g_cons_type, e->pred);
			fprintf(lui_write, " LBL: ");
			output_lui_mrs_var(e->lbl);
			for(j=0;j<e->nargs;j++)
			{
				fprintf(lui_write, " %s: ", e->args[j].name);
				output_lui_mrs_var(e->args[j].value);
			}
			fprintf(lui_write, "] REST: ");
		}
		fprintf(lui_write, "#D[%s]", g_null_type);
		for(i=0;i<m->nrels;i++)fprintf(lui_write, " ]");

		fprintf(lui_write, " HCONS: ");
		for(i=0;i<m->nhcons;i++)
		{
			struct mrs_hcons	*h = &m->hcons[i];
			char	*type = "???";
			if(h->type == hcons_qeq)type = "qeq";
			if(h->type == hcons_leq)type = "leq";
			if(h->type == hcons_geq)type = "geq";
			fprintf(lui_write, "#D[%s FIRST: #D[%s", g_cons_type, type);
			fprintf(lui_write, " HARG: ");
			output_lui_mrs_var(h->hi);
			fprintf(lui_write, " LARG: ");
			output_lui_mrs_var(h->lo);
			fprintf(lui_write, "] REST: ");
		}
		fprintf(lui_write, "#D[%s]", g_null_type);
		for(i=0;i<m->nhcons;i++)fprintf(lui_write, " ]");

		if(mrs_enable_icons)
		{
			fprintf(lui_write, " ICONS: ");
			for(i=0;i<m->nicons;i++)
			{
				struct mrs_icons	*h = &m->icons[i];
				fprintf(lui_write, "#D[%s FIRST: #D[%s", g_cons_type, h->type);
				fprintf(lui_write, " IARG1: ");
				output_lui_mrs_var(h->left);
				fprintf(lui_write, " IARG2: ");
				output_lui_mrs_var(h->right);
				fprintf(lui_write, "] REST: ");
			}
			fprintf(lui_write, "#D[%s]", g_null_type);
			for(i=0;i<m->nicons;i++)fprintf(lui_write, " ]");
		}

		fprintf(lui_write, "] \"Simple MRS\"\n");
		fflush(lui_write);
	}
	else if(!strcasecmp(type, "indexed"))
	{
		char	gave_props[m->vlist.nvars];
		bzero(gave_props, sizeof(gave_props));
		void imrs_var(struct mrs_var	*v)
		{
			if(v->is_const)
				fprintf(lui_write, " \"%s\"", slab_escape(v->name));
			else
			{
				fprintf(lui_write, " #X[%d \"%s%d\"]", v->vnum, v->type, v->vnum);
				if(!gave_props[v->vnum])
				{
					gave_props[v->vnum] = 1;
					if(!v->nprops)return;
					fprintf(lui_write, " \" (");
					int i;
					for(i=0;i<v->nprops;i++)
						fprintf(lui_write, "%s%s", i?", ":"", v->props[i].value);
					fprintf(lui_write, ")\"");
				}
			}
		}
		fprintf(lui_write, "text %d #X[\"TOP: \"", ++xtext_id_gen);
		imrs_var(m->ltop);
		fprintf(lui_write, " newline \"INDEX: \"");
		imrs_var(m->index);
		fprintf(lui_write, " newline newline");
		int i, j, k;
		for(i=0;i<m->nrels;i++)
		{
			struct mrs_ep	*e = &m->rels[i];
			imrs_var(e->lbl);
			if(e->pred[0]!='"')
				fprintf(lui_write, " \":%s\"", slab_escape(e->pred));
			else fprintf(lui_write, " \":\" %s", e->pred);
			fprintf(lui_write, " \"<%d:%d>(\"", e->cfrom, e->cto);
			for(j=0;j<e->nargs;j++)
			{
				if(j)fprintf(lui_write, " \", \"");
				imrs_var(e->args[j].value);
			}
			fprintf(lui_write, "\")\" newline\n");
		}
		fprintf(lui_write, " newline \"HCONS: \"\n");
		for(i=0;i<m->nhcons;i++)
		{
			fprintf(lui_write, " \"%s\"", i?", ":"");
			imrs_var(m->hcons[i].hi);
			fprintf(lui_write, " \" %s \"", (m->hcons[i].type==hcons_qeq)?"qeq":((m->hcons[i].type==hcons_leq)?"leq":((m->hcons[i].type==hcons_geq)?"geq":"???")));
			imrs_var(m->hcons[i].lo);
		}
		fprintf(lui_write, " newline\n");
		if(mrs_enable_icons)
		{
			fprintf(lui_write, " newline \"ICONS: \"\n");
			for(i=0;i<m->nicons;i++)
			{
				fprintf(lui_write, " \"%s\"", i?", ":"");
				imrs_var(m->icons[i].left);
				fprintf(lui_write, " \" %s \"", m->icons[i].type);
				imrs_var(m->icons[i].right);
			}
			fprintf(lui_write, " newline\n");
		}
		fprintf(lui_write, "] \"Indexed MRS\"\n");
		fflush(lui_write);
	}
	else
	{
		printf("mrs display of type '%s' is not implemented.\n", type);
	}
}

void	output_lui_edge_tree(struct edge	*e)
{
	struct hypothesis	*h = cheap_hypothesis(e);
	fprintf(lui_write, "tree %d", chart_id);
	lui_format_tree(h, NULL);
	fprintf(lui_write, " \"Edge #%d\"\n", e->id);
	fflush(lui_write);
}

void	output_lui_edge_entity(struct edge	*e)
{
	if(e->rule)lui_rule(e->rule->name);
	else if(e->lex)lui_lex(e->lex->word);
}

void	browse_lui(int eid, char *what, char	*what2)
{
	int i, j, k, l;

	if(!strstr(what, "gen-"))
	{
		char	eidstr[32];
		//find_edge:
		//fprintf(stderr, "LUI: browse edge %d's %s\n", eid, what);
		sprintf(eidstr, "%d", eid);
		if(!strcmp(what, "avm"))
		{
			struct dg	*d = lui_find_dg(eidstr);
			if(d)output_lui_dg(d, "edge");
			else printf("no such LUI dag #%s\n", eidstr);
		}
		else if(!strcmp(what, "mrs"))
		{
			struct hypothesis	*h = lui_find_tree(eidstr);
			if(h)output_lui_edge_mrs(h, what2);
			else printf("no such LUI tree #%s\n", eidstr);
		}
		else if(!strcmp(what, "tree"))
		{
			struct edge	*e = lui_find_edge(eidstr);
			if(e)output_lui_edge_tree(e);
			else printf("no such LUI edge #%s\n", eidstr);
		}
		else if(!strcmp(what, "entity"))
		{
			struct edge	*e = lui_find_edge(eidstr);
			if(e)output_lui_edge_entity(e);
			else printf("no such LUI edge #%s\n", eidstr);
		}
		else fprintf(stderr, "LUI: don't know how to browse edge %d's `%s'\n", eid, what);
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

int	ngenresults = 0;

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
			fprintf(lui_write, " #E[%d %d %d %d %s %s []", lui_register_edge(e), e->id, e->from, e->to, symb, e->rule->name);
			int l;
			for(l=0;l<e->have;l++)
				fprintf(lui_write, " %d", e->daughters[l]->id);
			fprintf(lui_write, "]");
		}
		else if(e->lex)
		{
			fprintf(lui_write, " #E[%d %d %d %d %s %s []", lui_register_edge(e), e->id, e->from, e->to, e->lex->word, e->lex->word);
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
