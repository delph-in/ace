#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>

#include	"morpho.h"
#include	"rule.h"
#include	"lexicon.h"
#include	"dag.h"

int	dagify_tdl_line(char	*lhs, char	*rhs, void	*ref);
int	typify_tdl_line(char	*lhs, char	*rhs, void	*ref);

char	*trim_string(char	*str)
{
	int	len;

	while(*str==' ' || *str=='\t' || *str=='\n')str++;
	len = strlen(str);
	if(len==0)return str;
	while(str[len-1]==' ' || str[len-1]=='\t' || str[len-1]=='\n')len--;
	str[len] = 0;
	return str;
}

struct tdl_line
{
	int	processed;
	char	*lhs, *rhs;
	char	*idef;
	int	lnum;
};

int		ntdllines;
struct tdl_line	*tdllines;

int	loud_noises = 0;

struct dg	*dagify(char	*str, struct type	*building_type);
int	constrain_type(char	*name, struct dg	*dg);

int	enable_out_of_order = 0;

void	add_tdlline(char	*lhs, char	*rhs, char	*idef, int	lnum)
{
	ntdllines++;
	tdllines = realloc(tdllines, sizeof(struct tdl_line)*ntdllines);
	tdllines[ntdllines-1].processed = 0;
	tdllines[ntdllines-1].lhs = strdup(lhs);
	tdllines[ntdllines-1].rhs = strdup(rhs);
	tdllines[ntdllines-1].idef = (*idef)?strdup(idef):0;
	tdllines[ntdllines-1].lnum = lnum;
}

char	*g_inflr_def;

int	process_comments(char	*line, int	in_comment)
{
	char	*ptr, *oline;
	int	inquote = 0;

	// handle block-comments first
	for(oline=ptr=line;*ptr;ptr++)
	{
		if(ptr[0]=='#' && ptr[1]=='|')in_comment++, ptr++;
		else if(ptr[0]=='|' && ptr[1]=='#')in_comment--, ptr++;
		else if(!in_comment)
		{
			if(*ptr=='"')inquote=!inquote;
			if(*ptr==';' && !inquote)*oline++ = 0;
			else *oline++ = *ptr;
		}
	}
	*oline = 0;
	return in_comment;
}

int	unbalanced(char	*line)
{
	int	inquote = 0, bal = 0;
	int	escape = 0;

	while(*line)
	{
		if(escape) { line++; escape = 0; }
		else if(*line==':' && line[1]=='<')line+=2;
		else switch(*line++)
		{
			case	'<':	if(!inquote)bal++;	break;
			case	'>':	if(!inquote)bal--;	break;
			case	'"':	inquote = !inquote;	break;
			case	'\\':	escape = 1;		break;
		}
	}
	return bal;
}

