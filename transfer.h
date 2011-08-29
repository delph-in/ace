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
};

extern int g_transfer;
extern int	ntransfer_rules;
extern struct transfer_rule	**transfer_rules;
int	load_transfer_rules();

void	align_transfer_rule(struct transfer_rule	*tr, struct mrs	*mrs, void	(*callback)(int	*alignment));
struct transfer_rule	*dg_to_transfer_rule(char	*name, struct dg	*dg);

int	study_trigger_rule(char	*name, struct dg	*dg);
void	activate_trigger_rules(struct mrs	*mrsin, void	trigger(char	*lexid));

extern char	*non_idiom_root;
int	study_idiom_rule(char	*name, struct dg	*dg);
int	check_idioms(struct dg	*dg, struct mrs	*m);

int	transfer_mrs(int	nrules, struct transfer_rule	**rules, struct mrs	*m, struct mrs	***Results);
