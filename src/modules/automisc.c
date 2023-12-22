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

/*Module that creates any directory that is accessed
  without checking if it should be created or not
  from other sources.
*/

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <grp.h>
#include <pwd.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "miscfuncs.h"
#include "module.h"
#include "msg.h"


#define MODULE_NAME			"automisc"
#define MODULE_PROTOCOL			(1001)

/********************************************************
*
* Sub-options supported by this module
*
*********************************************************/


/*real base directory*/
#define SUB_OPTION_REALPATH		"realpath"

/*real directory organizaton level*/
#define SUB_OPTION_LEVEL		"level"

/*directory owner*/
#define SUB_OPTION_USER			"owner"

/*directory group owner*/
#define SUB_OPTION_GROUP		"group"

/*directory creation mode*/
#define SUB_OPTION_MODE			"mode"

/*do not monitor directory permissions*/
#define SUB_OPTION_NOCHECK		"nocheck"

/*do everything quite fast!*/
#define SUB_OPTION_FASTMODE		"fastmode"



/*default module option values*/
#define DFLT_AUTOMISC_REALPATH		"/" MODULE_NAME
#define DFLT_AUTOMISC_LEVEL		2
#define DFLT_AUTOMISC_USER		"nobody"
#define DFLT_AUTOMISC_GROUP		"nobody"
#define DFLT_AUTOMISC_MODE		( S_IRWXU | S_IRWXG )


/**********************************************************
  module interface methods  
 **********************************************************/

module_info *module_init( char *subopt, const char *hdir );
void module_dir( char *buf, int len, const char *name );
int module_dowork(const char *name,
		const char *hdir,
		char *ghome, int glen);
void module_clean( void );




/*module option values*/
static struct {
	char realpath[ PATH_MAX+1 ];
	char *owner;
	int level;
	int nocheck;
	uid_t uid;
	gid_t gid;
	mode_t mode;
	int fastmode;
} am_conf;

module_info automisc_info = { MODULE_NAME, MODULE_PROTOCOL };



static const char *path_option_check( char *value, const char *option )
{
	if( ! value )
		msglog( MSG_FATAL, "module suboption '%s' requires value", option );
	else if( ! check_abs_path( value ) )
		msglog( MSG_FATAL, "invalid value for module suboption %s", option );
	return value;
}

static int level_option_check( const char *val )
{
	int level;

	if( ! string_to_number( val, &level ) )
		msglog( MSG_FATAL, "module suboption '%s' needs value", SUB_OPTION_LEVEL );
	else if( level > 2 )
		msglog( MSG_FATAL, "invalid '%s' module suboption %s", SUB_OPTION_LEVEL, val );
	return level;
}

static void get_owner_uid( const char *name, uid_t *uid )
{
	struct passwd *pass;
	
	errno = 0;

	pass = getpwnam( name );
	if( pass )
	{
		*uid = pass->pw_uid;
		return;
	}
	if( ! errno )
		msglog( MSG_FATAL, "no user found with name %s", name );
	else msglog( MSG_FATAL|LOG_ERRNO, "get_owner_uid: getpwnam" );
}

static int get_group_gid( const char *name, gid_t *gid, int fail)
{
	struct group *grp;

	errno = 0;
	grp = getgrnam( name );
	if( grp )
	{
		( *gid ) = grp->gr_gid;
		return 1; 
	}
	if( ! fail ) return 0;
	if( ! errno )
		msglog( MSG_FATAL, "no group found with name %s", name );
	else msglog( MSG_FATAL|LOG_ERRNO, "get_group_info: getgrnam" );

	return 0;
}

#define MODE_ALL	( S_ISUID | \
			  S_ISGID | \
			  S_ISVTX | \
			  S_IRWXU | \
			  S_IRWXG | \
			  S_IRWXO )

static int mode_option_check( const char *val )
{
	int len;
	unsigned int mode;

	if( ! val || ! isgraph( *val ) )
		msglog( MSG_FATAL, "module suboption '%s' needs proper mode value",
				SUB_OPTION_MODE );

	else if( ! ( len = octal_string2dec( val, &mode ) )
			|| ( mode & ~MODE_ALL )
			|| len > 4
			|| len < 3 )
	{
		msglog( MSG_FATAL, "invalid octal mode value '%s' with suboption '%s'",
				val, SUB_OPTION_MODE );
	}
	
	/*world permissions?*/
	if( mode & S_IRWXO )
		msglog( MSG_FATAL, "suboption '%s' is given too liberal " \
				"permissions '%#04o'",
				SUB_OPTION_MODE, mode );

	return mode;
}

