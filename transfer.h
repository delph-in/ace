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
};

int	study_trigger_rule(char	*name, struct dg	*dg);
void	activate_trigger_rules(struct mrs	*mrsin, void	trigger(char	*lexid));
