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

#ifndef _BACKUP_QUEUE_H_INCLUDED_
#define _BACKUP_QUEUE_H_INCLUDED_

void backup_queue_init( int backup_wait, int maxproc );
int backup_queue_remove( const char *name );
void backup_queue_add( const char *name, const char *path );
void backup_queue_stop_set( void );
void backup_queue_stop( void );

#endif
