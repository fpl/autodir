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

/* mutual execlusion based on strings.
   ie locking string names and unlocking.
*/

#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <malloc.h>
#include <stdlib.h>
#include <pthread.h>
#include <linux/limits.h>

#include "miscfuncs.h"
#include "msg.h"
#include "thread.h"
#include "workon.h"

#define WORKON_HASH_SIZE (13)
#define WORKON_CACHE_SIZE (50)

typedef struct wentry {
	char name[ NAME_MAX + 1 ]; /*file/dir name can not exceed more then this*/
	unsigned int hash;
	int in_use;
	pthread_mutex_t wait;	/*hang on here if some one else locked it already*/
	struct wentry *next;
} Wentry;

/* for reuse of malloced memory */
static struct {
	Wentry *list;
	int count;
	pthread_mutex_t lock;
} wcache;

static Wentry **whash;
static int whash_size;
static int whash_used;
static pthread_mutex_t hash_lock;

#define wentry_key( s )		( s % whash_size )

/*allocate from the unused previousely allocated memory
or else create new dynamic memory.
*/
static Wentry *wentry_malloc( void )
{
	Wentry *tmp;

	if( wcache.count > 0 )
	{
		pthread_mutex_lock( &wcache.lock );
		if( wcache.count <= 0 )
		{
			pthread_mutex_unlock( &wcache.lock );
			goto dyn_alloc;
		}
		tmp = wcache.list;
		wcache.list = wcache.list->next;
		wcache.count--;
		pthread_mutex_unlock( &wcache.lock );
		return tmp;
	}

dyn_alloc:
	return (Wentry *) malloc( sizeof(Wentry) );
}

static void wentry_free( Wentry *we )
{
	Wentry *tmp;

	if( ! we )
		return;

	pthread_mutex_destroy( &we->wait );

	if( wcache.count < WORKON_CACHE_SIZE )
	{
		pthread_mutex_lock( &wcache.lock );
		if( wcache.count >= WORKON_CACHE_SIZE )
		{
			pthread_mutex_unlock( &wcache.lock );
			free( we );
			return;
		}
		tmp = wcache.list;
		wcache.list = we;
		we->next = tmp;
		wcache.count++;
		pthread_mutex_unlock( &wcache.lock );
		return;
	}
	else free( we );
}

static void whash_resize( void )
{
	int new_size;
	int old_size = whash_size;
	Wentry **new_whash;
	Wentry **dptr;
	Wentry **h = whash;
	Wentry *wentry;
	Wentry *next;

	new_size = whash_size * 2;
	new_size |= 1;

	new_whash = ( Wentry ** ) calloc( new_size, sizeof(Wentry *) );
	if( new_whash == NULL )
	    return;

	while( old_size-- > 0 )
	{
		for( wentry = *h++; wentry; wentry = next)
		{
			next = wentry->next;
			dptr = &new_whash[ wentry->hash % new_size ];
			wentry->next = *dptr;
			*dptr = wentry;
		}
	}
	whash_size = new_size;
	free(whash);
	whash = new_whash;
}

/*not thread safe. mutual exclusion must be
  setup before calling this.
 */

#define WENTRY_LOCATE( name, hash, dptr )			\
do {								\
	dptr = &( whash[ wentry_key( hash ) ] ); 		\
	while( *dptr ) 						\
	{							\
		if( (*dptr)->hash == hash && 			\
			*name == (*dptr)->name[0] &&		\
		       	! strcmp( name, (*dptr)->name ) ) 	\
			break; 					\
		dptr = &( (*dptr)->next ); 			\
	} 							\
} while( 0 )


int workon_name( const char *name )
{
	unsigned int hash;
	Wentry **dptr, *new_ent, *ent;

	if( ! name || ! (*name) )
	{
		msglog( MSG_ERR, "workon_name: invalid name" );
		return 0;
	}

	/*PERFORMANCE:
	we allocate in advance. in almost all cases we
	need new entry to be added.*/
	new_ent = wentry_malloc();
	if( ! new_ent )
	{
		msglog( MSG_ALERT, "workon_name: " \
				"could not allocate memory" );
		return 0;
	}

	hash = string_hash( name );
	string_n_copy( new_ent->name, name, sizeof(new_ent->name) );
	new_ent->hash = hash;
	new_ent->in_use = 1;
	new_ent->next = NULL;
        thread_mutex_init(&new_ent->wait);

	/*Now the actual part*/
 	pthread_mutex_lock( &hash_lock );

	WENTRY_LOCATE( name, hash, dptr );
	ent = *dptr;
	if( ent ) /*entry exists*/
	{
		( ent->in_use )++;
		pthread_mutex_unlock( &hash_lock );

		/*we need to free new entry allocated above*/
		wentry_free( new_ent );
		pthread_mutex_lock( &( ent->wait ) );
		return 1;
	}

	(*dptr) = new_ent;
	whash_used++;
	if( whash_used > whash_size )
	    whash_resize();
	pthread_mutex_unlock( &hash_lock );
	pthread_mutex_lock( &new_ent->wait );

	return 1;
}

