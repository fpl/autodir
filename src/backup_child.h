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

#ifndef _BACKUP_CHILD_H_INCLUDED_
#define _BACKUP_CHILD_H_INCLUDED_

void backup_child_init( int size, int blife );
void backup_child_start( const char *name, const char *path );
void backup_child_wait( const char *name );
void backup_child_kill( const char *name );
int backup_child_count( void );
#if 0
int backup_child_running( const char *name );
#endif
void backup_child_stop( void );
void backup_child_stop_set( void );

#endif