int	iterate_tdl(char	*path, int	(*callback)(char	*lhs, char	*rhs, void	*ref), void	*ref)
{
	FILE	*f = fopen(path, "r");
	int	i;
	char	buffer[10240], *lhs, *rhs, *end;
	char	line[1024], *lp;
	char	idef[1024];
	int	in_comment = 0;
	int	lnum = 0;

	if(!f)
	{
		perror(path);
		return -1;
	}
	buffer[0] = 0;

	tdllines = 0;
	ntdllines = 0;

	*idef = 0;
	while(fgets(line, 1023, f))
	{
		if(line[0]==0)break;
		lnum++;
		if(line[0]=='\n')continue;
		line[strlen(line)-1] = 0;
		in_comment = process_comments(line, in_comment);
		if(line[0]==0)continue;
		if(line[0]=='%')
		{
			if(line[1]=='(')setup_morpho(line+1);
			else strcpy(idef, line+1);
			continue;
		}

		lp = trim_string(line);

		if(buffer[0])strcat(buffer, " ");
		strcat(buffer, lp);
		if(!buffer[0])continue;
		if(strlen(buffer) > 9000)
		{
			fprintf(stderr, "tdl statement too long: `%s'\n", buffer);
			return -1;
		}
		end = buffer + strlen(buffer)-1;
		if(*end != '.')
		{
			//fprintf(stderr, "tdl: `%s' malformed input string, does not end '.'\n", buffer);
			continue;
		}
		if(unbalanced(buffer))continue;	// '.' can validly occur inside a list without ending the tdl statement.
		*end = 0;
		lhs = buffer;
		end = strstr(lhs, ":=");
		if(!end)end = strstr(lhs, ":<");
		if(!end)
		{
			fprintf(stderr, "tdl: `%s' malformed input string, does not contain ':=' or ':<'\n", buffer);
			continue;
		}
		*end = 0;
		// for our purposes, there doesn't seem to be any distinction between := and :< ...
		rhs = end+2;
		lhs = trim_string(lhs);
		rhs = trim_string(rhs);
		if(*lhs==0 || *rhs==0)
		{
			fprintf(stderr, "tdl: `%s := %s' malformed input string, degenerate statement\n", lhs, rhs);
			continue;
		}

		//printf("tdl input: `%s'  -->  `%s'\n", lhs, rhs);
		add_tdlline(lhs, rhs, idef, lnum);
		*buffer = 0;
		*idef = 0;
	}
	for(i=0;i<ntdllines;i++)
	{
		if(tdllines[i].processed)continue;
		tdllines[i].processed = 1;
		g_inflr_def = tdllines[i].idef;
		if(callback(tdllines[i].lhs, tdllines[i].rhs, ref))
		{
			fprintf(stderr, "tdl: error (non-recursed) occured near line %d\n", tdllines[i].lnum);
			return -1;
		}
	}
	for(i=0;i<ntdllines;i++)
	{
		free(tdllines[i].lhs);
		free(tdllines[i].rhs);
		if(tdllines[i].idef)
			free(tdllines[i].idef);
	}
	ntdllines = 0;
	free(tdllines);
	tdllines = 0;
	fclose(f);
	return 0;
}

struct type	*find_type(char	*name, int	constrain)
{
	struct type	*type = lookup_type(name);
	int		i;

	if(!type || (name[0]!='"' && constrain && !type->dg))
	{
		// try to find constraint for this type before returning it
		if(type && name[0]=='G' && name[1]=='(')
		{
			// glb types may not be referenced explicitly
			type = 0;
			goto notype;
		}
		for(i=0;i<ntdllines;i++)
			if(!strcmp(name, tdllines[i].lhs))break;
		if(i<ntdllines)
		{
			if(tdllines[i].processed)
			{
				fprintf(stderr, "tdl: cycle detected! type `%s' needed before completely defined.\n", name);
				return 0;
			}
			else
			{
				if(enable_out_of_order)
				{
					//printf(" -- found `%s' later in .tdl file\n", name);
					tdllines[i].processed = 1;
					if(constrain)
					{
						if(dagify_tdl_line(tdllines[i].lhs, tdllines[i].rhs, constrain_type))
						{
							fprintf(stderr, "tdl: error constraining occured near line %d\n", tdllines[i].lnum);
							return 0;
						}
					}
					else if(typify_tdl_line(tdllines[i].lhs, tdllines[i].rhs, 0))
					{
						fprintf(stderr, "tdl: error typing occured near line %d\n", tdllines[i].lnum);
						return 0;
					}
					type = lookup_type(name);
					//printf(" -- finished sneaking `%s' in\n", name);
				}
			}
		}
	}

	if(!type)
	{
		notype:
		fprintf(stderr, "tdl: no such type as `%s'\n", name);
	}
	return type;
}

struct dg	*type_dg(char		*name)
{
	struct type	*type = find_type(name, 1);
	struct dg	*ret;

	if(!type)return 0;
	if(!type->dg)return atomic_dg(name);
	ret = copy_dg(type->dg);
	if(!ret) { fprintf(stderr, "type_dg(%s) couldn't copy %p\n", name, type->dg); exit(-1); }
	return ret;
}

struct dagify_record
{
	int	current_path[1024];
	int	current_pathlen;

	struct	coref
	{
		int	*path;
		int	pathlen;
		char	*name;
	}	*corefs;
	int	ncorefs;
};

