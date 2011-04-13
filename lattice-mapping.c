#define	DEBUG(x, ...)	do {} while(0)
#define	DEBUG_VERBOSE(x, ...)	do {} while(0)
//#define	DEBUG	printf
//#define	DEBUG_VERBOSE	printf
//#define	SHOW_OUTPUT_TOKENS

#include	<wctype.h>
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<sys/time.h>
#include	<assert.h>

#include	"tdl.h"
#include	"dag.h"
#include	"type.h"
#include	"conf.h"
#include	"freeze.h"
#include	"chart.h"
#include	"type.h"
#include	"lexicon.h"

#include	"lattice-mapping.h"
#include	"token.h"

#include	"unicode.h"

int			ntmrules = 0;
struct lattice_rule	**tmrules = NULL;

struct lattice_rule_set	*token_mapping_rules = NULL;
struct lattice_rule_set	*lexical_filtering_rules = NULL;

struct lattice_rule	*freeze_lattice_rule(struct lattice_rule	*r)
{
	struct lattice_rule	*fr = slab_alloc(sizeof(*fr));
	fr->name = freeze_string(r->name);
	fr->dg = freeze_dg(r->dg);
	fr->ninput = r->ninput;
	fr->noutput = r->noutput;
	fr->ncontext = r->ncontext;
	fr->nposition_constraints = r->nposition_constraints;
	fr->position_constraints = freeze_block(r->position_constraints,
		sizeof(struct lattice_position_constraint)*(r->nposition_constraints));
	fr->regex_constraints = slab_alloc(sizeof(struct lattice_regex_constraints)*(fr->ninput+fr->ncontext));
	int i, j;
	for(i=0;i<fr->ninput+fr->ncontext;i++)
	{
		struct lattice_regex_constraints	*frec = fr->regex_constraints+i;
		struct lattice_regex_constraints	*rec = r->regex_constraints+i;
		bzero(frec, sizeof(*frec));
		frec->count = rec->count;
		frec->paths = slab_alloc(sizeof(struct path)*frec->count);
		frec->regexs = slab_alloc(sizeof(wchar_t*)*frec->count);
		for(j=0;j<frec->count;j++)
		{
			frec->paths[j] = freeze_path(rec->paths[j]);
			frec->regexs[j] = freeze_wcs(rec->regexs[j]);
		}
	}
	return fr;
}

struct lattice_rule_set	*freeze_lattice_rule_set(struct lattice_rule_set	*rs)
{
	struct lattice_rule_set	*frs = slab_alloc(sizeof(*frs));
	int	i;
	frs->nrules = rs->nrules;
	frs->rules = slab_alloc(sizeof(struct lattice_rule*)*frs->nrules);
	for(i=0;i<frs->nrules;i++)
		frs->rules[i] = freeze_lattice_rule(rs->rules[i]);
	return frs;
}

void	compile_lattice_rule_set(struct lattice_rule_set	*rs)
{
	int	i, j, k;
	for(i=0;i<rs->nrules;i++)
		for(j=0;j<rs->rules[i]->ninput+rs->rules[i]->ncontext;j++)
		{
			struct lattice_regex_constraints	*rec = rs->rules[i]->regex_constraints+j;
			rec->re = malloc(sizeof(regex_t)*rec->count);
			for(k=0;k<rec->count;k++)
			{
				//printf("... compiling `%S'\n", rec->regexs[k]);
				int	err = regcomp(&rec->re[k], rec->regexs[k], REGEX_FLAGS);
				if(err)
				{
					wchar_t	buf[1024];
					regerror(err, &rec->re[k], buf, 1023);
					fprintf(stderr, "lattice-mapping-rule: while compiling `%S' for rule '%s': %S\n", rec->regexs[k], rs->rules[i]->name, buf);
					exit(-1);
				}
			}
		}
}

struct dg	*lattice_rule_spot(struct lattice_rule	*r, int	k)
{
	if(k<r->ninput)
	{
		struct dg	*d = dg_path(r->dg, lattice_mapping_input_path);
		while(k-->0)d = dg_hop(d, 2);	// REST
		return dg_hop(d, 1);	// FIRST
	}
	k -= r->ninput;
	if(k<r->ncontext)
	{
		struct dg	*d = dg_path(r->dg, lattice_mapping_context_path);
		while(k-->0)d = dg_hop(d, 2);	// REST
		return dg_hop(d, 1);	// FIRST
	}
	k -= r->ncontext;
	if(k<r->noutput)
	{
		struct dg	*d = dg_path(r->dg, lattice_mapping_output_path);
		while(k-->0)d = dg_hop(d, 2);	// REST
		return dg_hop(d, 1);	// FIRST
	}
	assert(!"Not reached");
}

