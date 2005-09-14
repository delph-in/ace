#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<assert.h>

#include	"freeze.h"
#include	"dag.h"

#include	"preprocessor.h"

struct subnfa
{
	int	in, out;
};

struct nfastate
{
	int	trans[256];
	int	eps[256];
	int	neps;
	int	copy;
	int	beginsave, endsave;
	int	accept;
};

static int	newstate();
static inline void	transition(int	from, int	to, int	on);
static inline void	etransition(int	from, int	to);
static struct subnfa	copy_subnfa(struct subnfa	sub);

static int		nnfa, nfastart;
static struct nfastate	*nfa;

static struct subnfa	parse_rule(struct fspp_rule	*r);
static struct subnfa	parse_pattern(char	*pat);
static struct subnfa	parse_charset(char	*str);
static struct subnfa	atom_nfa(char	ch);
static struct subnfa	anyletter_nfa();
static struct subnfa	concat_nfa(struct subnfa	a, struct subnfa	b);
static struct subnfa	repeat_term(struct subnfa	term, int	min, int	max);
static struct subnfa	memorize_term(struct subnfa	term);

struct dfastate		*dfa;
int					ndfa, ersatz_in = -1, ersatz_dfa;
int					npprules;
struct 	fspp_rule	*pprules;

#define	DHTSIZE	10239
static int		dfahashtable[DHTSIZE];

static char	*delim_pat = "[ \\t]";

static int	process(FILE	*f);
static char	*escdup(char	*str);

struct fspp
{
	struct dfastate		*dfa;
	struct fspp_rule	*pprules;
	int					ndfa, npprules, ersatz_dfa;
};

void	*freeze_fspp()
{
	struct fspp	*fspp = slab_alloc(sizeof(*fspp));
	int			i;
	fspp->dfa = freeze_block(dfa, sizeof(*dfa)*ndfa);
	for(i=0;i<ndfa;i++)
	{
		fspp->dfa[i].set = 0;
		fspp->dfa[i].nns = 0;
	}
	fspp->pprules = freeze_block(pprules, sizeof(*pprules)*npprules);
	for(i=0;i<npprules;i++)
	{
		fspp->pprules[i].pat = freeze_string(fspp->pprules[i].pat);
		fspp->pprules[i].rep = freeze_string(fspp->pprules[i].rep);
	}
	fspp->ndfa = ndfa;
	fspp->npprules = npprules;
	fspp->ersatz_dfa = ersatz_dfa;
	return fspp;
}

void	recover_fspp(void	*ptr)
{
	struct fspp	*fspp = ptr;
	dfa = fspp->dfa;
	ndfa = fspp->ndfa;
	pprules = fspp->pprules;
	npprules = fspp->npprules;
	ersatz_dfa = fspp->ersatz_dfa;
}

void load_fsr_preprocessor(char	*path)
{
	int i;

	load_pprules(path);
	//print_pprules();

	int task()
	{
		nnfa = ndfa = 0; ersatz_in = -1; nfastart = 0; ersatz_dfa = 0; bzero(dfahashtable, sizeof(int)*DHTSIZE);
		parse_pprules();
		return 0;
	}
	show_task_name("compiling preprocessor"); task();

	fprintf(stderr, "nfa: %d, dfa: %d\n", nnfa, ndfa);
	//print_nfa();
	//print_dfa();

//	save_preprocessor();
}

