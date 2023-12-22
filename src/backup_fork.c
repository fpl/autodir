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

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "msg.h"
#include "backup_argv.h"
#include "miscfuncs.h"
#include "backup_fork.h"

/*default is to give low priority*/
#define DFLT_PRI		30

#define PRIORITY_MAX		1
#define PRIORITY_MIN		40

static int priority;

static void reset_signals( void )
{
        sigset_t set;
                                                                                
        sigfillset( &set );

        if( sigprocmask( SIG_UNBLOCK, &set, NULL ) )
		msglog( MSG_ERR | LOG_ERRNO, "reset_signals: sigprocmask" );
}

/* to be called only from forked child process*/
static void backup_exec( const char *name, const char *path )
{
	char **argv;

	reset_signals();
	setpriority( PRIO_PROCESS, 0, priority );

	if( ! backup_argv_get( name, path, &argv ) )
	{
		fprintf( stderr, "backup_exec: " \
			"could not make argument vector" );
		_exit( EXIT_FAILURE );
	}

	if( execv( argv[0], argv ) )
	{
		fprintf( stderr, "backup_exec: execv: %s", strerror( errno ) );
		_exit( EXIT_FAILURE );
	}
	_exit( EXIT_FAILURE );
}

pid_t backup_fork_new( const char *name, const char *path )
{
	pid_t pid;

	msglog( MSG_INFO, "starting backup for %s", name );
	pid = fork();
	if( pid < 0 )
	{
		msglog( MSG_NOTICE|LOG_ERRNO, "fork: could not start " \
				"backup for %s", name );
		return -1;
	}
	else if( pid == 0 ) 
	{
		if( setpgrp() == -1 )
			fprintf( stderr, "setpgrp: %s", strerror( errno ) );
		backup_exec( name, path ); 
	}
	else
		setpgid( pid, pid );
	return pid;
}

int backup_waitpid(pid_t pid, const char *name, int block)
{
	int status;
	int options = block ? 0 : WNOHANG;
	int wrv;

again:
	wrv = waitpid( pid, &status, options );
	if( wrv == -1 && errno == EINTR )
	{
		msglog( MSG_ERR, "waitpid: EINTR received. trying again...");
		sleep( 1 );
		goto again;
	}

	if( wrv == -1 )
	{
		msglog( MSG_ERR|LOG_ERRNO, "waitpid %ld for %s",
							(long) pid, name );
		return 1;
	}
 	else if( ! wrv ) 
	{
		return 0;
	}
	else if( wrv > 0 )
	{
		if( WIFEXITED( status ) )
		{
			msglog( MSG_INFO,
				"backup process %ld for %s exited with %d",
				(long) pid, name, WEXITSTATUS( status ) );
		}
		else if( WIFSIGNALED( status ) )
		{
			msglog( MSG_INFO, "backup process %ld for %s " \
					"caught signal %d", (long) pid,
					name, WTERMSIG( status ) );
		}
		else
		{
			/*should not get here*/
			msglog( MSG_NOTICE,
				"backup process %ld for %s stopped",
					(long) pid, name );
			goto again;
		}
	}
	return 1;
}

void backup_soft_signal( pid_t pid )
{
	kill( - pid, SIGTERM );
}

void backup_fast_kill( pid_t pid, const char *name )
{
	kill( - pid, SIGKILL );
	backup_waitpid( pid, name, 1 );
}

void backup_kill( pid_t pid, const char *name )
{
	int i;

	if( backup_waitpid( pid, name, 0 ) != 0 )
	    return;

	/*send soft signal first and wait for some time*/
	kill( - pid, SIGTERM );
	sleep( 1 );
	if( backup_waitpid( pid, name, 0 ) != 0 )
	    return;

	for( i = 0; i < 3; i++ )
	{
		kill( - pid, SIGKILL );
		sleep( 1 );
		if( backup_waitpid( pid, name, 0 ) != 0 )
		    return;
	}
	backup_waitpid( pid, name, 1 );
}

/**********command line option handling funtions***************/

void backup_fork_option_pri( char ch, char *arg, int valid )
{
	int pri;

	if( ! valid ) priority = DFLT_PRI;

	else if( ! string_to_number( arg, &pri ) )
		msglog( MSG_FATAL, "invalid argument for -%c", ch );

	else if( pri < PRIORITY_MAX || pri > PRIORITY_MIN )
		msglog( MSG_FATAL, "invalid priority %d for -%c", pri, ch );

	else priority = pri - 21; /*change to -20 to +20 range*/
}

#ifdef TEST

char *autodir_name(void)
{
    return "test autodir";
}

int main(void)
{
	pid_t pid;

	msg_option_verbose('x', "", 1);
	msg_init();
	msg_console_on();
	backup_argv_init( strdup( "/home/devel/testautodir/backup" ) );

	while (1)
	{
		pid = backup_fork_new( "test", "/tmp" );
		if( pid > 0 )
		{
			backup_kill( pid, "test" );
		}
	}
}

#endif
