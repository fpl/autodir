/*

Copyright (C) (2004 - 2006) (Venkata Ramana Enaganti) <ramana@intraperson.com>
Copyright (C) 2007 - 2023 (Francesco Paolo Lovergine) <frankie@debian.org>


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

/*for keeping backup requests and for launching backup when timeouts*/

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "msg.h"
#include "miscfuncs.h"
#include "thread.h"
#include "time_mono.h"
#include "backup_queue.h"

#ifdef TEST

void backup_child_start(const char *name, const char *path);
int backup_child_count(void);
void backup_child_kill(const char *name);
void backup_child_wait(const char *name);

#else
#include "backup_child.h"
#endif

#define HASH_SIZE		13

typedef struct bqueue {
	unsigned int hash;

	char dname[NAME_MAX+1];
	char dpath[PATH_MAX+1];
	time_t estamp;	 /*time when entry added to chain*/

	/*for hash double list*/
	struct bqueue *next;
	struct bqueue *prev;

	/*entry creation time stamp based double linked list*/
	struct bqueue *next_t;
	struct bqueue *prev_t;

	int in_bchain;
	struct bqueue *bchain_next;
} Bqueue;

static struct {
	Bqueue **hash;
	int hash_used;
	int hash_size;

	time_t wait; /*how long to wait before starting backup*/
	int maxproc; /*max backup proc limit*/

	/*mutex access to hash structure*/
	pthread_mutex_t lock;

	/*thread that keep track of entries
	  which are ready for backup*/
	pthread_t queue_watch; 

	int stop; /* cleanup started? */

	/*for time stamp based double linked list*/
	Bqueue *start_t;
	Bqueue *end_t;
	Bqueue *cur_t;

	pthread_cond_t bchain_wait;
	Bqueue *bchain;
} BQ;


/***********************************************/
/* dynamic memory caching methods	       */
/***********************************************/


/*for dynamic memory cache to minimize dependence on malloc.*/

#define CACHE_MAX 50

static struct {
	int count;
	Bqueue *list;
	pthread_mutex_t lock;
} bcache;

static void entry_free( Bqueue *bq )
{
	Bqueue *tmp;

	if( bcache.count < CACHE_MAX )
	{
		pthread_mutex_lock( &bcache.lock );
		if( bcache.count >= CACHE_MAX )
		{
			pthread_mutex_unlock( &bcache.lock );
			free( bq );
			return;
		}
		tmp = bcache.list;
		bcache.list = bq;
		bq->next = tmp;
		bcache.count++;
		pthread_mutex_unlock( &bcache.lock );

		return;
	}
	free( bq );
}

/*use cache if available otherwise malloc*/
static Bqueue *entry_allocate( void )
{
	Bqueue *tmp;

	if( bcache.count > 0 )
	{
		pthread_mutex_lock( &bcache.lock );
		if( bcache.count <= 0 )
		{
			pthread_mutex_unlock( &bcache.lock );
			goto dalloc;
		}

		tmp = bcache.list;
		bcache.list = bcache.list->next;
		bcache.count--;
		pthread_mutex_unlock( &bcache.lock );

		memset( tmp, 0, sizeof(*tmp) );
		return tmp;
	}

dalloc:
	if( ( tmp = (Bqueue *) malloc( sizeof(Bqueue) ) ) )
		memset( tmp, 0, sizeof(*tmp) );
	return tmp;
}


/***********************************************/
/* hash manipulation		               */
/***********************************************/


#define QUEUE_ENTRY_LOCATE(name, hash) \
		(queue_entry_locate((name), (hash), (hash) % BQ.hash_size))

/*
   Mutual exclusion should be in effect before calling this.
 */
static Bqueue *queue_entry_locate( const char *name, unsigned int hash,
								    int key )
{
	Bqueue *bq;

	bq = BQ.hash[ key ];

	while( bq )
	{
		if( hash == bq->hash && 
				*name == bq->dname[0] &&
				! strcmp( bq->dname, name ) )
			return bq;
		bq = bq->next;
	}
	return NULL;
}

