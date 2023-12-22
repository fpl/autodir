
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

#ifndef MODULE_H
#define MODULE_H

typedef struct module_info {
	const char *name;
	int protocol;
} module_info;

/*last three digits are for minor and remaining for major*/
#define MODULE_PROTOCOL_SUPPORTED (1001)

module_info *(*mod_init)( char *, const char * );
void (*mod_dir)( char *, int , const char * );
int (*mod_dowork)( const char *, const char *, char *, int );
void (*mod_clean)( void );

void module_load(char *apath);
void module_option_modpath(char ch, char *arg, int valid);
void module_option_modopt(char ch, char *arg, int valid);
const char *module_name(void);

#endif