static int	count_list(struct dg	*list, char	*rname, char	*fname)
{
	int	N = 0;
	while(list)
	{
		if(!strcmp(list->xtype->name, g_null_type))break;
		if(!strcmp(list->xtype->name, g_list_type))
		{
			fprintf(stderr, "WARNING: lattice mapping rule '%s' had %s *list* (not *null* or *cons*)\n", rname, fname);
			break;
		}
		struct dg	*item = dg_hop(list, 1);	// FIRST
		if(!item)return -1;
		N++;
		list = dg_hop(list, 2);	// REST
	}
	return N;
}

void	extract_regex_constraints(struct dg	*d, struct lattice_regex_constraints	*rec, struct path	path)
{
	d = dereference_dg(d);
	if(d->xtype->name[0]=='"')
	{
		int	l = strlen(d->xtype->name);
		if(d->xtype->name[l-1]=='$')
		{
			// d->xtype->name looks like: "myregex$
			char	*re = malloc(l + 7);
			sprintf(re, "^(?:%.*s)$", l-2, d->xtype->name+1);
			//printf("extracted RE: %s\n", re);
			rec->count++;
			rec->paths = realloc(rec->paths, sizeof(struct path)*rec->count);
			rec->regexs = realloc(rec->regexs, sizeof(wchar_t*)*rec->count);
			rec->paths[rec->count-1].count = path.count;
			rec->paths[rec->count-1].fnums = malloc(sizeof(int)*path.count);
			memcpy(rec->paths[rec->count-1].fnums, path.fnums, sizeof(int)*path.count);
			rec->regexs[rec->count-1] = towide(re);
			extern struct type	*strtype;
			d->xtype = d->type = strtype;
			return;
		}
	}

	int	i;
	struct dg	**da = DARCS(d);
	for(i=0;i<d->narcs;i++)
	{
		path.fnums[path.count++] = d->label[i];
		extract_regex_constraints(da[i], rec, path);
		path.count--;
	}
}

struct lattice_rule	*study_lattice_rule(char	*name, struct dg	*dg)
{
	struct lattice_rule	*r = calloc(sizeof(*r), 1);
	r->name = strdup(name);
	r->dg = dg;
	struct dg	*input_list = dg_path(dg, lattice_mapping_input_path);
	struct dg	*output_list = dg_path(dg, lattice_mapping_output_path);
	struct dg	*context_list = dg_path(dg, lattice_mapping_context_path);
	struct dg	*position_constraint_list = dg_path(dg, lattice_mapping_position_path);
	if( (r->ninput = count_list(input_list, name, "+INPUT")) < 0)return NULL;
	if( (r->noutput = count_list(output_list, name, "+OUTPUT")) < 0)return NULL;
	if( (r->ncontext = count_list(context_list, name, "+CONTEXT")) < 0)return NULL;

	r->regex_constraints = malloc(sizeof(struct lattice_regex_constraints)*(r->ninput+r->ncontext));
	int	i;
	for(i=0;i<r->ninput+r->ncontext;i++)
	{
		struct lattice_regex_constraints	*rec = r->regex_constraints+i;
		struct dg	*d = lattice_rule_spot(r, i);
		int	fnum_buffer[128];
		struct path	path = {0,fnum_buffer};
		bzero(rec, sizeof(*rec));
		extract_regex_constraints(d, rec, path);
	}

	DEBUG_VERBOSE("lattice rule '%s': ", r->name);
	DEBUG_VERBOSE("%d inputs, %d outputs, %d contexts\n", r->ninput, r->noutput, r->ncontext);
	char	*constraint = position_constraint_list->type->name;
	if(constraint[0]=='"')
	{
		constraint = strdup(constraint);
		char	*cp = constraint+1;
		while(cp && *cp != '"' && *cp != 0)
		{
			char	*comma = strchr(cp, ',');
			if(!comma)comma = strchr(cp, '"');
			if(comma)*comma = 0;
			cp = trim_string(cp);
			again:;
			char	lhs_what, rhs_what, type[3] = {0,0,0};
			int	lhs_which, rhs_which;
			if(5 > strlen(cp) || !isdigit(cp[1]))
			{
				badconstraint:
				fprintf(stderr, "lattice mapping rule: bad positional constraint '%s'\n", cp);
				return NULL;
			}
			lhs_what = cp[0];
			lhs_which = cp[1] - '1';
			type[0] = cp[2];
			if(!isalpha(cp[3])) { type[1] = cp[3]; cp++; }
			rhs_what = cp[3];
			if(!isdigit(cp[4]))goto badconstraint;
			rhs_which = cp[4] - '1';
			//printf("... position constraint: lhs %c # %d,  rhs %c # %d,  type %s\n", lhs_what, lhs_which, rhs_what, rhs_which, type);
			r->nposition_constraints++;
			r->position_constraints = realloc(r->position_constraints, sizeof(struct lattice_position_constraint)*r->nposition_constraints);
			struct lattice_position_constraint	*pc = r->position_constraints+r->nposition_constraints-1;
			pc->lhs_what = lhs_what;
			pc->rhs_what = rhs_what;
			pc->lhs_which = lhs_which;
			pc->rhs_which = rhs_which;
			     if(!strcmp(type, "<"))pc->type = lpcImmediatelyPreceeds;
			else if(!strcmp(type, ">"))
			{
				pc->lhs_what = rhs_what;
				pc->lhs_which = rhs_which;
				pc->rhs_what = lhs_what;
				pc->rhs_which = lhs_which;
				pc->type = lpcImmediatelyPreceeds;
			}
			else if(!strcmp(type, "<<"))pc->type = lpcPreceeds;
			else if(!strcmp(type, ">>"))
			{
				pc->lhs_what = rhs_what;
				pc->lhs_which = rhs_which;
				pc->rhs_what = lhs_what;
				pc->rhs_which = lhs_which;
				pc->type = lpcPreceeds;
			}
			else if(!strcmp(type, "@"))
			{
				pc->lhs_what = lhs_what;
				pc->lhs_which = lhs_which;
				pc->rhs_what = rhs_what;
				pc->rhs_which = rhs_which;
				pc->type = lpcCovers;
			}
			else goto badconstraint;
			if(strlen(cp) > 5) { cp += 3; goto again; }
			else if(comma)cp = comma+1;
			else break;
		}
	}
	return r;
}

