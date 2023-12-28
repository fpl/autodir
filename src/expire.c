
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


#include <stdio.h>
#include <pthread.h>
#include <limits.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/auto_fs4.h>

#include "msg.h"
#include "thread.h"
#include "expire.h"

#define EXPIRE_MAX		500

#define EXPIRE_MAX_THREADS	10

static struct {
	pthread_t main;
	pthread_t threads[ EXPIRE_MAX_THREADS ];
	pthread_mutex_t lock;
	int cur;
	int stop;
	int ioctlfd;
        int *shutdown;
} expire;

#define DEFAULT_LIFE	5

#include <stdlib.h>

#ifdef TEST

/*rand is not thread safe. this is just for testing*/
#define EXPIRE_MULTI_EXTRA() (r = (rand() % 1) ? 0 : -1)
#define EXPIRE_MULTI() (r = (rand() % 1017) ? 0 : -1)

#else

#define EXPIRE_MULTI_EXTRA() \
	(r = ioctl( expire.ioctlfd, AUTOFS_IOC_EXPIRE_MULTI, NULL ))
#define EXPIRE_MULTI() \
	(r = ioctl( expire.ioctlfd, AUTOFS_IOC_EXPIRE_MULTI, NULL ))
#endif

/*extra thread to initiate directory expires*/
static void *extra_expire_mounts( void *x )
{
	int i, r, life = DEFAULT_LIFE;
	int j;
        pthread_t *ptr = ( pthread_t * ) x;

	pthread_setcanceltype( PTHREAD_CANCEL_ASYNCHRONOUS, NULL );

#ifdef TEST
	printf("\nextra_expire_mounts started\n");
#endif

	if( ! *ptr )
	{
		pthread_mutex_lock( &expire.lock );
		pthread_mutex_unlock( &expire.lock );
	}

	for( j = 0 ; j < 100 ; j++ )
	{
		for( i = 0, r = 0 ; ! r ; i++ )
		{
			EXPIRE_MULTI_EXTRA();
		}

		life = i < 2 ?  life - 1 : DEFAULT_LIFE;
		if( ! life )
		{
			pthread_mutex_lock( &expire.lock );
                        if( expire.stop ) {
                                pthread_mutex_unlock( &expire.lock );
                                return NULL;
                        }
                        pthread_detach( *ptr );
			*ptr = 0;
			pthread_mutex_unlock( &expire.lock );

#ifdef TEST
			printf("\nextra_expire_mounts end\n");
#endif
			return NULL;
		}
		sleep( 1 );
	}

	pthread_mutex_lock( &expire.lock );
        if( expire.stop ) {

#ifdef TEST
	printf("\nextra_expire_mounts end\n");
#endif
                pthread_mutex_unlock( &expire.lock );
                return NULL;
        }
        pthread_detach( *ptr );
	*ptr = 0;
	pthread_mutex_unlock( &expire.lock );

#ifdef TEST
	printf("\nextra_expire_mounts end\n");
#endif
	return NULL;
}

static void start_extra_expire_thread( void )
{
	int i;

	for( i = 0 ; i < EXPIRE_MAX_THREADS ; i++ )
	{
		if( ! expire.threads[ expire.cur ] )
		{
			/*not joinable*/
			pthread_mutex_lock( &expire.lock );
			if( thread_new_joinable( extra_expire_mounts,
					&expire.threads[ expire.cur ],
					&expire.threads[ expire.cur ] ) )
			{
				expire.cur = ( expire.cur + 1 )
						% EXPIRE_MAX_THREADS;
			}
			pthread_mutex_unlock( &expire.lock );
			return;
		}
		expire.cur = ( expire.cur + 1 ) % EXPIRE_MAX_THREADS;
	}
	msglog( MSG_NOTICE, "could not start extra expire thread" );
}

static void wait_extra_expirethread( void )
{
        int i;

        pthread_mutex_lock( &expire.lock );
        pthread_mutex_unlock( &expire.lock );
        /*if there are any remaining*/
        for( i = 0 ; i < EXPIRE_MAX_THREADS ; i++ )
        {
                if( expire.threads[ i ] )
                        pthread_join( expire.threads[ i ], NULL );
        }
}

/*separate thread to initiate directory expires*/
static void *main_expire_mounts( void *x )
{
	int i, r;

	pthread_setcanceltype( PTHREAD_CANCEL_ASYNCHRONOUS, NULL );

	while( 1 )
	{
		for( i = 0, r = 0 ; i < EXPIRE_MAX && ! r ; i++ )
		{
			EXPIRE_MULTI();
		}

		if( expire.stop ) {
			if( i == EXPIRE_MAX )
				continue;
                        wait_extra_expirethread();
                        *expire.shutdown = 1;
			return NULL;
                }

		if( i == EXPIRE_MAX )
			start_extra_expire_thread();
		else if( i < 2 )
			sleep( 1 );
	}
	return NULL;
}

/*Under graceful exit, shutdown gloable var is set by expire mechanism.
  That way as many as expires can be done before autodir exit.
*/
void expire_start( int time_out, int ioctlfd, int *shutdown )
{
  	int i;
	
	expire.stop = 0;
	expire.cur = 1;
	expire.ioctlfd = ioctlfd;
        expire.shutdown = shutdown;
        thread_mutex_init(&expire.lock);

	if( ! time_out )
		return;

	for( i = 0; i < EXPIRE_MAX_THREADS; i++ )
		expire.threads[ i ] = 0;

	/*IMP: We start first main thread with NULL data value*/
	if( ! thread_new_joinable( main_expire_mounts, NULL, &expire.main ) )
		msglog( MSG_FATAL, "could not start expire thread" );
}

void expire_stop_set(void)
{
	pthread_mutex_lock( &expire.lock );
	expire.stop = 1;
	pthread_mutex_unlock( &expire.lock );
}

void expire_stop( void )
{
	pthread_join( expire.main, NULL );
}













#ifdef TEST

char *autodir_name(void)
{
    return "test autodir";
}

int main(void)
{
	int shut = 0;

	msg_init();
	expire_start(1, -1, &shut);
	expire_stop();
}

#endif
