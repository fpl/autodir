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


#define MODULE_NAME			"autogroup"
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

/*do not allow user private groups*/
#define SUB_OPTION_NOPRIV		"nopriv"

/*mode to use for group directory creation*/
#define SUB_OPTION_MODE			"mode"

/*do no monitor directory permissioins*/
#define SUB_OPTION_NOCHECK		"nocheck"

/*owner for every group directory*/
#define SUB_OPTION_OWNER		"owner"

/*group for every group directory*/
#define SUB_OPTION_GROUP		"group"

/*do everything quite fast!*/
#define SUB_OPTION_FASTMODE		"fastmode"

/*Rename dir to copy all those group dirs with gid mismatch/stale*/
#define SUB_OPTION_RENAMEDIR		"renamedir"

/********************************************************/

/*default module option values*/
#define DFLT_AUTOGROUP_REALPATH		"/" MODULE_NAME
#define DFLT_AUTOGROUP_LEVEL		2
#define DFLT_AUTOGROUP_NOPRIV		0
#define DFLT_AUTOGROUP_MODE		( S_IRWXG | S_ISGID )


/**********************************************************
  module interface methods  
 **********************************************************/

module_info *module_init( char *subopt, const char *hdir );

void module_dir( char *buf, int len, const char *name );

int module_dowork(const char *name,
		const char *hdir,
		char *ghome, int glen);

void module_clean( void );




module_info autogroup_info = { MODULE_NAME, MODULE_PROTOCOL };

/*module option values*/
static struct {
	char realpath[ PATH_MAX+1 ];
	char renamedir[ PATH_MAX+1 ];
	int level;
	int nopriv;
	int nocheck;
	int fastmode;
	mode_t mode;
	uid_t owner;
	gid_t group;
} ag_conf;

static long int grp_bufsz;
static long int pwd_bufsz;

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
		msglog( MSG_FATAL, "module suboption '%s' needs value",
					SUB_OPTION_LEVEL );
	else if( level > 2 )
		msglog( MSG_FATAL, "invalid '%s' module suboption %s",
					SUB_OPTION_LEVEL, val );
	return level;
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
		msglog( MSG_ALERT, "suboption '%s' is given too liberal " \
				"permissions '%#04o'",
				SUB_OPTION_MODE, mode );

	/*group permissions given?*/
	else if( ( mode & S_IRWXG ) != S_IRWXG )
		msglog( MSG_ALERT, "suboption '%s' is given too restrictive " \
				"permissions '%#04o' " \
				"for group members", SUB_OPTION_MODE, mode );

	return mode;
}

static uid_t owner_option_check( const char *val )
{
	struct passwd *pass;
	
	errno = 0;
	pass = getpwnam( val );
	if( pass )
		return pass->pw_uid;
	if( ! errno )
		msglog( MSG_FATAL, "no user found with name %s", val );
	else
		msglog( MSG_FATAL|LOG_ERRNO, "owner_option_check: getpwnam %s",
						val );

	return 0;
}

static gid_t group_option_check( const char *val )
{
	struct group *grp;

	errno = 0;
	grp = getgrnam( val );
	if( grp )
		return grp->gr_gid;
	if( ! errno )
		msglog( MSG_FATAL, "no group found with name %s", val );
	msglog( MSG_FATAL|LOG_ERRNO, "group_option_check: getgrnam %s", val );

	return -1;
}

