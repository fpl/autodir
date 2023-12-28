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
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include "thread.h"
#include "msg.h"
#include "miscfuncs.h"
#include "time_mono.h"
#include "backup_pid.h"
#include "backup_child.h"

#ifdef TEST
extern void backup_kill( pid_t pid, const char *name );
extern int backup_waitpid(pid_t pid, const char *name, int block);
#else
#include "backup_fork.h"
#endif

static Backup_pid **hash;
static int hash_size;
static int hash_used;
static pthread_mutex_t hash_lock;
static pthread_t backup_monitor_th;
static int stop;
static int backup_life;

#define entry_key( s )		( (s) % hash_size )

#define ENTRY_LOCATE( name, h, dptr )				\
do {								\
	dptr = &( hash[ entry_key( h ) ] );			\
	while( *dptr ) 						\
	{							\
		if( (*dptr)->hash == h && 			\
			*name == (*dptr)->name[0] &&		\
		       	! strcmp( name, (*dptr)->name ) ) 	\
			break; 					\
		dptr = &( (*dptr)->next ); 			\
	} 							\
} while( 0 )

#if 0
static int backup_child_add( const char *name, pid_t pid )
{
	unsigned int h;
	Backup_pid **dptr, *new_ent, *ent;

	if( pid < 0 )
		return 0;

	/*PERFORMANCE:
	we allocate in advance. in almost all cases we
	need new entry to be added.*/
	new_ent = backup_pidmem_allocate();
	if( ! new_ent )
	{
		msglog( MSG_ALERT, "backup_child: " \
				"could not allocate memory" );
		return 0;
	}

	h = string_hash( name );
	string_n_copy( new_ent->name, name, sizeof(new_ent->name) );
	new_ent->hash = h;
	new_ent->next = NULL;

	/*Now the actual part*/
 	pthread_mutex_lock( &hash_lock );
	ENTRY_LOCATE( name, h, dptr );
	ent = *dptr;
	if( ent ) /*entry exists*/
	{
		pthread_mutex_unlock( &hash_lock );
		/*we need to free new entry allocated above*/
		backup_pidmem_free( new_ent );
		msglog( MSG_ALERT, "backup_child: already exists" );
		return 0;
	}

	(*dptr) = new_ent;
	hash_used++;
	new_ent->pid = pid;
	pthread_mutex_unlock( &hash_lock );
	return 1;
}
#endif

static int backup_child_add( const char *name, pid_t pid, time_t started )
{
	unsigned int h;
	Backup_pid **dptr, *new_ent, *ent;

	if( pid < 0 )
		return 0;
	h = string_hash( name );
	started = time_mono();

	/*Now the actual part*/
 	pthread_mutex_lock( &hash_lock );
	ENTRY_LOCATE( name, h, dptr );
	ent = *dptr;
	if( ent ) /*entry exists*/
	{
		pthread_mutex_unlock( &hash_lock );
		return 0;
	}

	new_ent = backup_pidmem_allocate();

	if( ! new_ent )
	{
		pthread_mutex_unlock( &hash_lock );
		msglog( MSG_ALERT, "backup_child: " \
				"could not allocate memory" );
		return 0;
	}

	string_n_copy( new_ent->name, name, sizeof(new_ent->name) );
	new_ent->hash = h;
	new_ent->started = started;
	new_ent->pid = pid;
	new_ent->next = NULL;
	(*dptr) = new_ent;
	hash_used++;
	pthread_mutex_unlock( &hash_lock );
	return 1;
}

static pid_t get_pid( const char *name, Backup_pid **id, int mark_kill )
{
	unsigned int h;
	Backup_pid **dptr, *bp;
	pid_t pid;

	h = string_hash( name );

 	pthread_mutex_lock( &hash_lock );
	ENTRY_LOCATE( name, h, dptr );
	if( ( bp = *dptr ) ) /*entry exists*/
	{
		pthread_mutex_lock( &bp->lock );
		pthread_mutex_unlock( &hash_lock );
		pid = bp->pid;
		if( pid > 0 )
		{
			bp->pid = 0;
			pthread_mutex_unlock( &bp->lock );
			*id = bp;
			return pid;
		}
		if( mark_kill )
			bp->kill = 1;
		/*pid taken by someone else. so wait until 
		  they do work for us and cleanup if we are 
		  the last one to hold this memory*/
		bp->waiting++;
		pthread_cond_wait( &bp->wait, &bp->lock );
		bp->waiting--;
		if( bp->waiting == 0 )
		{
			pthread_mutex_unlock( &bp->lock );
			/*we do not call backup_child_delete because,
			  found_wait signaled by that funtion itself.
			  Calling it again does not make sense.*/
			backup_pidmem_free( bp );
			return 0;
		}
		pthread_mutex_unlock( &bp->lock );
		return 0;
	}
 	pthread_mutex_unlock( &hash_lock );
	return 0;
}

