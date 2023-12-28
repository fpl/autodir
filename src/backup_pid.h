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

#ifndef _BACKUP_PID_H_INCLUDED_
#define _BACKUP_PID_H_INCLUDED_

#include <limits.h>
#include <pthread.h>

typedef struct backup_pids Backup_pids;

typedef struct backup_pid {
	/*hash table info*/
	unsigned int hash;
	char name[ NAME_MAX+1 ];
	pid_t pid;	/* backup pid*/
	time_t started;
	pthread_mutex_t lock;
	int waiting;
	pthread_cond_t wait;

	struct backup_pid *next;
	Backup_pids *pids;
	int pids_idx;
	int kill;
} Backup_pid;

struct backup_pids {
	int size;
	int count;
	Backup_pid **s;
	int free_slot;
	int in_use;
	Backup_pid *to_free;
};

void backup_pid_init( int size );
Backup_pids *backup_pids_get( void );
void backup_pids_unget( Backup_pids *pids );

Backup_pid *backup_pidmem_allocate( void );
void backup_pidmem_free( Backup_pid *bc );

#endif
