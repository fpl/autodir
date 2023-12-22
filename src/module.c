
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

#define _MODULE_C_

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <ltdl.h>
#include "msg.h"
#include "autodir.h"
#include "module.h"

#define SYMBOL_MODULE_INIT		"module_init"
#define SYMBOL_MODULE_DIR		"module_dir"
#define SYMBOL_MODULE_DOWORK		"module_dowork"
#define SYMBOL_MODULE_CLEAN	 	"module_clean"

/* for loading requested module from command line option*/

/* module callbacks*/
static struct {
	lt_dlhandle handle;

	/*command line args*/
	char *mod_path;
	char *mod_subopt;
} module;

static module_info *modinfo;

/***************************************************
  ltdl is not made thread safe because,
  ltdl library calls are made from only single thread
************* **************************************/

static lt_ptr module_symbol( const char *sname )
{
	lt_ptr ptr;

	ptr = lt_dlsym( module.handle, sname );
	if( ! ptr )
		msglog( MSG_FATAL, "'%s' symbol not defined in module %s",
				sname, module.mod_path );
	return ptr;
}

static void module_check( void )
{
	struct stat st;

	if( stat( module.mod_path, &st ) )
	{
		if( errno == ENOENT || errno == ENOTDIR )
			msglog( MSG_FATAL, "could not find module at %s",
					module.mod_path );
		else msglog( MSG_FATAL|LOG_ERRNO, "module_check: stat" );
	}
	if( ! S_ISREG(st.st_mode) )
		msglog( MSG_FATAL, "module %s is not regular file",
				module.mod_path );
	if( st.st_mode & S_IWOTH )
		msglog( MSG_FATAL, "module %s has world write permissions",
				module.mod_path );
	if( st.st_uid != 0 )
		msglog( MSG_FATAL, "module %s is not owned by root",
				module.mod_path );
}

static void module_clean( void )
{
	if( module.handle )
		lt_dlclose( module.handle );
}

void module_load( char *apath )
{
	module.handle = 0;

	module_check();

	if( atexit( module_clean ) )
		msglog( MSG_FATAL, "could not register module cleanup method" );

	if( lt_dlinit() )
		msglog( MSG_FATAL, "module_load: lt_dlinit: %s", lt_dlerror() );

	if( ! ( module.handle = lt_dlopenext( module.mod_path ) ) )
		msglog( MSG_FATAL, "module open error: %s", lt_dlerror() );

	mod_init = module_symbol( SYMBOL_MODULE_INIT );
	mod_dir = module_symbol( SYMBOL_MODULE_DIR );
	mod_dowork = module_symbol( SYMBOL_MODULE_DOWORK );
	mod_clean = module_symbol( SYMBOL_MODULE_CLEAN );

	if( ! ( modinfo = mod_init( module.mod_subopt, apath ) ) )
		msglog( MSG_FATAL, "could not initialize module" );

    	if( modinfo->protocol != MODULE_PROTOCOL_SUPPORTED)
		msglog( MSG_FATAL, "required protocol '%d', " \
			"module protocol '%d' not supported",
		 	MODULE_PROTOCOL_SUPPORTED, modinfo->protocol);

	if( ! modinfo->name )
		msglog( MSG_FATAL, "missing module name info" );

	msglog( MSG_INFO, "module %s loaded from %s",
			modinfo->name, module.mod_path );
}

const char *module_name( void )
{
    return modinfo->name;
}

/*************** option handling functions *****************/

void module_option_modpath( char ch, char *arg, int valid )
{
	if( ! valid )
		msglog( MSG_FATAL, "module option -%c missing", ch );
	if( ! arg || arg[ 0 ] != '/' )
		msglog( MSG_FATAL, "module option -%c argument " \
				"is not absolute path", ch );
	module.mod_path = arg;
}

void module_option_modopt( char ch, char *arg, int valid )
{
	module.mod_subopt = valid ? arg : NULL;
}

/*************** end of option handling functions *****************/
