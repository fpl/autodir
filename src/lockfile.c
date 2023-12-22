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

/*Keeps file descripter and other details in hash table for the locked files.
Uses fcntl file locks before mounting and remove fcntl locks after unmounting.*/

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
#include "lockfile.h"


#define LOCKFILE_HASH_SIZE 13

/*cache size of used memory*/
#define LOCKFILE_CACHE_SIZE 50

#define DEFAULT_LOCKDIR	"/var/lock"

typedef struct lentry {
	char name[ NAME_MAX + 1 ];
	char path[ PATH_MAX + 1 ];
	int fd;
	unsigned int hash;
	struct lentry *next;
} Lentry;

/* for reuse of malloced memory */
static struct {
	Lentry *list;
	int count;
	pthread_mutex_t lock;
} lcache;

static Lentry **lhash;
static pthread_mutex_t hash_lock; /*exclusive access for hash table*/
static int lockfiles; /*lock file option selected?*/
static char *lockdir; /*where to keep lockfiles*/
static char spid[ 128 ]; /*string form of pid*/
static int spid_len; /*len of above string*/

static int lhash_size;
static int lhash_used;

static int lockstop = 0; /* set before shutdown*/

/***************Memory management********************/

/*allocate from the unused previousely allocated memory
or else create new dynamic memory.
*/
static Lentry *lentry_malloc( void )
{
	Lentry *ent;

	if( lcache.count > 0 )
	{
		pthread_mutex_lock( &lcache.lock );
		if( lcache.count <= 0 )
		{
			pthread_mutex_unlock( &lcache.lock );
			goto dyn_alloc;
		}
		ent = lcache.list;
		lcache.list = lcache.list->next;
		lcache.count--;
		pthread_mutex_unlock( &lcache.lock );
		return ent;
	}

dyn_alloc:
	return (Lentry *) malloc( sizeof(Lentry) );
}

static void lentry_free( Lentry *le )
{
	Lentry *tmp;

	if( ! le )
		return;
	if( lcache.count < LOCKFILE_CACHE_SIZE )
	{
		pthread_mutex_lock( &lcache.lock );
		if( lcache.count >= LOCKFILE_CACHE_SIZE )
		{
			pthread_mutex_unlock( &lcache.lock );
			free( le );
			return;
		}
		tmp = lcache.list;
		lcache.list = le;
		le->next = tmp;
		lcache.count++;
		pthread_mutex_unlock( &lcache.lock );
		return;
	}
	else free( le );
}

/******************Hash management*****************/

#define lentry_key( s )		( s % lhash_size )

static void lhash_resize( void )
{
	int new_size;
	int old_size = lhash_size;
	Lentry **new_lhash;
	Lentry **dptr;
	Lentry **h = lhash;
	Lentry *lentry;
	Lentry *next;

	new_size = lhash_size * 2;
	new_size |= 1;

	new_lhash = ( Lentry ** ) calloc( new_size, sizeof(Lentry *) );
	if( new_lhash == NULL )
	    return;

	while( old_size-- > 0 )
	{
		for( lentry = *h++; lentry; lentry = next)
		{
			next = lentry->next;
			dptr = &new_lhash[ lentry->hash % new_size ];
			lentry->next = *dptr;
			*dptr = lentry;
		}
	}
	lhash_size = new_size;
	free(lhash);
	lhash = new_lhash;
}

/*not thread safe. mutual exclusion must be
  setup.
 */

#define LENTRY_LOCATE( name, hash, dptr )			\
do {								\
	dptr = &( lhash[ lentry_key( hash ) ] );		\
	while( *dptr )						\
	{							\
		if( (*dptr)->hash == hash &&			\
			*name == (*dptr)->name[0] && 		\
		       	! strcmp( name, (*dptr)->name ) )	\
			break;					\
		dptr = &( (*dptr)->next );			\
	}							\
} while( 0 );

/*PERFORMANCE:
	We allocate and prepare entry 'outside mutex lock'
	before checking if that entry exist already.

	Collision with existing entry is very rare case.*/
