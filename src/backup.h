/*

Copyright (C) (2004 - 2005) (Venkata Ramana Enaganti) <ramana@intraperson.com>

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

#ifndef _BACKUP_H_INCLUDED_
#define _BACKUP_H_INCLUDED_

void backup_init( void );
void backup_add( const char *name, const char *path );
void backup_remove( const char *name, int force );
void backup_stop( void );
void backup_stop_set( void );

void backup_option_path( char ch, char *arg, int valid );
void backup_option_wait( char ch, char *arg, int valid );
void backup_option_wait2finish( char ch, char *arg, int valid );
void backup_option_nokill( char ch, char *arg, int valid );
void backup_option_max_proc( char ch, char *arg, int valid );
void backup_option_life( char ch, char *arg, int valid );

#endif