static void option_process( char *subopt )
{
	char *value;
	enum {
		OPTION_REALPATH_IDX = 0,
		OPTION_LEVEL_IDX,
		OPTION_USER_IDX,
		OPTION_GROUP_IDX,
		OPTION_MODE_IDX,
		OPTION_NOCHECK_IDX,
		OPTION_FASTMODE_IDX,
		END
	};

	const char *sos[] = {
		[ OPTION_REALPATH_IDX ] = SUB_OPTION_REALPATH,
		[ OPTION_LEVEL_IDX    ] = SUB_OPTION_LEVEL,
		[ OPTION_USER_IDX     ] = SUB_OPTION_USER,
		[ OPTION_GROUP_IDX    ] = SUB_OPTION_GROUP,
		[ OPTION_MODE_IDX     ] = SUB_OPTION_MODE,
		[ OPTION_NOCHECK_IDX  ] = SUB_OPTION_NOCHECK,
		[ OPTION_FASTMODE_IDX ] = SUB_OPTION_FASTMODE,
		[ END                 ] = NULL
	};

	/*return to apply default values*/
	if( ! subopt || ! isgraph( *subopt ) ) return;

	while( *subopt != 0 )
	{
		switch( getsubopt( &subopt, (char* const *restrict)sos, &value ) )
		{
			case OPTION_REALPATH_IDX:
				string_n_copy( am_conf.realpath, 
					path_option_check( value,
						sos[ OPTION_REALPATH_IDX ] ),
				        sizeof(am_conf.realpath) );
				break;

			case OPTION_LEVEL_IDX:
				am_conf.level = level_option_check( value );
				break;

			case OPTION_USER_IDX:
				am_conf.owner = value;
				get_owner_uid( value, &am_conf.uid);
				break;

			case OPTION_GROUP_IDX:
				get_group_gid( value, &am_conf.gid, 1);
				break;

			case OPTION_MODE_IDX:
				am_conf.mode = mode_option_check( value );
				break;

			case OPTION_NOCHECK_IDX:
				am_conf.nocheck = 1;
				break;
		
			case OPTION_FASTMODE_IDX:
				am_conf.fastmode = 1;
				break;

			default:
				msglog( MSG_FATAL, "unknown module suboption %s", value );
		}
	}
}

static void automisc_conf_init( char *opts )
{
	am_conf.realpath[ 0 ] = 0;
	am_conf.level = -1;
	am_conf.uid = -1;
	am_conf.owner = NULL;
	am_conf.gid = -1;
	am_conf.mode = -1;
	am_conf.nocheck = 0;
	am_conf.fastmode = 0;

	option_process( opts );

	/*assign default values to unspecified options*/
	if( ! am_conf.realpath[ 0 ] )
	{
		msglog( MSG_NOTICE, "using default value '%s' for '%s'",
				DFLT_AUTOMISC_REALPATH, SUB_OPTION_REALPATH );
		string_n_copy( am_conf.realpath, DFLT_AUTOMISC_REALPATH,
				sizeof(am_conf.realpath) );
	}
	if( am_conf.level == -1 )
	{
		msglog( MSG_NOTICE, "using default value '%d' for '%s'",
				DFLT_AUTOMISC_LEVEL, SUB_OPTION_LEVEL );
		am_conf.level = DFLT_AUTOMISC_LEVEL;
	}
	if( am_conf.uid == -1 )
	{
		msglog( MSG_NOTICE, "using default owner '%s' for '%s'",
				DFLT_AUTOMISC_USER, SUB_OPTION_USER );
		get_owner_uid( DFLT_AUTOMISC_USER, &am_conf.uid);
	}
	if( am_conf.gid == -1 )
	{
		if( ! am_conf.owner || ! get_group_gid( am_conf.owner, &am_conf.gid, 0 ) )
		{
			msglog( MSG_NOTICE, "using default group '%s' for '%s'",
				DFLT_AUTOMISC_GROUP, SUB_OPTION_GROUP );
			get_group_gid( DFLT_AUTOMISC_GROUP, &am_conf.gid, 0 );
		}
	}
	if( am_conf.mode == -1 )
	{
		msglog( MSG_NOTICE, "using default mode value '%#04o' for '%s'",
				DFLT_AUTOMISC_MODE, SUB_OPTION_MODE );
		am_conf.mode = DFLT_AUTOMISC_MODE;
	}
}

