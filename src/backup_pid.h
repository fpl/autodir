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