struct lattice_rule_set	*load_lattice_rules(char	*tdl_status)
{
	struct lattice_rule_set	*rs = calloc(sizeof(*rs),1);
	int	callback(char	*name, struct dg	*dg)
	{
		struct lattice_rule	*r = study_lattice_rule(name, wellform_dg(dg));
		if(!r)return -1;
		rs->nrules++;
		rs->rules = realloc(rs->rules, sizeof(struct lattice_rule*)*rs->nrules);
		rs->rules[rs->nrules-1] = r;
		return 0;
	}
	if(process_tdl_dgs_status(tdl_status, callback))
	{
		fprintf(stderr, "token-mapping-rules: unable to load rules, bailing out.\n");
		exit(-1);
	}
	return rs;
}

struct lattice_vertex	*new_lattice_vertex(struct lattice	*tc)
{
	struct lattice_vertex	*lv = slab_alloc(sizeof(*lv));
	lv->id = tc->lattice_vertex_id_gen++;
	lv->nfollowers = 0;
	lv->followers = NULL;

	tc->nvertices++;
	tc->vertices = slab_realloc(tc->vertices, sizeof(struct lattice_vertex*)*(tc->nvertices-1), sizeof(struct lattice_vertex*)*(tc->nvertices));
	tc->vertices[tc->nvertices-1] = lv;

	return lv;
}

void	add_lattice_vertex_follower(struct lattice_vertex	*v, struct lattice_edge	*e)
{
	v->nfollowers++;
	v->followers = slab_realloc(v->followers, sizeof(struct lattice_edge*)*(v->nfollowers-1), sizeof(struct lattice_edge*)*(v->nfollowers));
	v->followers[v->nfollowers-1] = e;
}

void	remove_lattice_vertex_follower(struct lattice_vertex	*v, struct lattice_edge	*e)
{
	int	i;
	for(i=0;i<v->nfollowers;i++)
		if(v->followers[i] == e)break;
	assert(i<v->nfollowers);
	v->nfollowers--;
	memmove(v->followers+i, v->followers+i+1, sizeof(struct lattice_edge*)*(v->nfollowers-i));
}

void	add_lattice_edge(struct lattice	*lat, struct lattice_edge	*e)
{
	lat->nedges++;
	lat->edges = slab_realloc(lat->edges, sizeof(struct lattice_edge*)*(lat->nedges-1), sizeof(struct lattice_edge*)*(lat->nedges));
	lat->edges[lat->nedges-1] = e;
	add_lattice_vertex_follower(e->from, e);
}

void	maybe_dead_lattice_vertex(struct lattice	*lat, struct lattice_vertex	*v)
{
	int i;
	for(i=0;i<lat->nedges;i++)
	{
		if(lat->edges[i]->to == v)return;
		if(lat->edges[i]->from == v)return;
	}
	assert(v->nfollowers == 0);
	// v is dead, remove it
	for(i=0;i<lat->nvertices;i++)
		if(lat->vertices[i]==v)break;
	if(i==lat->nvertices)
	{
		fprintf(stderr, "tried to remove dead vertex from lattice, but it wasn't in\n", v);
		exit(-1);
	}
	lat->nvertices--;
	memmove(lat->vertices+i, lat->vertices+i+1, sizeof(struct lattice_vertex*)*(lat->nvertices-i));
}

void	remove_lattice_edge(struct lattice	*lat, struct lattice_edge	*e)
{
	int	i;
	for(i=0;i<lat->nedges;i++)
		if(lat->edges[i] == e)break;
	assert(i<lat->nedges);
	lat->nedges--;
	memmove(lat->edges+i, lat->edges+i+1, sizeof(struct lattice_edge*)*(lat->nedges-i));
	remove_lattice_vertex_follower(e->from, e);
	maybe_dead_lattice_vertex(lat, e->from);
	maybe_dead_lattice_vertex(lat, e->to);
}

