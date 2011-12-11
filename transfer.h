#include	"mrs.h"
#define		REGEX_FLAGS	REG_PERL
#define		UNICODE
#include	<boost/regex.h>

struct mrs_ep_desc
{
	wchar_t	*pred_re;
	regex_t	re;
	struct dg	*dg;
};

struct mrs_desc
{
	int	neps;
	int	nhcons;
	struct mrs_ep_desc	*eps;
	struct mrs	*mrs;
	struct dg	*dg;
};

struct transfer_rule
{
	char	*name;
	struct mrs_desc	input, context, filter, output;
	struct dg	*dg;

	int	neq, nss;
	struct dg	**eq, **ss;

	int	optional;
	int	ncopy;
	int	*copy;
	char	*output_copy_map;
};

extern int g_transfer;
extern int	ntransfer_rules;
extern struct transfer_rule	**transfer_rules;
int	load_transfer_rules();

// for use when loading rules with ugly +copy+ specifications
struct tcopy
{
	int	*path;
	int	pathlen;
};
extern int saved_ntcopies;
extern struct tcopy	*saved_tcopies;

int	align_transfer_rule(struct transfer_rule	*tr, struct mrs	*mrs, int	(*callback)(int	*alignment));
struct transfer_rule	*dg_to_transfer_rule(char	*name, struct dg	*dg);

int	study_trigger_rule(char	*name, struct dg	*dg);
void	activate_trigger_rules(struct mrs	*mrsin, void	trigger(char	*lexid));

extern char	*non_idiom_root;
int	study_idiom_rule(char	*name, struct dg	*dg);
int	check_idioms(struct dg	*dg, struct mrs	*m);

int	study_fixup_rule(char	*name, struct dg	*dg);
int	study_cleanup_rule(char	*name, struct dg	*dg);
struct mrs	*fixup_mrs(struct mrs	*m);
struct mrs	*cleanup_mrs(struct mrs	*m);

int	transfer_mrs(int	nrules, struct transfer_rule	**rules, struct mrs	*m, struct mrs	***Results, int first_route_only);