static int create_misc_dir( const char *mpath, uid_t uid, gid_t gid )
{
	struct stat st;

	if( ! mpath || mpath[ 0 ] != '/' )
	{
		msglog( MSG_WARNING, "create_misc_dir: invalid path" );
		return 0;
	}
	if( ! lstat( mpath, &st ) )
	{
		if( ! ( S_ISDIR( st.st_mode ) ) )
		{
			msglog( MSG_ALERT, "create_misc_dir: " \
				"%s exists " \
				"but its not directory", mpath );
			return 0;
		}
		if( am_conf.nocheck )
			return 1;
		if( st.st_uid != uid )
		{
			msglog( MSG_ALERT, "misc directory %s is not owned by its user. " \
				"fixing", mpath );

			if( chown( mpath, uid , -1 ) )
				msglog( MSG_ERR|LOG_ERRNO, "create_misc_dir: chown %s",
					mpath );
		}
		if( st.st_gid != gid )
		{
			msglog( MSG_ALERT, "misc directory %s is not owned by its group. " \
				"fixing", mpath );

			if( chown( mpath, -1 , gid ) )
				msglog( MSG_ERR|LOG_ERRNO, "create_misc_dir: chown %s",
					mpath );
		}
		if( ( st.st_mode & MODE_ALL ) != am_conf.mode )
		{
			msglog( MSG_ALERT, "unexpected permissions for misc directory '%s'. " \
					"fixing", mpath );

			if( chmod( mpath, am_conf.mode ) )
				msglog( MSG_ERR|LOG_ERRNO, "create_misc_dir: " \
					"chmod %s", mpath );
		}
	}
	else if( errno == ENOENT )
	{
		msglog( MSG_INFO, "misc directory %s does not exist. creating", mpath );

		if( ! create_dir( mpath, 0700 ) )
			return 0;

		if( chmod( mpath, am_conf.mode ) )
		{
			msglog( MSG_ERR|LOG_ERRNO, "create_misc_dir: chmod %s",
					mpath );
			return 0;
		}

		if( chown( mpath, uid, gid ) )
		{
			msglog( MSG_ERR|LOG_ERRNO, "create_misc_dir: chown %s",
					mpath );
			return 0;
		}
	}
	else
	{
		msglog( MSG_ERR|LOG_ERRNO, "create_misc_dir: lstat %s",
				mpath );
		return 0;
	}
	return 1;
}

module_info *module_init( char *subopt, const char *hdir )
{
	automisc_conf_init( subopt );

	if( ! create_dir( am_conf.realpath, 0700 ) )
	{
		msglog( MSG_ALERT, "module_init: " \
			"could not create automisc dir %s",
			am_conf.realpath );
		return NULL;
	}

	if( ! strcmp( hdir, am_conf.realpath ) )
	{
		msglog( MSG_ALERT, "misc dir and autofs dir are same" );
		return NULL;
	}

	return &automisc_info;
}

/*translates, for the given name, what is real dir */
void module_dir( char *buf, int len, const char *name )
{
	char a, b;

	switch( am_conf.level )
	{
		case 0:
			snprintf( buf, len, "%s/%s",
				am_conf.realpath, name);
			break;
		case 1:
			a = tolower( name[ 0 ] );
			snprintf( buf, len, "%s/%c/%s",
				am_conf.realpath, a,name );
			break;
		default:
			a = tolower( name[ 0 ] );
			b = tolower( name[ 1 ] ? name[ 1 ] : name[ 0 ] );
			snprintf( buf, len, "%s/%c/%c%c/%s",
				am_conf.realpath, a, a, b, name );
	}
}

/* create real misc dir and check permissions.*/
int module_dowork(const char *name,
		const char *dummy,
		char *realdir, int rlen)
{
	struct stat st;

	if( ! name || strlen( name ) > NAME_MAX )
		return 0;

	module_dir( realdir, rlen, name );

	if( am_conf.fastmode && ! stat( realdir, &st ) )
		return 1;

	return create_misc_dir( realdir, am_conf.uid, am_conf.gid );
}

void module_clean( void )
{
	/*nothing to be done here*/
}