struct dg	*lattice_dg(struct lattice_edge	*le)
{
	if(le->token) return le->token->dg;
	if(le->edge) return le->edge->dg;
	else assert(!"lattice edge with no content dag");
}

struct re_match_set
{
	wchar_t		**strings;
	regmatch_t	**matches;
};

void	replace_backrefs(struct dg	*d, struct lattice_rule	*r, struct re_match_set	*mats)
{
	if(d->xtype->name[0]=='"')
	{
		if(!strstr(d->xtype->name, "${"))return;
		char	*backref, *last = d->xtype->name, *patchto, *backref_end;
		wchar_t	build[1024]=L"", *buildp = build;
		char	expr[128];
		while(1)
		{
			backref = strstr(last, "${");
			if(backref == NULL)patchto = last + strlen(last);
			else patchto = backref;
			buildp += swprintf(buildp, 1023, L"%.*s", patchto-last, last);
			if(backref == NULL)break;
			backref_end = strchr(backref, '}');
			if(!backref_end)goto badsyntax;
			last = backref_end+1;
			sprintf(expr, "%.*s", (int)(backref_end - (backref+2)), backref+2);
			int	l = strlen(expr);
			char	*from = strtok(expr, ":");
			char	*pathstr = strtok(NULL, ":");
			char	*cgnum_str = strtok(NULL, ":");
			int	do_lowercase = 0;
			if(!strncmp(from, "lc(", 3))
			{
				// XXX an ugly hack in the "standard" calls for an ugly hack in the implementation...
				from += 3;
				do_lowercase = 1;
			}
			if(!from || !pathstr || !cgnum_str || !isalpha(from[0]) || !isdigit(from[1]))
			{
				badsyntax:
				fprintf(stderr, "bad backref syntax: %s in rule '%s'\n", d->xtype->name, r->name);
				exit(-1);
			}
			int	cgnum = atoi(cgnum_str) - 1;
			char	from_what = from[0];
			int	from_which = from[1]-'1';
			if(from_what == 'C')from_which += r->ninput;
			int	i;
			struct re_match_set	*mat = mats+from_which;
			struct lattice_regex_constraints	*rec = r->regex_constraints+from_which;
			for(i=0;pathstr[i];i++)if(pathstr[i]=='.')pathstr[i]=' ';
			struct path	path = slab_string_to_path(pathstr);
			for(i=0;i<rec->count;i++)
				if(!pathcmp(&rec->paths[i], &path))break;
			if(i==rec->count)
			{
				fprintf(stderr, "bad backref %s in rule '%s': no regex at that path.\n", d->xtype->name, r->name);
				exit(-1);
			}
			if(cgnum >= rec->re[i].re_nsub)
			{
				fprintf(stderr, "bad backref %s in rule '%s': asked for capture group %d (but only have %d)\n", d->xtype->name, r->name, (int)(cgnum+1), (int)(rec->re[i].re_nsub));
				exit(-1);
			}
			DEBUG_VERBOSE("should patch from %d path %s refid %d -- and found it (source string '%S')\n", from_which, pathstr, cgnum, mat->strings[i]);
			int	so = mat->matches[i][cgnum+1].rm_so;
			int	eo = mat->matches[i][cgnum+1].rm_eo;
			wchar_t	*ptr = mat->strings[i]+so;
			if(!do_lowercase)
			{
				while(so<eo)
				{
					*buildp++ = *ptr++;
					so++;
				}
			}
			else
			{
				wchar_t	str[512], *wp = str;
				swprintf(str, 511, L"%.*S", eo-so, ptr);
				while(*wp) { *wp = towlower(*wp);
					if(iswupper(*wp))
					{
						fprintf(stderr, "yikes! '%C' is uppercase and lowercase\n", *wp);
						exit(-1);
					}
					wp++; }
				wp = str;
				while(*wp)
					*buildp++ = *wp++;
			}
		}
		DEBUG_VERBOSE("built string: `%S'\n", build);
		d->xtype = d->type = temporary_string_type(freeze_tombs(build));
		DEBUG_VERBOSE("... resulting in type '%s'\n", d->xtype->name);
	}
	else
	{
		// recurse over all children
		int	i;
		struct dg	**da = DARCS(d);
		for(i=0;i<d->narcs;i++)
			replace_backrefs(da[i], r, mats);
	}
}

