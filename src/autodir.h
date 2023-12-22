
/*

Copyright (C) (2004 - 2005) (Venkata Ramana Enaganti) <ramana@intraperson.com>

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

#ifndef AUTODIR_H
#define AUTODIR_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#define AUTODIR_VERSION		PACKAGE_VERSION
#else
#define AUTODIR_VERSION		""
#endif

/*only versin 4 is supported*/
#define AUTODIR_PROTO_MIN 4
#define AUTODIR_PROTO_MAX 4
#define AUTODIR_PROTO_DEFAULT 4

char *autodir_name( void );
void autodir_option_path( char ch, char *arg, int valid );
void autodir_option_modpath( char ch, char *arg, int valid );
void autodir_option_modopt(char ch,char *arg,int valid);
void autodir_option_pidfile( char ch, char *arg, int valid );
void autodir_option_timeout( char ch, char *arg, int valid );
void autodir_option_fg( char ch, char *arg, int valid );
void autodir_option_multipath( char ch, char *arg, int valid );
void autodir_option_multiprefix( char ch, char *arg, int valid );

#endif
