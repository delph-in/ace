#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

#include	"dag.h"
#include	"chart.h"
#include	"hash.h"
#include	"freeze.h"
#include	"maxent.h"

struct maxent_feature
{
	float	score;
	// XXX features below this comment are not accessible at parse time -- only at grammar load time
	char	*head;
	char	*rhs[2];

	int		ngp;
	char	**gp;
};

void print_mef(struct maxent_feature *f);

static struct hash	*mem;

#define MAXENT_NOISE(fmt, ...)
//#define MAXENT_NOISE printf

int	nmaxent_requests = 0, nmaxent_visits = 0;

show_me()
{
	fprintf(stderr, "MAXENT: %d visits / %d requests = %.1f per\n", nmaxent_visits, nmaxent_requests, (float)nmaxent_visits / nmaxent_requests);
}

// visit all maxent features containing this local subtree and compatible with the ancestry
// add up their scores
float	maxent_score(char *lhs, char *rhs1, char *rhs2, int rtl_unused, struct scoring_ancestry	*anc)
{
	float ret = 0;
	//char	*key = rtl?rhs2:rhs1;
	char	hkey[512];

	void visitor(struct maxent_feature *feat)
	{
		int	i;
		nmaxent_visits++;
		ret += feat->score;
	}

	nmaxent_requests++;

	//return (double)rand()/RAND_MAX - 0.5;
	if(!mem)return 0;

	int	ngp;
	char	*hkp = hkey;
	hkp += sprintf(hkey, "%s:%s:%s", lhs, rhs1?:"", rhs2?:"");//key?:"");
	for(ngp=0;ngp<=anc->nparent_types;ngp++)
	{
		if(ngp) hkp += sprintf(hkp, ":%s", anc->parent_types[ngp-1]);
		MAXENT_NOISE("scoring key = %s\n", hkey);
		hash_visit_key(mem, hkey, (void(*)(void*))visitor);
	}
	if(anc->rooted)
	{
		hkp += sprintf(hkp, ":^");
		MAXENT_NOISE("scoring key = %s\n", hkey);
		hash_visit_key(mem, hkey, (void(*)(void*))visitor);
	}
	MAXENT_NOISE(" ... %f\n", ret);
	return ret;
}

// loading

void print_mef(struct maxent_feature *f)
{
	printf("maxent head `%s' rhs [%s %s] score %f\n",
		f->head, f->rhs[0]?:"NO-RHS[0]", f->rhs[1]?:"", f->score);
}

void record_maxent_feature(char *head, char *rhs[2], float score, int	ngp, char	**gp)
{
	struct maxent_feature *feat = malloc(sizeof(struct maxent_feature));
	//char	*key = malloc(3+strlen(head) + (rhs[0]?strlen(rhs[0]):0) + (rhs[1]?strlen(rhs[1]):0));
	char	key[512], *kp = key;
	int	i;

	kp += sprintf(key, "%s:%s:%s", head, rhs[0]?:"", rhs[1]?:"");
	for(i=0;i<ngp;i++)kp += sprintf(kp, ":%s", gp[i]);
	feat->score = score;

	feat->head = strdup(head);
	feat->rhs[0] = (rhs[0] && *rhs[0])?strdup(rhs[0]):NULL;
	feat->rhs[1] = (rhs[1] && *rhs[1])?strdup(rhs[1]):NULL;
	feat->ngp = ngp;
	feat->gp = malloc(sizeof(char*)*ngp);
	for(i=0;i<ngp;i++)feat->gp[i] = strdup(gp[i]);
	//print_mef(feat);
	//printf("recording feature at key '%s'\n", key);
	hash_add(mem, strdup(key), feat);
}

static char	*parse_word(char	**P)
{
	char	*p = *P, *ret;

	if(*p==' ')p++;
	ret = p;
	int	quoted = 0;
	while(*p && (*p!=' ' || quoted))
	{
		if(*p=='\\' && p[1])p++;
		else if(*p=='"')quoted=!quoted;
		p++;
	}
	if(*p==' '){*p=0;p++;}
	else assert(!quoted);
	*P = p;
	return *ret?ret:0;
}

