#ifndef	HASH_H
#define HASH_H

struct hash
{
	char	*name;
	int		size, entries;
	struct hash_bucket
	{
		char	*key;
		void	*value;
		struct hash_bucket *next;
	}		**buckets;
};

struct hash	*hash_new(char	*name);
struct hash	*freeze_hash(struct hash	*ht);
void		hash_add(struct hash	*ht, char	*key, void	*value);
void		*hash_find(struct hash	*ht, char	*key);
void		hash_visit_key(struct hash	*ht, char	*key, void	(*visitor)(void *value));

#endif