struct token	*extract_token_from_dg(struct dg	*dg)
{
	struct token	*t = slab_alloc(sizeof(struct token));
	t->dg = dg;
	struct dg	*str_dg = dg_path(dg, token_form_path);
	assert(str_dg != NULL);
	t->string = freeze_string(str_dg->xtype->name+1);
	int	sl = strlen(t->string);
	assert(sl>0 && t->string[sl-1]=='"');
	t->string[sl-1] = 0;
	struct dg	*cfrom_dg = dg_path(dg, token_from_path);
	assert(cfrom_dg != NULL);
	struct dg	*cto_dg = dg_path(dg, token_to_path);
	assert(cto_dg != NULL);
	t->cfrom = atoi(cfrom_dg->xtype->name+1);
	t->cto = atoi(cto_dg->xtype->name+1);
	struct dg	*taglist_dg = dg_path(dg, token_postags_path);
	assert(taglist_dg != NULL);
	struct dg	*tagfirst_dg = dg_hop(taglist_dg, 1);	// FIRST
	if(tagfirst_dg)
	{
		t->postag = freeze_string(tagfirst_dg->xtype->name+1);
		int	sl = strlen(t->postag);
		assert(sl>0 && t->postag[sl-1]=='"');
		t->postag[sl-1] = 0;
	}
	else t->postag = NULL;
	t->oc = NULL;
	return t;
}

