
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

/* syslog and console log messages management*/

#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "autodir.h"
#include "miscfuncs.h"
#include "thread.h"
#include "msg.h"

#define MSG_CONSOLE		1
#define MSG_SYSLOG		2

#define MSG_MASK 15

char * stpcpy (char *, const char *);

/*remap to custom priorities*/
static struct {
	int priority;
	const char *tag;
} msgmap[] = {

	[ MSG_FATAL   ] = { LOG_EMERG,		"fatal"		},
	[ MSG_EMERG   ] = { LOG_EMERG,		"emergency"	},
	[ MSG_ALERT   ] = { LOG_ALERT, 		"alert"		},
	[ MSG_CRIT    ] = { LOG_CRIT,  		"critical"	},
	[ MSG_ERR     ] = { LOG_ERR,   		"error"		},
	[ MSG_WARNING ] = { LOG_WARNING,	"warning" 	},
	[ MSG_NOTICE  ] = { LOG_NOTICE, 	"notice" 	},
	[ MSG_INFO    ] = { LOG_INFO,		"info" 		},
	[ MSG_DEBUG   ] = { LOG_DEBUG,  	"debug"		}
};

static struct {
	int msg_which; /*where to log*/
	int slog_init; /*syslog initialized? */
	int verbose_log;
	char *modname_prefix;
	pthread_mutex_t strerror_lock;
} mg;

static void msg_console( int msgprio, const char *txt )
{
	FILE *stream;

	stream = msgprio < MSG_INFO ? stderr: stdout;

	if( mg.modname_prefix ) 
		fprintf( stream, "%s [%s] %s: %.*s\n",
				autodir_name(),
				mg.modname_prefix,
				msgmap[ msgprio ].tag, (int) MSG_BUFSZ, txt );
	else
		fprintf( stream, "%s %s: %.*s\n",
				autodir_name(),
				msgmap[ msgprio ].tag, (int) MSG_BUFSZ, txt );
}

static void msg_syslog( int msgprio, const char *txt )
{
	if( mg.modname_prefix ) 
		syslog( msgmap[ msgprio ].priority, "[%s] %s: %.*s",
				mg.modname_prefix,
				msgmap[ msgprio ].tag, (int) MSG_BUFSZ, txt );
	else
		syslog( msgmap[ msgprio ].priority, "%s: %.*s",
				msgmap[ msgprio ].tag, (int) MSG_BUFSZ, txt );
}

/*main function that logs to console, syslog
depending on the preset preferences*/
void msglog( int mprio, const char *fmt, ... )
{
	char buf[ MSG_BUFSZ ], *b = buf;
	int len, sz = sizeof(buf);
	int pri = mprio & MSG_MASK;
	int saved_errno = errno;
	char *es;
	va_list ap;

	if( ! mg.verbose_log && pri >= MSG_INFO )
		return;

	if( pri > MSG_DEBUG || ! fmt )
		return;

	va_start( ap, fmt );
	len = vsnprintf( b, sz, fmt, ap );
	va_end( ap );

	if( len <= 0 || len >= sz || b[ len ] )
		return;
	b += len;
	sz -= len;

	/*append errno string*/
	if( ( mprio & LOG_ERRNO ) && sz > 3 )
	{
		*b++ = ':';
		*b++ = ' ';
		*b = '\0';
		sz -= 2;

		/*all this is for not useing buggy GNU version of strerror_r*/
		pthread_mutex_lock( &mg.strerror_lock );
		es = strerror( saved_errno );
		string_n_copy( b, es, sz );
		pthread_mutex_unlock( &mg.strerror_lock );
	}

	/*where to log?*/
	if( mg.msg_which & MSG_CONSOLE )
		msg_console( pri, buf );
	if( mg.msg_which & MSG_SYSLOG )
		msg_syslog( pri, buf );

	if( pri == MSG_FATAL )
		exit( EXIT_FAILURE );
}

static void msg_clean( void )
{
	if( mg.slog_init )
		closelog();
	if( mg.modname_prefix )
		free( mg.modname_prefix );
	pthread_mutex_destroy( &mg.strerror_lock );
}

void msg_init( void )
{
	mg.slog_init = 0;
	mg.msg_which = 0;
	mg.modname_prefix = NULL;
        thread_mutex_init(&mg.strerror_lock);
	if( atexit( msg_clean ) )
		msglog( MSG_FATAL, "msg_init: could not add msg "
						"cleanup method" );
}

/*where to log?*/

void msg_console_on( void )
{
	mg.msg_which |= MSG_CONSOLE;
}

void msg_console_off( void )
{
	mg.msg_which &= ~MSG_CONSOLE;
}

void msg_syslog_on( void )
{
	if( ! mg.slog_init )
	{
		openlog( autodir_name(), LOG_NDELAY|LOG_CONS, LOG_DAEMON );
		mg.slog_init = 1;
	}
	mg.msg_which |= MSG_SYSLOG;
}

void msg_syslog_off( void )
{
	mg.msg_which &= ~MSG_SYSLOG;
}

void msg_modname_prefix( const char *modname )
{
	mg.modname_prefix = strdup( modname );
}

/*************** option handling functions *****************/

void msg_option_verbose( char ch, char *arg, int valid )
{
	mg.verbose_log = valid ? 1 : 0;
}

/*************** end of option handling functions *****************/