static void coreference(struct dagify_record	*R, char	*name)
{
	R->ncorefs++;
	R->corefs = realloc(R->corefs, sizeof(struct coref)*R->ncorefs);
	R->corefs[R->ncorefs-1].path = malloc(sizeof(int) * R->current_pathlen);
	R->corefs[R->ncorefs-1].pathlen = R->current_pathlen;
	memcpy(R->corefs[R->ncorefs-1].path, R->current_path, sizeof(int) * R->current_pathlen);
	R->corefs[R->ncorefs-1].name = strdup(name);
}

static void add_dg_path(struct dg	*d, int	*path, int	len, struct dg	*target)
{
	int	i;
	struct dg *step;

	for(i=0;i<len-1;i++)
	{
		step = dg_hop(d, path[i]);
		if(!step)
		{
			step = atomic_dg("*top*");
			add_dg_hop(d, path[i], step);
		}
		d = step;
	}
	add_dg_hop(d, path[len-1], target);
}

static int patch_coreferences(struct dagify_record	*R, struct dg	*target)
{
	int		i, j;
	struct dg	*coref_template, *dg;
	struct dg	*reentrancies[256];
	char		*rnames[256];
	int		nr = 0;

	if(!R->ncorefs)return 0;
	coref_template = atomic_dg("*top*");
	for(i=0;i<256;i++)
	{
		reentrancies[i] = 0;
		rnames[i] = 0;
	}
	for(i=0;i<R->ncorefs;i++)
	{
		for(j=0;j<nr;j++)
			if(!strcmp(rnames[j], R->corefs[i].name))break;
		if(j<nr)dg  = reentrancies[j];
		else
		{
			dg = reentrancies[j] = atomic_dg("*top*");
			rnames[j] = R->corefs[i].name;
			nr++;
		}
		/*printf("coref %d: %s = ", i, R->corefs[i].name);
		for(j=0;j<R->corefs[i].pathlen;j++)printf("%d ", R->corefs[i].path[j]);
		printf("\n");*/

		add_dg_path(coref_template, R->corefs[i].path, R->corefs[i].pathlen, dg);
	}
	//printf("coref template:\n"); print_dg(coref_template); printf("\n");
	if(unify_dg_infrom(target, coref_template))
	{
		//printf("patch failed\n");
		return -1;
	}
	else
	{
		//printf("patch succeeded\n");
		return 0;
	}
}

static struct dg	*rdagify(struct dagify_record	*R, char	*str, struct type	*building_type);
static struct dg	*dlistify(struct dagify_record	*R, char	*str);

static struct dg	*listify(struct dagify_record	*R, char	*str, struct dg	**last)
{
	int	len = strlen(str), bracket, iplen = R->current_pathlen;
	char		ch, *item, *comma;
	struct dg	*dg, *lp, *nlp, *idg;
	int		inquote = 0;

	if(str[1]=='!' && !last)return dlistify(R, str);

	if(str[len-1] != '>')
	{
		fprintf(stderr, "tdl: lists syntax is < ... >, not `%s'\n", str);
		return 0;
	}
	str[len-1] = 0;
	str = trim_string(str+1);

	if(*str=='!')
	{
		len = strlen(str);
		if(!last || str[len-1]!='!')
		{
			fprintf(stderr, "tdl: diff-list syntax is <! ... !>\n");
			return 0;
		}
		// chop '!'s out, we already know it's a difflist.
		str[len-1] = 0;
		str = trim_string(str+1);
	}
	//printf("listify (%p) `%s'\n", last, str);

	if(*str==0)
	{
		lp = type_dg(last?"*list*":"*null*");
		if(last)*last = lp;
		return lp;
	}
	if(!strcmp(str, "..."))
	{
		lp = type_dg("*list*");
		if(last)*last = lp;
		return lp;
	}

	lp = dg = type_dg("*cons*");
	if(!dg)return 0;