int	lattice_mapping_rule_output(struct lattice	*lat, struct lattice_rule	*r, struct lattice_edge	**inputs, struct re_match_set	*mats, struct dg	**out_dg)
{
	int	did_add = 0, did_remove = 0;

	// first step: iterate through all the out_dg's and replace backreferences
	int i, j;
	for(i=0;i<r->noutput;i++)
		replace_backrefs(out_dg[i], r, mats);
	// second step: build and insert edge structures for the new tokens
	// determining the from/to vertices for each output edge is a bit tricky

/*
	// if we are splitting an edge into several consecutive ones,
	// the POSITION constraints *must* define '<' constraints on each side of each output token except the outside ones
	// and this determines a total order, so let's find the order
	int	follower[r->noutput], preceder[r->noutput];
	int	nips = 0;
	for(i=0;i<r->noutput;i++)follower[i] = preceder[i] = -1;
	for(i=0;i<r->nposition_constraints;i++) if(r->position_constraints[i].type==lpcImmediatelyPreceeds)
	{
		struct lattice_position_constraint	*lpc = r->position_constraints+i;
		if(lpc->lhs_what!='O' || lpc->rhs_what!='O')continue;
		nips++;
		follower[lpc->lhs_which] = lpc->rhs_which;
		preceder[lpc->rhs_which] = lpc->lhs_which;
		//DEBUG_VERBOSE(" ... O%d preceeds O%d\n", lpc->lhs_which, lpc->rhs_which);
	}
	int	ordered[r->noutput], oord[r->noutput], n=0;
	bzero(ordered, sizeof(ordered));
	if(nips)
	{
		int	first = -1, last = -1;
		for(i=0;i<r->noutput;i++)
		{
			if(follower[i]==-1)
			{
				if(last==-1)last = i;
				else { fprintf(stderr, "lattice-mapping rule '%s' doesn't specify valid ordering on outputs.\n", r->name); exit(-1); }
			}
			if(preceder[i]==-1)
			{
				if(first==-1)first = i;
				else { fprintf(stderr, "lattice-mapping rule '%s' doesn't specify valid ordering on outputs.\n", r->name); exit(-1); }
			}
		}
		for(i=first;i!=-1;i=follower[i])
		{
			if(!ordered[i])
			{
				ordered[i] = 1;
				oord[i] = n++;
			}
			else { fprintf(stderr, "lattice-mapping rule '%s' doesn't specify valid ordering on outputs.\n", r->name); exit(-1); }
		}
		if(n!=r->noutput) { fprintf(stderr, "lattice-mapping rule '%s' doesn't specify valid ordering on outputs.\n", r->name); exit(-1); }
	}
	// next, find the min and max vertices indicated by the "Covers" constraint
	struct lattice_vertex	*min=NULL, *max=NULL;
	for(i=0;i<r->nposition_constraints;i++) if(r->position_constraints[i].type==lpcCovers)
	{
		struct lattice_position_constraint	*lpc = r->position_constraints+i;
		if(lpc->lhs_what=='O')
		{
			// On spans rhs
			int	ow = lpc->lhs_which;
			int	which = lpc->rhs_which;
			if(lpc->rhs_what == 'C')which += r->ninput;
			struct lattice_edge	*e = inputs[which];
			if(!min)min = e->from;
			else if(lattice_vertex_follows(lat, min, e->from))min = e->from;
			if(!max)max = e->to;
			else if(lattice_vertex_follows(lat, e->to, max))max = e->to;
		}
		else if(lpc->rhs_what=='O')
		{
			// lhs spans On
			int	ow = lpc->rhs_which;
			int	which = lpc->lhs_which;
			if(lpc->lhs_which == 'C')which += r->ninput;
			struct lattice_edge	*e = inputs[which];
			if(!min)min = e->from;
			else if(lattice_vertex_follows(lat, min, e->from))min = e->from;
			if(!max)max = e->to;
			else if(lattice_vertex_follows(lat, e->to, max))max = e->to;
		}
	}
	// we may need some new vertices
	struct lattice_vertex	*verts[r->noutput+1];
	if(nips)
	{
		verts[0] = min;
		verts[r->noutput] = max;
		for(i=1;i<r->noutput;i++)verts[i] = new_lattice_vertex(lat);
	}
	// finally, go ahead and make the new tokens
	for(i=0;i<r->noutput;i++)
	{
		struct lattice_vertex	*from,*to;
		if(nips)
		{
			from = verts[oord[i]];
			to = verts[oord[i]+1];
		}
		else
		{
			from = min;
			to = max;
		}
		assert(from && to);
#ifdef	SHOW_OUTPUT_TOKENS
		printf("want to add token dag [%d -> %d]: \n", from->id, to->id); print_dg(out_dg[i]); printf("\n");
#endif
		for(j=0;j<from->nfollowers;j++)
		{
			struct lattice_edge	*le = from->followers[j];
			if(le->to != to)continue;
			if(equivalent_dg(out_dg[i], lattice_dg(le), 1, 1) == 2)break;
		}
		if(j<from->nfollowers)
		{
			DEBUG("... not adding that edge, since we had an identical one already.\n");
			continue;
		}
		struct lattice_edge	*le = slab_alloc(sizeof(*le));
		bzero(le, sizeof(*le));
		le->from = from;
		le->to = to;
		// NOTE: all lexical filtering rules have no output, so we only reach here for token mapping
		le->token = extract_token_from_dg(out_dg[i]);
		DEBUG("O%d = `%s'\n", i+1, le->token->string);
		add_lattice_edge(lat, le);
		did_add++;
	}
	*/

	// allocate blank output edges
	struct lattice_edge	*le = slab_alloc(sizeof(*le) * r->noutput);
	bzero(le,sizeof(*le)*r->noutput);
	// determine vertices
	for(i=0;i<r->nposition_constraints;i++)
	{
		struct lattice_position_constraint	*lpc = r->position_constraints+i;
		switch(lpc->type)
		{
			case	lpcImmediatelyPreceeds:
				if(lpc->lhs_what=='O' && lpc->rhs_what=='O')
				{
					// invent a new vertex for lhs's TO and rhs's FROM
					struct lattice_vertex	*v = new_lattice_vertex(lat);
					le[lpc->lhs_which].to = v;
					le[lpc->rhs_which].from = v;
				}
				else if(lpc->lhs_what=='O')
				{
					// set lhs's TO to rhs's FROM
					int	which = lpc->rhs_which;
					if(lpc->rhs_what == 'C')which += r->ninput;
					le[lpc->lhs_which].to = inputs[which]->from;
				}
				else if(lpc->rhs_what=='O')
				{
					// set rhs's FROM to lhs's TO
					int	which = lpc->lhs_which;
					if(lpc->lhs_what == 'C')which += r->ninput;
					le[lpc->rhs_which].from = inputs[which]->to;
				}
				break;
			case	lpcCovers:
				if(lpc->lhs_what=='O' && lpc->rhs_what=='O')
				{
					assert(!"don't know how to implement Ox@Oy positional constraints");
				}
				else if(lpc->lhs_what=='O')
				{
					// make sure lhs covers rhs
					// if lhs has NULL endpoints, copy rhs's
					int	which = lpc->rhs_which;
					if(lpc->rhs_what == 'C')which += r->ninput;
					struct lattice_edge	*rhs = inputs[which];
					struct lattice_edge	*lhs = &le[lpc->lhs_which];
					if(!lhs->to || lattice_vertex_follows(lat, rhs->to, lhs->to))
						lhs->to = rhs->to;
					if(!lhs->from || lattice_vertex_follows(lat, lhs->from, rhs->from))
						lhs->from = rhs->from;
				}
				else if(lpc->rhs_what=='O')
				{
					// verify that lhs covers rhs
					// if rhs has NULL endpoints, copy lhs's
					int	which = lpc->lhs_which;
					if(lpc->lhs_what == 'C')which += r->ninput;
					struct lattice_edge	*lhs = inputs[which];
					struct lattice_edge	*rhs = &le[lpc->rhs_which];
					if(!rhs->to)rhs->to = lhs->to;
					else if(lattice_vertex_follows(lat, rhs->to, lhs->to))
						assert(!"inconsistant lattice mapping positional constraints");
					if(!rhs->from)rhs->from = lhs->from;
					else if(lattice_vertex_follows(lat, lhs->from, rhs->from))
						assert(!"inconsistant lattice mapping positional constraints");
				}
				break;
		}
	}

	struct lattice_edge	*save_le[r->noutput];

	// add them to the lattice
	for(i=0;i<r->noutput;i++)
	{
		if(!le[i].from || !le[i].to)
			assert(!"incomplete lattice mapping positional constraints");

		int	j;
		struct lattice_vertex	*from = le[i].from, *to = le[i].to;

#ifdef	SHOW_OUTPUT_TOKENS
		printf("want to add token dag [%d -> %d]: \n", from->id, to->id); print_dg(out_dg[i]); printf("\n");
#endif

		for(j=0;j<from->nfollowers;j++)
		{
			struct lattice_edge	*e = from->followers[j];
			if(e->to != to)continue;
			//printf("checking for eq with %p\n", e);
			//printf("which is:\n");print_dg(lattice_dg(e));printf("\n");
			if(equivalent_dg(out_dg[i], lattice_dg(e), 1, 1) == 2)break;
		}
		if(j<from->nfollowers)
		{
			DEBUG("... not adding that edge, since we had an identical one already.\n");
			save_le[i] = from->followers[j];
			continue;
		}
		save_le[i] = NULL;

		le[i].token = extract_token_from_dg(out_dg[i]);
		DEBUG("O%d = `%s' [%d - %d] = %p\n", i+1, le[i].token->string, le[i].from->id, le[i].to->id, &le[i]);
		add_lattice_edge(lat, &le[i]);
		did_add++;
	}

	// third step: remove input edge structures
	for(i=0;i<r->ninput;i++)
	{
		for(j=0;j<r->noutput;j++)
			if(save_le[j] == inputs[i])
				break;
		if(j<r->noutput)continue;
		remove_lattice_edge(lat, inputs[i]);
		did_remove++;
	}
	return did_add + did_remove;
}

