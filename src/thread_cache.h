
/*

Copyright (C) (2004 - 2005) (Venkata Ramana Enaganti) <ramana@intraperson.com>

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

#ifndef THREAD_CACHE_H
#define THREAD_CACHE_H

typedef struct thread_cache thread_cache;

#include <pthread.h>
#include "mpacket.h"

struct thread_cache {
        pthread_mutex_t lock;
        int stop;

	/*call-back to do the specific work.*/
        void (*cb)( Packet * );

	/*No of threads running at any time*/
        int thread_count;

	/*We first add to pending_count and then at regualr intervels
	   we transfer total accumulated count to thread_count*/
        int pending_count;

        pthread_cond_t count_cond;

	/*count of threads waiting for new packets*/
        int thread_waiting;
	/*We do not allow more then the following number of threads
	to wait at any time.*/
	int max_thread_wait;
        pthread_cond_t waiting_cond;

	/*FIFO buffer*/
        Packet **pkt_slots;
	int n_slots; /*size of FIFO buffer*/
        int in; /*packets in index*/
        int out; /*packets out index*/
};

void thread_cache_new( thread_cache *tc, Packet *pkt );
void thread_cache_init( thread_cache *tc, void (*cb)( Packet *), int n_slots,
				int max_thread_wait );
void thread_cache_stop( thread_cache *tc );

#endif
