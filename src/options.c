/*

Copyright (C) (2004 - 2006) (Venkata Ramana Enaganti) <ramana@intraperson.com>
Copyright (C) 2025, Francesco Paolo Lovergine <frankie@debian.org>

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

/* Command line options processing.
   Calls all registred functions which handle options.
   Now supports both short options and GNU long options.
*/

#include <stdio.h>
#include <unistd.h>
#include <getopt.h>

#include "miscfuncs.h"
#include "msg.h"
#include "autodir.h"
#include "backup.h"
#include "backup_fork.h"
#include "module.h"
#include "lockfile.h"
#include "options.h"

#define MAX_OPTIONS	20

#define ARG_REQUIRED	1
#define ARG_NOTREQ	0

#define OPTION_AUTOFS_DIR	    'd'
#define OPTION_PID_FILE		    'l'
#define OPTION_TIME_OUT		    't'
#define OPTION_MODULE_PATH	    'm'
#define OPTION_MODULE_SUBOPT    'o'
#define OPTION_BACK_WAIT	    'w'
#define OPTION_MAX_BPROC	    'c'
#define OPTION_BPROC_PRI	    'p'
#define OPTION_BACKUP		    'b'
#define OPTION_FOREGROUND	    'f'
#define OPTION_VERSION		    'v'
#define OPTION_HELP		        'h'
#define OPTION_USE_LOCKS	    'k'
#define OPTION_LOCK_DIR		    'r'
#define OPTION_WAIT4BACKUP	    'n'
#define OPTION_NO_KILL		    'N'
#define OPTION_MULTI_PATH	    'a'
#define OPTION_MULTI_PREFIX	    'x'
#define OPTION_VERBOSE_LOG	    'V'
#define OPTION_BACKUP_LIFE	    'L'

struct opt_cb{
	char opch;                  /*option char*/
	char *arg_str;
	int valid;                  /*command line option specified for this?*/
	int req_arg;                /*argument required*/
	const char *long_name;      /*long option name*/
	const char *description;    /*option description*/
	void ( *cb )( char, char *, int); /*call-back function*/
};

static struct {
	int count;
	char ostr[ MAX_OPTIONS * 2 + 1 ];
	struct opt_cb opt[ MAX_OPTIONS ];
	Cary ca;
	struct option long_options[ MAX_OPTIONS + 1 ];
} options;

static struct opt_cb *option_is_exist( char opch )
{
	int i;

	for( i = 0 ; i < options.count ; i++ )
	{
		if( options.opt[ i ].opch == opch )
			return options.opt + i;
	}
	return NULL;
}

#define helpopt(o,l,t)	printf("\t-%c, --%-20s %s\n",o,l,t)

static void option_usage( void )
{
	printf( "Usage: %s [OPTIONS]\n\n", autodir_name() );

	helpopt(OPTION_AUTOFS_DIR, "directory=DIR", "mount point for autofs file system");
	helpopt(OPTION_PID_FILE, "pidfile=FILE", "pid file path");
	helpopt(OPTION_TIME_OUT, "timeout=SECS", "time in seconds for unmounting of inactive directories");
	helpopt(OPTION_MODULE_PATH, "module=PATH", "module absolute path");
	helpopt(OPTION_MODULE_SUBOPT, "options=OPTS", "module sub-options");

	helpopt(OPTION_BACK_WAIT, "wait=SECS", "wait period for backup process to begin after unmount");
	helpopt(OPTION_WAIT4BACKUP, "wait-for-backup", "not to kill backup process but wait for it to finish");
	helpopt(OPTION_NO_KILL, "no-kill", "not to kill backup process and not to wait for it to finish");
	helpopt(OPTION_MAX_BPROC, "max-backups=NUM", "maximum backup processes");
	helpopt(OPTION_BACKUP_LIFE, "backup-life=SECS", "maximum time in seconds backup can run");
	helpopt(OPTION_BPROC_PRI, "priority=NUM", "backup process priority");
	helpopt(OPTION_BACKUP, "backup=PROG", "backup executable absolute path");
	helpopt(OPTION_USE_LOCKS, "use-locks", "use backup locks");
	helpopt(OPTION_LOCK_DIR, "lock-dir=DIR", "backup lock files directory path");

	helpopt(OPTION_MULTI_PATH, "multipath", "multi path support");
	helpopt(OPTION_MULTI_PREFIX, "prefix=CHAR", "multi path prefix");

	helpopt(OPTION_FOREGROUND, "foreground", "stay foreground and log messages to console");
	helpopt(OPTION_VERBOSE_LOG, "verbose", "verbose logging");
	helpopt(OPTION_VERSION, "version", "version");
	helpopt(OPTION_HELP, "help", "help -- this text");

	printf("\n\n");
}

static void option_help( char ch, char *arg, int valid )
{
	if( valid )
	{
		option_usage();
		exit( EXIT_SUCCESS );
	}
}

static void option_version( char ch, char *arg, int valid )
{
	if( valid )
	{
		printf( "%s: Version %s\n\n", autodir_name(),
				AUTODIR_VERSION );
		exit( EXIT_SUCCESS );
	}
}

static void option_call_cbs( void )
{
	int i;
	struct opt_cb *tmp;

	tmp = options.opt;
	for( i = 0 ; i < options.count ; i++ )
	{
		if( tmp->req_arg )
			( tmp->cb )( tmp->opch, tmp->arg_str, tmp->valid );
		else
			( tmp->cb )( tmp->opch, NULL, tmp->valid );
		tmp++;
	}
}

