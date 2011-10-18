#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<unistd.h>
#include	<math.h>
#include	<sys/fcntl.h>

#include	"words.h"
#include	"tags.h"

int	post_verbose = 0;

#define	WH_SIZE	65535

int				nwords;
struct word		**word_index;
struct word		**word_hash[WH_SIZE];
int				word_hlen[WH_SIZE];
static int		words_ascii_len, words_hash_len;
char			word_hfrozen[WH_SIZE];

static inline unsigned int	hash_str(char	*str)
{
	unsigned int h = 0;
	while(*str)
	{
		h <<= 5;
		h ^= *str++;
	}
	return h;
}

double	*unknown_tprob(char	*word, int	first)
{
	int		len = strlen(word), i, bi;
	double	*prob = calloc(sizeof(double), ntags), m = 0;
	double	best;

#define	SPIKE(tg,x)	prob[lookup_tag(tg)->index]+=x, m+=x
#ifdef	NO_MORPH_RULES
	SPIKE("NN", 1.0);
#else
	SPIKE("NN", 1.5);
	SPIKE("JJ", 0.75);
	SPIKE("RB", 0.25);
	SPIKE("VB", 0.5);
	if(len>0 && word[0]>='A' && word[0]<='Z')
	{
		if(!first)
		{
			if(word[len-1]=='s')SPIKE("NNPS", 19);
			else SPIKE("NNP", 22);
			SPIKE("NNP", 17);
		}
		else
		{
			if(word[len-1]=='s')SPIKE("NNPS", 2);
			else SPIKE("NNP", 2);
			SPIKE("NNP", 2);
		}
	}
	if(len>2 && !strcmp(word+len-2, "ly"))SPIKE("RB", 3);
	if(len>2 && !strcmp(word+len-2, "ed")){SPIKE("VBN", 2);SPIKE("VBD", 2);SPIKE("JJ", 1.5);}
	if(len>3 && !strcmp(word+len-3, "ing")){SPIKE("VBG", 5);SPIKE("JJ", 2);}
	if(len>1 && !strcmp(word+len-1, "s")){SPIKE("NNS", 4);SPIKE("VBZ", 3);}
#endif

	//printf("unknown word `%s'\n", word);
	for(best=bi=i=0;i<ntags;i++)
	{
		prob[i] /= m;
		//if(prob[i])printf("p(%s) = %f\n", tags[i]->tag, prob[i]);
		prob[i] = log(prob[i]);
	}

	return prob;
}

struct word	*lookup_word(char	*word, int	mode, int	first)
{
	unsigned int	hash = hash_str(word) % WH_SIZE;
	int				i;
	struct word		*w;
	struct tag		*t;

	for(i=0;i<word_hlen[hash];i++)
	{
		if(!strcasecmp(word_hash[hash][i]->word, word))
			return word_hash[hash][i];
	}

	// insert the word
	word_hlen[hash]++;
	words_hash_len++;
	if(word_hfrozen[hash])
	{
		struct word	**n = malloc(sizeof(struct word**)*word_hlen[hash]);
		memcpy(n, word_hash[hash], sizeof(struct word**)*(word_hlen[hash]-1)); 
		word_hash[hash] = n;
		word_hfrozen[hash] = 0;
	}
	else word_hash[hash] = realloc(word_hash[hash], sizeof(struct word**)*word_hlen[hash]);
	w = malloc(sizeof(struct word));
	w->index = nwords++;
	w->word = strdup(word);
	w->nfreqs = ntags;
	w->count = 0;
	words_ascii_len += (strlen(w->word)&~3) + 4;
	if(mode)w->tprob = 0;
	else w->tprob = unknown_tprob(word, first);
	word_hash[hash][i] = w;
	word_index = realloc(word_index, sizeof(struct word**)*nwords);
	word_index[w->index] = w;
	return w;
}

struct words_info
{
	int				nwords;
	int				words_ascii_len, words_hash_len;
	struct word		**word_index;
	struct word		**word_hash[WH_SIZE];
	int				word_hlen[WH_SIZE];
};

int	post_words_image_size() { return sizeof(struct words_info) + (nwords + words_hash_len)*sizeof(struct word*)
									+ nwords * (sizeof(struct word) + ntags*sizeof(double)) + words_ascii_len; }

void	post_words_freeze(void	*ptr)
{
	struct words_info	*p = ptr;
	struct word			**wp = ptr + sizeof(struct words_info);
	struct word			*w = (struct word*)(wp + nwords + words_hash_len);
	double				*d = (double*)(w + nwords);
	extern int			ntags;
	char				*a = (char*)(d + nwords*ntags);
	int					i,j;

	//printf("start of words image %p ; end %p ; words_ascii_len = %d\n", ptr, ptr+post_words_image_size(), words_ascii_len);
	p->nwords = nwords;
	p->words_ascii_len = words_ascii_len;
	p->words_hash_len = words_hash_len;
	p->word_index = wp;
	for(i=0;i<nwords;i++,w++)
	{
		//printf("word index: `%s' at %p / %p\n", word_index[i]->word, w, a);
		wp[i] = w;
		*w = *word_index[i];
		strcpy(a, w->word);
		w->word = a;
		a += strlen(a)+1;
		memcpy(d, w->tprob, sizeof(double)*ntags);
		w->tprob = d;
		d += ntags;
	}
	wp += nwords;
	memcpy(p->word_hlen, word_hlen, sizeof(word_hlen));
	for(i=0;i<WH_SIZE;i++)
	{
		p->word_hash[i] = word_hlen[i]?wp:0;
		for(j=0;j<word_hlen[i];j++)
			p->word_hash[i][j] = p->word_index[word_hash[i][j]->index];
		wp += word_hlen[i];
	}
}

void	post_words_recover(void	*ptr)
{
	struct words_info	*p = ptr;
	nwords = p->nwords;
	words_ascii_len = p->words_ascii_len;
	words_hash_len = p->words_hash_len;
	word_index = malloc(sizeof(struct word*)*nwords);
	memcpy(word_index, p->word_index, sizeof(struct word*)*nwords);
	memset(word_hfrozen, 1, WH_SIZE);
	memcpy(word_hash, p->word_hash, sizeof(word_hash));
	memcpy(word_hlen, p->word_hlen, sizeof(word_hlen));
}
