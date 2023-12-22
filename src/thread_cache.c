
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

#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include "thread.h"
#include "mpacket.h"
#include "msg.h"
#include "time_mono.h"
#include "thread_cache.h"

/*We maintain some spare threads here to improve performance*/


/*New threads are given their own packets first.
But subsequently, threads try to get packets
from cache using following method.

This could be exit point for a thread.*/
static Packet *get_cache( thread_cache *tc )
{
        Packet *pkt;

        while( pthread_mutex_trylock( &tc->lock ) )
		sleep( 1 );
	pkt = tc->pkt_slots[ tc->out ];
        while( 1 )
        {
                if( pkt )
                {
                        tc->pkt_slots[ tc->out ] = NULL;
			tc->out = ( tc->out + 1 ) % tc->n_slots;
                        pthread_mutex_unlock( &tc->lock );
                        return pkt;
                }
		/*we do not keep more threads than allowed*/
                if( tc->stop || tc->thread_waiting >= tc->max_thread_wait  )
                {
                        tc->thread_count--;
                        if( tc->stop && ! tc->thread_count )
                                pthread_cond_signal( &tc->count_cond );
                        pthread_mutex_unlock( &tc->lock );
                        pthread_exit( NULL );
                }
                tc->thread_waiting++;
                pthread_cond_wait( &tc->waiting_cond, &tc->lock );
                tc->thread_waiting--;

		pkt = tc->pkt_slots[ tc->out ];
        }
}

#ifdef TEST
#define MAX_REUSE 2
#else
#define MAX_REUSE 300
#endif

/*re-usable thread. But we do not stay alive for ever.
  Thread argument must not be null.*/
static void *thread_cache_thread( void *x )
{
	int i = 0;
        Packet *pkt = (Packet *) x;
        thread_cache *tc;

	pthread_detach( pthread_self() );
	/*we only service limited requests per thread*/
	do
	{
        	tc = pkt->tc;

		/*call back do the actual work*/
		tc->cb( pkt );
		if( ++i > MAX_REUSE )
			break;
		pkt = get_cache( tc );
	} while( pkt );
	
	/*decrement count and exit*/
	pthread_mutex_lock( &tc->lock );
	tc->thread_count--;
	pthread_mutex_unlock( &tc->lock );
	pthread_exit( NULL );
}

/*Adding new packets to FIFO buffer.

Add new packets to the cache OR start threads to hanle them directly.

pending_count is used by single thread and can be operated without locks.
*/
void thread_cache_new( thread_cache *tc, Packet *pkt )
{
	int pkt_pending;

	pkt->tc = tc;
	pthread_mutex_lock( &tc->lock );
	pkt_pending = tc->out <= tc->in
				? tc->in - tc->out
				: tc->n_slots - tc->out + tc->in;
	/*if( no thread waiting OR
		cache buffer full OR
		not enough waiting threads? )*/
	if( ! tc->thread_waiting || tc->pkt_slots[ tc->in ] ||
				pkt_pending > tc->thread_waiting )
	{
		/*This is optimizatoin*/
		if( tc->pending_count > 30000 )
		{
			tc->thread_count += tc->pending_count;
			tc->pending_count = 0;
		}
		pthread_mutex_unlock( &tc->lock );
		/*Try starting new thread*/
		if( thread_new( thread_cache_thread, pkt, NULL ) )
		{
			tc->pending_count++;
			return;
		}
		pthread_mutex_lock( &tc->lock );
		/*if there is not even one thread waiting OR
		buffer cache is full, we forcefully start a thread*/
		if( ! tc->thread_waiting || tc->pkt_slots[ tc->in ] )
		{
			pthread_mutex_unlock( &tc->lock );
			thread_new_wait( thread_cache_thread, pkt, 1 );
			tc->pending_count++;
			return;
		}
		/*If the above conditions fail, we add packet
		to the cache and hope existing threads handle
		through thread reuse mechanism.*/
	}
	/*add to the buffer and wake up existing threads.*/
	tc->pkt_slots[ tc->in ] = pkt;
	tc->in = ( tc->in + 1 ) % tc->n_slots;
	pthread_mutex_unlock( &tc->lock );

	pthread_cond_signal( &tc->waiting_cond );
}

void thread_cache_init( thread_cache *tc, void (*cb)( Packet *),
			int n_slots, int max_thread_wait )
{
        thread_mutex_init(&tc->lock);
        thread_cond_init(&tc->count_cond);
        thread_cond_init(&tc->waiting_cond);

	tc->stop = 0;
	tc->cb = cb;

	tc->thread_count = 0;
	tc->pending_count = 0;
	tc->thread_waiting = 0;

	tc->n_slots = n_slots;
	tc->max_thread_wait = max_thread_wait;
	tc->pkt_slots = ( Packet ** ) calloc( n_slots, sizeof(Packet *) );
	if( ! tc->pkt_slots )
		msglog( MSG_FATAL, "thread_cache_init: " \
				"could not allocate memory" );
	tc->in = 0;
	tc->out = 0;
}

/*Before shutdown. Try waiting for all threads.*/
void thread_cache_stop( thread_cache *tc )
{
	int i;
        struct timespec timeout;

	tc->stop = 1;

	pthread_mutex_lock( &tc->lock );
	tc->thread_count += tc->pending_count;
	tc->pending_count = 0;

	for( i = 0; i < 3 && tc->thread_count; i++ )
	{
                thread_cond_timespec(&timeout, 2 * i + 1);
		pthread_cond_broadcast( &tc->waiting_cond );
                pthread_cond_timedwait( &tc->count_cond,
					&tc->lock, &timeout );
#if 0
		pthread_cond_wait( &tc->count_cond, &tc->lock);
#endif
	}
#if 0
	if( tc->thread_count )
		msglog( MSG_ERR, "threads still remaining %d",
				tc->thread_count );
#endif
	pthread_mutex_unlock( &tc->lock );
}












#ifdef TEST

char *autodir_name(void)
{
	return "test autodir";
}

void cb(Packet *pkt)
{
	sleep(1);
	packet_free(pkt);
}

int main(void)
{
	thread_cache tc;
	Packet *pkt;
	int i = 0;

	msg_init();
	msg_console_on();
	thread_init();
	packet_init();
	thread_cache_init(&tc, cb, 300, 30);

	while (1) {
		pkt = packet_allocate();
		thread_cache_new(&tc, pkt);
		i++;
		if (i == 10000) {
			printf("thread_cache_new %d\n", i);
			i = 0;
		}
	}
}

#endif
