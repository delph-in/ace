enum
{
	rtReplace = 1,
	rtErsatz = 2,
};

struct fspp_rule
{
	int	type;
	char	*pat, *rep;
	int	dfastart;
	int	nsave;
};

struct dfastate
{
	short	trans[256];
	short	accept;

	// which nfa states are in it?
	char	*set;
	short	nns;
	// which vars do we start/stop saving?
	char	start[4];
	char	end[4];
};

extern int		ndfa;
extern struct dfastate	*dfa;

extern int				npprules;
extern struct 		fspp_rule	*pprules;

int	preprocess_line(char	*line, int	*nwordsP, char	***wordsP);