static void option_process( char *subopt )
{
	char *value;
	enum {
		OPTION_REALPATH_IDX = 0,
		OPTION_LEVEL_IDX,
		OPTION_NOPRIV_IDX,
		OPTION_MODE_IDX,
		OPTION_NOCHECK_IDX,
		OPTION_OWNER_IDX,
		OPTION_GROUP_IDX,
		OPTION_FASTMODE_IDX,
		OPTION_RENAMEDIR_IDX,
		END
	};

	char *const sos[] = {
		[ OPTION_REALPATH_IDX ] = (char*const)SUB_OPTION_REALPATH,
		[ OPTION_LEVEL_IDX    ] = (char*const)SUB_OPTION_LEVEL,
		[ OPTION_NOPRIV_IDX   ] = (char*const)SUB_OPTION_NOPRIV,
		[ OPTION_MODE_IDX     ] = (char*const)SUB_OPTION_MODE,
		[ OPTION_NOCHECK_IDX  ] = (char*const)SUB_OPTION_NOCHECK,
		[ OPTION_OWNER_IDX    ]	= (char*const)SUB_OPTION_OWNER,
		[ OPTION_GROUP_IDX    ]	= (char*const)SUB_OPTION_GROUP,
		[ OPTION_FASTMODE_IDX ] = (char*const)SUB_OPTION_FASTMODE,
		[ OPTION_RENAMEDIR_IDX ] = (char*const)SUB_OPTION_RENAMEDIR,
		[ END                 ] = NULL
	};

	/*return to apply default values*/
	if( ! subopt || ! isgraph( *subopt ) ) return;

	while( *subopt != 0 )
	{
		switch( getsubopt( &subopt, sos, &value ) )
		{
			case OPTION_REALPATH_IDX:
				string_n_copy( ag_conf.realpath, 
					path_option_check( value,
						sos[ OPTION_REALPATH_IDX ] ),
				        sizeof(ag_conf.realpath) );
				break;

			case OPTION_NOPRIV_IDX:
				ag_conf.nopriv = 1;
				break;

			case OPTION_LEVEL_IDX:
				ag_conf.level = level_option_check( value );
				break;

			case OPTION_MODE_IDX:
				ag_conf.mode = mode_option_check( value );
				break;

			case OPTION_NOCHECK_IDX:
				ag_conf.nocheck = 1;
				break;

			case OPTION_OWNER_IDX:
				ag_conf.owner = owner_option_check( value );
				break;

			case OPTION_GROUP_IDX:
				ag_conf.group = group_option_check( value );
				break;
		
			case OPTION_FASTMODE_IDX:
				ag_conf.fastmode = 1;
				break;

			case OPTION_RENAMEDIR_IDX:
				string_n_copy( ag_conf.renamedir, 
					path_option_check( value,
						sos[ OPTION_RENAMEDIR_IDX ] ),
					sizeof( ag_conf.renamedir) );
				break;

			default:
				msglog( MSG_FATAL,
					"unknown module suboption %s", value );
		}
	}
}

static void autogroup_conf_init( char *opts )
{
	ag_conf.realpath[ 0 ] = 0;
	ag_conf.renamedir[ 0 ] = 0;
	ag_conf.nopriv = -1;
	ag_conf.level = -1;
	ag_conf.mode = -1;
	ag_conf.nocheck = 0;
	ag_conf.owner = 0;
	ag_conf.group = -1;
	ag_conf.fastmode = 0;

	option_process( opts );

	/*assign default values to unspecified options*/
	if( ! ag_conf.realpath[ 0 ] )
	{
		msglog( MSG_NOTICE, "using default value '%s' for '%s'",
				DFLT_AUTOGROUP_REALPATH, SUB_OPTION_REALPATH );
		string_n_copy( ag_conf.realpath, DFLT_AUTOGROUP_REALPATH,
				sizeof(ag_conf.realpath) );
	}
	if( ag_conf.level == -1 )
	{
		msglog( MSG_NOTICE, "using default value '%d' for '%s'",
				DFLT_AUTOGROUP_LEVEL, SUB_OPTION_LEVEL );
		ag_conf.level = DFLT_AUTOGROUP_LEVEL;
	}
	if( ag_conf.nopriv == -1 )
	{
		msglog( MSG_NOTICE, "using default value '%s' for '%s'",
				DFLT_AUTOGROUP_NOPRIV > 0 ? "true": "false",
				SUB_OPTION_NOPRIV );
		ag_conf.nopriv = DFLT_AUTOGROUP_NOPRIV;
	}
	if( ag_conf.mode == -1 )
	{
		msglog( MSG_NOTICE, "using default value '%#04o' for '%s'",
				DFLT_AUTOGROUP_MODE, SUB_OPTION_MODE );
		ag_conf.mode = DFLT_AUTOGROUP_MODE;
	}
}


#define TME_FORMAT "-%Y_%d%b_%H:%M:%S." MODULE_NAME

