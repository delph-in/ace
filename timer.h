#ifndef	TIMER_H
#define	TIMER_H

#include	<stdio.h>

// allocate a new timer
int	new_timer(char	*name);

// start the specified timer
void start_timer(int	timer);

// stop the specified timer, and optionally
//   treat the elapsed time as `count' separate
//   events when producing reports
double stop_timer(int	timer, int	count);

// print the given time interval in the most
//   appropriate units (e.g. microseconds, or minutes)
void	fprint_time(FILE	*f, double seconds);

// show the state of every registered timer on stdout
void	report_timers();
void	freport_timers(FILE	*f);

// write an XML representation of the state of every registered
//   timer to the stream `f', with schema:
//    <timers>
//      <timer name='countdown' total='3m 20s' events='374' per='0.535s'/>
//    </timers>
void	xml_timers(FILE	*f);

// log a single line record to `fname',
//   consisting of tab-separated total elapsed times for each timer
void	log_timers(char	*fname);

struct profiler
{
	int				nsub;
	struct profiler	**sub;

	int	ncalls, nevents, isrunning;
	struct timeval	started;
	double	elapsed;
	char	*name;
	int	sortable;
};

extern struct profiler *parse_profiler;
extern struct profiler	*preprocess_profiler;
extern struct profiler	*token_mapping_profiler;
extern struct profiler	*lexical_lookup_profiler;
extern struct profiler	*lexical_parsing_profiler;
extern struct profiler	*lexical_filtering_profiler;
extern struct profiler	*chart_parsing_profiler;
extern struct profiler	  *packing_profiler;
extern struct profiler	*unpacking_profiler;
extern struct profiler	*transfer_profiler;
extern int g_profiling;

void	start_profiler(struct profiler	*prof);
void	start_and_alloc_profiler(struct profiler	**prof, char	*name, struct profiler	*parent, struct profiler	*tostop);
void	stop_profiler(struct profiler	*prof, int	nevents);
void	stop_profilers_recursively(struct profiler	*prof);

struct profiler	*new_profiler(char	*name, struct profiler	*parent);
void	freport_profiler_indent(FILE	*fout, struct profiler	*prof, int	ind);
void	report_profiler(struct profiler	*prof);

#endif