// TODO find a more efficient algorithm for finding application sites
// TODO keep a cache of what rule already failed to unify with what edge... a *lot* of wasted unification attempts
// TODO   -- or use quickcheck?
int	apply_lattice_rule1(struct lattice	*lat, struct lattice_rule	*r, struct lattice_edge	**inputs, int	have)
{
	int i, j;
	struct re_match_set	*mats = NULL;
	if(have==r->ninput + r->ncontext)
		mats = slab_alloc(sizeof(struct re_match_set)*have);
	if(have>0)
	{
		// verify the inputs match the position constraints
		for(i=0;i<r->nposition_constraints;i++)
		{
			struct lattice_position_constraint	*pc = r->position_constraints+i;
			if(pc->lhs_what == 'O' || pc->rhs_what == 'O')continue;
			int	lhs_which = pc->lhs_which;
			if(pc->lhs_what == 'C')lhs_which += r->ninput;
			int	rhs_which = pc->rhs_which;
			if(pc->rhs_what == 'C')rhs_which += r->ninput;
			if(lhs_which >= have || rhs_which >= have)continue;
			struct lattice_vertex	*rhs_start = inputs[rhs_which]->from;
			struct lattice_vertex	*lhs_start = inputs[lhs_which]->from;
			struct lattice_vertex	*lhs_end = inputs[lhs_which]->to;
			struct lattice_vertex	*rhs_end = inputs[rhs_which]->to;
			switch(pc->type)
			{
				case	lpcImmediatelyPreceeds:
					if(lhs_end != rhs_start)
					{
						DEBUG_VERBOSE("failed immediate preceeder test\n");
						return 0;
					}
					break;
				case	lpcPreceeds:
					if(!(lattice_vertex_follows(lat, rhs_start, lhs_end) || lhs_end==rhs_start))
					{
						DEBUG_VERBOSE("failed somewhere-preceeder test\n");
						return 0;
					}
					break;
				case	lpcCovers:
					if(! ((lattice_vertex_follows(lat, rhs_start, lhs_start) || rhs_start==lhs_start)
					   && (lattice_vertex_follows(lat, lhs_end, rhs_end) || lhs_end==rhs_end)))
					{
						DEBUG_VERBOSE("failed covers test\n");
						return 0;
					}
					break;
			}
		}
		// verify regular expression matches
		for(i=0;i<have;i++)
		{
			struct dg	*idg = lattice_dg(inputs[i]);//lattice_rule_spot(r, i);
			struct lattice_regex_constraints	*rec = r->regex_constraints+i;
			if(mats)
			{
				mats[i].strings = slab_alloc(sizeof(wchar_t*) * rec->count);
				mats[i].matches = slab_alloc(sizeof(regmatch_t*) * rec->count);
			}
			for(j=0;j<rec->count;j++)
			{
				//printf("... should hop path: "); print_path(rec->paths[j]); printf("\n");
				//printf(" from dg: "); print_dg(idg); printf("\n");
				struct dg	*str_dg = dg_path(idg, rec->paths[j]);
				if(str_dg)
				{
					char	*str = str_dg->xtype->name;
					int	l = strlen(str);
					char	copy[256];
					sprintf(copy, "%.*s", l-2, str+1);
					wchar_t	*wcopy = towide(copy);
					regex_t	re = rec->re[j];
					DEBUG_VERBOSE("... comparing string '%S' against regex '%S'\n", wcopy, rec->regexs[j]);
					regmatch_t	m[re.re_nsub+1], *M = m;
					if(mats)
					{
						mats[i].strings[j] = freeze_wcs(wcopy);
						mats[i].matches[j] = slab_alloc(sizeof(regmatch_t) * (re.re_nsub+1));
						M = mats[i].matches[j];
					}
					int	result = regexec(&re, wcopy, re.re_nsub+1, M, 0);
					//int	wl = wstrlen(wcopy);
					free(wcopy);
					if(0 == result)// && M[0].rm_so==0 && M[0].rm_eo==wl)
					{
						DEBUG_VERBOSE(" ... matched!\n");
					}
					else
					{
						DEBUG_VERBOSE(" ... didn't match.\n");
						return 0;
					}
				}
				else return 0;
			}
		}
		// verify inputs unify into the rule
		for(i=0;i<have;i++)
		{
			if(unify_dg_tmp(lattice_dg(inputs[i]), lattice_rule_spot(r, i), -1) != 0)
				{ forget_tmp(); DEBUG_VERBOSE(" ... didn't unify\n"); /*unify_backtrace(); print_dg(lattice_rule_spot(r,i));printf("\n");*/return 0; }
		}
	}
	if(have == r->ninput + r->ncontext)
	{
		// apply the rule as matched
		DEBUG("found an alignment of rule '%s'\n", r->name);
		if(inputs[0]->token)
		{
			for(i=0;i<r->ninput;i++)DEBUG("I%d = '%s' = %p\n", i+1, inputs[i]->token->string, inputs[i]);
			for(i=0;i<r->ncontext;i++)DEBUG("C%d = '%s'\n", i+1, inputs[r->ninput+i]->token->string);
		}
		else
		{
			for(i=0;i<r->ninput;i++)DEBUG("I%d = '#%d' %s %d-%d\n", i+1, inputs[i]->edge->id, inputs[i]->edge->lex->word, inputs[i]->edge->from, inputs[i]->edge->to);
			for(i=0;i<r->ncontext;i++)DEBUG("C%d = '#%d' %s %d-%d\n", i+1, inputs[r->ninput+i]->edge->id, inputs[r->ninput+i]->edge->lex->word, inputs[r->ninput+i]->edge->from, inputs[r->ninput+i]->edge->to);
		}
		struct dg	*out_dg[r->noutput];
		for(i=0;i<r->noutput;i++)
		{
			struct dg	*o = lattice_rule_spot(r, i+r->ninput+r->ncontext);
			out_dg[i] = copy_dg_with_comp_arcs(o);
		}
		bump_generation();

		return lattice_mapping_rule_output(lat, r, inputs, mats, out_dg);
	}
	else
	{
		if(have)bump_generation();
		// find next input candidate
		for(i=0;i<lat->nvertices;i++)
			for(j=0;j<lat->vertices[i]->nfollowers;j++)
			{
				struct lattice_edge	*e = lat->vertices[i]->followers[j];
				// make sure we don't already have 'e' in inputs[]
				int k;
				for(k=0;k<have;k++)
					if(inputs[k] == e)break;
				if(k<have)continue;
				inputs[have] = e;
				if(e->token)
					DEBUG_VERBOSE(".. considering '%s' as inputs[%d] for rule %s\n", e->token->string, have, r->name);
				else 
					DEBUG_VERBOSE(".. considering '#%d' as inputs[%d] for rule %s\n", e->edge->id, have, r->name);
				int	result = apply_lattice_rule1(lat, r, inputs, have+1);
				if(result)return result;
			}
		return 0;
	}
}