#define PTR_LOCATE( bp, dptr )					\
do {								\
	dptr = &( hash[ entry_key( bp->hash ) ] );		\
	while( *dptr ) 						\
	{							\
		if( (*dptr) == bp )				\
			break; 					\
		dptr = &( (*dptr)->next ); 			\
	} 							\
} while( 0 )

static void remove_pid( Backup_pid *bp )
{
	Backup_pid **dptr;

 	pthread_mutex_lock( &hash_lock );
	PTR_LOCATE( bp, dptr );
	if( *dptr ) /*must exist*/
	{
		(*dptr) = bp->next;
		hash_used--;
		pthread_mutex_unlock( &hash_lock );

		/*this locking/unlocking will make sure 
		  no one is on back of us*/
		pthread_mutex_lock( &bp->lock );
		pthread_mutex_unlock( &bp->lock );

		if( bp->waiting )
		{
			pthread_cond_broadcast( &bp->wait );
			return;
		}
		backup_pidmem_free( bp );
		return;
	}
 	pthread_mutex_unlock( &hash_lock );
}

static pid_t wait_pid( pid_t pid, Backup_pid *bp )
{
	pthread_mutex_lock( &bp->lock );
	if( ! bp->kill )
	{
		bp->pid = pid;
		bp->waiting++;
		pthread_cond_wait( &bp->wait, &bp->lock );
		bp->waiting--;
		if( bp->waiting == 0 )
		{
			pthread_mutex_unlock( &bp->lock );
			/*we do not call backup_child_delete because,
			  found_wait signaled by that funtion itself.
			  Calling it again does not make sense.*/
			backup_pidmem_free( bp );
			return 0;
		}
		pthread_mutex_unlock( &bp->lock );
		return 0;
	}
	pthread_mutex_unlock( &bp->lock );
	return pid;
}

static void *backup_monitor_thread( void *x )
{
	Backup_pids *pids;
	int i = 0;
	pid_t pid = 0;
	Backup_pid *bp;
	time_t now = 0;

	sleep (1);

	while (1) {
		pids = backup_pids_get();
		if( pids->count == 0 )
		{
			backup_pids_unget(pids);
			if( stop )
				pthread_exit( NULL );
			sleep( 2 );
			continue;
		}
		if( backup_life > 0 )
			now = time_mono();
		for( i = 0; i < pids->size; i++ )
		{
			if( stop ) {
				backup_pids_unget(pids);
				pthread_exit( NULL );
			}

			if( ( bp = pids->s[ i ] ) == 0 )
				continue;
			if( bp->pid <= 0 )
				continue;

			if( pthread_mutex_trylock( &bp->lock ) )
				continue;
			pid = bp->pid;
			if( pid > 0 )
				bp->pid = 0;
			pthread_mutex_unlock( &bp->lock );
			if( pid <= 0 )
				continue;

			if( backup_life > 0 &&
					now - bp->started > backup_life )
			{
				msglog( MSG_INFO, "backup timedout for %s",
								bp->name );
				backup_kill( pid, bp->name );
			}
			else if( backup_waitpid( pid, bp->name, 0 ) <= 0 )
			{
				pthread_mutex_lock( &bp->lock );
				if( ! bp->kill )
				{
					bp->pid = pid;
					pthread_mutex_unlock( &bp->lock );
					continue;
				}
				pthread_mutex_unlock( &bp->lock );
				backup_kill( pid, bp->name );
			}
			remove_pid( bp );
		}
		backup_pids_unget( pids );
		if( stop )
			pthread_exit( NULL );
		sleep( 2 );
	}
}

#if 0
int backup_child_running( const char *name )
{
	unsigned int h;
	Backup_pid **dptr;
	int ret;

	h = string_hash( name );

 	pthread_mutex_lock( &hash_lock );
	ENTRY_LOCATE( name, h, dptr );
	ret = *dptr ? 1 : 0;
 	pthread_mutex_unlock( &hash_lock );
	return ret;
}
#endif