void workon_release( const char *name )
{
	Wentry **dptr, *ent, *tmp = NULL;
	unsigned int hash;

	if( ! name || ! (*name) )
	{
		msglog( MSG_ERR, "workon_release: invalid name" );
		return;
	}

	hash = string_hash( name );

	pthread_mutex_lock( &hash_lock );
	WENTRY_LOCATE( name, hash, dptr );
	ent = *dptr;
	if( ! ent ) /*entry does not exist*/
	{
		pthread_mutex_unlock( &hash_lock );
		msglog( MSG_ALERT, "workon_release: " \
				"entry for %s does not exist", name );
		return;
	}

	( ent->in_use )--;
	if( ! ( ent->in_use ) )
	{
		(*dptr) = ent->next;
		tmp = ent;
		whash_used--;
	}
	pthread_mutex_unlock( &hash_lock );
	pthread_mutex_unlock( &ent->wait );

	if( tmp ) 
		wentry_free( tmp );
}

static void workon_cleanup( void )
{
	int i;
	Wentry *we, *tmp;

	/*clean hash first*/
	pthread_mutex_destroy( &hash_lock );

	for( i = 0 ; i < whash_size ; i ++ )
	{
		if( ! whash[ i ] ) continue;
		we = whash[ i ];
		while( we )
		{
			pthread_mutex_destroy( &we->wait );
			tmp = we;
			we = we->next;
			free( tmp );
		}
	}
	free( whash );

	/*clean cache*/
	pthread_mutex_destroy( &wcache.lock );
	we = wcache.list;

	while( we )
	{
		tmp = we;
		we = we->next;
		free( tmp );
	}
}

void workon_init( void )
{
	whash = ( Wentry ** ) calloc( WORKON_HASH_SIZE, sizeof(Wentry *) );

	if( ! whash )
		msglog( MSG_FATAL, "workon_init: " \
			"could not allocate hash table" );

	whash_used = 0;
	whash_size = WORKON_HASH_SIZE;

        thread_mutex_init(&hash_lock);

	wcache.list = NULL;
	wcache.count = 0;
        thread_mutex_init(&wcache.lock);

	if( atexit( workon_cleanup ) )
	{
		workon_cleanup();
		msglog( MSG_FATAL, "workon_init: " \
			"could not reigster cleanup method" );
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
	if (workon_name(str)) {
	    workon_release(str);
	}
	if (i == 1000000) {
	    printf("%s: %d\n", str, i);
	    i = 0;
	}
    }
}

/* compile  gcc -g -DTEST workon.c msg.o  miscfuncs.o -lpthread thread.o */

