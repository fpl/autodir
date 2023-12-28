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

/* For prepairing arguments for backup proc. 
   %x characters have special meaning and
   replaced with strftime and others specific to autodir.
*/

#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <limits.h>
#include "miscfuncs.h"
#include "msg.h"
#include "options.h"
#include "backup_argv.h"

#define ARG_STATIC		1
#define ARG_DYNAMIC		2

#define HOSTNAME_LEN		64

#define ARG_BUF_SIZE		((PATH_MAX)+256)

/*These structures for holding raw and
unexpanded backup program arguments.*/
typedef struct barg {
	char *arg;
	int type;
	struct barg *next;
} Barg;

static struct{
	int count;
	Barg *end;
	Barg *start;
} barg_list;

static char hostname[ HOSTNAME_LEN+1 ];

/* For adding to linked list 'at program startup'
 each argument for backup proc

 In this linked list args are not expanded 
so that this can be used for every back proc.
*/
static void backarg_add( char *arg )
{
	Barg *tmp;

	tmp = (Barg *) malloc( sizeof(Barg) );
	if( ! tmp )
		msglog( MSG_FATAL, "backarg_add: malloc: " \
				"could not allocate memory" );

	tmp->arg = arg;
	tmp->type = ( strchr( arg, '%' ) ) ? ARG_DYNAMIC : ARG_STATIC;
	tmp->next = NULL;

	if( barg_list.end )
	{
		barg_list.end->next = tmp;
		barg_list.end = tmp;
	}
	else barg_list.start = barg_list.end = tmp;

	barg_list.count++;
}

/* expand any '%x' in argv argument with
 strftime and others specific to autodir
*/
static int backarg_expand( char *buf, const int len, const char *fmt,
		const char *dname, const char *dpath,
		const char *host, struct tm *tm )
{
	int i, r = 1, strf_expand = 0;
	int special = 0;
	char b[ ARG_BUF_SIZE + 1 ], c;
	Cary ca;

	cary_init( &ca, b, sizeof(b) );
	for( i = 0 ; ( c = fmt[ i ] ) && r ; i++ )
	{
		if( c == '%' && ! special )
		{
			special = 1;
			continue;
		}

		if( special )
		{
			switch( c )
			{
				case 'L':
					r = cary_add_str( &ca, dpath );
					break;
				case 'N':
					r = cary_add_str( &ca, dname );
					break;
				case 'K':
					r = cary_add_str( &ca, host );
					break;
				default:
					cary_add( &ca, '%' );
					r = cary_add( &ca, c );
					/*need to be expanded further
					with strftime?*/
					strf_expand = 1;
			}
			special = 0;
		}
		else r = cary_add( &ca, c );
	}
	if( ! r ) return 0;
	if( strf_expand )
		return strftime( buf, len, b, tm ) ? 1 : 0;
	else
	{
		string_n_copy( buf, b, len );
		return 1;
	}
}

/*
 Expected to be called from
 child process for prepaining argv vector
*/
int backup_argv_get( const char *name, const char *path, char ***argv )
{
	char **av, arg_buf[ ARG_BUF_SIZE + 1 ];
	int i, ret;
	struct tm tm;
	time_t ct;
	Barg *arg;

	ct = time( NULL );
	if( ! localtime_r( &ct, &tm ) )
	{
		fprintf( stderr, "backarg_get_argv: localtime_r error\n" );
		return 0;
	}

	av = (char **) malloc( sizeof(char *) * ( barg_list.count + 1) );
	if( ! av )
	{
		fprintf( stderr, "backarg_get_argv: malloc: " \
				"could not allocate memory\n" );
		return 0;
	}

	arg = barg_list.start;
	for( i = 0 ; i < barg_list.count ; i++ )
	{
		if( arg->type == ARG_STATIC )
			av[ i ] = arg->arg;
		else
		{
			ret = backarg_expand( arg_buf, sizeof(arg_buf),
					arg->arg, name, path, hostname, &tm );
			if( ! ret )
			{
				fprintf( stderr, "backarg_get_argv: " \
						"could not expand back args\n");
				return 0;
			}

			av[ i ] = strdup( arg_buf );
			if( ! av[ i ] )
			{
				fprintf( stderr, "backarg_get_argv: strdup: " \
						"could not allocate memory\n");
				return 0;
			}
		}
		arg = arg->next;
	}
	av[ barg_list.count ] = NULL;
	*argv = av;
	return 1;
}

/*before termination*/
static void backarg_clean( void )
{
	Barg *bg, *tmp;

	bg = barg_list.start;

	while( bg )
	{
		tmp = bg;
		bg = bg->next;
		free( tmp );
	}
}

/*initialization at startup*/
void backup_argv_init( char *bopt )
{
	char *arg;

	if( ! bopt ) return;

	if( gethostname( hostname, sizeof(hostname) ) )
		msglog( MSG_FATAL|LOG_ERRNO, "backup_argv_init: gethostname" );

	barg_list.end = barg_list.start = NULL;
	barg_list.count = 0;

	arg = strtok( bopt, " \f\n\r\t\v" );

	while( arg )
	{
		backarg_add( arg );
		arg = strtok( NULL, " \f\n\r\t\v" );
	}

	if( atexit( backarg_clean ) )
		msglog( MSG_FATAL, "backup_argv_init: could not register " \
					"cleanup method" );
}