	while(*str)
	{
		comma = str;
		bracket=0;
		while(*comma && (bracket || inquote || (*comma!=',' && *comma!='.')))switch(*comma++)
		{
			case	'<':	case	'[':	bracket++;	break;
			case	'>':	case	']':	bracket--;	break;
			case	'\\':	if(inquote && *comma)comma++;	break;	// skip a char
			case	'"':	inquote = !inquote;		break;
		}
		if(bracket)
		{
			fprintf(stderr, "tdl: unmatched bracket in list near `%s'\n", str);
			R->current_pathlen = iplen;
			return 0;
		}

		if(*comma)
		{
			ch = *comma;
			*comma = 0;
			item = trim_string(str);
			str = trim_string(comma+1);
			if(ch == '.')
			{
				if(last) fprintf(stderr, "tdl: WARNING -- '.' list cons notation used in a diff-list!\n");
				R->current_path[R->current_pathlen++] = 2;	// REST
				nlp = rdagify(R, str, 0);
				R->current_pathlen--;
				if(!nlp)return 0;
				*str = 0;	// make sure we break
			}
			else if(!strcmp(str, "..."))
			{
				*str = 0;
				nlp = type_dg("*list*");
			}
			else nlp = type_dg("*cons*");
		}
		else
		{
			item = str;
			str = item+strlen(item);
			if(last)nlp = type_dg("*list*");	// dlists need to be open-ended
			else nlp = type_dg("*null*");
		}

		//printf("list item `%s'\n", item);
		R->current_path[R->current_pathlen++] = 1;	// FIRST
		idg = rdagify(R, item, 0);
		R->current_pathlen--;
		if(!idg)
		{
			R->current_pathlen = iplen;
			return 0;
		}
		add_dg_feature(lp, "FIRST", idg);
		add_dg_feature(lp, "REST", nlp);
		lp = nlp;
		R->current_path[R->current_pathlen++] = 2;	// REST
	}
	R->current_pathlen = iplen;
	if(last)*last = lp;

	return dg;
}

static struct dg	*dlistify(struct dagify_record	*R, char	*str)
{
	struct dg	*list, *last, *dg;

	R->current_path[R->current_pathlen++] = lookup_fname("LIST");
	list = listify(R, str, &last);
	R->current_pathlen--;
	if(!list)return 0;

	dg = type_dg("*diff-list*");
	add_dg_feature(dg, "LIST", list);
	add_dg_feature(dg, "LAST", last);
	return dg;
}

static struct dg	*structify(struct dagify_record	*R, char	*str)
{
	char	*ptr, *dot;
	char	*label, *vptr;
	struct dg	*dg, *value;
	int	bracket, len, depth;

	str = trim_string(str);

	if(*str == '#')
	{
		if(strchr(str, ' ') || strchr(str, '\t'))
		{
			fprintf(stderr, "tdl: tag `%s' may not have whitespace in it!\n", str);
			return 0;
		}
		coreference(R, str+1);
		return atomic_dg("*top*");
	}

	if(*str == '<')
	{
		dg = listify(R, str, 0);
		if(!dg)fprintf(stderr, "hint: listify failed\n");
		return dg;
	}
	if(*str != '[')
	{
		dg = type_dg(str);
		if(!dg)fprintf(stderr, "hint: type_dg(%s) failed\n", str);
		return dg;
	}

	dg = atomic_dg("*top*");

	ptr = trim_string(str+1);
	len = strlen(ptr);
	if(ptr[len-1] != ']')
	{
		fprintf(stderr, "tdl: unbalanced '[' in dag string `%s'\n", str);
		return 0;
	}
	ptr[--len] = 0;
	trim_string(ptr);

