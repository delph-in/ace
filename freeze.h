int	load_frozen_grammar(char	*path);
int	setup_save_frozen_grammar(char	*path);
int	load_grammar();
int	freeze_grammar();
void	dump_grammar();
void	reload_frozen_grammar();

extern long	loaded_freeze_size;

#define	FREEZER_MMAP_BASE	0x60000000

#ifdef	LP64
#define	FREEZER_MMAP_SIZE	0x10000000
#else
#define	FREEZER_MMAP_SIZE	0x14000000
#endif

#define	FROZEN(x)		((long)x>=FREEZER_MMAP_BASE && (long)x<(FREEZER_MMAP_BASE+loaded_freeze_size))
