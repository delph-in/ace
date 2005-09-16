#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<assert.h>
#include	<regex.h>
#include	"dag.h"
#include	"freeze.h"

enum
{
	rtReplace = 1,
	rtErsatz = 2,
};

struct pprule
{
	int		type;
	char	*pat, *rep;
	regex_t	re;
};

static int					npprules;
static struct pprule		*pprules;
static regex_t				delim;
static char	*delim_pat = "[ \t]+";

struct preprocessor
{
	int				npprules;
	struct pprule	*pprules;
	char			*delim_pat;
};

void	*freeze_fspp()
{
	int	i;
	struct preprocessor	*fspp = slab_alloc(sizeof(struct preprocessor));
	fspp->pprules = freeze_block(pprules, sizeof(struct pprule)*npprules);
	for(i=0;i<npprules;i++)
	{
		fspp->pprules[i].pat = freeze_string(fspp->pprules[i].pat);
		fspp->pprules[i].rep = freeze_string(fspp->pprules[i].rep);
	}
	fspp->npprules = npprules;
	fspp->delim_pat = freeze_string(delim_pat);
	return fspp;
}

void	recover_fspp(void	*ptr)
{
	struct preprocessor	*fspp = ptr;
	delim_pat = fspp->delim_pat;
	pprules = fspp->pprules;
	npprules = fspp->npprules;
	parse_pprules();
}

static char	*fixup_pattern(char	*str)
{
	char	*out = strdup(str);
	int		i, bs, be, f = 0;

	for(i=0;out[i];i++)
	{
		if(out[i]=='\\'){i++; continue;}
		if(out[i]=='[')
		{
			bs = i+1;
			for(;out[i];i++)
				if(out[i]=='\\')
					memmove(out+i, out+i+1, strlen(out+i+1)+1);
				else if(out[i]==']')break;
			be = i-1;
			for(i=bs;i<=be;i++)
				if(out[i]==']')break;
			if(i<=be)
			{
				// found a ']' in the block that needs to be moved to the front.
				out[i] = out[bs];
				out[bs] = ']';
				f=1;
			}
			for(i=bs;i<=be;i++)
				if(out[i]=='[' && strchr(":.=", out[i+1]))break;
			if(i<=be)
			{
				// found a '[:' or `[=' or `[.' in the block that needs to be twiddled.
				out[i] = out[i+1];
				out[i+1] = '[';
				f=1;
			}
		}
	}
	//if(f)printf("fixed pattern: `%s'\n", out);
	return out;
}

static char	*unescape(char	*str)
{
	char	*out = strdup(str), *p = out, *q = str;

	while(*q)
	{
		if(*q=='\\' && q[1])
		{
			q+=2;
			switch(q[-1])
			{
				case	't':	*p++ = '\t';	break;
				case	'n':	*p++ = '\n';	break;
				case	'r':	*p++ = '\r';	break;
				case	'0':	*p++ = '\0';	break;
				default:		*p++ = q[-1];	break;
			}
		}
		else *p++ = *q++;
	}
	*p = 0;
	return out;
}

print_pprules()
{
	int	i;

	for(i=0;i<npprules;i++)
		fprintf(stderr, "%s:  pat=`%s', rep=`%s'\n", pprules[i].type==rtReplace?"repl":"ersz", pprules[i].pat, pprules[i].rep);
}

parse_pprules()
{
	int		i;

	for(i=0;i<npprules;i++)
		regcomp(&pprules[i].re, pprules[i].pat, REG_EXTENDED);
	regcomp(&delim, delim_pat, REG_EXTENDED);
}

int	load_fsr_preprocessor(char	*fname)
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
			case	':':	delim_pat = unescape(pattern);	continue;
			case	'<':
				sprintf(subpath, "%s/%s", basepath, pattern);
				load_fsr_preprocessor(subpath);
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
		pprules = realloc(pprules, sizeof(struct pprule) * npprules);
		pprules[npprules-1].type = type;
		pprules[npprules-1].pat = fixup_pattern(pattern);
		pprules[npprules-1].rep = strdup(rep);
	}
	fclose(f);
	return 0;
}

static char	*replace(char	*orig, int olen, regmatch_t	*m, char	*r, int	fr)
{
	char	*out, *p;

	out = malloc(olen + strlen(r) + 1);
	memcpy(out, orig, m->rm_so);
	p = out + m->rm_so;

	while(*r)
	{
		if(*r == '\\' && r[1])
		{
			int		i = r[1] - '1';
			char	*x = orig + m[1 + i].rm_so, *e = orig + m[1 + i].rm_eo;
			while(x<e)*p++ = *x++;
			r += 2;
		}
		else *p++ = *r++;
	}

	memcpy(p, orig+m->rm_eo, olen - m->rm_eo);
	p[olen - m->rm_eo] = 0;

	if(fr)free(orig);
	return out;
}

static void	preprocess_word(char	*word, int	len, char	**lemma, char	**form)
{
	int			i;
	char		*out;
	regmatch_t	m[8];

	out = malloc(len+1);
	memcpy(out, word, len);
	out[len] = 0;
	*lemma = out;

	for(i=0;i<npprules;i++)if(pprules[i].type == rtErsatz)
	{
		if(!regexec(&pprules[i].re, out, 8, m, 0) && m->rm_so==0 && m->rm_eo==len)
		{
			out = replace(word, len, m, pprules[i].rep, 0);
			*form = out;
			return;
		}
	}
	*form = strdup(out);
}

void	preprocess_line(char	*string, int	*Nwords, char	***Lemmas, char	***Forms)
{
	int	i, nwords = 0;
	char *s = string, *p;
	char	**lemmas = 0, **forms = 0;
	regmatch_t	b;

	// apply string-level replacements
	for(i=0;i<npprules;i++) if(pprules[i].type == rtReplace)
	{
		struct pprule	*r = &pprules[i];
		int				redo = strlen(s);
		regmatch_t	m[r->re.re_nsub+1];
		again:
		m->rm_so = strlen(s) - redo;
		m->rm_eo = strlen(s);
		if(!regexec(&r->re, s, r->re.re_nsub+1, m, REG_STARTEND | (m->rm_so!=0 ? REG_NOTBOL : 0)))
		{
			redo = strlen(s) - m->rm_eo;
			s = replace(s, strlen(s), m, r->rep, s!=string);
			//printf(" `%s' -> `%s' yields `%s'\n", r->pat, r->rep, s);
			if(redo)goto again;
		}
	}

	// tokenize
	p = s;
	while(!regexec(&delim, p, 1, &b, 0))
	{
		if(b.rm_so!=0)
		{
			nwords++;
			lemmas = realloc(lemmas, sizeof(char*)*nwords);
			forms = realloc(forms, sizeof(char*)*nwords);
			preprocess_word(p, (int)b.rm_so, &lemmas[nwords-1], &forms[nwords-1]);
			//printf("lemma `%-16s' form `%16s'\n", lemmas[nwords-1], forms[nwords-1]);
		}
		p += b.rm_eo;
	}
	if(Nwords)*Nwords = nwords;
	if(Lemmas)*Lemmas = lemmas;
	else if(nwords) { for(i=0;i<nwords;i++)free(lemmas[i]); free(lemmas); }
	if(Forms)*Forms = forms;
	else if(nwords) { for(i=0;i<nwords;i++)free(forms[i]); free(forms); }
}