load_pprules(char	*fname)
{
	FILE	*f = fopen(fname, "r");
	char	basepath[1024], subpath[1024];
	char	line[1024], typec, pattern[1024], rep[1024], *p, *s;
	int	escape, type;

	if(!f)
	{
		perror(fname);
		exit(-1);
	}

	strcpy(basepath, fname);
	p = strrchr(basepath, '/');
	if(!p)strcpy(basepath, ".");
	else *p = 0;

	while(fgets(line, 1024, f))
	{
		if(!*line)continue;
		if(line[0]==';')continue;
		if(line[0]=='\n')continue;
		line[strlen(line)-1] = 0;
		escape = 0;

		typec = line[0];
		p = line+1;
		s = pattern;
		while(*p && *p != '\t')*s++ = *p++;
		*s = 0;

		switch(typec)
		{
			case	'@':	/*fprintf(stderr, "date: %s\n", pattern);*/	continue;
			case	':':	delim_pat = strdup(pattern);	continue;
			case	'<':
				sprintf(subpath, "%s/%s", basepath, pattern);
				load_pprules(subpath);
				continue;
			case	'!':	type = rtReplace;	break;
			case	'^':	type = rtErsatz;	break;
			default:	fprintf(stderr, "format error: unknown type `%c'.\n", typec); continue; //exit(-1);
		}

		if(*p != '\t' || escape)
		{
			fprintf(stderr, "format error: type `%c' needs two fields (e=%d).\n", typec, escape);
			exit(-1);
		}

		while(*p=='\t')p++;
		strcpy(rep, p);

		npprules++;
		pprules = realloc(pprules, sizeof(struct fspp_rule) * npprules);
		pprules[npprules-1].type = type;
		pprules[npprules-1].pat = strdup(pattern);
		pprules[npprules-1].rep = strdup(rep);
	}
	fclose(f);
}

print_pprules()
{
	int	i;

	for(i=0;i<npprules;i++)
		fprintf(stderr, "%s:  pat=`%s', rep=`%s'\n", pprules[i].type==rtReplace?"repl":"ersz", pprules[i].pat, pprules[i].rep);
}

int	mem_id_gen;

parse_pprules()
{
	int		i;
	struct subnfa	rnfa;

	//nfastart = newstate();
	for(i=0;i<npprules;i++)
	{
		mem_id_gen = 1;
		rnfa = parse_rule(pprules+i);
		nfa[rnfa.out].accept = i;
		if(pprules[i].type==rtErsatz)
		{
			if(ersatz_in ==-1)ersatz_in = newstate();
			etransition(ersatz_in, rnfa.in);
		}
		pprules[i].dfastart = subset_construction(rnfa.in);
		pprules[i].nsave = mem_id_gen-1;
	}
	ersatz_dfa = subset_construction(ersatz_in);
}

struct subnfa parse_rule(struct fspp_rule	*r)
{
	struct subnfa	nfa;
	int		so = 0, eo = 0;
	char	*patuse = strdup(r->pat);

	if(patuse[0]=='^')
	{
		//printf("start-only\n");
		so=1;
		patuse++;
		if(!*patuse)
		{
			fprintf(stderr, "malformed regex `^'\n");
			exit(-1);
		}
	}
	if(patuse[0] && patuse[strlen(patuse)-1]=='$' && (!patuse[1] || patuse[strlen(patuse)-2]!='\\'))
	{
		//printf("end-only\n");
		eo=1;
		patuse[strlen(patuse)-1] = 0;
		if(!*patuse)
		{
			fprintf(stderr, "malformed regex '$'\n");
			exit(-1);
		}
	}
	//fprintf(stderr, "try constructing `%s'... ", patuse);
	nfa = parse_pattern(patuse);
	//fprintf(stderr, "[%d, %d]\n", nfa.in, nfa.out);
	return nfa;
}

#define	COMPOUND()	if(haveterm) do { etransition(link, term.in); link = term.out; haveterm = 0; } while(0)

struct subnfa	parse_pattern(char	*pat)
{
	char	esc,*p,*q,deep,set,haveterm = 0;
	int		link;
	struct subnfa	built = {-1,-1};
	struct subnfa	term = {-1,-1};

	link = built.in = newstate();