static void hash_resize( void )
{
	int new_size;
	int old_size = BQ.hash_size;
	Bqueue **new_hash;
	Bqueue **dptr;
	Bqueue **h = BQ.hash;
	Bqueue *entry;
	Bqueue *next;

	new_size = BQ.hash_size * 2;
	new_size |= 1;

	new_hash = ( Bqueue ** ) calloc( new_size, sizeof(Bqueue *) );
	if( new_hash == NULL )
	    return;

	while( old_size-- > 0 )
	{
		for( entry = *h++; entry; entry = next)
		{
			next = entry->next;
			dptr = &new_hash[ entry->hash % new_size ];
			if (*dptr) {
			    entry->next = *dptr;
			    (*dptr)->prev = entry;
			} else {
			    entry->next = NULL;
			}
			*dptr = entry;
			entry->prev = NULL;
		}
	}
	BQ.hash_size = new_size;
	free(BQ.hash);
	BQ.hash = new_hash;
}

/*Only linked lists and pointers to them are manipulated.
  Mutual exclusion should be in effect before calling this.

  Does not deal with bchain list.
*/
static void queue_entry_release( Bqueue *bq )
{
	Bqueue *nxt, *prv;
	int key;

	BQ.hash_used--;

	prv = bq->prev_t;
	nxt = bq->next_t;
	/*update BQ structure if anything points to current entry*/
	if( bq == BQ.start_t ) BQ.start_t = nxt;
	if( bq == BQ.cur_t )   BQ.cur_t = nxt;
	if( bq == BQ.end_t )   BQ.end_t = prv;

	/*release links in time based double link list*/
	if( nxt ) nxt->prev_t = prv;
	if( prv ) prv->next_t = nxt;

	/*release links in hash double link list*/
	prv = bq->prev;
	nxt = bq->next;

	if( nxt ) nxt->prev = prv;
	if( prv ) prv->next = nxt;

	key = bq->hash % BQ.hash_size;
	/*first entry in hash chain?*/
	if( BQ.hash[ key ] == bq )
	       BQ.hash[ key ] = nxt;
}

/*
   Manipulate only linked lists and pointers for them. 

   Mutual exclusion should be in effect before calling this.

   Does not deal with bchain list.
*/
static int queue_entry_add( Bqueue *new )
{
	int key;
	unsigned int hash;
	Bqueue **dptr;

	BQ.hash_used++;
	if (BQ.hash_used > BQ.hash_size) {
		hash_resize();
	}

	hash = new->hash = string_hash( new->dname );
	key = hash % BQ.hash_size;
	if (queue_entry_locate(new->dname, hash, key))
		return (0);

	dptr = &BQ.hash[ key ];
	new->next = *dptr;
	if( *dptr )
		(*dptr)->prev = new;
	*dptr = new;
	new->prev = NULL;

	/*update time based linked list*/
	new->next_t = NULL;
	if( ! BQ.start_t )
	{
		new->prev_t = NULL;
		BQ.cur_t = BQ.start_t = BQ.end_t = new;
	}
	else
	{
		new->prev_t = BQ.end_t;
		if( ! BQ.cur_t )
		{
			BQ.cur_t = new;
		}
		BQ.end_t->next_t = new;
		BQ.end_t = new;
	}
	return 1;
}


/***********************************************/
/* backup starting mechanism		       */
/***********************************************/



/* PERFORMANCE IMPROVEMENTS
	This is major performance improvement done with bchain.
	Previsousely backup programs forked while in main mutual exclusion.
	Forking is taken outside with additional benifit of
	starting as many backup programs as we wish at a time in between waitings.
*/

static void bchain_process( void )
{
	Bqueue *bc, *next;

	for( bc = BQ.bchain ; bc ; bc = bc->bchain_next )
	{
		backup_child_start(bc->dname, bc->dpath);
		mono_nanosleep( 100000000 );
	}

	pthread_mutex_lock( &BQ.lock );
	for( bc = BQ.bchain ; bc ; bc = bc->bchain_next )
		queue_entry_release( bc );
	pthread_mutex_unlock( &BQ.lock );

	pthread_cond_broadcast( &BQ.bchain_wait );
	for( bc = BQ.bchain ; bc ; bc = next ) {
		next = bc->bchain_next;
		entry_free(bc);
	}
}

/*monitor time based linked list
  and start backup when wait time expired
 */

static void queue_watch_wait( int dift )
{
	while( ! BQ.cur_t || dift > 0 )
	{
		sleep( 1 );
		if( dift > 0 )
			dift--;
		if( BQ.stop )
			pthread_exit( NULL );
	}
	if( BQ.stop )
		pthread_exit( NULL );
}

#define BACK_START_MAX		300

