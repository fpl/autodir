
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
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include "miscfuncs.h"
#include "module.h"
#include "msg.h"


#define MODULE_NAME			"autohome"
#define MODULE_PROTOCOL			(1001)

/*****************************************************
 Sub-options supported by this module
*****************************************************/

/*real directory organizaton level*/
#define SUB_OPTION_LEVEL		"level"

/*system skel directory*/
#define SUB_OPTION_SKEL			"skel"

/*no skel need to copy?*/
#define SUB_OPTION_NOSEKL		"noskel"

/*real base directory*/
#define SUB_OPTION_REALPATH		"realpath"

/*mode to use for home directory creation*/
#define SUB_OPTION_MODE			"mode"

/*do no monitor directory permissioins*/
#define SUB_OPTION_NOCHECK		"nocheck"

/*do not check skel directory contents while copying*/
#define SUB_OPTION_NOSKELCHECK		"noskelcheck"
 
/*Do not check if directory to be created really is the
  home directory */
#define SUB_OPTION_NOHOMECHECK		"nohomecheck"

/*group for every home directory*/
#define SUB_OPTION_GROUP		"group"

/*owner for every home directory*/
#define SUB_OPTION_OWNER		"owner"

/*Try to be fast. skipping everything else*/
#define SUB_OPTION_FASTMODE		"fastmode"

/*Rename dir to copy all those home dirs with uid mismatch/stale homes*/
#define SUB_OPTION_RENAMEDIR		"renamedir"

/************************************************************/


/*default module option values*/
#define DFLT_AUTOHOME_REALDIR		"/" MODULE_NAME
#define DFLT_AUTOHOME_SKELDIR		"/etc/skel"
#define DFLT_AUTOHOME_LEVEL		2
#define DFLT_AUTOHOME_MODE		S_IRWXU /*full owner permissions*/

/****************************
 * module interface methods  
 ****************************/

void module_dir( char *buf, int len, const char *name );

int module_dowork(const char *name,
		const char *hdir,
		char *ahome, int alen);

void module_clean( void );

module_info *module_init( char *subopt, const char *hdir );

/*****************************/


module_info autohome_info = { MODULE_NAME, MODULE_PROTOCOL };

/*this is the file 'touched'
  in newly created home directory
  to keep record that home directory
  created successfully without interuption
 */
#define AUTOHOME_STAMP_FILE		"." MODULE_NAME
#define READ_BUF_SIZE			8000
#define SKEL_FILE_MAX_COPY		(1024*1024) /*1MB*/


/*module sub-option values*/
static struct {
	char realpath[ PATH_MAX+1 ]; /*real base directory*/
	char skel[ PATH_MAX+1 ]; 
	char renamedir[ PATH_MAX+1 ];
	int noskel; 
	int level; 
	int nocheck; 
	int noskelcheck; 
	int fastmode;
 	int nohomecheck;
	mode_t mode; 
	gid_t group; 
	uid_t owner;
} ah_conf;

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
	{
		msglog( MSG_FATAL, "module suboption '%s' needs value",
				SUB_OPTION_LEVEL );
	}
	else if( level > 2 )
	{
		msglog( MSG_FATAL, "invalid '%s' module suboption %s",
				SUB_OPTION_LEVEL, val );
	}

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
		msglog( MSG_FATAL, "invalid octal mode value '%s' " \
				"with suboption '%s'",
				val, SUB_OPTION_MODE );
	}
	
	/*world permissions?*/
	if( mode & S_IRWXO )
		msglog( MSG_ALERT, "suboption '%s' is given too liberal " \
				"permissions '%#04o'",
				SUB_OPTION_MODE, mode );

	/*home user permissions?*/
	else if( ( mode & S_IRWXU ) != S_IRWXU )
		msglog( MSG_ALERT, "suboption '%s' is given too restrictive " \
				"permissions '%#04o' " \
				"for home owners", SUB_OPTION_MODE, mode );

	return mode;
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