static Lentry* lockfile_add2hash( const char *name,
				const char *path,
				int *exist )
{
	unsigned int hash;
	Lentry *ent, **dptr;

	*exist = 0;

	/*get and prepare entry*/
	if( ! ( ent = lentry_malloc() ) )
	{
		msglog( MSG_ALERT, "lockfile_add2hash: " \
					    "could not allocate memory" );
		return NULL;
	}

	hash = string_hash( name );

	string_n_copy( ent->name, name, sizeof(ent->name) );
	string_n_copy( ent->path, path, sizeof(ent->path) );
	ent->fd = -1;
	ent->hash = hash;
	ent->next = NULL;

	pthread_mutex_lock( &hash_lock );
	/*always after mutex*/
	LENTRY_LOCATE( name, hash, dptr );

	/*already exist*/
	if( *dptr )
	{
		if( (*dptr)->fd != -1 )
			*exist = 1;
		pthread_mutex_unlock( &hash_lock );

		if( ! *exist )
			msglog( MSG_ALERT, "lockfile_add2hash: " \
					"Invalid state: %s", name );
		/*free our preallocated entry*/
		lentry_free( ent );
		return NULL;
	}
		
	*dptr = ent;
	lhash_used++;
	if (lhash_used > lhash_size)
	    lhash_resize();
	pthread_mutex_unlock( &hash_lock );

	return ent;
}

/*remove from hash*/
static Lentry *lockfile_unhash( const char *name, int force )
{
	unsigned int hash = string_hash( name );
	Lentry **dptr, *ent;

	pthread_mutex_lock( &hash_lock );
	/*always after mutex*/
	LENTRY_LOCATE( name, hash, dptr );

	ent = *dptr;
	if( ! ent ) /*entry does not exist*/
	{
		pthread_mutex_unlock( &hash_lock );
		msglog( MSG_ALERT, "lockfile_unhash: " \
			"entry for %s does not exist", name );
		return NULL;
	}
	lhash_used--;

	if( ent->fd == -1 && ! force )
	{
		pthread_mutex_unlock( &hash_lock );
		msglog( MSG_ALERT, "lockfile_unhash: " \
				"Invalid state for %s", name );
		return NULL;
	}

	(*dptr) = ent->next;
	pthread_mutex_unlock( &hash_lock );

	if( force )
	{
		lentry_free( ent );
		return NULL;
	}
	else
		return ent;
}

static int exclusive_lock( int fd, const char *path )
{
	struct flock lk;

	/*First we try get exclusive lock to make sure no one already
	have shared lock. If we are successful we can remove the file.*/
	lk.l_type = F_WRLCK;
	lk.l_start = 0;
	lk.l_whence = SEEK_SET;
	lk.l_len = 0;

	if( fcntl( fd, F_SETLK, &lk ) == -1 )
	{
		/*if we do not get exclusive lock means 
		some one using shared lock already. so leave it alone*/

		if( errno != EAGAIN && errno != EACCES )
			msglog( MSG_ERR|LOG_ERRNO, "fcntl F_SETLK: %s", path );

		return 0; /*do not unlink*/
	}
	return 1; /*can do unlink now*/
}

/*This does not happen very offten. So keep it simple and stupid*/
static int lockfile_sleep( int n )
{
	int i;

	for( i = 0; i < n ; i++ )
	{
		if( lockstop )
			return 0;
		sleep( 3 );
	}
	return 1;
}

static int shared_lock( int fd, const char *path )
{
	int wait_msg = 0;
	struct flock lk;

	/*get shared lock and lock whole file*/
	lk.l_type = F_RDLCK;
	lk.l_start = 0;
	lk.l_whence = SEEK_SET;
	lk.l_len = 0;

	while( fcntl( fd, F_SETLK, &lk ) == -1 )
	{
		if( errno == EAGAIN || errno == EACCES )
		{
			if( ! wait_msg++ )
				msglog( MSG_NOTICE, "waiting for lock file: %s",
							 path );
			if( ! lockfile_sleep( 3 ) )
				return 0;
			continue;
		}
		msglog( MSG_ERR|LOG_ERRNO, "fcntl F_SETLK" );
		return 0;
	}
	return 1;
}



