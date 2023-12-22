/*

Copyright (C) (2004 - 2006) (Venkata Ramana Enaganti) <ramana@intraperson.com>

This program is free software; you can redistribute it and/or 
modify it under the terms of the GNU General Public License 
as published by the Free Software Foundation; either 
version 2 of the License, or (at your option) any later 
version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <time.h>
#include <unistd.h>

/* Utility library. */

#include "msg.h"

/* Application-specific. */

#include "time_mono.h"

#if defined(_POSIX_TIMERS) && (_POSIX_TIMERS > 0)
clockid_t clockid = CLOCK_REALTIME;
#endif

static int initialized = 0;

#ifdef TEST
#if 0
#ifdef _POSIX_MONOTONIC_CLOCK
#undef _POSIX_MONOTONIC_CLOCK
#endif
#ifdef _POSIX_TIMERS
#undef _POSIX_TIMERS
#endif
#endif
#endif

time_t time_mono( void )
{
	const char *myname = "time_monotonic";

#if defined(_POSIX_TIMERS) && (_POSIX_TIMERS > 0)
	struct timespec tp;

	if( clock_gettime( clockid, &tp ) )
		msglog( MSG_FATAL|LOG_ERRNO, 
				    "%s: clock_gettime failed", myname );
	return tp.tv_sec;
#else
#warning "time_mono using time() interface"
	return time( NULL );
#endif
}

/*For use with pthread_cond_timedwait*/
struct timespec *thread_cond_timespec( struct timespec *tp, time_t sec )
{
#if defined _POSIX_CLOCK_SELECTION && _POSIX_CLOCK_SELECTION >= 0 \
			&& defined(_POSIX_TIMERS) && (_POSIX_TIMERS > 0)
	if ( clock_gettime( clockid, tp ) )
		msglog( MSG_FATAL|LOG_ERRNO, "thread_cond_timespec: " \
						    "clock_gettime failed" );
#else
#warning "not using _POSIX_CLOCK_SELECTION"
	tp->tv_sec = time( NULL );
	tp->tv_nsec = 0;
#endif
	tp->tv_sec += sec;
	return tp;
}

#if 0
struct timespec *mono_timespec( struct timespec *tp, time_t sec, long nsec )
{
#if defined(_POSIX_TIMERS) && (_POSIX_TIMERS > 0)
#if defined(_POSIX_MONOTONIC_CLOCK) && (_POSIX_MONOTONIC_CLOCK >= 0)
	if ( clock_gettime( clockid, tp ) )
		msglog( MSG_FATAL|LOG_ERRNO, 
				    "mono_timespec: clock_gettime failed" );
#else
	if ( clock_gettime( CLOCK_REALTIME, tp ) )
		msglog( MSG_FATAL|LOG_ERRNO, 
				    "mono_timespec: clock_gettime failed" );
#endif
#else
	tp->tv_sec = time( NULL );
	tp->tv_nsec = 0;
#endif
	tp->tv_sec += sec;
	tp->tv_nsec += nsec;
	return tp;
}
#endif

void mono_nanosleep( long nsec )
{
	struct timespec tp;

#if defined(_POSIX_TIMERS) && (_POSIX_TIMERS > 0)
	tp.tv_sec = 0;
	tp.tv_nsec = nsec;
	if( clock_nanosleep( clockid, 0, &tp, NULL ) )
		msglog( MSG_ERR|LOG_ERRNO, 
			    "mono_nanosleep: clock_nanosleep" );
	return;
#else
	tp.tv_sec = 0;
	tp.tv_nsec = nsec;
	if( nanosleep( &tp, NULL ) )
		msglog( MSG_ERR|LOG_ERRNO, "mono_nanosleep: nanosleep" );
#endif
}

void time_mono_init(void)
{
	if( initialized )
		return;
#if defined(_POSIX_TIMERS) && (_POSIX_TIMERS > 0) && \
	defined(_POSIX_MONOTONIC_CLOCK) && (_POSIX_MONOTONIC_CLOCK >= 0)

	if( sysconf( _SC_MONOTONIC_CLOCK ) > 0 )
		clockid = CLOCK_MONOTONIC;
#else
#warning "CLOCK_MONOTONIC not available. using CLOCK_REALTIME"
#endif

	/*call for the first time to see any fatal errors*/
	time_mono();
	initialized = 1;
}

#ifdef TEST

#include "thread.h"

char *autodir_name(void)
{
    return "test autodir";
}

int main(void)
{
	int i;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	struct timespec tspec;

	thread_init();
	time_mono_init();
	
	printf("time_mono %ld\n", (long) time_mono());
	sleep(1);
	printf("time_mono %ld\n", (long) time_mono());
	sleep(1);
	printf("time_mono %ld\n", (long) time_mono());
	sleep(1);
	printf("time_mono %ld\n", (long) time_mono());
	sleep(1);
	printf("time_mono %ld\n", (long) time_mono());
	sleep(1);
	printf("time_mono %ld\n", (long) time_mono());
	sleep(1);
	printf("time_mono %ld\n", (long) time_mono());
	sleep(1);
	printf("time_mono %ld\n", (long) time_mono());
	printf("time_mono %ld\n", (long) time_mono());
	printf("time_mono %ld\n", (long) time_mono());
	printf("time_mono %ld\n", (long) time_mono());
	printf("time_mono %ld\n", (long) time_mono());
	printf("time_mono %ld\n", (long) time_mono());
	printf("time_mono %ld\n", (long) time_mono());
	printf("time_mono %ld\n", (long) time_mono());
	printf("time_mono %ld\n", (long) time_mono());
	printf("time_mono %ld\n", (long) time_mono());
	printf("time_mono %ld\n", (long) time_mono());

	thread_mutex_init(&lock);
	thread_cond_init(&cond);
	thread_cond_timespec(&tspec, 10);
	pthread_mutex_lock(&lock);
	printf("pthread_cond_timedwait next %ld\n", time_mono());
	pthread_cond_timedwait(&cond, &lock, &tspec);
	printf("pthread_cond_timedwait end %ld\n", time_mono());

	printf("mono_nanosleep next %ld\n", time_mono());
	for( i = 0 ; i < 1000; i++) {
		mono_nanosleep(100000000);
	}
	printf("mono_nanosleep end %ld\n", time_mono());
}

#endif