/*monitor queue*/
static void *queue_watch_thread( void *x )
{
	int dift;
	int i;
	time_t cte;
	Bqueue **bchain;
	int child_count;

	while( 1 )
	{
		dift = 0;

		/* get locks and set something to workon*/
		queue_watch_wait( dift );
		child_count = backup_child_count();
		if( child_count < 0 || pthread_mutex_trylock( &BQ.lock ) )
		{
			dift = 1;
			continue;
		}

		cte = time_mono();
		/* still got to wait for starting backup?*/
		if( ! BQ.cur_t ||
			( dift = BQ.wait - ( cte - BQ.cur_t->estamp ) ) > 0 )
		{
			pthread_mutex_unlock( &BQ.lock );
			continue;
		}

		bchain = &BQ.bchain;
		*bchain = NULL;
		/*who is ready for backup? make a list first while mutex locked*/
		for( i = 0 ; i < BACK_START_MAX
				    && ( ! BQ.wait || dift <= 0 )
				    && BQ.cur_t
				    && child_count + i <= BQ.maxproc; i++ )
		{
			*bchain = BQ.cur_t;
			BQ.cur_t->in_bchain = 1;
			bchain = &( BQ.cur_t->bchain_next );
			*bchain = NULL;
			if( ( BQ.cur_t = BQ.cur_t->next_t ) )
				dift = BQ.wait - ( cte - BQ.cur_t->estamp );
		}
		pthread_mutex_unlock( &BQ.lock );

		/*something in bchain list?*/
		if( BQ.bchain )
			bchain_process();
	}
    return x;
}

void backup_queue_add( const char *name, const char *path )
{
	int r;
	Bqueue *bc;

	if( BQ.stop )
		return;

	if( ! ( bc = entry_allocate() ) )
	{
		msglog( MSG_ALERT,
			"backup_queue_add: could not get free entry" );
		return;
	}

	/* initialize entry data here*/
	string_n_copy( bc->dname, name, sizeof(bc->dname) );
	string_n_copy( bc->dpath, path, sizeof(bc->dpath) );
	bc->estamp = time_mono();

	/*add to the list and update links while mutex locked*/
	pthread_mutex_lock( &BQ.lock );
	r = queue_entry_add( bc );
	pthread_mutex_unlock( &BQ.lock );

	if( ! r )
	    entry_free( bc );
}

int backup_queue_remove( const char *name )
{
	Bqueue *bq;
	unsigned int hash;

	hash = string_hash( name );

	pthread_mutex_lock( &BQ.lock );
	bq = QUEUE_ENTRY_LOCATE( name, hash );
	if( bq ) {
		/* entry in bchain list? then wait*/
		if( bq->in_bchain )
		{
			pthread_cond_wait( &( BQ.bchain_wait ), &BQ.lock );
			pthread_mutex_unlock( &BQ.lock );
			return 0;
		}
		else
		{
			queue_entry_release( bq );
			pthread_mutex_unlock( &BQ.lock );
			entry_free( bq );
			return (1);
		}
	}
	pthread_mutex_unlock( &BQ.lock );
	return 0;
}

void backup_queue_stop_set( void )
{
	BQ.stop = 1;
}

/*before autodir exit*/
void backup_queue_stop( void )
{
	BQ.stop = 1;
	pthread_join( BQ.queue_watch, NULL );
}

/* startup initialization*/
void backup_queue_init( int bwait, int maxproc )
{
	memset( &BQ, 0, sizeof(BQ) );
	BQ.hash = (Bqueue **) calloc( HASH_SIZE, sizeof(Bqueue *) );
	if( ! BQ.hash )
		msglog( MSG_FATAL, 
			"backup_queue_init: could not allocate memory" );
	BQ.hash_size = HASH_SIZE;

	BQ.wait = bwait;
	BQ.maxproc = maxproc;

        thread_mutex_init(&BQ.lock);
        thread_cond_init(&BQ.bchain_wait);

        thread_mutex_init(&bcache.lock);
	bcache.list = NULL;
	bcache.count = 0;

	if( ! thread_new_joinable( queue_watch_thread, NULL, &BQ.queue_watch ) )
		msglog( MSG_FATAL, 
			"backup_queue_init: could not start new thread" );
}










#ifdef TEST

char *autodir_name(void)
{
    return "test autodir";
}

void backup_child_init( int unused )
{
}

void backup_child_stop(void)
{
}

void backup_child_start(const char *name, const char *path)
{
    printf("start %s, path %s\n", name, path);
    sleep( 5 );
}