static void option_process( char *subopt )
{
	char *value;
	enum {
		OPTION_REALPATH_IDX = 0,
		OPTION_SKEL_IDX,
		OPTION_NOSKEL_IDX,
		OPTION_LEVEL_IDX,
		OPTION_MODE_IDX,
		OPTION_NOCHECK_IDX,
		OPTION_NOSKELCHECK_IDX,
		OPTION_OWNER_IDX,
		OPTION_GROUP_IDX,
		OPTION_FASTMODE_IDX,
 		OPTION_NOHOMECHECK_IDX,
		OPTION_RENAMEDIR_IDX,
		END
	};

	char *const sos[] = {
		[ OPTION_REALPATH_IDX ] = (char*const)SUB_OPTION_REALPATH,
		[ OPTION_SKEL_IDX     ] = (char*const)SUB_OPTION_SKEL,
		[ OPTION_NOSKEL_IDX   ] = (char*const)SUB_OPTION_NOSEKL,
		[ OPTION_LEVEL_IDX    ] = (char*const)SUB_OPTION_LEVEL,
		[ OPTION_MODE_IDX     ] = (char*const)SUB_OPTION_MODE,
		[ OPTION_NOCHECK_IDX  ] = (char*const)SUB_OPTION_NOCHECK,
		[ OPTION_NOSKELCHECK_IDX ] = (char*const)SUB_OPTION_NOSKELCHECK,
		[ OPTION_OWNER_IDX    ]	= (char*const)SUB_OPTION_OWNER,
		[ OPTION_GROUP_IDX    ]	= (char*const)SUB_OPTION_GROUP,
		[ OPTION_FASTMODE_IDX ] = (char*const)SUB_OPTION_FASTMODE,
 		[ OPTION_NOHOMECHECK_IDX ] = (char*const)SUB_OPTION_NOHOMECHECK,
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
				string_n_copy( ah_conf.realpath, 
					path_option_check( value,
						sos[ OPTION_REALPATH_IDX ] ),
					sizeof( ah_conf.realpath) );
				break;

			case OPTION_SKEL_IDX:
				string_n_copy( ah_conf.skel,
					path_option_check( value,
						sos[ OPTION_SKEL_IDX ] ),
					sizeof( ah_conf.skel) );
				break;

			case OPTION_NOSKEL_IDX:
				ah_conf.noskel = 1;
				break;

			case OPTION_LEVEL_IDX:
				ah_conf.level = level_option_check( value );
				break;

			case OPTION_MODE_IDX:
				ah_conf.mode = mode_option_check( value );
				break;

			case OPTION_NOCHECK_IDX:
				ah_conf.nocheck = 1;
				break;

			case OPTION_NOSKELCHECK_IDX:
				ah_conf.noskelcheck = 1;
				break;

			case OPTION_OWNER_IDX:
				ah_conf.owner = owner_option_check( value );
				break;

			case OPTION_GROUP_IDX:
				ah_conf.group = group_option_check( value );
				break;

			case OPTION_FASTMODE_IDX:
				ah_conf.fastmode = 1;
				break;

 			case OPTION_NOHOMECHECK_IDX:
 				ah_conf.nohomecheck = 1;
 				break;

			case OPTION_RENAMEDIR_IDX:
				string_n_copy( ah_conf.renamedir, 
					path_option_check( value,
						sos[ OPTION_RENAMEDIR_IDX ] ),
					sizeof( ah_conf.renamedir) );
				break;

			default:
				msglog( MSG_FATAL,
				    "unknown module suboption '%s'", value );
		}
	}
}

static void autohome_conf_init( char *opts )
{
	ah_conf.realpath[ 0 ] = 0;
	ah_conf.skel[ 0 ] = 0;
	ah_conf.renamedir[ 0 ] = 0;
	ah_conf.noskel = 0;
	ah_conf.level = -1;
	ah_conf.mode = -1;
	ah_conf.nocheck = 0;
	ah_conf.noskelcheck = 0;
	ah_conf.owner = -1;
	ah_conf.group = -1;
	ah_conf.fastmode = 0;
 	ah_conf.nohomecheck = 0;

	option_process( opts );

	/*assign default values to unspecified options*/
	if( ! ah_conf.realpath[ 0 ] )
	{
		msglog( MSG_NOTICE, "using default value '%s' for '%s'",
				DFLT_AUTOHOME_REALDIR, SUB_OPTION_REALPATH );

		string_n_copy( ah_conf.realpath, DFLT_AUTOHOME_REALDIR,
				sizeof(ah_conf.realpath) );
	}
	if( ! ah_conf.skel[ 0 ] && ! ah_conf.noskel )
	{
		msglog( MSG_NOTICE, "using default value '%s' for '%s'",
			DFLT_AUTOHOME_SKELDIR, SUB_OPTION_SKEL );

		string_n_copy( ah_conf.skel, DFLT_AUTOHOME_SKELDIR,
				sizeof(ah_conf.skel) );
	}
	if( ah_conf.level == -1 )
	{
		msglog( MSG_NOTICE, "using default value '%d' for '%s'",
				DFLT_AUTOHOME_LEVEL, SUB_OPTION_LEVEL );
		ah_conf.level = DFLT_AUTOHOME_LEVEL;
	}
	if( ah_conf.mode == -1 )
	{
		msglog( MSG_NOTICE, "using default value '%#4o' for '%s'",
				DFLT_AUTOHOME_MODE, SUB_OPTION_MODE );
		ah_conf.mode = DFLT_AUTOHOME_MODE;
	}
}

