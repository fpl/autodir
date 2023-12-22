#ifndef _BACKUP_QUEUE_H_INCLUDED_
#define _BACKUP_QUEUE_H_INCLUDED_

void backup_queue_init( int backup_wait, int maxproc );
int backup_queue_remove( const char *name );
void backup_queue_add( const char *name, const char *path );
void backup_queue_stop_set( void );
void backup_queue_stop( void );

#endif
