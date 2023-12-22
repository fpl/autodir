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