/*
   When auto home stamp file is missing in home dir,
  every skel file is checked for correct ownership
 */
static int check_file_owner( const char *file, uid_t uid )
{
	struct stat st;

	if( lstat( file, &st ) )
	{
		msglog( MSG_ERR|LOG_ERRNO, "check_file_owner: lstat %s", file );
		return 0;
	}
	if( ! S_ISREG( st.st_mode ) )
	{
		msglog( MSG_NOTICE, "check_file_owner: " \
			"%s is not regular file", file );
		return 0;
	}
	if( st.st_uid != uid )
	{
		msglog( MSG_NOTICE, "improper owner for file %s. fixing",
					file );

		if( chown( file, uid, -1 ) )
		{
			msglog( MSG_ERR|LOG_ERRNO, "check_file_owner: chown %s",
					file );
			return 0;
		}
	}
	return 1;
}

/*
   When auto home stamp file is missing in home dir,
  every skel dir is checked for correct ownership
 */
static int check_dir_owner( const char *dir, uid_t uid )
{
	struct stat st;

	if( lstat( dir, &st ) )
	{
	       msglog( MSG_ERR|LOG_ERRNO, "check_dir_owner: lstat %s", dir );
	       return 0;
	}
	if( ! S_ISDIR( st.st_mode ) )
	{
	       msglog( MSG_NOTICE, "check_dir_owner: " \
		       "%s is not directory", dir );
	       return 0;
	}
	if( st.st_uid != uid )
	{
		msglog( MSG_NOTICE, "improper owner for dir %s. fixing", dir );

		if( chown( dir, uid, -1 ) )
		{
			msglog( MSG_ERR|LOG_ERRNO, "check_dir_owner: chown %s",
					dir );
			return 0;
		}
	}
	return 1;
}

static int copy_skel_file( const char *sfile, /*source file*/
				const char *dfile, /*destination file*/
				const struct stat *st, /*stat of source file*/
				uid_t uid, /*file owner*/
				gid_t gid ) /*file group*/
{
	char buf[ READ_BUF_SIZE ];
	int sfd, dfd, count = 0, n;

	/*must be absolute path*/
	if( ! sfile || ! dfile ||
		sfile[ 0 ] != '/' ||
		dfile[ 0 ] != '/' )
	{
		msglog( MSG_WARNING, "copy_skel_file: invalid path" );
		return 0;
	}

	if( ! ah_conf.noskelcheck )
	{
		/*definitly NO*/
		if( st->st_mode & S_IWOTH )
		{
			msglog( MSG_WARNING, "copy_skel_file: " \
				"world write permission for %s. omitting", sfile );
			return 0;
		}

		/*we do not want more then one door to this file*/
		if( st->st_nlink > 1 )
		{
			msglog( MSG_WARNING, "copy_skel_file: " \
				"more then one hard link for %s. omitting", sfile );
			return 0;
		}
	}

	if( ( sfd = open( sfile, O_RDONLY ) ) == -1 )
	{
		msglog( MSG_WARNING, "copy_skel_file: " \
				"open %s", sfile );
		return 0;
	}

	dfd = open( dfile, O_WRONLY | O_CREAT | O_EXCL,
			st->st_mode & S_IRWXU );
	if( dfd == -1 )
	{
		if( errno == EEXIST )
		{
			msglog( MSG_NOTICE, "copy_skel_file: " \
				"file %s already exists", dfile );
			check_file_owner( dfile, uid );
		}
		else msglog( MSG_ERR|LOG_ERRNO, "copy_skel_file: open %s",
						dfile );
		return 0;
	}

	while( 1 )
	{
		n = read( sfd, buf, sizeof(buf) );
		if( ! n )
		{
			if( fchown( dfd, uid, gid ) == -1 )
			{
				msglog( MSG_ERR|LOG_ERRNO, 
					"copy_skel_file: fchown %s", dfile );
				return 0;
			}
			close( sfd );
			close( dfd );
			return 1;
		}
		else if( n < 0 )
			msglog( MSG_ERR|LOG_ERRNO, "copy_skel_file: read %s",
					sfile );

		else if( ( count = count + n ) > SKEL_FILE_MAX_COPY
				&& ! ah_conf.noskelcheck )
			msglog( MSG_WARNING, "copy_skel_file: " \
				"%s exceeding size limit", sfile );

		else if( ! write_all( dfd, buf, n ) )
			msglog( MSG_ERR, "copy_skel_file: write error %s",
					dfile );

		else continue;

		/*Remove the file if half copied or for some other errors.*/
		/*Assuming nothing better then something in inconsistent state.*/
		unlink( dfile );
		break;
	}

	close( sfd );
	close( dfd );

	return 0;
}