void backup_child_kill( const char *name )
{
	Backup_pid *bp;
	pid_t pid;

	pid = get_pid( name, &bp, 1 );
	if( pid == 0 )
		return;
	backup_kill( pid, name );
	remove_pid( bp );
}

void backup_child_wait( const char *name )
{
	Backup_pid *bp;
	pid_t pid;

	pid = get_pid( name, &bp, 0 );
	if( pid == 0 )
		return;
	if( backup_waitpid( pid, name, 0 ) <= 0 )
	{
		if( ( pid = wait_pid( pid, bp ) ) == 0 )
			return;
		backup_kill( pid, name );
	}
	remove_pid( bp );
}

int backup_child_count( void )
{
	int ret;

	if( pthread_mutex_trylock( &hash_lock ) )
		return -1;
	ret = hash_used;
	pthread_mutex_unlock( &hash_lock );
	return (ret);
}

void backup_child_start(const char *name, const char *path)
{
	pid_t pid;

	pid = backup_fork_new( name, path );
	if( pid <= 0 )
		return;
	if( backup_child_add( name, pid, time_mono() ) == 0 )
		backup_kill( pid, name );
}

void backup_child_init( int size, int blife )
{
	hash = ( Backup_pid ** ) calloc( size, sizeof(Backup_pid *) );

	if( ! hash )
		msglog( MSG_FATAL, "backup_child_init: " \
			"could not allocate hash table" );

	stop = 0;
	hash_used = 0;
	hash_size = size;
        thread_mutex_init( &hash_lock );
	backup_life = blife > 0 ? blife : 0;

	if( ! thread_new_joinable( backup_monitor_thread, NULL,
						    &backup_monitor_th ) )
		msglog( MSG_FATAL,
			"backup_child_init: could not start new thread" );
}

void backup_child_stop_set( void )
{
	stop = 1;
}

static void backup_signal_all( void )
{
	int i;
	Backup_pid *bp, **dptr;

	pthread_mutex_lock( &hash_lock );
	for( i = 0; i < hash_size; i++ )
	{
		for( dptr = &hash[ i ]; *dptr; )
		{
			bp = *dptr;
			dptr = &bp->next;
			pthread_mutex_lock( &bp->lock );
			if( bp->pid > 0 )
				backup_soft_signal( bp->pid );
			pthread_mutex_unlock( &bp->lock );
		}
	}
	pthread_mutex_unlock( &hash_lock );
}

static void backup_kill_all( void )
{
	int i;
	Backup_pid *bp, **dptr;

	pthread_mutex_lock( &hash_lock );
	for( i = 0; i < hash_size; i++ )
	{
		for( dptr = &hash[ i ]; *dptr; )
		{
			bp = *dptr;
			dptr = &bp->next;
			pthread_mutex_lock( &bp->lock );
			if( bp->pid > 0 )
			{
				backup_fast_kill( bp->pid, bp->name );
				bp->pid = 0;
				pthread_cond_broadcast( &bp->wait );
			}
			pthread_mutex_unlock( &bp->lock );
		}
	}
	pthread_mutex_unlock( &hash_lock );
}

void backup_child_stop( void )
{
	stop = 1;
	pthread_join( backup_monitor_th, NULL );
	backup_signal_all();
	sleep( 1 );
	backup_kill_all();
}














#ifdef TEST

#include "backup_argv.h"

void backup_kill( pid_t pid, const char *name )
{
	//printf("kill %lu, name = %s\n", (long) pid, name);
	//sleep( 3 );
}

int backup_waitpid(pid_t pid, const char *name, int block)
{
	//sleep ( 3 );
	printf("backup_waitpid %lu, name %s\n", (long) pid, name);
	return rand()%2;
}

pid_t backup_fork_new( const char *name, const char *path)
{
	//printf("fork name = %s\n", name);
	//sleep( 2 );
	return (*name);
}

#ifdef TEST1

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
	backup_child_start(str, "/tmp");
	//backup_child_kill(str);
	if (i == 1000000) {
	    printf("KILL %s: %d\n", str, i);
	    i = 0;
	    sleep(1);
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
	backup_child_wait(str);
	//backup_child_kill(str);
	if (i == 1000000) {
	    printf("WAIT %s: %d\n", str, i);
	    i = 0;
	    sleep(1);
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
    backup_argv_init(strdup("/home/devel/testautodir/backup"));
    backup_pid_init(20000);
    backup_child_init(20000, 0);

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

    pthread_join(id, NULL);
}

#endif
#endif