int backup_child_count(void)
{
    return (1);
}

void backup_child_kill(const char *name)
{
    printf("kill %s\n", name);
    sleep( 5 );
}

void backup_child_wait(const char *name)
{
    printf("wait %s\n", name);
}

#ifdef TEST1

void *test_th(void *s)
{
    char *str = (char *) s;
    int i = 0;

    sleep(2);
    while (1) {
	i++;
	backup_queue_add(str, "/tmp");
	//sleep(1);
	//backup_queue_remove(str);
	if (i == 1000000) {
	    printf("%s: %d\n", str, i);
	    i = 0;
	}
    }
}

void *test_th2(void *s)
{
    char *str = (char *) s;
    int i = 0;

    sleep(2);
    while (1) {
	i++;
	//backup_queue_add(str, "/tmp");
	//sleep(1);
	backup_queue_remove(str);
	if (i == 1000000) {
	    printf("%s: %d\n", str, i);
	    i = 0;
	}
    }
}


int main(void)
{
    pthread_t id;

    thread_init();
    msg_option_verbose('x', "", 1);
    msg_init();
    msg_console_on();
    backup_queue_init(0, 1000);

    pthread_create(&id, 0, test_th, "1");
    pthread_create(&id, 0, test_th, "2");
    pthread_create(&id, 0, test_th, "3");
    pthread_create(&id, 0, test_th, "4");
    pthread_create(&id, 0, test_th, "5");
    pthread_create(&id, 0, test_th, "6");
    pthread_create(&id, 0, test_th, "7");
    pthread_create(&id, 0, test_th, "8");
    pthread_create(&id, 0, test_th, "9");
    pthread_create(&id, 0, test_th, "a");
    pthread_create(&id, 0, test_th, "b");
    pthread_create(&id, 0, test_th, "c");
    pthread_create(&id, 0, test_th, "d");
    pthread_create(&id, 0, test_th, "e");
    pthread_create(&id, 0, test_th, "f");
    pthread_create(&id, 0, test_th, "g");
    pthread_create(&id, 0, test_th, "h");
    pthread_create(&id, 0, test_th, "i");
    pthread_create(&id, 0, test_th, "j");
    pthread_create(&id, 0, test_th, "k");
    pthread_create(&id, 0, test_th, "l");
    pthread_create(&id, 0, test_th, "m");
    pthread_create(&id, 0, test_th, "n");
    pthread_create(&id, 0, test_th, "o");
    pthread_create(&id, 0, test_th, "p");
    pthread_create(&id, 0, test_th, "q");
    pthread_create(&id, 0, test_th, "r");
    pthread_create(&id, 0, test_th, "s");
    pthread_create(&id, 0, test_th, "t");
    pthread_create(&id, 0, test_th, "u");
    pthread_create(&id, 0, test_th, "v");
    pthread_create(&id, 0, test_th, "w");
    pthread_create(&id, 0, test_th, "x");
    pthread_create(&id, 0, test_th, "y");
    pthread_create(&id, 0, test_th, "z");

    pthread_create(&id, 0, test_th, "1");
    pthread_create(&id, 0, test_th, "2");
    pthread_create(&id, 0, test_th, "3");
    pthread_create(&id, 0, test_th, "4");
    pthread_create(&id, 0, test_th, "5");
    pthread_create(&id, 0, test_th, "6");
    pthread_create(&id, 0, test_th, "7");
    pthread_create(&id, 0, test_th, "8");
    pthread_create(&id, 0, test_th, "9");
    pthread_create(&id, 0, test_th, "a");
    pthread_create(&id, 0, test_th, "b");
    pthread_create(&id, 0, test_th, "c");
    pthread_create(&id, 0, test_th, "d");
    pthread_create(&id, 0, test_th, "e");
    pthread_create(&id, 0, test_th, "f");
    pthread_create(&id, 0, test_th, "g");
    pthread_create(&id, 0, test_th, "h");
    pthread_create(&id, 0, test_th, "i");
    pthread_create(&id, 0, test_th, "j");
    pthread_create(&id, 0, test_th, "k");
    pthread_create(&id, 0, test_th, "l");
    pthread_create(&id, 0, test_th, "m");
    pthread_create(&id, 0, test_th, "n");
    pthread_create(&id, 0, test_th, "o");
    pthread_create(&id, 0, test_th, "p");
    pthread_create(&id, 0, test_th, "q");
    pthread_create(&id, 0, test_th, "r");
    pthread_create(&id, 0, test_th, "s");
    pthread_create(&id, 0, test_th, "t");
    pthread_create(&id, 0, test_th, "u");
    pthread_create(&id, 0, test_th, "v");
    pthread_create(&id, 0, test_th, "w");
    pthread_create(&id, 0, test_th, "x");
    pthread_create(&id, 0, test_th, "y");
    pthread_create(&id, 0, test_th, "z");

    pthread_create(&id, 0, test_th2, "1");
    pthread_create(&id, 0, test_th2, "2");
    pthread_create(&id, 0, test_th2, "3");
    pthread_create(&id, 0, test_th2, "4");
    pthread_create(&id, 0, test_th2, "5");
    pthread_create(&id, 0, test_th2, "6");
    pthread_create(&id, 0, test_th2, "7");
    pthread_create(&id, 0, test_th2, "8");
    pthread_create(&id, 0, test_th2, "9");
    pthread_create(&id, 0, test_th2, "a");
    pthread_create(&id, 0, test_th2, "b");
    pthread_create(&id, 0, test_th2, "c");
    pthread_create(&id, 0, test_th2, "d");
    pthread_create(&id, 0, test_th2, "e");
    pthread_create(&id, 0, test_th2, "f");
    pthread_create(&id, 0, test_th2, "g");
    pthread_create(&id, 0, test_th2, "h");
    pthread_create(&id, 0, test_th2, "i");
    pthread_create(&id, 0, test_th2, "j");
    pthread_create(&id, 0, test_th2, "k");
    pthread_create(&id, 0, test_th2, "l");
    pthread_create(&id, 0, test_th2, "m");
    pthread_create(&id, 0, test_th2, "n");
    pthread_create(&id, 0, test_th2, "o");
    pthread_create(&id, 0, test_th2, "p");
    pthread_create(&id, 0, test_th2, "q");
    pthread_create(&id, 0, test_th2, "r");
    pthread_create(&id, 0, test_th2, "s");
    pthread_create(&id, 0, test_th2, "t");
    pthread_create(&id, 0, test_th2, "u");
    pthread_create(&id, 0, test_th2, "v");
    pthread_create(&id, 0, test_th2, "w");
    pthread_create(&id, 0, test_th2, "x");
    pthread_create(&id, 0, test_th2, "y");
    pthread_create(&id, 0, test_th2, "z");

    pthread_create(&id, 0, test_th2, "1");
    pthread_create(&id, 0, test_th2, "2");
    pthread_create(&id, 0, test_th2, "3");
    pthread_create(&id, 0, test_th2, "4");
    pthread_create(&id, 0, test_th2, "5");
    pthread_create(&id, 0, test_th2, "6");
    pthread_create(&id, 0, test_th2, "7");
    pthread_create(&id, 0, test_th2, "8");
    pthread_create(&id, 0, test_th2, "9");
    pthread_create(&id, 0, test_th2, "a");
    pthread_create(&id, 0, test_th2, "b");
    pthread_create(&id, 0, test_th2, "c");
    pthread_create(&id, 0, test_th2, "d");
    pthread_create(&id, 0, test_th2, "e");
    pthread_create(&id, 0, test_th2, "f");
    pthread_create(&id, 0, test_th2, "g");
    pthread_create(&id, 0, test_th2, "h");
    pthread_create(&id, 0, test_th2, "i");
    pthread_create(&id, 0, test_th2, "j");
    pthread_create(&id, 0, test_th2, "k");
    pthread_create(&id, 0, test_th2, "l");
    pthread_create(&id, 0, test_th2, "m");
    pthread_create(&id, 0, test_th2, "n");
    pthread_create(&id, 0, test_th2, "o");
    pthread_create(&id, 0, test_th2, "p");
    pthread_create(&id, 0, test_th2, "q");
    pthread_create(&id, 0, test_th2, "r");
    pthread_create(&id, 0, test_th2, "s");
    pthread_create(&id, 0, test_th2, "t");
    pthread_create(&id, 0, test_th2, "u");
    pthread_create(&id, 0, test_th2, "v");
    pthread_create(&id, 0, test_th2, "w");
    pthread_create(&id, 0, test_th2, "x");
    pthread_create(&id, 0, test_th2, "y");
    pthread_create(&id, 0, test_th2, "z");

    pthread_join(id, NULL);
}

#endif
#endif