/*recursive function*/
static int copy_skel_dir( const char *src, /*source directory*/
				const char *dest, /*destination directory*/
				const struct stat *st, /*stat of source directory*/
				uid_t uid,
				gid_t gid )
{
	char sdent[ PATH_MAX+1 ]; /*source directory entry*/
	char ddent[ PATH_MAX+1 ]; /*destination directory entry*/
	struct stat sdent_st; /*source directory entry stat*/
	DIR *dir;
	struct dirent *dent;

	if( ! src || ! dest ||
			src[ 0 ] != '/' ||
			dest[ 0 ] !='/' )
	{
		msglog( MSG_WARNING, "copy_skel_dir: invalid directory name");
		return 0;
	}

	/*definitly NO*/
	if( ! ah_conf.noskelcheck && st->st_mode & S_IWOTH )
	{
		msglog( MSG_WARNING, "copy_skel_dir: " \
			"dir %s has world write " \
			"permission. omitting", src );
		return 0;
	}

	/*careful. opendir return value must be closed
	  to avoid memory leaks.*/
	if( ( dir = opendir( src ) ) == NULL )
	{
		msglog( MSG_ERR|LOG_ERRNO, "copy_skel_dir: opendir %s",
				src );
		return 0;
	}

	while( ( dent = readdir( dir ) ) )
	{
		if( ! strcmp( dent->d_name, "." ) ||
		    ! strcmp( dent->d_name, ".." ) )
			continue;

		snprintf( sdent, sizeof(sdent),
			"%s/%s", src, dent->d_name );
		snprintf( ddent, sizeof(ddent),
			"%s/%s", dest, dent->d_name );

		if( ! ah_conf.noskelcheck && lstat( sdent, &sdent_st ) == -1 )
		{
			msglog( MSG_ERR|LOG_ERRNO, "lstat %s", sdent );
			continue;
		}
		else if( ah_conf.noskelcheck && stat( sdent, &sdent_st ) == -1 )
		{
			msglog( MSG_ERR|LOG_ERRNO, "stat %s", sdent );
			continue;
		}

		if( S_ISREG( sdent_st.st_mode ) )
		{
			copy_skel_file( sdent, ddent, &sdent_st, uid, gid );
		}
		else if( S_ISDIR( sdent_st.st_mode ) )
		{
			if( mkdir( ddent, sdent_st.st_mode & S_IRWXU ) == -1 )
			{
				if( errno == EEXIST )
				{
					msglog( MSG_NOTICE, "copy_skel_dir: " \
						"skel dir %s already exists", ddent );
					check_dir_owner( ddent, uid );
					copy_skel_dir( sdent, ddent, &sdent_st, uid, gid );
					continue;
				}
				msglog( MSG_ERR|LOG_ERRNO,
					"copy_skel_dir: mkdir %s",
					ddent );
				continue;
			}
			else
			{
				copy_skel_dir( sdent, ddent, &sdent_st, uid, gid );
				chown( ddent, uid, gid );
			}
		}
		else msglog( MSG_WARNING, "copy_skel_dir: %s is not " \
			"regular file or directory", sdent );
	}

	/*do not return without doing this*/
	closedir( dir );

	return 1;
}

/*stamp file is used to mark that
  home dir building complete and successfull*/
static int autohome_stamp( const char *dir )
{
	int fd;
	char stamp[ PATH_MAX+1 ];
	
	snprintf( stamp, sizeof(stamp),
		"%s/%s", dir, AUTOHOME_STAMP_FILE );

	fd = open( stamp, O_WRONLY | O_CREAT | O_TRUNC, 0 );
	if( fd != -1 )
	{
		close( fd );
		return 1;
	}
	return 0;
}

