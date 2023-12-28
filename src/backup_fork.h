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

#ifndef _BACKUP_FORK_H_INCLUDED_
#define _BACKUP_FORK_H_INCLUDED_

#include <sys/types.h>


pid_t backup_fork_new( const char *name, const char *path );
int backup_waitpid(pid_t pid, const char *name, int block);
void backup_kill( pid_t pid, const char *name );
void backup_soft_signal( pid_t pid );
void backup_fast_kill( pid_t pid, const char *name );

void backup_fork_option_pri( char ch, char *arg, int valid );

#endif
