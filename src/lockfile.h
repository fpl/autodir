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

#ifndef LOCKFILE_H
#define LOCKFILE_H

#include <sys/types.h>

int lockfile_create(const char *name);
void lockfile_remove(const char *name);
void lockfile_option_lockdir(char ch, char *arg, int valid);
void lockfile_option_lockfiles(char ch, char *arg, int valid);
void lockfile_init( pid_t pid, const char *mod_name );
void lockfile_stop_set( void );

#endif