	while(*ptr)
	{
		// handle one feature
		label = ptr;
		while(*ptr && (*ptr!=' ' && *ptr!='\t'))ptr++;
		if(!*ptr)
		{
			novalue:
			fprintf(stderr, "tdl: label `%s' had no value\n", label);
			return 0;
		}
		*ptr = 0;
		ptr = trim_string(ptr+1);
		if(!*ptr)goto novalue;
		// find the length of its value
		vptr = ptr;
		bracket = 0;
		while(*ptr && (bracket || *ptr!=','))switch(*ptr++)
		{
			case	'<':	case	'[':	bracket++;	break;
			case	'>':	case	']':	bracket--;	break;
		}
		if(bracket)
		{
			fprintf(stderr, "tdl: unbalanced '[' in dag value `%s'\n", vptr);
			return 0;
		}
		*ptr = 0;
		ptr = trim_string(ptr+1);

		//printf("label %s, value %s\n", label, vptr);
		depth = 0;
		do
		{
			dot = strchr(label, '.');
			if(dot)*dot = 0;
			R->current_path[R->current_pathlen++] = lookup_fname(trim_string(label));
			depth++;
			label = dot+1;
		} while(dot);
		value = rdagify(R, vptr, 0);
		R->current_pathlen-=depth;
		if(!value)
		{
			fprintf(stderr, "hint: structify -> rdagify failed\n");
			return 0;
		}
		add_dg_path(dg, R->current_path+R->current_pathlen, depth, value);
	}

	return dg;
}

static struct dg	*rdagify(struct dagify_record	*R, char	*str, struct type	*building_type)
{
	int	bracket = 0, inquote = 0;
	char	*ptr;
	struct dg	*lhs, *rhs, *res;

	str = trim_string(str);
	if(!*str)
	{
		fprintf(stderr, "tdl: expected a dag definition\n");
		return 0;
	}
	ptr = str;
	while(*ptr && (bracket>0 || inquote || *ptr!='&'))switch(*ptr++)
	{
		case	'<': case	'[':	bracket++;	break;
		case	'>': case	']':	bracket--;	break;
		case	'\\': if(*ptr)ptr++;	break;	// skip a char
		case	'"': inquote = !inquote;		break;
	}
	if(*ptr=='&')
	{
		// ptr = first '&' in the dag
		*ptr = 0;
		lhs = structify(R, str);
		if(!lhs)return 0;
		rhs = rdagify(R, ptr+1, building_type);
		if(!rhs)return 0;
		if(building_type)
		{
			lhs->type = rhs->type = building_type;
			lhs->xtype = rhs->xtype = building_type;
		}
		if(loud_noises)
		{
			if(building_type)printf("building %s!\n", building_type->name);
			printf("lhs: "); print_dg(lhs); printf("\n");
			printf("rhs: "); print_dg(rhs); printf("\n");
		}
		res = unify_dg(lhs, rhs, -1);
		if(!res)
		{
			fprintf(stderr, "tdl: requested unification failed\n");
			unify_backtrace();
			printf("lhs: "); print_dg(lhs); printf("\n");
			printf("rhs: "); print_dg(rhs); printf("\n");
			return 0;
		}
		else if(loud_noises)
		{
			printf("res: "); print_dg(res); printf("\n");
		}
		return res;
	}
	else return structify(R, str);
}

struct dg	*dagify(char	*str, struct type	*building_type)
{
	struct dg	*dg;
	int	i;
	struct dagify_record	R;

	R.ncorefs=0;
	R.corefs = 0;
	R.current_pathlen = 0;

	dg = rdagify(&R, str, building_type);
	if(!dg)
	{
		return 0;
	}
	//printf(" pre-coref: "); print_dg(dg); printf("\n");

	if(patch_coreferences(&R, dg))
	{
		fprintf(stderr, "dagify: failed to patch in coreferences\n");
		unify_backtrace();
		dg = 0;
	}

	for(i=0;i<R.ncorefs;i++)
	{
		if(R.corefs[i].path)
			free(R.corefs[i].path);
		free(R.corefs[i].name);
	}
	if(R.corefs)free(R.corefs);

	return dg;
}

int	lexify_tdl_line(char	*lhs, char	*rhs, void	*ref)
{
	struct dg	*dg;
	char	*type, *amp, *dag;
	struct type	*ty;
	extern int	study_lexeme(char	*name, struct dg	*dg, struct type	*t);

	amp = strchr(rhs, '&');
	if(!amp) { fprintf(stderr, "lexeme `%s': no & found\n", lhs); return -1; }
	*amp = 0;
	dag = trim_string(amp+1);
	type = trim_string(rhs);

	dg = dagify(dag, 0);
	if(!dg) { fprintf(stderr, "hint: dagify failed\n"); return -1; }

	ty = lookup_type(type);
	if(!ty) { fprintf(stderr, "lexeme `%s' has nonextant type `%s'\n", lhs, type); return -1; }
	return study_lexeme(lhs, dg, ty);
}

