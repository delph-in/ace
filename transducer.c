#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

#include	"preprocessor.h"

static char	*process_word(char	*win);
static int	process_string(char	*str, char	*out, struct fspp_rule	*r, int	full);

int	preprocess_line(char	*line, int	*nwordsP, char	***wordsP)
{
	char	out[10240];
	char	*wptr, **words = 0;
	int	i, j, nwords = 0;

	for(i=0;i<npprules;i++)
	{
		if(pprules[i].type != rtReplace)continue;
#ifndef JACY
		if(process_string(line, out, pprules+i, 0))
			strcpy(line, out);
#endif
	}
	//printf("line `%s'\n", line);
	strcat(line, " ");

	wptr = 0;
	for(j=0;line[j];j++)
	{
		if(line[j]==' ' || line[j]=='\t')
		{
			line[j]=0;
			if(wptr)
			{
				nwords++;
				words = realloc(words, sizeof(char*)*nwords);
#ifndef	JACY
				words[nwords-1] = process_word(wptr);
#else
				words[nwords-1] = strdup(wptr);
#endif
			}
			wptr = 0;
		}
		else if(!wptr) wptr = line+j;
	}
	//printf("\n");
	*nwordsP = nwords;
	*wordsP = words;
	return 0;
}


static char	*process_word(char	*win)
{
	int	i, state;
	char	*p;
	extern int ersatz_dfa;

	p = win;
	state = ersatz_dfa;
	while(state!=-1 && *p!=0)
		state = dfa[state].trans[(int)*p++];
	if(state!=-1 && *p==0 && dfa[state].accept!=-1)
	{
		i = dfa[state].accept;
		return strdup(pprules[i].rep);
	}
	return strdup(win);
}

static int	process_string(char	*str, char	*out, struct fspp_rule	*r, int	full)
{
	int	state, astate;
	char	*p, *ap;
	char	*sstart[4], *send[4];
	int	i, nacc = 0;

	while(*str)
	{
		//printf("applying dfa %d to `%s'\n", r->dfastart, str);
		p = str;
		state = r->dfastart;
		astate = -1;
		ap = 0;
		for(i=0;i<4;i++)sstart[i] = send[i] = 0;
		while(state!=-1)
		{
			if(dfa[state].accept!=-1 && p!=str)astate = state, ap = p;
			for(i=0;i<r->nsave;i++)
			{
				if(dfa[state].start[i+1] && !sstart[i])sstart[i] = p;
				if(dfa[state].end[i+1])send[i] = p;
			}
			state = dfa[state].trans[(int)*p++];
		}
		if(astate==-1)
			*out++ = *str++;
		else if(!full || !*ap)
		{
			// substring from str up to ap was accepted
			nacc++;
			str = ap;
			p = r->rep;
			while(*p)
			{
				if(*p=='\\' && (p[1]>='1' && p[1]<='4'))
				{
					p++;
					i = *p-'1';
					if(sstart[i] && send[i])
					{
						for(ap=sstart[i];ap<send[i];ap++)
							*out++ = *ap;
					}
					else { fprintf(stderr, "backref %d: sstart %p, send %p\n", i+1, sstart[i], send[i]); exit(-1); }
					p++;
				}
				else *out++ = *p++;
			}
		}
		if(full)
		{
			while(*str)*out++ = *str++;
			break;
		}
	}
	*out = 0;
	return nacc;
}
