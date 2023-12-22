
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
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "msg.h"
#include "miscfuncs.h"
#include "thread.h"
#include "multipath.h"

/*All this code is to keep track of the usage count for virtual directories.
  For example, when both home directories /home/user1 and /home/.user1 accessed,
  usage count for directory user1 is 2.

  We need this tracking for deciding to start backup when usage count gets to 0.*/

#define MULTIPATH_HASH_SIZE (3)

/*cache of used memory size*/
#define MULTIPATH_CACHE_SIZE (50)

typedef struct mentry {
	char name[ NAME_MAX + 1 ];
	int count;
	unsigned int hash;
	struct mentry *next;
} mentry;

/* for reuse of malloced memory */
static struct {
	mentry *list;
	int count;
} mcache;

static mentry **mhash;
static int mhash_size;
static int mhash_used;
static pthread_mutex_t hash_lock; /*exclusive access for hash table*/

#define mentry_key( s )		( s % mhash_size )

/*allocate from the unused previousely allocated memory
  or else create new dynamic memory.

  mutex must be set before calling this.
*/
static mentry *mentry_malloc( void )
{
	mentry *tmp;

	if( mcache.count > 0 )
	{
		tmp = mcache.list;
		mcache.list = mcache.list->next;
		mcache.count--;
		return tmp;
	}

	return (mentry *) malloc( sizeof(mentry) );
}

/*mutex must be set before calling this.*/
static void mentry_free( mentry *me )
{
	mentry *tmp;

	if( ! me ) return;
	if( mcache.count < MULTIPATH_CACHE_SIZE )
	{
		tmp = mcache.list;
		mcache.list = me;
		me->next = tmp;
		mcache.count++;
		return;
	}
	else free( me );
}

static void mhash_resize( void )
{
	int new_size;
	int old_size = mhash_size;
	mentry **new_mhash;
	mentry **dptr;
	mentry **h = mhash;
	mentry *entry;
	mentry *next;

	new_size = mhash_size * 2;
	new_size |= 1;

	new_mhash = ( mentry ** ) calloc( new_size, sizeof(mentry *) );
	if( new_mhash == NULL )
	    return;

	while( old_size-- > 0 )
	{
		for( entry = *h++; entry; entry = next)
		{
			next = entry->next;
			dptr = &new_mhash[ entry->hash % new_size ];
			entry->next = *dptr;
			*dptr = entry;
		}
	}
	mhash_size = new_size;
	free(mhash);
	mhash = new_mhash;
}

/*not thread safe. mutual exclusion must be
  setup before calling this.
 */

#define MENTRY_LOCATE(name, hash, dptr)				\
do {								\
	dptr = &( mhash[ mentry_key( hash ) ] );		\
	while( *dptr )						\
	{							\
		if( (*dptr)->hash == hash &&			\
			*name == (*dptr)->name[0] &&		\
		       	! strcmp( name, (*dptr)->name ) )	\
			break;					\
		dptr = &( (*dptr)->next );			\
	}							\
} while( 0 );

/*public interface. Increment count for the given name.*/
int multipath_inc( const char *name )
{
	unsigned int hash;
	mentry *ent, **dptr;

	if( ! name || ! (*name) )
	{
		msglog( MSG_ERR, "multipath_inc: invalid name" );
		return 0;
	}

	hash = string_hash( name );

	pthread_mutex_lock( &hash_lock );

	MENTRY_LOCATE( name, hash, dptr );
	if( ! *dptr )
	{
		if( ! ( ent = mentry_malloc() ) )
		{
			pthread_mutex_unlock( &hash_lock );
			msglog( MSG_ALERT, "multipath_inc: " \
				"could not allocate memory" );
			return 0;
		}

		ent->count = 1;
		string_n_copy( ent->name, name, sizeof(ent->name) );
		ent->hash = hash;

		ent->next = NULL;
		*dptr = ent;
		mhash_used++;
		if( mhash_used > mhash_size )
		    mhash_resize();

		pthread_mutex_unlock( &hash_lock );
		return 1;
	}

	(*dptr)->count++;
	pthread_mutex_unlock( &hash_lock );
	return 1;
}

/*public interface. Decrement count for the given name.*/
int multipath_dec( const char *name )
{
	unsigned int hash;
	int count;
	mentry **dptr, *ent;

	if( ! name || ! (*name) )
	{
		msglog( MSG_ERR, "lockfile_unhash: invalid name" );
		return 0;
	}

	hash = string_hash( name );

	pthread_mutex_lock( &hash_lock );

	MENTRY_LOCATE( name, hash, dptr );
	ent = *dptr;
	if( ! ent ) /*entry does not exist*/
	{
		pthread_mutex_unlock( &hash_lock );
		msglog( MSG_ALERT, "multipath_dec: " \
				"entry for %s does not exist", name );
		return -1;
	}

	count = --(ent->count);
	if( ! count )
	{
		(*dptr) = ent->next;
		mentry_free( ent );
		mhash_used--;
	}
	pthread_mutex_unlock( &hash_lock );
	return count;
}

/*cleanup and initialization code*/
static void multipath_clean( void )
{
	int i;
	mentry *me, *tmp;

	/*clean hash first*/
	pthread_mutex_destroy( &hash_lock );

	if( mhash )
	{
		for( i = 0 ; i < mhash_size ; i ++ )
		{
			if( ! mhash[ i ] ) continue;
			me = mhash[ i ];
			while( me )
			{
				tmp = me;
				me = me->next;
				free( tmp );
			}
		}
		free( mhash );
	}

	/*clean cache*/
	me = mcache.list;

	while( me )
	{
		tmp = me;
		me = me->next;
		free( tmp );
	}
}

void multipath_init( void )
{
	mhash = ( mentry ** ) calloc( MULTIPATH_HASH_SIZE,
			sizeof(mentry *) );

	if( ! mhash )
		msglog( MSG_FATAL, "multipath_init: " \
			"could not allocate hash table" );

	mhash_used = 0;
	mhash_size = MULTIPATH_HASH_SIZE;

        thread_mutex_init(&hash_lock);
	mcache.list = NULL;
	mcache.count = 0;
	
	if( atexit( multipath_clean ) )
	{
		multipath_clean();
		msglog( MSG_FATAL, "multipath_init: " \
			"could not register cleanup method" );
	}
}
















#ifdef TEST

char *autodir_name(void)
{
    return "test autodir";
}

void *test_th(void *s)
{
    char *str = (char *) s;
    int i = 0;

    sleep(2);
    while (1) {
	i++;
	if (multipath_inc(str)) {
	    multipath_dec(str);
	}
	if (i == 1000000) {
	    printf("%s: %d\n", str, i);
	    i = 0;
	}
    }
}

/* compile  gcc -g -DTEST multipath.c msg.o  miscfuncs.o -lpthread thread.o */

int main(void)
{
    pthread_t id;

    thread_init();
    msg_init();
    multipath_init();

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

    pthread_join(id, NULL);
}

#endif
