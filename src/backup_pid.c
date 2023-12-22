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

#include <stdio.h>
#include <limits.h>
#include <sys/types.h>
#include "msg.h"
#include "miscfuncs.h"
#include "thread.h"
#include "backup_pid.h"


/*memory cache management for backup_child*/

#define CACHE_MAX 50

static struct {
	pthread_mutex_t lock;
	Backup_pid *list;
	int count;
} bcache;

static void entry_free( Backup_pid *to_free )
{
	Backup_pid *next;

	if( bcache.count < CACHE_MAX )
	{
		pthread_mutex_lock( &bcache.lock );
		for( ; to_free && bcache.count < CACHE_MAX; bcache.count++ )
		{
			next = to_free->next;
			to_free->next = bcache.list;
			bcache.list = to_free;
			to_free = next;
		}
		pthread_mutex_unlock( &bcache.lock );
	}
	for( ; to_free; to_free = next)
	{
		next = to_free->next;
		free( to_free );
	}
}

/*IMP should be called from single thread. NOT THREAD SAFE*/
/*use cache if available otherwise malloc*/
static Backup_pid *entry_allocate( void )
{
	static Backup_pid *list = 0;
	Backup_pid *tmp;

	if( list )
	{
		tmp = list;
		list = list->next;
		memset( tmp, 0, sizeof(*tmp) );
		return tmp;
	}

	if( bcache.count > 0 )
	{
		pthread_mutex_lock( &bcache.lock );
		if( bcache.count <= 0 )
		{
			pthread_mutex_unlock( &bcache.lock );
			goto dalloc;
		}

		tmp = bcache.list;
		list = bcache.list->next;
		bcache.list = NULL;
		bcache.count = 0;
		pthread_mutex_unlock( &bcache.lock );

		memset( tmp, 0, sizeof(*tmp) );
		return tmp;
	}

dalloc:
	tmp = (Backup_pid *) malloc( sizeof(Backup_pid) );
	if ( tmp )
		memset( tmp, 0, sizeof(*tmp) );
	return tmp;
}

/*this builds on top of memory cache management and emulates like above*/

/*This additional layer is mainly to do waitpid without worrying 
  about getting entry freed by someone else*/

static Backup_pids pids1;
static Backup_pids pids2;

static struct {
	pthread_mutex_t lock;
	Backup_pids *pids1;
	Backup_pids *pids2;
} pid_holder;

void backup_pid_init( int size )
{
	thread_mutex_init( &bcache.lock );
	bcache.list = 0;
	bcache.count = 0;

	size = size + size / 4;
	thread_mutex_init( &pid_holder.lock );

	pids1.s = (Backup_pid **) calloc( size, sizeof(Backup_pid) );
	pids1.free_slot = 0;
	pids1.in_use = 0;
	pids1.size = size;
	pids1.count = 0;
	pids1.to_free = 0;
	pid_holder.pids1 = &pids1;

	pids2.s = (Backup_pid **) calloc( size, sizeof(Backup_pid) );
	pids2.free_slot = 0;
	pids2.in_use = 0;
	pids2.size = size;
	pids2.count = 0;
	pids2.to_free = 0;
	pid_holder.pids2 = &pids2;
}

Backup_pids *backup_pids_get( void )
{
	Backup_pids *pids;

	pthread_mutex_lock( &pid_holder.lock );
	if( pid_holder.pids2->in_use )
	{
		pthread_mutex_unlock( &pid_holder.lock );
		return NULL;
	}
	pid_holder.pids1->in_use = 1;

	/*swap*/
	pids = pid_holder.pids1;
	pid_holder.pids1 = pid_holder.pids2;
	pid_holder.pids2 = pids;

	pthread_mutex_unlock( &pid_holder.lock );
	return pids;
}

void backup_pids_unget( Backup_pids *pids )
{
	Backup_pid *to_free = 0;
	Backup_pid *bp;
	int free_slot = -1;

	pthread_mutex_lock( &pid_holder.lock );
	to_free = pids->to_free;
	pids->to_free = NULL;
	for( bp = to_free; bp; bp = bp->next )
	{
		pids->s[ bp->pids_idx ] = 0;
		pids->count--;
		if( free_slot < 0 )
		    free_slot = bp->pids_idx;
	}
	pids->in_use = 0;
	if( free_slot != -1 )
		pids->free_slot = free_slot;
	pthread_mutex_unlock( &pid_holder.lock );

	for( bp = to_free; bp; bp = bp->next )
	{
		pthread_mutex_destroy( &bp->lock );
		pthread_cond_destroy( &bp->wait );
	}
	entry_free( to_free );
}

static int free_slot_get( Backup_pids *pids )
{
	int start;
	int i;

	i = start = pids->free_slot;
	do
	{
		if( ! pids->s[ i ] )
		{
			pids->free_slot++;
			pids->free_slot %= pids->size;
			return i;
		}
		i = ( i + 1 ) % pids->size;
	} while ( i != start );
	return -1;
}

/*use cache if available otherwise malloc*/
Backup_pid *backup_pidmem_allocate( void )
{
	Backup_pid *bp;
	int pids_idx;
	Backup_pids *pids = 0;

	bp = entry_allocate();
	bp->pid = -1;
	thread_mutex_init( &bp->lock );
	thread_cond_init( &bp->wait );

	pthread_mutex_lock( &pid_holder.lock );
	pids = pid_holder.pids1;
	pids_idx = free_slot_get( pids );
	if( pids_idx < 0 ) {
		pthread_mutex_unlock( &pid_holder.lock );
		entry_free( bp );
		return NULL;
	}
	bp->pids = pids;
	bp->pids_idx = pids_idx;
	pids->s[ pids_idx ] = bp;
	pids->count++;
	pthread_mutex_unlock( &pid_holder.lock );

	return bp;
}

void backup_pidmem_free( Backup_pid *bp )
{
	pthread_mutex_lock( &pid_holder.lock );
	if( bp->pids->in_use )
	{
		bp->next = bp->pids->to_free;
		bp->pids->to_free = bp;
		pthread_mutex_unlock( &pid_holder.lock );
		return;
	}
	bp->pids->s[ bp->pids_idx ] = 0;
	bp->pids->count--;
	pthread_mutex_unlock( &pid_holder.lock );
	bp->next = NULL;
	pthread_mutex_destroy( &bp->lock );
	pthread_cond_destroy( &bp->wait );
	entry_free( bp );
}












#ifdef TEST

char *autodir_name(void)
{
    return "test autodir";
}

void *test_th(void *x)
{
    Backup_pid *bc;

    sleep (1);
    while (1) {
	bc = backup_pidmem_allocate();
	//sleep (1);
	if (!bc) {
	    printf("mem allocation failed\n");
	    continue;
	}
	backup_pidmem_free(bc);
    }
}

void *test_th2(void *x)
{
    Backup_pids *pids;
    int i = 0, j;
    Backup_pid *bc;

    sleep (1);

    while (1) {
	pids = backup_pids_get();
	for( j = 0; j < pids->size; j++ )
	{
		bc = pids->s[ i ];
		if( bc )
		{
			pthread_mutex_lock( &bc->lock );
			pthread_mutex_unlock( &bc->lock );
		}
	}
	backup_pids_unget(pids);
	i++;
	if (i == 10) {
	    printf("backup_pids_get %d\n", i);
	    i = 0;
	}
    }
}

int main(void)
{
    pthread_t pt;

    backup_pid_init(20000);

    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);
    pthread_create(&pt, 0, test_th, 0);

    pthread_create(&pt, 0, test_th2, 0);

    pthread_join(pt, NULL);
}

#endif