int	dagify_tdl_line(char	*lhs, char	*rhs, void	*ref)
{
	struct dg	*dg;
	struct type	*bt = 0;
	int		(*callback)(char	*name, struct dg	*dg) = ref;

	if(enable_out_of_order)
	{
		bt = lookup_type(lhs);
		if(!bt)
		{
			fprintf(stderr, "tdl: no such type to constrain `%s'\n", lhs);
			return -1;
		}
	}

	//if(!strcmp(bt->name, "prep_nomod_phr_synsem"))loud_noises = 1;
	//else loud_noises = 0;

	//printf("constrain `%s' by `%s'\n", lhs, rhs);

	dg = dagify(rhs, bt);
	if(!dg) { fprintf(stderr, "hint: dagify failed\n"); return -1; }

	return callback(lhs, dg);
}

int	typify_tdl_line(char	*lhs, char	*rhs, void	*ref)
{
	char		*p, *chunk = rhs;
	struct type	**parents = 0;
	int		nparents = 0, done = 0;
	int		depth = 0, quote = 0;
	int		res;

	for(p=rhs;!done;p++)
	{
		if((*p=='&' && depth==0 && quote==0) || *p==0)
		{
			if(depth!=0 || quote!=0)
			{
				fprintf(stderr, "unbalanced brackets or quotes!\n");
				return -1;
			}
			if(!*p)done = 1;
			*p = 0;
			if(!(*chunk=='<' || *chunk=='#' || *chunk=='['))
			{
				nparents++;
				parents = realloc(parents, sizeof(struct type*)*nparents);
				parents[nparents-1] = find_type(trim_string(chunk), 0);
				if(!parents[nparents-1])
				{
					free(parents);
					return -1;
				}
			}
			if(!done)chunk = trim_string(p+1);
		}
		else if(!quote && (*p == '[' || *p=='<'))depth++;
		else if(!quote && (*p == ']' || *p=='>'))depth--;
		else if(*p == '"')quote = !quote;
		else if(*p == '\\' && p[1])p++;
	}
	res = add_type_only(lhs, nparents, parents);
	if(parents)free(parents);
	return res;
}

int	iterate_tdl_dgs(char	*path, int	(*callback)(char	*rhs, struct dg	*dg))
{
	if(callback == constrain_type)enable_out_of_order = 1;
	else enable_out_of_order = 0;

	return iterate_tdl(path, dagify_tdl_line, callback);
}

int	iterate_tdl_types(char	*path)
{
	enable_out_of_order = 1;

	return iterate_tdl(path, typify_tdl_line, 0);
}

int	iterate_tdl_lex(char	*path)
{
	enable_out_of_order = 0;

	return iterate_tdl(path, lexify_tdl_line, 0);
}

int	g_loaded = 0;
extern int	ntypes, nlexemes, nrules, nirules, nroots, glb_type_count, debug_level, nfnames, nstrings;

int	load_grammar()
{
	clear_slab();

	init_fnames();
	if(load_types())exit(-1);
	if(load_rules())exit(-1);
	if(load_roots())exit(-1);
	if(load_inflr())exit(-1);
	if(load_lexicon())exit(-1);
	number_strings();
	setup_glb_lut();
	g_loaded = 1;	// make unify shut up about failures
	setup_rule_filter();
	g_loaded = 0;

	if(debug_level)
		fprintf(stderr, "%d types (%d glb), %d lexemes, %d rules, %d irules, %d roots, %d strings, %d features\n",
				ntypes, glb_type_count, nlexemes, nrules, nirules, nroots, nstrings, nfnames);

	// make sure none of the grammar is auto-freed when parse is over
	commit_slab();
	return 0;
}
