
/*

Copyright (C) (2004 - 2006) (Venkata Ramana Enaganti) <ramana@intraperson.com>

This program is free software; you can redistribute it and/or 
modify it under the terms of the GNU General Public License 
as published by the Free Software Foundation; either 
version 3 of the License, or (at your option) any later 
version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.

*/

/* for creating threads*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include "msg.h"
#include "time_mono.h"
#include "thread.h"

/*If it is 32bit machine you might want to optimize this value.
This value given is not portable. Better to do own tuning and testing.*/

#if 1
#define THREAD_STACKSZ (0) /*OPTIMIZE*/
#else
#define THREAD_STACKSZ	(400000) /*Tested value for Fedora 3 IA32*/
#endif

/*common attributes for all threads*/
static pthread_attr_t cmn_attr;
static pthread_attr_t join_attr;

pthread_mutexattr_t common_mutex_attr;
pthread_condattr_t common_cond_attr;

static void thread_attribute_init( pthread_attr_t *attr,
			int joinable, size_t stacksz )
{
	int j = ( joinable ? PTHREAD_CREATE_JOINABLE
				: PTHREAD_CREATE_DETACHED );

	if( ( errno = pthread_attr_init( attr ) ) )
		msglog( MSG_FATAL|LOG_ERRNO, "thread_attribute_init: " \
			"pthread_attr_init" );

	if( ( errno = pthread_attr_setdetachstate( attr, j ) ) )
		msglog( MSG_FATAL|LOG_ERRNO, "thread_attribute_init: " \
			"pthread_attr_setdetachstate" );

	if( stacksz && ( errno = pthread_attr_setstacksize( attr, stacksz ) ) )
		msglog( MSG_FATAL|LOG_ERRNO, "thread_attribute_init: " \
			"pthread_attr_setstacksize %d", stacksz );

        if( ( errno = pthread_attr_setscope( attr, PTHREAD_SCOPE_SYSTEM ) ) )
                msglog( MSG_FATAL|LOG_ERRNO, "thread_attribute_init: " \
                       "pthread_attr_setscope PTHREAD_SCOPE_SYSTEM" );
}

static void mutexattr_init(void)
{
        const char *myname = "mutex_init";

        if( ( errno = pthread_mutexattr_init( &common_mutex_attr ) ) )
                msglog( MSG_FATAL|LOG_ERRNO, "%s: pthread_mutexattr_init",
                        myname );

        if( ( errno = pthread_mutexattr_setpshared( &common_mutex_attr,
                                                PTHREAD_PROCESS_PRIVATE ) ) )
                msglog( MSG_FATAL|LOG_ERRNO, "%s: pthread_mutexattr_setpshared " \
                        "PTHREAD_PROCESS_PRIVATE", myname );

        if( ( errno = pthread_mutexattr_settype( &common_mutex_attr,
                                                PTHREAD_MUTEX_NORMAL ) ) )
                msglog( MSG_FATAL|LOG_ERRNO, "%s: pthread_mutexattr_settype " \
                        "PTHREAD_MUTEX_NORMAL: %m", myname );
}

static void condattr_init(void)
{
        const char        *myname = "condattr_init";

	time_mono_init();

        if ( ( errno = pthread_condattr_init( &common_cond_attr ) ) )
                msglog(MSG_FATAL|LOG_ERRNO, "%s: pthread_condattr_init",
                        myname);

        if ( ( errno = pthread_condattr_setpshared( &common_cond_attr,
                                                PTHREAD_PROCESS_PRIVATE ) ) )
                msglog( MSG_FATAL|LOG_ERRNO, "%s: pthread_condattr_setpshared: " \
                        "PTHREAD_PROCESS_PRIVATE", myname);

#if defined _POSIX_CLOCK_SELECTION && _POSIX_CLOCK_SELECTION >= 0 \
			&& defined(_POSIX_TIMERS) && (_POSIX_TIMERS > 0)
        if ( ( errno = pthread_condattr_setclock(&common_cond_attr, clockid ) ) )
                msglog( MSG_FATAL|LOG_ERRNO, "%s: pthread_condattr_setclock",
                                                myname );
#endif
}

void thread_init( void )
{
	/*common attributes*/
	thread_attribute_init( &cmn_attr, 0, THREAD_STACKSZ );

	/*joinable attributes*/
	thread_attribute_init( &join_attr, 1, THREAD_STACKSZ );

        mutexattr_init();
        condattr_init();
}

#define DEFAULT_RETRY_WAIT	5

/*We can not simply exit or report error back
  if there is failure starting a thread
  because of resource scarcity.
*/

pthread_t thread_new_wait( void *( *th_func )( void * ),
		void *data, time_t retry )
{
	pthread_t pt;

	if( retry <= 0 )
		retry = DEFAULT_RETRY_WAIT;

	while( ( errno = pthread_create( &pt, &cmn_attr, th_func,data ) ) )
	{
		msglog( MSG_ERR|LOG_ERRNO, "thread_new_wait: " \
				"pthread_create" );

		/*real BOTTLE-NECK if you are not careful*/
		sleep( retry );
	}

	return pt;
}

int thread_new( void *( *th_func )( void * ),
				void *data, pthread_t *pt )
{
	pthread_t p;

	if( ( errno = pthread_create( &p, &cmn_attr, th_func, data) ) )
	{
		msglog( MSG_ERR|LOG_ERRNO, "thread_new: pthread_create" );
		if( pt ) *pt = 0;
		return 0;
	}

	if( pt ) *pt = p;
	return 1;
}

/* these threads can be joined by others. Watch out for memory leaks.*/
int thread_new_joinable( void *( *th_func )( void * ),
				void *data, pthread_t *pt )
{
	pthread_t p;

	if( ( errno = pthread_create( &p, &join_attr, th_func, data ) ) )
	{
		msglog( MSG_ERR|LOG_ERRNO, "thread_new_joinable: " \
				"pthread_create" );
		if( pt ) *pt = 0;
		return 0;
	}

	if( pt ) *pt = p;
	return 1;
}
