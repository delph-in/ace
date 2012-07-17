#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>

#define	LOAD_FACTOR	1.0
#define	GROW_FACTOR 4.0

#include	"dag.h"
#include	"hash.h"
#include	"freeze.h"

struct hash	*hash_new(char	*name)
{
	struct hash	*ht = calloc(sizeof(struct hash), 1);
	ht->name = name;
	ht->size = 1024;
	ht->entries = 0;
	ht->has_flk = 0;
	ht->buckets = calloc(sizeof(struct hash_bucket*), ht->size);
	return ht;
}

static inline unsigned hash_code(struct hash *ht, char	*key)
{
	unsigned code = 0, off = 0;

	if(ht->has_flk)
	{
		int	i;
		for(i=0;i<ht->has_flk;i++)
		{
			int	b = i%24;
			code ^= ((unsigned)key[i])<<b;
		}
	}
	else
	{
		for(;*key;key++, off=(off+3)%25)
			code ^= (*key)<<off;
	}

	return code % (ht->size - 1);
}

static inline void		hash_add_bucket(struct hash	*ht, struct hash_bucket	*bucket)
{
	unsigned	hash = hash_code(ht, bucket->key);

	bucket->next = ht->buckets[hash];
	ht->buckets[hash] = bucket;
	ht->entries++;
}

void		grow_hash(struct hash	*ht)
{
	int					old_size = ht->size, i;
	struct hash_bucket	**old_buckets = ht->buckets, *walker;

	ht->size = GROW_FACTOR * ht->size;
	ht->entries = 0;
	ht->buckets = calloc(sizeof(struct hash_bucket*), ht->size);
	for(i=0;i<old_size;i++)
	{
		struct hash_bucket *next;
		for(walker=old_buckets[i];walker;walker=next)
		{
			next = walker->next;
			hash_add_bucket(ht, walker);
		}
	}
	free(old_buckets);
}

void		hash_add(struct hash	*ht, char	*key, void	*value)
{
	float		load = (float)(ht->entries + 1) / (ht->size);
	struct hash_bucket	*bucket = calloc(sizeof(struct hash_bucket), 1);

	if(load > LOAD_FACTOR)grow_hash(ht);

	bucket->key = key;
	bucket->value = value;
	hash_add_bucket(ht, bucket);
}

void		hash_visit_key(struct hash	*ht, char	*key, void	(*visitor)(void *value))
{
	unsigned h = hash_code(ht, key);
	struct hash_bucket	*bucket;
	
	assert(!ht->has_flk);
	for(bucket=ht->buckets[h];bucket;bucket=bucket->next)
	{
		//printf(" visit `%s' found `%s'\n", key, bucket->key);
		if(!strcmp(bucket->key, key))visitor(bucket->value);
	}
}

void		*hash_find(struct hash	*ht, char	*key)
{
	unsigned h = hash_code(ht, key);
	struct hash_bucket	*bucket;
	
	if(!ht->has_flk)
	{
		for(bucket=ht->buckets[h];bucket;bucket=bucket->next)
			if(!strcmp(bucket->key, key))return bucket->value;
	}
	else
	{
		for(bucket=ht->buckets[h];bucket;bucket=bucket->next)
			if(!memcmp(bucket->key, key, ht->has_flk))return bucket->value;
	}
	return 0;
}

struct hash	*freeze_hash(struct hash	*ht)
{
	struct hash	*nh = slab_alloc(sizeof(struct hash));
	struct hash_bucket	*buck, **Bp, *walk;
	int				i;

	assert(!ht->has_flk);

	*nh = *ht;
	nh->buckets = slab_alloc(sizeof(struct hash_bucket*) * nh->size);
	buck = slab_alloc(sizeof(struct hash_bucket) * nh->entries);
	for(i=0;i<nh->size;i++)
	{
		Bp = &nh->buckets[i];
		for(walk=ht->buckets[i];walk;walk=walk->next)
		{
			*Bp = buck;
			Bp = &buck->next;
			buck->key = freeze_string(walk->key);
			buck->value = walk->value;
			buck++;
		}
		*Bp = 0;
	}
	//printf("freezing hash %s bucket pointers at %p\n", ht->name, nh->buckets);
	nh->name = freeze_string(ht->name);

	return nh;
}

void	hash_free(struct hash	*ht)
{
	int	i;
	for(i=0;i<ht->size;i++)
	{
		while(ht->buckets[i])
		{
			struct hash_bucket	*b = ht->buckets[i];
			ht->buckets[i] = b->next;
			free(b->key);
			free(b);
		}
	}
	free(ht->buckets);
	free(ht);
}

void	hash_free_nokeys(struct hash	*ht)
{
	int	i;
	for(i=0;i<ht->size;i++)
	{
		while(ht->buckets[i])
		{
			struct hash_bucket	*b = ht->buckets[i];
			ht->buckets[i] = b->next;
			free(b);
		}
	}
	free(ht->buckets);
	free(ht);
}

size_t	hash_memory_usage(struct hash	*ht)
{
	size_t	len = sizeof(*ht);
	int	i;
	len += sizeof(struct hash_bucket*) * ht->size;
	len += sizeof(struct hash_bucket) * ht->entries;
	for(i=0;i<ht->size;i++)
	{
		struct hash_bucket	*b;
		for(b = ht->buckets[i];b;b=b->next)
			len += strlen(b->key) + 1;
	}
	len += strlen(ht->name) + 1;
	return len;
}

struct hash	*hash_copy(struct hash	*ht, void*	(*alloc)(size_t	len))
{
	struct hash	*nh = alloc(sizeof(struct hash));
	struct hash_bucket	*buck, **Bp, *walk;
	int				i;

	char	*freeze_string(char	*S)
	{
		char	*c = alloc(strlen(S)+1);
		strcpy(c, S);
		return c;
	}

	*nh = *ht;
	nh->buckets = alloc(sizeof(struct hash_bucket*) * nh->size);
	buck = alloc(sizeof(struct hash_bucket) * nh->entries);
	for(i=0;i<nh->size;i++)
	{
		Bp = &nh->buckets[i];
		for(walk=ht->buckets[i];walk;walk=walk->next)
		{
			*Bp = buck;
			Bp = &buck->next;
			buck->key = freeze_string(walk->key);
			buck->value = walk->value;
			buck++;
		}
		*Bp = 0;
	}
	//printf("freezing hash %s bucket pointers at %p\n", ht->name, nh->buckets);
	nh->name = freeze_string(ht->name);

	return nh;
}