static int create_group_dir( const char *name, const char *groupdir,
							uid_t uid, gid_t gid )
{
	struct stat st;

	if( ! groupdir || groupdir[ 0 ] != '/' )
	{
		msglog( MSG_WARNING, "create_group_dir: invalid path" );
		return 0;
	}
	if( ! lstat( groupdir, &st ) )
	{
		if( ! ( S_ISDIR( st.st_mode ) ) )
		{
			msglog( MSG_ALERT, "create_group_dir: " \
				"%s exists " \
				"but its not directory", groupdir );
			return 0;
		}
		if( ag_conf.nocheck )
			return 1;
		if( st.st_gid != gid )
		{
			if( *ag_conf.renamedir )
			{
				msglog( MSG_ALERT,
					"group dir %s is not owned by its user. " \
					"moving to %s", groupdir,
					ag_conf.renamedir );
				if( rename_dir( groupdir, ag_conf.renamedir,
							    name, TME_FORMAT) )
					return 0;
				else
					goto groupdir_create;
				return 0;
			}
			msglog( MSG_ALERT, "group directory %s is not owned by its group. " \
				"fixing", groupdir );

			if( chown( groupdir, -1 , gid ) )
				msglog( MSG_ERR|LOG_ERRNO, "create_group_dir: chown %s",
					groupdir );
		}
		if( st.st_uid != uid )
		{
			msglog( MSG_ALERT, "group directory %s is not owned by its user. " \
				"fixing", groupdir );

			if( chown( groupdir, uid, -1 ) )
				msglog( MSG_ERR|LOG_ERRNO, "create_group_dir: chown %s",
					groupdir );
		}
		if( ( st.st_mode & MODE_ALL ) != ag_conf.mode )
		{
			msglog( MSG_ALERT, "unexpected permissions for group directory '%s'. " \
					"fixing", groupdir );

			if( chmod( groupdir, ag_conf.mode ) )
				msglog( MSG_ERR, "create_group_dir: " \
					"chmod %s", groupdir );
		}
	}
	else if( errno == ENOENT )
	{
groupdir_create:
		msglog( MSG_INFO, "creating group directory %s", groupdir );

		if( ! create_dir( groupdir, 0700 ) )
			return 0;

		if( chmod( groupdir, ag_conf.mode ) )
		{
			msglog( MSG_ERR|LOG_ERRNO, "create_group_dir: chmod %s",
					groupdir );
			return 0;
		}

		if( chown( groupdir, uid, gid ) )
		{
			msglog( MSG_ERR|LOG_ERRNO, "create_group_dir: chown %s",
					groupdir );
			return 0;
		}
	}
	else
	{
		msglog( MSG_ERR|LOG_ERRNO, "create_group_dir: lstat %s",
				groupdir );
		return 0;
	}
	return 1;
}

/*groupbase is virtual base directory for group directories*/
module_info *module_init( char *subopt, const char *groupbase )
{
	autogroup_conf_init( subopt );

	if( ! create_dir( ag_conf.realpath, 0700 ) )
	{
		msglog( MSG_ALERT, "could not create group dir %s",
							ag_conf.realpath );
		return NULL;
	}
	if( *ag_conf.renamedir && ! create_dir( ag_conf.renamedir, 0700 ) )
	{
		msglog( MSG_ALERT, "could not create renamedir %s",
							ag_conf.renamedir );
		return NULL;
	}
	if( ! strcmp( groupbase, ag_conf.realpath ) )
	{
		msglog( MSG_ALERT, "group dir and autofs dir are same" );
		return NULL;
	}
	if( ( pwd_bufsz = sysconf( _SC_GETPW_R_SIZE_MAX ) ) == -1 )
	{
		msglog( MSG_ALERT|LOG_ERRNO, "sysconf _SC_GETPW_R_SIZE_MAX" );
		return NULL;
	}
	if( ( grp_bufsz = sysconf( _SC_GETGR_R_SIZE_MAX ) ) == -1 )
	{
		msglog( MSG_ALERT|LOG_ERRNO, "sysconf _SC_GETGR_R_SIZE_MAX" );
		return NULL;
	}

	return &autogroup_info;
}

