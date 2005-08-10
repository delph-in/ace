#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

#include	"dag.h"
#include	"chart.h"
#include	"hash.h"

struct maxent_feature
{
	char	*head;
	char	*rhs[2];
	float	score;
};

static struct hash	*mem;

void print_mef(struct maxent_feature *f)
{
	printf("maxent head `%s' rhs [%s %s] score %f\n",
		f->head, f->rhs[0]?:"NO-RHS[0]", f->rhs[1]?:"", f->score);
}

void record_maxent_feature(char *head, char *rhs[2], float score)
{
	struct maxent_feature *feat = malloc(sizeof(struct maxent_feature));

	feat->head = head;
	feat->rhs[0] = rhs[0];
	feat->rhs[1] = rhs[1];
	feat->score = score;
	//print_mef(feat);
	hash_add(mem, head, feat);
}

static char	*parse_word(char	**P)
{
	char	*p = *P, *ret;

	if(*p==' ')p++;
	ret = p;
	while(*p && *p!=' ')p++;
	if(*p==' '){*p=0;p++;}
	*P = p;
	return *ret?ret:0;
}

int load_mem(char *fname)
{
	FILE	*f = fopen(fname, "r");
	char	line[1024];

	if(!f) { perror(fname); return -1; }

#define DEBUG(fmt, ...)

	if(mem)
		fprintf(stderr, "NOTE: overwriting pre-existing max-ent model with `%s'\n", fname);
	mem = hash_new("maxent model");

	while(fgets(line, 1024, f))
	{
		int llen = strlen(line), tint, i = 0;
		float sfloat;
		char *p, *type, *head, *rhs[2], *score;

		if(line[0]!='['){DEBUG("IGNORE maxent line `%s'\n", line);continue;}
		if(line[llen-1]=='\n')line[--llen] = 0;
		p = strrchr(line, ']');
		if(!p){DEBUG("IGNORE maxent line `%s' (no closing ])\n", line);continue;}
		*p = 0; score = p+1; p = line+1;
		while(*score==' ' || *score=='\t')score++;
		if((*score<'0' || *score>'9') && *score!='.' && *score!='+' && *score!='-'){DEBUG("IGNORE maxent score `%s'\n", score);continue;}
		sfloat = atof(score);

		type = parse_word(&p);
		head = parse_word(&p);
		if(!type || !head){DEBUG("IGNORE maxent line with type %s head %s\n", type?:"(null)", head?:"(null)");continue;}
		tint = atoi(type);
		if(tint <1 || tint >2){DEBUG("IGNORE maxentline with type %s\n", type);continue;}
		rhs[0] = rhs[1] = 0;
		while(i<2)
		{
			rhs[i++] = parse_word(&p);
			if(!rhs[i-1])break;
		}
		record_maxent_feature(head, rhs, sfloat);
	}

	fclose(f);
	return 0;
}

void	*freeze_stochastic_model()
{
	int	i;
	struct hash_bucket *walk;
	if(!mem)return 0;
	for(i=0;i<mem->size;i++)
		for(walk=mem->buckets[i];walk;walk=walk->next)
			walk->value = (void*)freeze_block(walk->value, sizeof(struct maxent_feature));
	return freeze_hash(mem);
}

void	recover_stochastic_model(void *_frozen)
{
	mem = _frozen;
	if(mem)printf("NOTE: max-ent model hash contains %d entries in %d slots\n", mem->entries, mem->size);
}