static int copy_skel( const char *src, /*skel directory*/
			const char *dst, /*home directory*/
			uid_t uid, /*file owner*/
			gid_t gid ) /*file group*/
{
	struct stat st;

	if( ! src || ! dst ||
			src[ 0 ] != '/' ||
			dst[ 0 ] != '/' )
		msglog( MSG_WARNING, "copy_skel: invalid dir name" );

	else if( ! ah_conf.noskelcheck && lstat( src, &st ) == -1 )
		msglog( MSG_ERR|LOG_ERRNO, "copy_skel: lstat %s", src );

	else if( ah_conf.noskelcheck && stat( src, &st ) == -1 )
		msglog( MSG_ERR|LOG_ERRNO, "copy_skel: stat %s", src );

	else if( ! S_ISDIR( st.st_mode ) )
		msglog( MSG_WARNING, "copy_skel: skel source %s is " \
			"not directory", src );

	else if( copy_skel_dir( src, dst, &st, uid, gid ) &&
			autohome_stamp( dst ) )
		return 1;

	return 0;
}


#define TME_FORMAT "-%Y_%d%b_%H:%M:%S." MODULE_NAME

static int create_home_dir( const char *name,
			const char *home, /*real path for home directory*/
			const char *skel, /*system skel source directory*/
			uid_t uid,
			gid_t gid )
{
	char stamp[ PATH_MAX+1 ];
	struct stat home_st, stamp_st;

	if( ! home || ! skel || home[ 0 ] != '/' )
	{
		msglog( MSG_WARNING, "create_home_dir: invalid path" );
		return 0;
	}

	if( ! lstat( home, &home_st ) )
	{
		if( ! ( S_ISDIR( home_st.st_mode ) ) )
		{
			msglog( MSG_ALERT, "create_home_dir: " \
				"home %s exists " \
				"but it is not directory", home );
			return 0;
		}
		if( ah_conf.nocheck )
			return 1;
		if( home_st.st_uid != uid )
		{
			if( *ah_conf.renamedir )
			{
				msglog( MSG_ALERT,
					"home %s is not owned by its user. " \
					"moving to %s", home, ah_conf.renamedir );
				if( rename_dir( home, ah_conf.renamedir,
							    name, TME_FORMAT) )
					return 0;
				else
					goto home_create;
				return 0;
			}
			msglog( MSG_ALERT, "home %s is not owned by its user. " \
				"fixing", home );

			if( chown( home, uid, -1 ) )
			{
				msglog( MSG_ERR|LOG_ERRNO, "create_home_dir: " \
					"chown %s", home );
			}
		}
		if( home_st.st_gid != gid )
		{
			msglog( MSG_ALERT, "home %s is not owned by " \
				"its group. fixing", home );

			if( chown( home, -1, gid ) )
			{
				msglog( MSG_ERR|LOG_ERRNO, "create_home_dir: " \
					"chown %s", home );
			}
		}
		if( ( home_st.st_mode & MODE_ALL ) != ah_conf.mode )
		{
			msglog( MSG_ALERT, "unexpected permissions for " \
					"home directory '%s'. fixing", home );

			if( chmod( home, ah_conf.mode ) )
			{
				msglog( MSG_ERR|LOG_ERRNO, "create_home_dir: " \
					"chmod %s", home );
			}
		}
		if( ! ah_conf.noskel )
		{
			snprintf( stamp, sizeof(stamp), "%s/%s",
					home, AUTOHOME_STAMP_FILE );
			if( lstat( stamp, &stamp_st ) && errno == ENOENT )
			{
				msglog( MSG_NOTICE, "create_home_dir: " \
					"skel stamp file %s does not exist. " \
					"copying skel dir", stamp );
				copy_skel( skel, home, uid, gid );
			}
		}
	}
	else if( errno == ENOENT )
	{
home_create:
		msglog( MSG_INFO, "creating home %s", home );

		if( ! create_dir( home, S_IRUSR | S_IWUSR | S_IXUSR ) )
			return 0;
		if( ! ah_conf.noskel )
			copy_skel( skel, home, uid, gid );
		if( chmod( home, ah_conf.mode ) )
		{
			msglog( MSG_ERR|LOG_ERRNO, "create_home_dir: chmod %s",
					home );
			return 0;
		}
		if( chown( home, uid, gid ) )
		{
			msglog( MSG_ERR, "create_home_dir: chown %s", home );
			return 0;
		}
	}
	else
	{
		msglog( MSG_ERR|LOG_ERRNO, "create_home_dir: lstat %s", home );
		return 0;
	}
	return 1;
}