int load_mem(char *fname)
{
	FILE	*f;
	char	line[1024];

#define DEBUG(fmt, ...)
//#define DEBUG	printf

	if(!fname)return 0;
	f = fopen(fname, "r");
	if(!f) { perror(fname); return -1; }

	if(mem)
		fprintf(stderr, "NOTE: overwriting pre-existing max-ent model with `%s'\n", fname);
	mem = hash_new("maxent model");

	DEBUG("loading maxent file from '%s'\n", fname);

struct timeval tv, tv2;
gettimeofday(&tv, NULL);
double tfgets = 0;
	while(fgets(line, 1024, f))
	{
		gettimeofday(&tv2, NULL);
		tfgets += (double)(tv2.tv_sec - tv.tv_sec + 0.000001 * (tv2.tv_usec - tv.tv_usec));
		int llen = strlen(line), tint, i = 0;
		float sfloat;
		char *p, *type, *grandparents_count, *head, *rhs[2], *score;

		if(line[0]!='('){DEBUG("IGNORE maxent line `%s'\n", line);continue;}
		if(line[llen-1]=='\n')line[--llen] = 0;
		DEBUG("process maxent line '%s'\n", line);
		p = strrchr(line, ']');
		if(p>line+1)
		{
			// find *second* to last ']'
			p--;
			while(p>line && *p!=']')p--;
			if(p==line)p=NULL;
		}
		if(!p){DEBUG("IGNORE maxent line `%s' (no closing ])\n", line);continue;}
		*p = 0; score = p+1; p = strchr(line, '[');
		if(!p){DEBUG("IGNORE maxent line `%s' (no opening [)\n", line);continue;}
		p = p+1;
		while(*score==' ' || *score=='\t')score++;
		if((*score<'0' || *score>'9') && *score!='.' && *score!='+' && *score!='-'){DEBUG("IGNORE maxent score `%s'\n", score);continue;}
		sfloat = atof(score);

		type = parse_word(&p);
		grandparents_count = parse_word(&p);
		if(*grandparents_count != '('){DEBUG("IGNORE maxent line with bad grandparent count\n");continue;}
		int	ngrandparents = atoi(grandparents_count+1), j;
		char	*grandparents[ngrandparents];
		for(j=ngrandparents-1;j>=0;j--)grandparents[j] = parse_word(&p);
		head = parse_word(&p);
		if(!type || !head){DEBUG("IGNORE maxent line with type %s head %s\n", type?:"(null)", head?:"(null)");continue;}
		tint = atoi(type);
		if(tint <1 || tint >2){DEBUG("IGNORE maxentline with type %s\n", type);continue;}
		rhs[0] = parse_word(&p);
		while(*p==' ')p++;
		rhs[1] = p;
		record_maxent_feature(head, rhs, sfloat, ngrandparents, grandparents);
	}
	//printf("maxent loading fgets took %.2fs\n", tfgets);

	fclose(f);
	return 0;
}

void	*freeze_stochastic_model()
{
	int	i, j;
	struct hash_bucket *walk;
	if(!mem)return 0;
	for(i=0;i<mem->size;i++)
		for(walk=mem->buckets[i];walk;walk=walk->next)
		{
			struct maxent_feature *fi = walk->value;
			struct maxent_feature *fo = slab_alloc(sizeof(float));//slab_alloc(sizeof(struct maxent_feature));
			fo->score = fi->score;
			walk->value = fo;
			/*struct maxent_feature *fo = slab_alloc(sizeof(struct maxent_feature));
			fo->head = freeze_string(fi->head);
			fo->rhs[0] = freeze_string(fi->rhs[0]);
			fo->rhs[1] = freeze_string(fi->rhs[1]);
			fo->score = fi->score;
			fo->ngp = fi->ngp;
			fo->gp = slab_alloc(sizeof(char*)*fo->ngp);
			for(j=0;j<fo->ngp;j++)
				fo->gp[j] = freeze_string(fi->gp[j]);
			walk->value = fo;*/
		}
	return freeze_hash(mem);
}

void	recover_stochastic_model(void *_frozen)
{
	extern int debug_level;
	mem = _frozen;
	if(mem && debug_level)printf("NOTE: max-ent model hash contains %d entries in %d slots\n", mem->entries, mem->size);
}
