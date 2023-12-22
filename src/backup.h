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