/**********Public interface for lock file creation****************/

/*we do not unlink created file if any other error occurs.

  Race condition: This is not to happen, following procedure followed.
		  First, file created with open.
		  Second, shared lock obtained on the open file.
		  Finally, hard link count checked, if failed repeat again.

		  This protocol is expected to be followed by external programs
		  which create these lock files for their access.
*/
int lockfile_create( const char *name )
{
	int fd = -1;
	int i, exist;
	char path[ PATH_MAX + 1 ];
	struct stat st;
	Lentry *le;

	if( ! lockfiles ) return 1;

	if( ! name || ! *name )
	{
		msglog( MSG_ERR, "lockfile_create: invalid name" );
		return 0;
	}

	snprintf( path, sizeof(path), "%s/%s.lock",
			lockdir, name );

	if( ! ( le = lockfile_add2hash( name, path, &exist ) ) )
		return exist ? 1: 0;

	/*loop until we get rid of dead files.*/
	for( i = 0 ; i < 10 ; i++ )
	{
		if( ( fd = open( path, O_RDWR | O_CREAT, 0644 ) ) == -1 )
		{
			msglog( MSG_ERR|LOG_ERRNO, "open %s", path );
			lockfile_unhash( name, 1 );
			return 0;
		}

		if( fcntl( fd, F_SETFD, FD_CLOEXEC ) == -1 )
			msglog( MSG_ERR|LOG_ERRNO, "lockfile_create: " \
						"fcntl FD_CLOEXEC %s", path );

		if( ! shared_lock( fd, path ) )
		{
			lockfile_unhash( name, 1 );
			close( fd );
			return 0;
		}

		if( fstat( fd, &st ) )
		{
			lockfile_unhash( name, 1 );
			close( fd );
			msglog( MSG_ERR|LOG_ERRNO, "fstat %s", path );
			return 0;
		}

		/*not dead file?*/
		if( st.st_nlink )
			break;

		close( fd );
	}

	/*We do not operate on dead file whose hard link count is zero*/
	if( i >= 10 )
	{
		lockfile_unhash( name, 1 );
		msglog( MSG_NOTICE, "Giving up on dead file %s", path );
		return 0;
	}

        if( ! write_all( fd, spid, spid_len ) )
        {
                msglog( MSG_NOTICE, "could not write pid %s to lock file %s",
					spid, path );
		lockfile_unhash( name, 1 );
                close( fd );
                return 0;
        }
	le->fd = fd;
	return 1;
}

/**********Public interface for lock file removal****************/


/* IMP: Lock file can be unlinked 'when and only when' after obtaining
   exclusive lock on it. If exclusive lock can not be obtained,
   unlinking of the lock file should not be performed
   if autodir lock protocol is to be followed.*/

void lockfile_remove( const char *name )
{
	Lentry *le;

	if( ! lockfiles ) return;

	if( ! name || ! *name )
	{
		msglog( MSG_ERR, "lockfile_remove: invalid name" );
		return; 
	}

	if( ! ( le = lockfile_unhash( name, 0 ) ) )
	{
		msglog( MSG_ALERT, "could not remove lock file for %s", name );
		return;
	}

	if( exclusive_lock( le->fd, le->path ) )
		if( unlink( le->path ) == -1 )
			msglog( MSG_ERR|LOG_ERRNO, "unlink %s", le->path );
	close( le->fd );
	lentry_free( le );
}

/*******Cleaning and initialization**************/

static void free_hash( void )
{
	int i;
	Lentry *le, *tmp;

	for( i = 0 ; i < lhash_size ; i ++ )
	{
		if( ! lhash[ i ] ) continue;
		le = lhash[ i ];
		while( le && le->fd != -1 )
		{
			tmp = le;
			le = le->next;

			if( exclusive_lock( tmp->fd, tmp->path ) )
				if( unlink( tmp->path ) == -1 )
					msglog( MSG_ERR|LOG_ERRNO,
						"unlink %s", tmp->path );
			close( tmp->fd );
			lentry_free( tmp );
		}
	}
	free( lhash );
}