int main(void)
{
    pthread_t id;

    msg_init();
    thread_init();
    workon_init();

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
    pthread_create(&id, 0, test_th, "A");
    pthread_create(&id, 0, test_th, "B");
    pthread_create(&id, 0, test_th, "C");
    pthread_create(&id, 0, test_th, "D");
    pthread_create(&id, 0, test_th, "E");
    pthread_create(&id, 0, test_th, "F");
    pthread_create(&id, 0, test_th, "G");
    pthread_create(&id, 0, test_th, "H");
    pthread_create(&id, 0, test_th, "I");
    pthread_create(&id, 0, test_th, "J");
    pthread_create(&id, 0, test_th, "K");
    pthread_create(&id, 0, test_th, "L");
    pthread_create(&id, 0, test_th, "M");
    pthread_create(&id, 0, test_th, "N");
    pthread_create(&id, 0, test_th, "O");
    pthread_create(&id, 0, test_th, "P");
    pthread_create(&id, 0, test_th, "Q");
    pthread_create(&id, 0, test_th, "R");
    pthread_create(&id, 0, test_th, "S");
    pthread_create(&id, 0, test_th, "T");
    pthread_create(&id, 0, test_th, "U");
    pthread_create(&id, 0, test_th, "V");
    pthread_create(&id, 0, test_th, "W");
    pthread_create(&id, 0, test_th, "X");
    pthread_create(&id, 0, test_th, "Y");
    pthread_create(&id, 0, test_th, "Z");

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
    pthread_create(&id, 0, test_th, "A");
    pthread_create(&id, 0, test_th, "B");
    pthread_create(&id, 0, test_th, "C");
    pthread_create(&id, 0, test_th, "D");
    pthread_create(&id, 0, test_th, "E");
    pthread_create(&id, 0, test_th, "F");
    pthread_create(&id, 0, test_th, "G");
    pthread_create(&id, 0, test_th, "H");
    pthread_create(&id, 0, test_th, "I");
    pthread_create(&id, 0, test_th, "J");
    pthread_create(&id, 0, test_th, "K");
    pthread_create(&id, 0, test_th, "L");
    pthread_create(&id, 0, test_th, "M");
    pthread_create(&id, 0, test_th, "N");
    pthread_create(&id, 0, test_th, "O");
    pthread_create(&id, 0, test_th, "P");
    pthread_create(&id, 0, test_th, "Q");
    pthread_create(&id, 0, test_th, "R");
    pthread_create(&id, 0, test_th, "S");
    pthread_create(&id, 0, test_th, "T");
    pthread_create(&id, 0, test_th, "U");
    pthread_create(&id, 0, test_th, "V");
    pthread_create(&id, 0, test_th, "W");
    pthread_create(&id, 0, test_th, "X");
    pthread_create(&id, 0, test_th, "Y");
    pthread_create(&id, 0, test_th, "Z");

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
    pthread_create(&id, 0, test_th, "A");
    pthread_create(&id, 0, test_th, "B");
    pthread_create(&id, 0, test_th, "C");
    pthread_create(&id, 0, test_th, "D");
    pthread_create(&id, 0, test_th, "E");
    pthread_create(&id, 0, test_th, "F");
    pthread_create(&id, 0, test_th, "G");
    pthread_create(&id, 0, test_th, "H");
    pthread_create(&id, 0, test_th, "I");
    pthread_create(&id, 0, test_th, "J");
    pthread_create(&id, 0, test_th, "K");
    pthread_create(&id, 0, test_th, "L");
    pthread_create(&id, 0, test_th, "M");
    pthread_create(&id, 0, test_th, "N");
    pthread_create(&id, 0, test_th, "O");
    pthread_create(&id, 0, test_th, "P");
    pthread_create(&id, 0, test_th, "Q");
    pthread_create(&id, 0, test_th, "R");
    pthread_create(&id, 0, test_th, "S");
    pthread_create(&id, 0, test_th, "T");
    pthread_create(&id, 0, test_th, "U");
    pthread_create(&id, 0, test_th, "V");
    pthread_create(&id, 0, test_th, "W");
    pthread_create(&id, 0, test_th, "X");
    pthread_create(&id, 0, test_th, "Y");
    pthread_create(&id, 0, test_th, "Z");

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
    pthread_create(&id, 0, test_th, "A");
    pthread_create(&id, 0, test_th, "B");
    pthread_create(&id, 0, test_th, "C");
    pthread_create(&id, 0, test_th, "D");
    pthread_create(&id, 0, test_th, "E");
    pthread_create(&id, 0, test_th, "F");
    pthread_create(&id, 0, test_th, "G");
    pthread_create(&id, 0, test_th, "H");
    pthread_create(&id, 0, test_th, "I");
    pthread_create(&id, 0, test_th, "J");
    pthread_create(&id, 0, test_th, "K");
    pthread_create(&id, 0, test_th, "L");
    pthread_create(&id, 0, test_th, "M");
    pthread_create(&id, 0, test_th, "N");
    pthread_create(&id, 0, test_th, "O");
    pthread_create(&id, 0, test_th, "P");
    pthread_create(&id, 0, test_th, "Q");
    pthread_create(&id, 0, test_th, "R");
    pthread_create(&id, 0, test_th, "S");
    pthread_create(&id, 0, test_th, "T");
    pthread_create(&id, 0, test_th, "U");
    pthread_create(&id, 0, test_th, "V");
    pthread_create(&id, 0, test_th, "W");
    pthread_create(&id, 0, test_th, "X");
    pthread_create(&id, 0, test_th, "Y");
    pthread_create(&id, 0, test_th, "Z");

    pthread_join(id, NULL);
}

#endif