module_info *module_init( char *subopt, const char *homebase )
{
	autohome_conf_init( subopt );

	if( ! create_dir( ah_conf.realpath, 0700 ) )
	{
		msglog( MSG_ALERT, "could not create home real path %s",
							ah_conf.realpath );
		return NULL;
	}
	if( *ah_conf.renamedir && ! create_dir( ah_conf.renamedir, 0700 ) )
	{
		msglog( MSG_ALERT, "could not create renamedir %s",
							ah_conf.renamedir );
		return NULL;
	}
	if( ! strcmp( homebase, ah_conf.realpath ) )
	{
		msglog( MSG_ALERT, "home base '%s' and real path '%s' are same",
				homebase, ah_conf.realpath );
		return NULL;
	}
	if( ( pwd_bufsz = sysconf( _SC_GETPW_R_SIZE_MAX ) ) == -1 )
	{
		msglog( MSG_ALERT|LOG_ERRNO, "sysconf _SC_GETPW_R_SIZE_MAX" );
		return NULL;
	}

	return &autohome_info;
}

/*translates, for the given name,
  what is real home dir
  under auto home directory
 */
void module_dir( char *buf, int len, const char *name )
{
	char a, b;

	switch( ah_conf.level )
	{
		case 0:
			snprintf( buf, len, "%s/%s",
				ah_conf.realpath, name);
			break;
		case 1:
			a = tolower( name[ 0 ] );
			snprintf( buf, len, "%s/%c/%s",
				ah_conf.realpath, a,name );
			break;
		default:
			a = tolower( name[ 0 ] );
			b = tolower( name[ 1 ] ? name[ 1 ] : name[ 0 ] );
			snprintf( buf, len, "%s/%c/%c%c/%s",
				ah_conf.realpath, a, a, b, name );
	}
}

static int get_passwd_info( const char *name, uid_t *uid,
		gid_t *gid, char *home, int len )
{
	char *buf = alloca( sizeof(char)*pwd_bufsz );
	struct passwd pwd, *pass;
	
	if( ! buf )
	{
		msglog( MSG_ERR, "alloca: could not allocate stack" );
		return 0;
	}
	errno = getpwnam_r( name, &pwd, buf, pwd_bufsz, &pass );
	if( pass )
	{
		(*uid) = ah_conf.owner != -1 ? ah_conf.owner : pass->pw_uid;
		(*gid) = ah_conf.group != -1 ? ah_conf.group : pass->pw_gid;
		string_n_copy( home, pass->pw_dir ,len );
		return 1;
	}
	if( ! errno )
		msglog( MSG_WARNING, "no user found with name %s", name );
	else
		msglog( MSG_ERR|LOG_ERRNO, "get_passwd_info: getpwnam_r" );

	return 0;
}

/* create real home dir under realpath,
   and check permissions.
 */
int module_dowork( const char *name,
			const char *homebase, /*home directory base eg. /home */
			char *realhome, /*real home directory path.
					This value is returned to autodir daemon*/
			int reallen ) /*realhome buf length*/
{
	char tmp[ PATH_MAX+1 ];
	char home[ PATH_MAX+1 ];
	struct stat st;
	uid_t uid;
	gid_t gid;

	if( ! name || strlen( name ) > NAME_MAX )
		return 0;

	module_dir( realhome, reallen, name );

	/*bypass everything if we can stat, in fast mode*/
	if( ah_conf.fastmode && ! stat( realhome, &st ) )
		return 1;

	if( ! get_passwd_info( name, &uid, &gid, home, sizeof(home) ) )
		return 0;

 	if( ( ! ah_conf.nohomecheck ) )
	{
		snprintf( tmp, sizeof(tmp), "%s/%s", homebase, name );
		if( strcmp( home, tmp ) ) {
			msglog( MSG_NOTICE, "home dirs %s,%s do not match",
					home, tmp );
			return 0;
		}
	}

	return create_home_dir( name, realhome, ah_conf.skel, uid, gid );
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
	    assert(module_dowork(name, "/test", realdir, sizeof(realdir)));
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