static void lockfile_hash_clean( void )
{
	Lentry *le, *tmp;

	/*clean hash first*/
	pthread_mutex_destroy( &hash_lock );

	if( lhash )
		free_hash();

	/*clean cache*/
	pthread_mutex_destroy( &lcache.lock );
	le = lcache.list;

	while( le )
	{
		tmp = le;
		le = le->next;
		free( tmp );
	}
}

static void lockfile_hash_init( void )
{
	lhash = ( Lentry ** ) calloc( LOCKFILE_HASH_SIZE,
			sizeof(Lentry *) );

	if( ! lhash )
		msglog( MSG_FATAL, "lockfile_hash_init: " \
			"could not allocate hash table" );

	lhash_used = 0;
	lhash_size = LOCKFILE_HASH_SIZE;

        thread_mutex_init(&hash_lock);
        thread_mutex_init(&lcache.lock);

	lcache.list = NULL;
	lcache.count = 0;
}

static void lockfile_clean( void )
{
	if( lockdir )
		free( lockdir );
	lockfile_hash_clean();
}

void lockfile_init( pid_t pid, const char *mod_name )
{
	long int lpid = pid;
	char ldir[ PATH_MAX + 1 ];

	if( ! lockfiles ) return;

	/*autodir is mutlithreaded so we have only one pid*/
	spid_len = snprintf( spid, sizeof(spid), "%ld \n", lpid );
	if( spid_len >= sizeof(spid) || spid_len == -1 )
		msglog( MSG_FATAL, "lockfile_init: overflow for pid buffer" );

	/*argument not given? apply default value*/
	if( ! lockdir )
	{
		snprintf( ldir, sizeof(ldir), "%s/%s",
				DEFAULT_LOCKDIR, mod_name );
		msglog( MSG_NOTICE, "using default lock dir '%s'", ldir );

		if( ! ( lockdir = strdup( ldir ) ) )
			msglog( MSG_FATAL, "lockfile_init: " \
					"could not allocate memory" );
	}

	if( ! create_dir( lockdir, 0755 ) )
		msglog( MSG_FATAL, "could not create lock dir %s", lockdir );

	lockfile_hash_init();

	if( atexit( lockfile_clean ) )
	{
		lockfile_clean();
		msglog( MSG_FATAL, "lockfile_init: " \
				"could not reigster cleanup method" );
	}
}

void lockfile_stop_set( void )
{
	lockstop = 1;
}

/*************** option handling functions *****************/

void lockfile_option_lockdir( char ch, char *arg, int valid )
{
	if( ! valid ) lockdir = NULL;

	else if( ! check_abs_path( arg ) )
		msglog( MSG_FATAL, "invalid argument for path -%c option", ch );

	else
	{
		if( ! ( lockdir = strdup( arg ) ) )
			msglog( MSG_FATAL, "lockfile_option_lockdir: " \
					"could not allocate memeory" );
	}
}

void lockfile_option_lockfiles( char ch, char *arg, int valid )
{
	lockfiles = valid ? 1 : 0;
}

/*************** end of option handling functions *****************/
















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
	    if (!lockfile_create(str))
		printf("could not create lockfile for %s\n", str);
	    workon_release(str);
	}
	if (workon_name(str)) {
	    lockfile_remove(str);
	    workon_release(str);
	}
	if (i == 10000) {
	    printf("%s: %d\n", str, i);
	    i = 0;
	}
    }
}

/* compile  gcc -g -DTEST lockfile.c workon.o msg.o  miscfuncs.o -lpthread thread.o */

int main(void)
{
    pthread_t id;

    msg_init();
    workon_init();
    thread_init();
    lockfile_option_lockfiles('x', "unused arg", 1);
    lockfile_option_lockdir('x', strdup("/tmp/lockdir"), 1);
    lockfile_init(99999, "mymod");

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
