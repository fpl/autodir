#ifndef TIME_MONO_H
#define TIME_MONO_H

#include <time.h>

/*INITIALIZE*/
extern void time_mono_init(void);
/*INITIALIZE*/

#if defined(_POSIX_TIMERS) && (_POSIX_TIMERS > 0)
extern clockid_t clockid;
#endif

time_t time_mono(void);
#if 0
struct timespec *mono_timespec(struct timespec *tp, time_t sec, long nsec);
#endif
struct timespec *thread_cond_timespec(struct timespec *, time_t);
void mono_nanosleep( long nsec );

#endif
