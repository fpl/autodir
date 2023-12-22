
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
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "msg.h"
#include "miscfuncs.h"

char *string_n_copy( char *str1, const char *str2, int len )
{
	int i = 0;

	if( ! str1 )
		return NULL;

	if( ! str2 )
	{
		*str1 = '\0';
		return NULL;
	}

	while( str2[ i ] && i != len - 1 )
	{
		str1[ i ] = str2[ i ];
		i++;
	}

	str1[ i ] = '\0';
	return str1 + i;
}

/* absolute path check. should start with '/'
 and any trailing '/' are removed
*/
int check_abs_path( char *path )
{
	int i = 0, j = 0, sl = 0;
	char c;

	if( ! path || path[ 0 ] != '/' )
		return 0;

	while( ( c = path[ i ] ) )
	{
		if( c == '/' )
		{
			if( sl )
			{
				i++;
				continue;
			}
			sl = 1;
		}
		else
			sl = 0;

		path[ j++ ] = path[ i++ ];
	}

	if( path[ j - 1 ] == '/' )
		path[ j - 1 ] = 0;
	else
		path[ j ] = 0;

	return 1;
}

/* for adding chars and strings to char buffer without overflow*/
void cary_init( Cary *c, char *buf, int max )
{
	c->array = buf;
	c->array[ 0 ] = 0;
	c->max = max;
	c->cur = 0;
}

int cary_add( Cary *c, char ch )
{
	if( c->cur == c->max - 1)
		return 0;

	c->array[ c->cur++ ] = ch;
	c->array[ c->cur ] = 0; /*null terminate*/

	return 1;
}

int cary_add_str( Cary *ca, const char *str )
{
	int i = 0;

	if( ! str )
		return 1;

	while( str[ i ] && cary_add( ca, str[ i++ ] ) );
	
	return ( str[ i ] ) ? 0 : 1;
}

int string_to_number( const char *str, int *num )
{
	int i = 0, val = 0;

	if( ! str )
		return 0;

	while( str[ i ] )
	{
		if( isdigit( str[ i ] ) )
			val = val * 10 + ( str[ i ] - 48 );
		else return 0;
		i++;
	}

	(*num) = val;
	return 1;
}

int octal_string2dec( const char *str, unsigned int *oct )
{
	int i = 0;
	unsigned int val = 0;

	if( ! str || ! isgraph( *str ) )
		return 0;

	if( *str == '0' )
		str++;
	while( str[ i ] )
	{
		if( str[ i ] < '0' || str[ i ] > '7' )
			return 0;
		val = val * 8 + ( str[ i ] - 48 );
		i++;
	}

	(*oct) = val;
	return i;
}

/*Create dir and its path. Thread safe*/
int create_dir( const char *dir, mode_t mode )
{
	char path[ PATH_MAX + 1 ];
	struct stat st;
	char *p;

	if( ! dir || ! isgraph( dir[ 0 ] ) )
	{
		msglog( MSG_ERR, "create_dir: invalid dir name %s", dir );
		return 0;
	}

	string_n_copy( path, dir, sizeof(path) );
	p = path[ 0 ] == '/' ? path + 1 : path;

	for( p = strchr( p, '/' ) ; p ; p = strchr( p+1, '/' ) )
	{
		*p = 0;
		if( ! lstat( path, &st ) ) /*check if it exists already*/
		{
			if( S_ISDIR( st.st_mode ) )
			{
				*p = '/';
				continue;
			}
			else
			{
				msglog( MSG_ERR, "create_dir: path %s " \
					"exists but not directory", path );
				return 0;
			}
		}
		else if( errno != ENOENT )
		{
			msglog( MSG_ERR|LOG_ERRNO, "create_dir: lstat %s",
							path );
			return 0;
		}
		if( mkdir( path, mode ) == -1 && errno != EEXIST )
		{
			msglog( MSG_ERR|LOG_ERRNO, "create_dir: mkdir %s", p );
			return 0;
		}
		*p = '/';
	}

	if( mkdir( path, mode ) == -1 && errno != EEXIST )
	{
		msglog( MSG_ERR|LOG_ERRNO, "create_dir: mkdir %s", p );
		return 0;
	}

	return 1;
}

int write_all( int fd, const char *buf, int buf_sz )
{
	int n;

	do {
                n = write( fd, buf, buf_sz );
		/*n == 0 should not happen but still check for it*/
                if( n <= 0 ) 
                {
                        if( n )
				msglog( MSG_ERR|LOG_ERRNO, "write_all: write" );
                        return 0;
                }
                buf_sz -= n;
                buf += n;
        } while( buf_sz );
	
	return 1;
}

/*IMP: taken from postfix*/
unsigned int string_hash( const char *str )
{
	unsigned long h = 0;
	unsigned long g;

	/*
	 * From the "Dragon" book by Aho, Sethi and Ullman.
	*/

	while ( *str )
	{
        	h = ( h << 4 ) + *str++;
		if ( ( g = ( h & 0xf0000000 ) ) != 0 )
		{
			h ^= ( g >> 24 );
			h ^= g;
		}
	}
	return h;
}

void string_safe( char *str, int rep )
{
	while( *str )
	{
		if( ! isascii( *str ) || ! isprint( *str ) )
			*str = rep;
		str++;
	}
}

int rename_dir( const char *from, const char *to_dir, const char *to_name,
							const char *tme_format )
{
	char to[ PATH_MAX+1 ];
	time_t now;
	struct tm tmval;
	char str_time[ 256 ];
	int try;

	now = time( NULL );
	for( try = 0; try < 10; try++ )
	{
		if( localtime_r(&now, &tmval) == NULL )
		{
			msglog( MSG_ERR,
				"localtime_r: could not convert current time" );
			return -1;
		}
		if( ! strftime(str_time, sizeof(str_time), tme_format, &tmval ) )
		{
			msglog( MSG_ERR, "strftime: could not make the time") ;
			return -1;
		}
		snprintf(to, sizeof(to), "%s/%s%s", to_dir, to_name, str_time );
		if( rename( from, to ) )
		{
			if ( errno == ENOTEMPTY || errno == EEXIST )
			{
				now++;
				continue;
			}
			msglog( MSG_WARNING|LOG_ERRNO,
					"could not rename %s to %s", from, to );
			return -1;
		}
		return 0;
	}
	return -1;
}