int	apply_lattice_rule(struct lattice	*lat, struct lattice_rule	*r)
{
	struct lattice_edge	*inputs[r->ninput + r->ncontext];
	DEBUG_VERBOSE("trying to apply '%s'...\n", r->name);
	return apply_lattice_rule1(lat, r, inputs, 0);
}

void	apply_lattice_mapping(struct lattice	*lat, struct lattice_rule_set	*rs)
{
	int	i, redo = 0;

	for(i=0;i<rs->nrules;i++)
		while(apply_lattice_rule(lat, rs->rules[i])){}
/*again:
	redo = 0;
	for(i=0;i<rs->nrules;i++)
		redo += apply_lattice_rule(lat, rs->rules[i]);
	if(redo)goto again;*/
}

int	load_token_mapping_rules()
{
	token_mapping_rules = load_lattice_rules("token-mapping-rule");
	return 0;
}

void	apply_token_mapping(struct lattice	*lat)
{
	apply_lattice_mapping(lat, token_mapping_rules);
}

int	load_lexical_filtering_rules()
{
	lexical_filtering_rules = load_lattice_rules("lexical-filtering-rule");
	return 0;
}

void	apply_lexical_filtering(struct lattice	*lat)
{
	apply_lattice_mapping(lat, lexical_filtering_rules);
}