	while(*pat)
	{
		switch(*pat)
		{
			// terms
			case	'(':
				COMPOUND();
				for(set=0,esc=0,deep=1,p=pat+1;*p;p++)
				{
					if(!set&&!esc&&*p=='(')deep++;
					if(!set&&!esc&&*p==')'){deep--;if(!deep)break;}
					if(!esc&&*p=='[')set=1;
					if(!esc&&set&&*p==']')set=0;
					esc = (*p=='\\');
				}
				if(!*p)
				{
					fprintf(stderr, "unbalanced paren `%s'\n", pat);
					exit(-1);
				}
				*p = 0;
				pat++;
				//printf("subex `%s'\n", pat);
				term = parse_pattern(pat);
				term = memorize_term(term);
				haveterm = 1;
				pat = p+1;
				break;
			case	'[':
				COMPOUND();
				for(esc=0,p=pat+1;*p&&(*p!=']'||esc);p++)
					esc = (*p=='\\');
				if(!*p)
				{
					fprintf(stderr, "unbalanced bracket `%s'\n", pat);
					exit(-1);
				}
				*p = 0;
				pat++;
				term = parse_charset(pat);
				haveterm = 1;
				pat = p+1;
				break;
			case	'\\':
				pat++;
				if(!*pat) { fprintf(stderr, "`\\' escape requires a following character!\n"); exit(-1); }
				COMPOUND();
				term = atom_nfa(*pat);
				haveterm = 1;
				pat++;
				break;
			case	'.':
				COMPOUND();
				term = anyletter_nfa();
				haveterm = 1;
				pat++;
				break;
			default:
				COMPOUND();
				term = atom_nfa(*pat);
				haveterm = 1;
				pat++;
				break;

			// modifiers
			case	'?':
				if(!haveterm){ fprintf(stderr, "`?' modifier requires a term to operate on!\n"); exit(-1); }
				term = repeat_term(term, 0, 1);
				pat++;
				break;
			case	'+':
				if(!haveterm){ fprintf(stderr, "`+' modifier requires a term to operate on!\n"); exit(-1); }
				term = repeat_term(term, 1, -1);
				pat++;
				break;
			case	'*':
				if(!haveterm){ fprintf(stderr, "`*' modifier requires a term to operate on!"); exit(-1); }
				term = repeat_term(term, 0, -1);
				pat++;
				break;
			case	'{':
				p = strchr(pat, '}');
				if(!p) { fprintf(stderr, "{} numbered repeat missing closing }: `%s'\n", pat); exit(-1); }
				*p = 0;
				q = strchr(pat, ',');
				if(q)
				{
					*q = 0;
					if(q[1] && pat[1])
						term = repeat_term(term, atoi(pat+1), atoi(q+1));
					else if(q[1])term = repeat_term(term, 0, atoi(q+1));
					else if(pat[1])term = repeat_term(term, atoi(pat+1), -1);
					else { fprintf(stderr, "{,} syntax not allowed\n"); exit(-1); }
				}
				else term = repeat_term(term, atoi(pat+1), atoi(pat+1));
				pat = p+1;
				break;
		}
		
	}
	COMPOUND();
	built.out = link;
	return built;
}

static inline void	transition(int	from, int	to, int	on)
{
	struct nfastate	*fs;

	fs = nfa+from;
	if(from<0||from>=nnfa||to<0||to>=nnfa)
	{
		fprintf(stderr, "transition: one of states %d,%d doesn't exist\n", from, to);
		exit(-1);
	}
	if(on<0 || on>=256)
	{
		fprintf(stderr, "transition on %d impossible\n", on);
		exit(-1);
	}
	fs->trans[on] = to;
}

static inline void	etransition(int	from, int	to)
{
	struct nfastate	*fs;

	fs = nfa+from;
	if(from<0||from>=nnfa||to<0||to>=nnfa)
	{
		fprintf(stderr, "etransition: one of states %d,%d doesn't exist\n", from, to);
		exit(-1);
	}
	if(fs->neps>=256)
	{
		fprintf(stderr, "state %d already has too many epsilon transitioN!\n", from);
		exit(-1);
	}
	fs->eps[fs->neps++] = to;
}

struct subnfa	parse_charset(char	*str)
{
	struct subnfa	built = {-1,-1};
	int		en[256], esc = 0, invert = 0, ch, rangefrom = -1;
	char		*p = str;

