#include	<sys/time.h>
#include	<stdio.h>
#include	<string.h>
#include	<assert.h>
#include	<stdlib.h>
#include	"timer.h"

static struct timeval	times[100];
static char				*timernames[100];
static double			total[100], lap[100];
static long long		events[100];
static long long		int_calls[100];
static long long		calls[100];
static long long		ocount, ostart[100];
static int				ntimers;

int	new_timer(char	*name)
{
	timernames[ntimers] = strdup(name);
	return ntimers++;
}

void start_timer(int	i)
{
	gettimeofday(&times[i], 0);
	ostart[i] = ocount;
}

double stop_timer(int	i, int	count)
{
	struct timeval	tv;
	double t;
	gettimeofday(&tv, 0);
	t = tv.tv_sec - times[i].tv_sec;
	ocount++;
	t += 0.000001 * (double)(tv.tv_usec - times[i].tv_usec);// - 0.000002116 * (ocount - ostart[i]);
	int_calls[i] += ocount - ostart[i] - 1;
	calls[i] += 1;
	total[i] += t;
	times[i] = tv;
	events[i]+=count;
	return t;
}

void	fprint_time(FILE	*f, double s)
{
	int m = s/60, h = m/60;
	if(h) { fprintf(f, "%dh ", h); m -= 60*h; s -= 60*60*h; }
	if(h || m) { fprintf(f, "%dm ", m); s -= 60*m; }
	if(h || m)fprintf(f, "%ds", (int)s);
	else if(s > 0.5)fprintf(f, "%.3fs", s);
	else if(s > 0.0005)fprintf(f, "%.1fms", s*1000);
	else fprintf(f, "%.1fµs", s*1000000);
}

//const double k_overhead = 0.000002116
const double k_int_overhead = 0.000000137;
const double k_self_overhead = 0.000000064;

void	freport_timers(FILE	*f)
{
	int i;
	fprintf(f, "TIMERS (%lld calls = ~ ", ocount);
	fprint_time(f, k_int_overhead * ocount);
	fprintf(f, " overhead):\n");
	for(i=0;i<ntimers;i++)
	{
		double	t_minus_oh = total[i] - k_int_overhead * int_calls[i] - k_self_overhead * calls[i];
		fprintf(f, "%-32s	", timernames[i]);
		fprint_time(f, t_minus_oh);
		if(events[i]>0)
		{
			fprintf(f, "	for %lld events = ", events[i]);
			fprint_time(f, events[i]?(t_minus_oh / events[i]):t_minus_oh);
			fprintf(f, " per event\n");
		}
		else fprintf(f, "\n");
	}
}

void	report_timers()
{
	freport_timers(stdout);
}

void	xml_timers(FILE	*f)
{
	int i;
	fprintf(f, "<timers>\n");
	for(i=0;i<ntimers;i++)
	{
		fprintf(f, "<timer name='%s' total='", timernames[i]);
		fprint_time(f, total[i]);
		fprintf(f, "' events='%lld' per='", events[i]);
		fprint_time(f, events[i]?(total[i] / events[i]):total[i]);
		fprintf(f, "'/>\n");
	}
	fprintf(f, "</timers>\n");
}

void	log_timers(char	*fname)
{
	int i;
	FILE	*f = fopen(fname, "a"); assert(f!=0);
	for(i=0;i<ntimers;i++)
	{
		fprintf(f, "%f%c", total[i] - lap[i], (i==(ntimers-1))?'\n':'	');
		lap[i] = total[i];
	}
	fclose(f);
}

struct profiler	*new_profiler(char	*name, struct profiler	*parent)
{
	struct profiler	*p = calloc(sizeof(*p),1);
	if(parent)
	{
		parent->nsub++;
		parent->sub = realloc(parent->sub, sizeof(struct profiler*)*parent->nsub);
		parent->sub[parent->nsub-1] = p;
	}
	p->name = strdup(name);
	return p;
}

void	start_profiler(struct profiler	*prof)
{
	if(prof->isrunning)
		fprintf(stderr, "ERROR: can't start profiler `%s'; it's already running.\n", prof->name);
	prof->isrunning = 1;
	gettimeofday(&prof->started, NULL);
}

void	start_and_alloc_profiler(struct profiler	**prof, char	*name, struct profiler	*parent, struct profiler	*tostop)
{
	if(!*prof)
		*prof = new_profiler(name, parent);
	start_profiler(*prof);
	if(tostop)stop_profiler(tostop, 1);
}

void	stop_profiler(struct profiler	*prof, int	nevents)
{
	if(!prof->isrunning)
	{
		fprintf(stderr, "ERROR: can't stop profiler `%s'; it's not running.\n", prof->name);
		return;
	}
	prof->isrunning = 0;
	struct timeval	stopped;
	gettimeofday(&stopped, NULL);
	double	dt = stopped.tv_sec - prof->started.tv_sec;
	dt += 0.000001 * (stopped.tv_usec - prof->started.tv_usec);
	prof->elapsed += dt;
	prof->ncalls++;
	prof->nevents += nevents;
}

void	sort_profiler(struct profiler	*prof)
{
	int pcmp(const struct profiler	**a, const struct profiler	**b)
	{
		if((*a)->elapsed > (*b)->elapsed)return -1;
		if((*a)->elapsed < (*b)->elapsed)return +1;
		return 0;
	}
	qsort(prof->sub, prof->nsub, sizeof(struct profiler*), (int(*)(const void*,const void*))pcmp);
}

void	freport_profiler_indent(FILE	*fout, struct profiler	*prof, int	ind)
{
	int i;
	for(i=0;i<ind;i++)
		fprintf(fout, "    ");
	fprintf(fout, "%32s : ", prof->name);
	fprint_time(fout, prof->elapsed);
	if(prof->nevents)
	{
		fprintf(fout, " for %d events = ", prof->nevents);
		fprint_time(fout, prof->elapsed / prof->nevents);
		fprintf(fout, " per event");
	}
	fprintf(fout, "\n");
	if(prof->sortable)sort_profiler(prof);
	for(i=0;i<prof->nsub;i++)
	{
		if(prof->sortable && i >= prof->sortable)break;
		freport_profiler_indent(fout, prof->sub[i], ind+1);
	}
}

void	report_profiler(struct profiler	*prof)
{
	if(!prof)return;
	freport_profiler_indent(stdout, prof, 0);
}

void	stop_profilers_recursively(struct profiler	*prof)
{
	int	i;
	if(prof->isrunning)stop_profiler(prof, 1);
	for(i=0;i<prof->nsub;i++)
		stop_profilers_recursively(prof->sub[i]);
}

struct profiler	*parse_profiler;
struct profiler	*preprocess_profiler;
struct profiler	*token_mapping_profiler;
struct profiler	*lexical_lookup_profiler;
struct profiler	*lexical_parsing_profiler;
struct profiler	*lexical_filtering_profiler;
struct profiler	*chart_parsing_profiler;
struct profiler	  *packing_profiler;
struct profiler	*unpacking_profiler;
struct profiler	*post_generation_mapping_profiler;
struct profiler	*transfer_profiler;