static void option_process( char *argv[], int argc )
{
	int c, option_index = 0;
	struct opt_cb *ocb;

	opterr = 0;
	while( ( c = getopt_long( argc, argv, options.ostr, 
			          options.long_options, &option_index ) ) != -1 )
	{
		if( c == '?' )
		{
			if( optopt != 0 )
			{
				if( ( ocb = option_is_exist( optopt ) ) && ocb->req_arg )
					msglog( MSG_FATAL, "missing argument for -%c", optopt );
				else if( ! ocb )
					msglog( MSG_FATAL, "unknown option -%c", optopt );
				else
					msglog( MSG_FATAL, "option processing error" );
			}
			else
			{
				/* Long option error - getopt_long sets optopt to 0 for unknown long options */
				msglog( MSG_FATAL, "unknown or ambiguous option" );
			}
		}
		else if( ( ocb = option_is_exist( c ) ) )
		{
			ocb->valid = 1;
			if( ocb->req_arg )
				ocb->arg_str = optarg;
		}
	}
	if( argc != optind )
		msglog( MSG_FATAL, "unexpected argument %s" , argv[ optind ] );

	option_call_cbs();
}

#define OREG option_register

static void option_register( const char opch,
	void ( * const cb ) ( char, char *, int ),
	const int areq,
	const char *long_name,
	const char *description )
{
	if( options.count == MAX_OPTIONS || option_is_exist( opch ) )
		msglog( MSG_FATAL, "option_register: " \
			"could not register option -%c", opch );

	cary_add( &options.ca,opch );
	if( areq == ARG_REQUIRED )
		cary_add( &options.ca, ':' );

	options.opt[ options.count ].req_arg = ( areq == ARG_REQUIRED );
	options.opt[ options.count ].opch = opch;
	options.opt[ options.count ].cb = cb;
	options.opt[ options.count ].arg_str = NULL;
	options.opt[ options.count ].valid = 0;
	options.opt[ options.count ].long_name = long_name;
	options.opt[ options.count ].description = description;

	/* Set up the corresponding long option */
	options.long_options[ options.count ].name = long_name;
	options.long_options[ options.count ].has_arg = areq;
	options.long_options[ options.count ].flag = NULL;
	options.long_options[ options.count ].val = opch;

	options.count++;
	
	/* Null terminate the long_options array */
	options.long_options[ options.count ].name = NULL;
	options.long_options[ options.count ].has_arg = 0;
	options.long_options[ options.count ].flag = NULL;
	options.long_options[ options.count ].val = 0;
}

void option_init( int argc, char *argv[] )
{
	options.count = 0;
	options.ostr[ 0 ] = 0;

	if( argc < 2 )
	{
		option_usage();
		exit( EXIT_FAILURE );
	}

	cary_init( &options.ca, options.ostr, sizeof(options.ostr) );

	/* these must be added first*/
	OREG( OPTION_HELP,		option_help,		    ARG_NOTREQ,   "help", "show this help message" );
	OREG( OPTION_VERSION,		option_version,		    ARG_NOTREQ,   "version", "show version information" );
	OREG( OPTION_AUTOFS_DIR,	autodir_option_path,	    ARG_REQUIRED, "directory", "autofs mount point directory" );
	OREG( OPTION_PID_FILE,		autodir_option_pidfile,	    ARG_REQUIRED, "pidfile", "PID file path" );
	OREG( OPTION_TIME_OUT,		autodir_option_timeout,	    ARG_REQUIRED, "timeout", "inactivity timeout in seconds" );
	OREG( OPTION_FOREGROUND,	autodir_option_fg,	    ARG_NOTREQ,   "foreground", "run in foreground" );
	OREG( OPTION_MODULE_PATH,	module_option_modpath,	    ARG_REQUIRED, "module", "module path" );
	OREG( OPTION_MODULE_SUBOPT,	module_option_modopt,	    ARG_REQUIRED, "options", "module options" );
	OREG( OPTION_BACK_WAIT,		backup_option_wait,	    ARG_REQUIRED, "wait", "backup wait time" );
	OREG( OPTION_WAIT4BACKUP,	backup_option_wait2finish,  ARG_NOTREQ,   "wait-for-backup", "wait for backup to finish" );
	OREG( OPTION_NO_KILL,		backup_option_nokill,	    ARG_NOTREQ,   "no-kill", "don't kill backup processes" );
	OREG( OPTION_MAX_BPROC,		backup_option_max_proc,	    ARG_REQUIRED, "max-backups", "maximum backup processes" );
	OREG( OPTION_BPROC_PRI,		backup_fork_option_pri,	    ARG_REQUIRED, "priority", "backup process priority" );
	OREG( OPTION_BACKUP,		backup_option_path,	    ARG_REQUIRED, "backup", "backup program path" );
	OREG( OPTION_BACKUP_LIFE,	backup_option_life,	    ARG_REQUIRED, "backup-life", "backup process lifetime" );
	OREG( OPTION_USE_LOCKS,		lockfile_option_lockfiles,  ARG_NOTREQ,   "use-locks", "use backup locks" );
	OREG( OPTION_LOCK_DIR,		lockfile_option_lockdir,    ARG_REQUIRED, "lock-dir", "lock files directory" );
	OREG( OPTION_MULTI_PATH,	autodir_option_multipath,   ARG_NOTREQ,   "multipath", "enable multipath support" );
	OREG( OPTION_VERBOSE_LOG,	msg_option_verbose, 	    ARG_NOTREQ,   "verbose", "verbose logging" );
	OREG( OPTION_MULTI_PREFIX,      autodir_option_multiprefix, ARG_REQUIRED, "prefix", "multipath prefix character" );

	option_process( argv,argc );
}