	built.in = newstate();
	built.out = newstate();
	for(esc=1;esc<256;esc++)en[esc] = 0;
	if(*p == '^'){invert = 1;p++;}
	esc = 0;
	while(*p)
	{
		if(!esc)
		{
			if(*p=='\\')esc = 1, ch = -1;
			else ch = *p;
		}
		else
		{
			esc = 0;
			switch(*p)
			{
				case	't':	ch = '\t';	break;
				case	'n':	ch = '\n';	break;
				case	'r':	ch = '\r';	break;
				case	'[':	ch = '[';	break;
				case	']':	ch = ']';	break;
				default:	fprintf(stderr, "charset: unknown escape `%c'\n", *p);	exit(-1);
			}
		}
		if(ch>=0)
		{
			if(p[1]=='-' && p[2]){rangefrom = ch;p++;}
			else if(rangefrom!=-1)
			{
				//printf("range %c - %c = %d - %d\n", rangefrom, ch, rangefrom, ch);
				for(esc=rangefrom;esc<=ch;esc++)
					en[esc] = 1;
				rangefrom = -1;
				esc = 0;
			}
			else en[ch] = 1;
		}
		p++;
	}
	if(invert) for(esc=1;esc<256;esc++)en[esc] = !en[esc];
	for(esc=1;esc<256;esc++) if(en[esc])
		transition(built.in, built.out, esc);
	return built;
}

struct subnfa	repeat_term(struct subnfa	term, int	min, int	max)
{
	struct subnfa	built = {-1,-1};
	struct subnfa	copy;
	int		i, link;

	//fprintf(stderr, "repeat [%d,%d] for {%d,%d}\n", term.in, term.out, min, max);
	link = built.in = newstate();
	built.out = newstate();
	if(min==0)etransition(built.in, built.out);
	if(max==0)return built;

	if(max<min && max!=-1)
	{
		fprintf(stderr, "repetition: max %d < min %d!\n", max, min);
		exit(-1);
	}

	for(i=0;i<min-1;i++)
	{
		copy = copy_subnfa(term);
		etransition(link, copy.in);
		link = copy.out;
	}

	if(max==-1)
	{
		etransition(link, term.in);
		etransition(term.out, built.out);
		etransition(built.out, term.in);
		return built;
	}

	if(min-1 > 0) { max -= min-1; min = 1; }

	for(i=0;i<max-1;i++)
	{
		copy = copy_subnfa(term);
		etransition(link, copy.in);
		etransition(copy.out, built.out);
		link = copy.out;
	}

	etransition(link, term.in);
	etransition(term.out, built.out);
	return built;
}

struct subnfa	anyletter_nfa()
{
	struct subnfa	built;
	int		i;

	built.in = newstate();
	built.out = newstate();
	for(i=1;i<126;i++)if(i!='\n' && i!='\r')
		transition(built.in, built.out, i);
	return built;
}

struct subnfa	atom_nfa(char	ch)
{
	struct subnfa	built;

	built.in = newstate();
	built.out = newstate();
	transition(built.in, built.out, (unsigned char)ch);
	return built;
}

struct subnfa	concat_nfa(struct subnfa	a, struct subnfa	b)
{
	struct subnfa	built;

	built.in = a.in;
	built.out = b.out;
	etransition(a.out, b.in);
	return built;
}

int	newstate()
{
	int	i;

	nnfa++;
	nfa = realloc(nfa, sizeof(struct nfastate)*nnfa);
	bzero(&nfa[nnfa-1], sizeof(struct nfastate));
	for(i=0;i<256;i++)nfa[nnfa-1].trans[i] = -1;
	nfa[nnfa-1].accept = -1;
	return nnfa-1;
}

int	rcopy_nfa(int	node)
{
	struct nfastate	*st = nfa+node;
	int		i, cp, xcp;

	if(node==-1)return -1;
	if(node<0 || node>=nnfa)
	{
		fprintf(stderr, "rcopy_nfa: no such node as %d\n", node);
		exit(-1);
	}

	if(st->copy>=0)return st->copy;
	cp = st->copy = nnfa;
	nnfa++;
	nfa = realloc(nfa, sizeof(struct nfastate)*nnfa);

	nfa[cp].copy = -1;
	nfa[cp].accept = -1;
	nfa[cp].neps = nfa[node].neps;
	nfa[cp].beginsave = 0;
	nfa[cp].endsave = 0;
	if(nfa[node].beginsave || nfa[node].endsave)
	{
		fprintf(stderr, "rcopy_nfa: attempt to copy a saving node!\n");
		exit(-1);
	}
	for(i=0;i<256;i++)
	{
		xcp = rcopy_nfa(nfa[node].trans[i]);
		nfa[cp].trans[i] = xcp;
	}
	for(i=0;i<nfa[cp].neps;i++)
	{
		xcp = rcopy_nfa(nfa[node].eps[i]);
		nfa[cp].eps[i] = xcp;
	}
	return cp;
}