/*translates, for the given name, what is real dir */
void module_dir( char *buf, int len, const char *name )
{
	char a, b;

	switch( ag_conf.level )
	{
		case 0:
			snprintf( buf, len, "%s/%s",
				ag_conf.realpath, name);
			break;
		case 1:
			a = tolower( name[ 0 ] );
			snprintf( buf, len, "%s/%c/%s",
				ag_conf.realpath, a,name );
			break;
		default:
			a = tolower( name[ 0 ] );
			b = tolower( name[ 1 ] ? name[ 1 ] : name[ 0 ] );
			snprintf( buf, len, "%s/%c/%c%c/%s",
				ag_conf.realpath, a, a, b, name );
	}
}

static int is_user_private_group( const char *name, gid_t gid )
{
	char *buf = alloca( sizeof(char)*pwd_bufsz );
	struct passwd pwd, *pass;
	gid_t g = -1;
	
	if( ! buf )
	{
		msglog( MSG_ERR, "alloca: could not allocate stack" );
		return 0;
	}
	
	errno = getpwnam_r( name, &pwd, buf, pwd_bufsz, &pass );
	if( pass ) g = pass->pw_gid;

	if( pass && g == gid )
		return 1;

	if( ! pass && errno )
	{
		msglog( MSG_ERR|LOG_ERRNO, "getpwnam_r" );
		return -1;
	}

	return 0;
}

static int get_group_info( const char *name, gid_t *gid )
{
	char *buf = alloca( sizeof(char)*grp_bufsz );
	struct group grp, *group;

	if( ! buf )
	{
		msglog( MSG_ERR, "alloca: could not allocate stack" );
		return 0;
	}
	errno = getgrnam_r( name, &grp, buf, grp_bufsz, &group );
	if( group )
	{
		( *gid ) = ag_conf.group == -1 ? group->gr_gid : ag_conf.group;
		return 1;
	}
	if( ! errno )
		msglog( MSG_WARNING, "no group found with name %s", name );
	else msglog( MSG_ERR|LOG_ERRNO, "get_group_info: getgrnam_r" );

	return 0;
}

/* create real group dir and check permissions.*/
int module_dowork(const char *name,
		const char *unused,
		char *realdir, int rlen)
{
	int upriv;
	gid_t gid;
	struct stat st;

	if( ! name || strlen( name ) > NAME_MAX )
		return 0;

	module_dir( realdir, rlen, name );

	if( ag_conf.fastmode && ! stat( realdir, &st ) )
		return 1;

	if( ! get_group_info( name, &gid ) )
		return 0;

	if( ag_conf.nopriv )
	{
		if( ( upriv = is_user_private_group( name, gid ) ) == 1 )
		{
			msglog( MSG_WARNING, "user private group %s " \
						"not allowed", name );
			return 0;
		}
		if( upriv == -1 ) return 0;
	}
	return create_group_dir( name, realdir, ag_conf.owner, gid );
}

void module_clean( void )
{
	/*nothing to be done*/
}










#ifdef TEST

#include <assert.h>

char *autodir_name(void)
{
    return "test autodir";
}

void *test_th(void *x)
{
    char realdir[PATH_MAX+1];
    char name[128];
    int i;

    while (1) {
	for (i = 10000; i < 19000; i++) {
	    snprintf(name, sizeof(name), "t%d", i);
	    workon_name(name);
	    assert(module_dowork(name, NULL, realdir, sizeof(realdir)));
	    workon_release(name);
	}
	printf("i=%d\n", i);
    }
}

int main(void)
{
    pthread_t id;

    thread_init();
    msg_init();
    msg_console_on();
    workon_init();
    module_init(strdup("renamedir=/tmp/renamedir"), "/test");

    //test_th(NULL);
    pthread_create(&id, 0, test_th, NULL);
    pthread_create(&id, 0, test_th, NULL);
    pthread_create(&id, 0, test_th, NULL);
    pthread_create(&id, 0, test_th, NULL);
    pthread_create(&id, 0, test_th, NULL);
    pthread_create(&id, 0, test_th, NULL);
    pthread_create(&id, 0, test_th, NULL);
    pthread_create(&id, 0, test_th, NULL);
    pthread_create(&id, 0, test_th, NULL);
    pthread_create(&id, 0, test_th, NULL);

    pthread_join(id, NULL);
}

#endif
