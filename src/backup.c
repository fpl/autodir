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

#include "msg.h"
#include "miscfuncs.h"
#include "backup_queue.h"
#include "backup_child.h"
#include "backup_argv.h"
#include "backup_pid.h"
#include "backup.h"

#define DFLT_BACK_WAIT		(0)

/*backup programs will not exceed this number at any time*/
#define DFLT_MAX_PROC		200

#define BACKUP_WAIT_MAX		86400 /*one day*/

static char *backup_path = 0;
static int do_backup = 0;
static int backup_wait_before = 0;
static int backup_wait2finish = 0;
static int backup_limit = 0;
static int backup_nokill = 0;
static int backup_life = 0;

void backup_init( void )
{
	if( ! do_backup )
		return;
	backup_argv_init( backup_path );
	backup_queue_init( backup_wait_before, backup_limit );
	backup_pid_init( backup_limit );
	backup_child_init( backup_limit, backup_life );
}

void backup_add( const char *name, const char *path )
{
	if( do_backup <= 0 )
		return;

	backup_queue_add( name, path );
}

void backup_remove( const char *name, int force )
{
	if( do_backup <= 0 || backup_nokill )
		return;

	backup_queue_remove( name );

	if( backup_wait2finish && ! force )
		backup_child_wait( name );
	else
		backup_child_kill( name );
}

void backup_stop_set( void )
{
	if( ! do_backup )
		return;
	do_backup = -1;
	backup_child_stop_set();
	backup_queue_stop_set();
}

void backup_stop( void )
{
	if( ! do_backup )
		return;
	backup_stop_set();

	backup_queue_stop();
	backup_child_stop();
}

/**********command line option handling funtions***************/

void backup_option_path( char ch, char *arg, int valid )
{
	/*argument not given. so make these ineffective*/
	if( ! valid )
		return;

	/*first argument must be absolute path to executable*/
	if( arg[ 0 ] != '/' )
		msglog( MSG_FATAL,
			"absolute path expected for option -%c",ch);

	backup_path = arg;
	do_backup = 1;
}

void backup_option_wait( char ch, char *arg, int valid )
{
	int wt;

	if( ! valid ) backup_wait_before = DFLT_BACK_WAIT;

	else if( ! string_to_number( arg, &wt ) )
		msglog( MSG_FATAL, "invalid argument for -%c", ch );

	else if( wt > BACKUP_WAIT_MAX )
		msglog( MSG_FATAL,
			    "argument exceeding upper limit for -%c", ch );

	else backup_wait_before = wt;
}

void backup_option_wait2finish( char ch, char *arg, int valid )
{
	backup_wait2finish = valid ? 1 : 0;
}

void backup_option_nokill( char ch, char *arg, int valid )
{
	backup_nokill = valid ? 1 : 0;
}

void backup_option_max_proc( char ch, char *arg, int valid )
{
	if( ! valid ) backup_limit = DFLT_MAX_PROC;

	else if( ! string_to_number( arg, &backup_limit ) ||
		       backup_limit == 0 )
		msglog( MSG_FATAL, "invalid argument for -%c", ch );
}

void backup_option_life( char ch, char *arg, int valid )
{
	int life = 0;

	if( ! valid )
		return;

	else if( ! string_to_number( arg, &life ) )
		msglog( MSG_FATAL, "invalid argument for -%c", ch );

	else backup_life = life;
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
	backup_add(str, "/tmp");
	//sleep( 1 );
	backup_remove(str, 0);
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
    backup_option_path('x', strdup("/home/devel/testautodir/backup"), 1);
    backup_option_max_proc('x', "20000", 1);
    //backup_option_max_proc( 'x', "", 0 );
    //backup_option_wait2finish('x', "", 1);
    backup_init();

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

    /*
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
    */

    pthread_join(id, NULL);
}

#endif

#ifdef TEST2

char *autodir_name(void)
{
    return "test autodir";
}

void *test_th(void *s)
{
    char *str = (char *) s;
    int i = 0;
    int r;
    char buf[200];

    sleep(5);
    while (1) {
	i++;
	r = rand() % 1000;
	snprintf(buf, sizeof(buf), "%s%d", str, r);
	workon_name(buf);
	backup_add(buf, "/tmp");
	workon_release(buf);
	//sleep(1);
	if (i == 1000000) {
	    printf("ADD %s: %d\n", str, i);
	    i = 0;
	    //sleep(10);
	}
    }
}

void *test_th2(void *s)
{
    char *str = (char *) s;
    int i = 0;
    int r;
    char buf[200];

    sleep(5);
    while (1) {
	i++;
	r = rand() % 1000;
	snprintf(buf, sizeof(buf), "%s%d", str, r);
	//sleep(1);
	workon_name(buf);
	backup_remove(buf, 1);
	workon_release(buf);
	if (i == 1000000) {
	    printf("REMOVE....................... %s: %d\n", str, i);
	    i = 0;
	    //sleep(10);
	}
    }
}

void *test_th3(void *s)
{
    char *str = (char *) s;
    int i = 0;
    int r;
    char buf[200];

    sleep(5);
    while (1) {
	i++;
	r = rand() % 1000;
	snprintf(buf, sizeof(buf), "%s%d", str, r);
	//sleep(1);
	workon_name(buf);
	backup_remove(buf, 0);
	workon_release(buf);
	if (i == 1000000) {
	    printf("REMOVE2........................ %s: %d\n", str, i);
	    i = 0;
	    //sleep(10);
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
    backup_option_path('x', strdup("/home/devel/testautodir/backup"), 1);
    backup_option_max_proc('x', "20000", 1);
    workon_init();
    backup_option_wait2finish('x', "", 1);
    backup_init();

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

    pthread_create(&id, 0, test_th3, "1");
    pthread_create(&id, 0, test_th3, "2");
    pthread_create(&id, 0, test_th3, "3");
    pthread_create(&id, 0, test_th3, "4");
    pthread_create(&id, 0, test_th3, "5");
    pthread_create(&id, 0, test_th3, "6");
    pthread_create(&id, 0, test_th3, "7");
    pthread_create(&id, 0, test_th3, "8");
    pthread_create(&id, 0, test_th3, "9");
    pthread_create(&id, 0, test_th3, "a");
    pthread_create(&id, 0, test_th3, "b");
    pthread_create(&id, 0, test_th3, "c");
    pthread_create(&id, 0, test_th3, "d");
    pthread_create(&id, 0, test_th3, "e");
    pthread_create(&id, 0, test_th3, "f");
    pthread_create(&id, 0, test_th3, "g");
    pthread_create(&id, 0, test_th3, "h");
    pthread_create(&id, 0, test_th3, "i");
    pthread_create(&id, 0, test_th3, "j");
    pthread_create(&id, 0, test_th3, "k");
    pthread_create(&id, 0, test_th3, "l");
    pthread_create(&id, 0, test_th3, "m");
    pthread_create(&id, 0, test_th3, "n");
    pthread_create(&id, 0, test_th3, "o");
    pthread_create(&id, 0, test_th3, "p");
    pthread_create(&id, 0, test_th3, "q");
    pthread_create(&id, 0, test_th3, "r");
    pthread_create(&id, 0, test_th3, "s");
    pthread_create(&id, 0, test_th3, "t");
    pthread_create(&id, 0, test_th3, "u");
    pthread_create(&id, 0, test_th3, "v");
    pthread_create(&id, 0, test_th3, "w");
    pthread_create(&id, 0, test_th3, "x");
    pthread_create(&id, 0, test_th3, "y");
    pthread_create(&id, 0, test_th3, "z");

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