struct subnfa	copy_subnfa(struct subnfa	sub)
{
	int		i;
	struct subnfa	cp;

	//fprintf(stderr, "copy: [%d,%d]\n", sub.in, sub.out);
	for(i=0;i<nnfa;i++)nfa[i].copy = -1;
	cp.in = rcopy_nfa(sub.in);
	cp.out = rcopy_nfa(sub.out);
	return cp;
}

struct subnfa	memorize_term(struct subnfa	term)
{
	struct subnfa	built;

	if(mem_id_gen>5) { fprintf(stderr, "only support memorization up to \\5!\n"); exit(-1); }
	built.in = newstate();
	built.out = newstate();
	nfa[built.in].beginsave = mem_id_gen;
	nfa[built.out].endsave = mem_id_gen++;
	etransition(built.in, term.in);
	etransition(term.out, built.out);

	return built;
}

int	subset_construction(int	nfastart)
{
	char	*set;
	int	nns;

	set = calloc(sizeof(char), nnfa);

	nns = dfaclose(set, nfastart);
	return dfanode(set, nns);
}

dfaclose(char	*set, int n)
{
	int	i, j, cnt = 0;
	struct nfastate	*s;

	set[n] = 1;
	cnt++;
	s = nfa+n;
	for(i=0;i<s->neps;i++)
		if(!set[s->eps[i]])
			cnt += dfaclose(set, s->eps[i]);
	return cnt;
}

unsigned int dfahash(char	*set)
{
	unsigned int	hash = 0;
	int				i;

	for(i=0;i<nnfa;i++)
		if(set[i])hash ^= 1 << (i%30);
	return hash;
}

int	dfanode(char	*set, int	nns)
{
	int	i, ch, dn, newnode, prev_dst = -2;
	unsigned int	hash = dfahash(set);
	char	*nset;
	int		nfa_nums[nns];

	// rollover hash table to find dfa nodes quickly
	for(i=hash;i<(hash+DHTSIZE);i++)
	{
		int I;
		I = dfahashtable[i%DHTSIZE]-1;
		if(I==-1)
		{
			dfahashtable[i%DHTSIZE] = ndfa+1;
			break;
		}
		if(nns==dfa[I].nns && hash==dfa[I].hash)
		{
			if(!memcmp(set, dfa[I].set, sizeof(char)*nnfa))
				return I;
		}
	}

	// make explicit list of nfa state numbers
	ch = 0;
	for(i=0;i<nnfa;i++)if(set[i])nfa_nums[ch++] = i;
	assert(ch == nns);

	dn = ndfa++;
	dfa = realloc(dfa, sizeof(struct dfastate)*ndfa);
	dfa[dn].hash = hash;
	dfa[dn].set = set;
	dfa[dn].nns = nns;
	dfa[dn].accept = -1;
	bzero(dfa[dn].start, 4);
	bzero(dfa[dn].end, 4);
	// see whether this DFA state is accepting
	for(i=0;i<nns;i++)
	{
		if(nfa[nfa_nums[i]].accept!=-1)
		{
			dfa[dn].accept = nfa[nfa_nums[i]].accept;
			break;
		}
	}
	for(i=0;i<nns;i++)
	{
		if(nfa[nfa_nums[i]].beginsave)
			dfa[dn].start[nfa[nfa_nums[i]].beginsave] = 1;
		if(nfa[nfa_nums[i]].endsave)
			dfa[dn].end[nfa[nfa_nums[i]].endsave] = 1;
	}

	for(ch=0;ch<256;ch++)
	{
		int tnns = 0;
		if(prev_dst != -2)
		{
			// check whether this transition is identical to the prev ch's
			for(i=0;i<nns;i++)
				if(nfa[nfa_nums[i]].trans[ch]!=nfa[nfa_nums[i]].trans[ch-1])
					break;
			if(i==nns)
			{
				dfa[dn].trans[ch] = prev_dst;
				continue;
			}
		}
		nset = calloc(sizeof(char), nnfa);
		for(i=0;i<nns;i++)
			if(nfa[nfa_nums[i]].trans[ch]!=-1)
				tnns += dfaclose(nset, nfa[nfa_nums[i]].trans[ch]);
		if(tnns)
		{
			newnode = dfanode(nset, tnns);
			dfa[dn].trans[ch] = newnode;
			/*fprintf(stderr, "set [%d] for trans %c: ", tnns, ch);
			for(i=0;i<nnfa;i++)if(nset[i])fprintf(stderr, "%d ", i);
			fprintf(stderr, "\n");
			fprintf(stderr, " node  %d\n", dfa[dn].trans[ch]);*/
		}
		else {free(nset);dfa[dn].trans[ch] = -1;}
		prev_dst = dfa[dn].trans[ch];
	}

	return dn;
}

