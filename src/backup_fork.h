#ifndef _BACKUP_FORK_H_INCLUDED_
#define _BACKUP_FORK_H_INCLUDED_

pid_t backup_fork_new( const char *name, const char *path );
int backup_waitpid(pid_t pid, const char *name, int block);
void backup_kill( pid_t pid, const char *name );
void backup_soft_signal( pid_t pid );
void backup_fast_kill( pid_t pid, const char *name );

void backup_fork_option_pri( char ch, char *arg, int valid );

#endif