print_nfa()
{
	int	i, j;

	for(i=0;i<nnfa;i++)
	{
		fprintf(stderr, "%%%d:	", i);
		for(j=0;j<256;j++)
			if(nfa[i].trans[j]!=-1)
				fprintf(stderr, "[%c=>%d] ", j, nfa[i].trans[j]);
		if(nfa[i].accept!=-1)fprintf(stderr, "ACC(%d) ", nfa[i].accept);
		for(j=0;j<nfa[i].neps;j++)
				fprintf(stderr, "{%d} ", nfa[i].eps[j]);
		fprintf(stderr, "\n");
	}
}

print_dfa()
{
	int	i, j;

	for(i=0;i<ndfa;i++)
	{
		fprintf(stderr, "#%d:	", i);
		for(j=0;j<256;j++)
			if(dfa[i].trans[j]!=-1)
				fprintf(stderr, "[%c=>%d] ", j, dfa[i].trans[j]);
		if(dfa[i].accept!=-1)fprintf(stderr, "ACC(%d) ", dfa[i].accept);
		for(j=0;j<10;j++)
		{
			if(dfa[i].start[j])
				fprintf(stderr, "S(%d) ", j);
			if(dfa[i].end[j])
				fprintf(stderr, "E(%d) ", j);
		}
		fprintf(stderr, "\n");
	}
}

save_preprocessor()
{
	int	i, j, k, to;

	printf("#include	\"preprocessor.h\"\n\n");
	printf("int	ndfa = %d;\n", ndfa);
	printf("static struct dfastate _dfa[%d] = {\n", ndfa);
	for(i=0;i<ndfa;i++)
	{
		printf("	{ { [0 ... 255] = -1");
		for(j=0;j<128;j++)
		{
			if(dfa[i].trans[j]!=-1)
			{
				to = dfa[i].trans[j];
				for(k=j+1;k<128;k++)if(dfa[i].trans[k]!=to)break;
				k--;
				if(k>j)printf(", [%d ... %d]=%d", j, k, to);
				else printf(", [%d]=%d", j, to);
				j = k;
			}
		}
		printf("}, %d, 0, 0, %d, {%d,%d,%d,%d}, {%d,%d,%d,%d} },\n",
			dfa[i].accept, dfa[i].hash, dfa[i].start[0], dfa[i].start[1], dfa[i].start[2], dfa[i].start[3],
			dfa[i].end[0], dfa[i].end[1], dfa[i].end[2], dfa[i].end[3]);
	}
	printf("		};\n");
	printf("struct dfastate	*dfa = _dfa;\n\n");

	printf("int	npprules = %d;\n", npprules);
	printf("static struct fspp_rule	_pprules[%d] = {\n", npprules);
	for(i=0;i<npprules;i++)
		printf("	{%d, \"%s\", \"%s\", %d, %d},\n",
				pprules[i].type, escdup(pprules[i].pat), escdup(pprules[i].rep),
				pprules[i].dfastart, pprules[i].nsave);
	printf("		};\n");
	printf("struct fspp_rule	*pprules = _pprules;\n");
	printf("int			ersatz_dfa = %d;\n", ersatz_dfa);
}

char	*escdup(char	*str)
{
	char	*p = malloc(strlen(str)*2);
	char	*copy = p;

	while(*str)
	{
		if(*str=='"' || *str=='\\')*p++ = '\\';
		*p++ = *str++;
	}
	*p++ = 0;
	return copy;
}
