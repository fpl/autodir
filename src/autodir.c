/*

Copyright (C) (2004 - 2006) (Venkata Ramana Enaganti) <ramana@intraperson.com>

This program is free software; you can redistribute it and/or 
modify it under the terms of the GNU General Public License 
as published by the Free Software Foundation; either 
version 2 of the License, or (at your option) any later 
version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

/* 		IMPORTANT

 Heavely modified from autofs package.
Some code snippets may look
 close to autofs package code.

I acknowledge autofs people for any
 code taken from autofs package.
*/

#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <ltdl.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <linux/types.h>
#include <linux/auto_fs4.h>

#include "miscfuncs.h"
#include "msg.h"
#include "workon.h"
#include "options.h"
#include "mpacket.h"
#include "thread.h"
#include "backup.h"
#include "module.h"
#include "dropcap.h"
#include "lockfile.h"
#include "multipath.h"
#include "thread_cache.h"
#include "expire.h"
#include "time_mono.h"
#include "autodir.h"

static struct {
	char *path; /* autofs mount point*/
	int mounted; /* autofs mounted?*/
	int k_pipe;
	int ioctlfd;
	int time_out;
	int proto;	/* autofs protocol*/
	dev_t dev; /* autofs mouted fs dev value*/
} autodir;

static struct {
	char name[ NAME_MAX+1 ]; /* modified argv[0] */
        const char *module_name; /*name of module being loaded*/
	pid_t pid;		/*self pid */
	pid_t pgrp;		/*self pgrp */

	int fg;		/*stay foreground?*/
	char *pid_file;
	int shutdown;
	int stop;
	pthread_t sig_th; /* independent thread for handling singals*/

	int multi_path; /*multi path feature requested?*/
	char multi_prefix; /*prefix char for multipath*/

	thread_cache expire_tc;
	thread_cache missing_tc;
} self;

/* all slashes are removed from argv[0]*/
static void autodir_setname(const char *name)
{
	int i = 0;
	int start = 0;

	self.name[ 0 ] = 0;
	for( i = 0 ; name[ i ] ; i++ )
		if( name[ i ] == '/' )
			start = i + 1;

	string_n_copy( self.name, name + start, sizeof(self.name) );
}

static void send_ready( autofs_wqt_t wait_queue_token )
{
	if( ioctl( autodir.ioctlfd, AUTOFS_IOC_READY, wait_queue_token ) < 0 )
		msglog( MSG_ERR|LOG_ERRNO, "ioctl AUTOFS_IOC_READY" );
}

static void send_fail( autofs_wqt_t wait_queue_token )
{
	if( ioctl( autodir.ioctlfd, AUTOFS_IOC_FAIL, wait_queue_token ) < 0 )
		msglog( MSG_ERR|LOG_ERRNO, "ioctl AUTOFS_IOC_FAIL" );
}

#define UMOUNT_ERROR		0
#define UMOUNT_SUCCESS		1
#define UMOUNT_NOCHANGE		2

/*unmount dir from autofs mounted dir*/
static int umount_dir( char *path )
{
	struct stat st;

	if( lstat( path, &st ) )
	{
		if( errno == ENOENT )
			return UMOUNT_SUCCESS;
		msglog( MSG_ERR|LOG_ERRNO, "umount_dir: lstat %s", path );
		return UMOUNT_ERROR;
	}
	if( ! S_ISDIR( st.st_mode ) )
	{
		msglog( MSG_ALERT, "umount_dir: not directory: %s", path );
		return UMOUNT_ERROR;
	}
	if( st.st_dev != autodir.dev )
	{
		if( umount( path ) )
		{
			msglog( MSG_NOTICE|LOG_ERRNO, "umount %s", path );

			if( errno == EBUSY )
				return UMOUNT_NOCHANGE;
		}
	}
	if( rmdir( path ) )
	{
		msglog( MSG_ERR|LOG_ERRNO, "umount_dir: rmdir %s", path );
		return UMOUNT_ERROR;
	}
	return UMOUNT_SUCCESS;
}

/*try to unmount all dirs in autofs mounted directory*/
static int umount_all( void )
{
	char path[ PATH_MAX+1 ];
	struct dirent *de;
	DIR *dp;

	if( ! ( dp = opendir( autodir.path ) ) )
	{
		msglog( MSG_ERR|LOG_ERRNO, "umount_all: opendir %s",
				autodir.path );
		return 0;
	}
  
	while( ( de = readdir( dp ) ) != NULL)
	{
		if( ! strcmp( de->d_name, ".") ||
		    ! strcmp( de->d_name, ".." ) )
			continue;

		snprintf( path, sizeof(path), "%s/%s",
				autodir.path, de->d_name );

		if( umount_dir( path ) != UMOUNT_SUCCESS )
		{
			msglog( MSG_WARNING, "could not unmount %s", path );
			continue;
		}
		else lockfile_remove( de->d_name );
	}
	closedir( dp );
	return 1;
}

static void mount_autodir( char *path, pid_t pgrp,
		pid_t pid, int minp, int maxp)
{
	int pipefd[ 2 ];
	char options[ 128 ];
	char our_name[ 128 ];
	char dot_path[ PATH_MAX+1 ];
	struct stat st;
  
	if( pipe( pipefd ) < 0 )
		msglog( MSG_FATAL|LOG_ERRNO, "mount_autodir: pipe" );
  
	snprintf( options, sizeof(options),
		"fd=%d,pgrp=%u,minproto=%d,maxproto=%d",
		pipefd[1], (unsigned)pgrp, minp, maxp );

	snprintf( our_name, sizeof(our_name),
		"%s(pid%u)", autodir_name(), (unsigned) pid);

	if( mount( our_name, path, "autofs", MS_MGC_VAL, options ) )
	{
		close( pipefd[ 0 ] );
		close( pipefd[ 1 ] );
		msglog( MSG_FATAL|LOG_ERRNO,
				"incorrect autofs module loaded? mount %s", path );
	}
  
	autodir.mounted = 1;
	close( pipefd[ 1 ] );	/* Close kernel pipe end */
	autodir.k_pipe = pipefd[ 0 ];
	if( fcntl( autodir.k_pipe, F_SETFL, O_NONBLOCK ) )
		msglog( MSG_FATAL|LOG_ERRNO, "mount_autodir: fcntl" );

	snprintf( dot_path, sizeof(dot_path), "%s/.", path );
	autodir.ioctlfd = open( dot_path, O_RDONLY ); /* Root directory for ioctl()'s*/
	if( autodir.ioctlfd < 0 )
		msglog( MSG_FATAL|LOG_ERRNO, "mount_autodir: open %s", dot_path );

	if( fstat( autodir.ioctlfd, &st ) < 0 )
		msglog( MSG_FATAL|LOG_ERRNO, "mount_autodir: fstat %s", dot_path );

	autodir.dev = st.st_dev;
}

static int poll_read( int fd, char *buf, int sz )
{
	int r, n;
	struct pollfd pf;

	pf.fd = fd;
	pf.events = POLLIN;

	while( sz )
	{
		r = poll( &pf, 1, 1000 );

		if( r == -1 && errno != EINTR )
		{
			msglog( MSG_ERR|LOG_ERRNO, "poll_read: poll" );
			return -1;
		}
		if( r == 0 && self.shutdown )
			return 0;
		if( r <= 0 ) continue; /*including errno == EINTR */

		while( ( n = read( fd, buf, sz ) ) == -1 &&
				errno == EINTR );
		if( n > 0 )
		{
			sz -= n;
			buf += n;
		}
		else if( ! n )
		{
			msglog( MSG_ALERT, "poll_read: kernel pipe closed" );
			return -1;
		}
		else if( n == -1 )
		{
			if( errno == EAGAIN )
			{
				sleep( 1 );
				continue;
			}
			msglog( MSG_ERR|LOG_ERRNO, "poll_read: read" );
			return -1;
		}
	}
	return 1;
}

#define SEND_FAIL		0
#define SEND_READY		1

/*exit code for thread handle_missing*/
static void missing_exit( char *mname, char *name, autofs_wqt_t wqt, int result )
{
	if( result == SEND_READY )
		send_ready( wqt );
	else
		send_fail( wqt );

	if( mname )
		workon_release( mname );
	if( name && mname != name )
		workon_release( name );
}

/*missing directory handling for autofs mounted directory*/
static void handle_missing( Packet *pkt )
{
	int len;
	char mname[ NAME_MAX+1 ]; /*origial requested directory.
					could be multi path also*/
	char *name = mname; /*requested autofs directory after
					removing multi prefix.*/
	char vpath[ PATH_MAX+1 ];
	char rpath[ PATH_MAX+1 ];
	struct stat st;
	autofs_wqt_t wqt;
	struct autofs_packet_missing *pmis;

	pmis = &( pkt->ap.missing );
	wqt = pmis->wait_queue_token;

	/*check the packet integrity*/
	if( pmis->len > NAME_MAX || pmis->len < 1 || pmis->name[ pmis->len ] )
	{
		send_fail( wqt );
		packet_free( pkt );
		return;
	}

	/*copy packet info to local variables
	  so that it can be freed immediately*/
	string_n_copy( mname, pmis->name, sizeof(mname) );
	len = pmis->len;
	packet_free( pkt );

	if( self.stop )
		return missing_exit( NULL, NULL, wqt, SEND_FAIL );

	/*unsafe data or black magic? replace*/
	string_safe( mname, ' ' );

	if( self.multi_path && self.multi_prefix == *mname )
		++name;

	if( ! *name )
	{
		msglog( MSG_NOTICE, "invalid directory '%s' requested", mname );
		return missing_exit( NULL, NULL, wqt, SEND_FAIL );
	}

	/*preliminary setup. get mutex on name string*/
	if( ! workon_name( mname ) )
		return missing_exit( NULL, NULL, wqt, SEND_FAIL );

	/* any backup running or 
	   entry under process? stop it*/
	backup_remove( name, name != mname );

	if( mname != name && ! workon_name( name ) )
		return missing_exit( mname, NULL, wqt, SEND_FAIL );

	/*Create virtual dir on autofs mount point.*/
	snprintf( vpath, sizeof(vpath), "%s/%s", autodir.path, mname );
	errno = 0;

	/*check if directory exist already*/
	if( lstat( vpath, &st ) && errno != ENOENT )
	{
		msglog( MSG_ERR|LOG_ERRNO, "handle_missing: lstat %s", vpath );
		return missing_exit( mname, name, wqt, SEND_FAIL );
	}

	/*directory exist already!*/
	if( ! errno )
	{
		/*check the directory is directory, or something else*/
	       	if( ! S_ISDIR( st.st_mode ) )
		{
			msglog( MSG_ALERT, "handle_missing: " \
				"unexpected file type %s", vpath );
			return missing_exit( mname, name, wqt, SEND_FAIL );
		}

		/*everything is there already for us. no need to work!*/
		if( autodir.dev != st.st_dev )
			return missing_exit( mname, name, wqt, SEND_READY );
	}
	else if( mkdir( vpath, 0700 ) ) /*does not exist. create it.*/
	{
		msglog( MSG_ERR|LOG_ERRNO, "handle_missing: mkdir %s", vpath );
		return missing_exit( mname, name, wqt, SEND_FAIL );
	}

	/*get lock file first before mounting*/
	if( ! lockfile_create( mname ) )
	{
		msglog( MSG_ERR, "handle_missing: could not get " \
				"lock file for %s", mname );
		rmdir( vpath );
		return missing_exit( mname, name, wqt, SEND_FAIL );
	}

	/*assign some work to our module. Create real dir if it does not exist*/
	if( ! mod_dowork( name, autodir.path, rpath, sizeof(rpath) ) )
	{
		msglog( MSG_ALERT, "module %s failed on %s",
					self.module_name, name );
		rmdir( vpath );
		lockfile_remove( mname );
		return missing_exit( mname, name, wqt, SEND_FAIL );
	}

	msglog( MSG_INFO, "mounting %s on %s", rpath, vpath );

	/*take note. This is BIND mount*/
	if( mount( rpath, vpath, NULL, MS_BIND, NULL ) )
	{
		msglog( MSG_ERR|LOG_ERRNO, "handle_missing: mount %s", rpath );
		rmdir( vpath );
		lockfile_remove( mname );
		return missing_exit( mname, name, wqt, SEND_FAIL );
	}

	if( self.multi_path && ! multipath_inc( name ) )
	{
		umount( vpath );
		rmdir( vpath );
		lockfile_remove( mname );
		return missing_exit( mname, name, wqt, SEND_FAIL );
	}

	return missing_exit( mname, name, wqt, SEND_READY );
}

static void handle_expire( Packet *pkt )
{
	int len, r;
	char name[ NAME_MAX+1 ], *n;
	char path[ PATH_MAX+1 ];
	struct autofs_packet_expire_multi *exppkt;
	autofs_wqt_t wqt;

	exppkt = &( pkt->ap.expire_multi );
	wqt = exppkt->wait_queue_token;

	/*check the packet integrity*/
	if( exppkt->len > NAME_MAX || exppkt->len < 1 ||
				exppkt->name[ exppkt->len ] )
	{
		send_fail( wqt );
		packet_free( pkt );
		return;
	}

	string_n_copy( name, exppkt->name, sizeof(name) );
	len = exppkt->len;
	packet_free( pkt );

	/*unsafe data or black magic? replace*/
	string_safe( name, ' ' );

	/*If we can not get lock on name, do not send error
	  so that it does not end up as ENOENT
	  as everything there is in right order.*/
	if( ! workon_name( name ) )
	{
		send_ready( wqt );
		return;
	}

	snprintf( path, sizeof(path), "%s/%s", autodir.path, name );
	msglog( MSG_INFO, "unmounting %s", path );
	r = umount_dir( path );

	if( r == UMOUNT_SUCCESS )
	{
		/*get real path from module*/
		mod_dir( path, sizeof(path), name );
		lockfile_remove( name );

		if( self.multi_path )
		{
			n = self.multi_prefix == *name ? name + 1 : name;
			if( ! multipath_dec( n ) && ! self.stop )
				backup_add( n, path );
		}
		else if ( ! self.stop )
			backup_add( name, path );

		send_ready( wqt );
	}
	else if( r == UMOUNT_NOCHANGE )
		send_ready( wqt );
	/*umount_dir left with incomplete state*/
	else send_fail( wqt );

	workon_release( name );
	return;
}

/* main loop to handle all events from autofs kernel*/
static void handle_events( int fd )
{
	Packet *pkt;
	union autofs_packet_union *autopkt;

	while( 1 )
	{
		if( ! ( pkt = packet_allocate() ) )
		{
			msglog( MSG_CRIT, "handle_events: " \
				"could not get free packet" );
			sleep( 3 );
			continue;
		}

		autopkt = &( pkt->ap );
		if( poll_read( autodir.k_pipe, (char *) autopkt,
					sizeof(*autopkt) ) != 1 )
			return;

		if( autopkt->hdr.proto_version != AUTODIR_PROTO_DEFAULT )
		{
			msglog( MSG_ALERT, "autofs protocol '%d' not supported",
					autopkt->hdr.proto_version );
			return;
		}
		if( autopkt->hdr.type == autofs_ptype_missing )
			thread_cache_new( &self.missing_tc, pkt);
		else if( autopkt->hdr.type == autofs_ptype_expire_multi )
			thread_cache_new( &self.expire_tc, pkt );
		else
		{
			msglog( MSG_ALERT, "handle_events: " \
					"unexpected autofs packet type %d",
					autopkt->hdr.type );
			return;
		}
	}
}

static void become_daemon( void )
{
	pid_t pid;
	int nullfd;

	chdir( "/" );

	pid = fork();
	if( pid > 0 )
		exit( EXIT_SUCCESS );
	else if( pid < 0 )
		msglog( MSG_FATAL|LOG_ERRNO, "become_daemon: fork" );

	/*We already forked. No need to check for errors*/
	setsid();

	/*direct all messages only to syslog in daemon mode*/
	msg_console_off();
	msg_syslog_on();

	if( ( nullfd = open( "/dev/null", O_RDWR ) ) < 0 )
		msglog( MSG_ERR|LOG_ERRNO, "become_daemon: open /dev/null" );
	else 
	{
		if( dup2( nullfd, STDIN_FILENO ) < 0 ||
			dup2( nullfd, STDOUT_FILENO ) < 0 ||
			dup2( nullfd, STDERR_FILENO ) < 0 )
			msglog( MSG_ERR|LOG_ERRNO, "become_daemon: dup2" );
		close( nullfd );
	}
}

/* write pidfile only if requested*/
static void write_pidfile( pid_t pid )
{
	FILE *pidfp;

	if( self.pid_file )
	{
		if( ( pidfp = fopen( self.pid_file, "wt" ) ) )
		{
			fprintf( pidfp, "%lu\n", (unsigned long) pid );
			fclose( pidfp );
		}
		else msglog( MSG_WARNING|LOG_ERRNO, "write_pidfile: " \
				"failed to write pid file %s",
				self.pid_file );
	}
}

/*separate thread to handle unix signals*/
static void *signal_handle( void *v )
{
        int sig;
        sigset_t set;
                                                                                
	pthread_setcanceltype( PTHREAD_CANCEL_ASYNCHRONOUS, NULL );
        sigfillset( &set );

        while( 1 )
	{
		sig = 0;
                sigwait( &set, &sig );

		if( sig != SIGUSR1
				&& sig != SIGCHLD
				&& sig != SIGALRM
				&& sig != SIGHUP
				&& sig != SIGPIPE )
		{
			msglog( MSG_NOTICE, "signal received %d", sig );
                        self.stop = 1;
                        backup_stop_set();
                        lockfile_stop_set();
                        expire_stop_set();
                        return NULL;
		}
        }
}

static void signal_block( void )
{
        sigset_t set;
                                                                                
        sigfillset( &set );
        if( sigprocmask( SIG_SETMASK, &set, NULL ) )
		msglog( MSG_FATAL|LOG_ERRNO, "signal_block: sigprocmask" );
}

static void autodir_clean( void )
{
	mod_clean();

	if( self.sig_th )
		pthread_cancel( self.sig_th );

	if( autodir.ioctlfd >= 0 )
	{
		ioctl( autodir.ioctlfd, AUTOFS_IOC_CATATONIC, 0 );
		close( autodir.ioctlfd );
	}

	if( autodir.k_pipe >= 0 )
		close( autodir.k_pipe );

	if( autodir.mounted && umount( autodir.path ) )
		msglog( MSG_ERR|LOG_ERRNO, "autodir_clean: umount %s",
				autodir.path );

	if( self.pid_file )
		unlink( self.pid_file );
}

char *autodir_name( void )
{
	return self.name;
}

int main( int argc, char *argv[] )
{
	unsigned long timeout; 

	autodir_setname( argv[0] );
	msg_init();
	msg_console_on();

	if ( geteuid() != 0 )
		msglog( MSG_FATAL, "this program must be run by root" );

	/*drop unneeded root powers*/
	dropcap_drop();
	signal_block();
	option_init( argc, argv ); 

	module_load( autodir.path );
	self.module_name = module_name();
	msg_modname_prefix( self.module_name );

	thread_init();
	packet_init();
	workon_init();
        time_mono_init();
	if( self.multi_path )
		multipath_init();

	if( self.fg ) setpgrp(); /*stay foreground */
	else become_daemon();

	/*autodir initialization*/
	self.pid = getpid();
	self.pgrp = getpgrp();
	self.shutdown = 0;
	self.stop = 0;
	self.sig_th = 0;

	/*thread starting initializations
	  should be done only after forking*/
	backup_init();

	lockfile_init( self.pid, self.module_name );

	/*OPTIMIZE spare threads*/
	/*final argument defines how many cached threads to keep*/
	thread_cache_init( &self.expire_tc, handle_expire, 100, 10 );
	thread_cache_init( &self.missing_tc, handle_missing, 1000, 30 );

	write_pidfile( self.pid );

	autodir.k_pipe = autodir.ioctlfd = -1;
	autodir.mounted = 0;

	if( atexit( autodir_clean ) )
		msglog( MSG_FATAL, "could not register " \
				"autodir cleanup method" );

	msglog( MSG_INFO, "starting %s version %s",
		autodir_name(), AUTODIR_VERSION );

	create_dir( autodir.path , 0700 );

	if( ! thread_new_joinable( signal_handle, NULL, &self.sig_th ) )
		msglog( MSG_FATAL, "could not start signal handler thread" );

	mount_autodir( autodir.path, self.pgrp, self.pid,
			AUTODIR_PROTO_MIN, AUTODIR_PROTO_MAX );

	if( ioctl( autodir.ioctlfd, AUTOFS_IOC_PROTOVER, &autodir.proto ) == -1 )
		msglog( MSG_FATAL|LOG_ERRNO, "ioctl: AUTOFS_IOC_PROTOVER" );

	timeout = autodir.time_out;
	ioctl( autodir.ioctlfd, AUTOFS_IOC_SETTIMEOUT, &timeout );

	if( autodir.proto != AUTODIR_PROTO_DEFAULT )
		msglog( MSG_FATAL, "unsupported autofs protocol version" );

	expire_start( autodir.time_out, autodir.ioctlfd, &self.shutdown );

	/*main loop*/
	handle_events( autodir.k_pipe );

	self.stop = 1;
        backup_stop_set();
        lockfile_stop_set();
        expire_stop_set();
	msglog( MSG_INFO, "shutting down" );

	expire_stop();
	backup_stop();

	/* wait for all missing handle threads to finish*/
	thread_cache_stop( &self.expire_tc );

	/* wait for all expiry threads to finish*/
	thread_cache_stop( &self.missing_tc );

	umount_all();

	/* exit calls other cleanup methods*/
        exit( 0 );
}

/*************** option handling functions *****************/

#define DFLT_TIME_OUT			300 	/*5min*/

void autodir_option_path( char ch, char *arg, int valid )
{
	if( ! valid )
		msglog( MSG_FATAL, "autofs directory option -%c missing", ch );
	if( ! check_abs_path( arg ) )
		msglog( MSG_FATAL, "invalid argument for path -%c option", ch );
	autodir.path = arg;
}

void autodir_option_pidfile( char ch, char *arg, int valid )
{
	if( ! valid )
		self.pid_file = NULL;
	else if( ! check_abs_path( arg ) )
		msglog( MSG_FATAL, "invalid argument for pid file -%c option ", ch );
	else self.pid_file = arg;
}

void autodir_option_timeout( char ch, char *arg, int valid )
{
	if( ! valid )
		autodir.time_out = DFLT_TIME_OUT;
	else if( ! string_to_number( arg, &autodir.time_out ) )
		msglog( MSG_FATAL, "invalid argument for timeout -%c option", ch );
}

void autodir_option_fg( char ch, char *arg, int valid )
{
	self.fg = valid ? 1 : 0;
}

void autodir_option_multipath( char ch, char *arg, int valid )
{
	self.multi_path = valid ? 1 : 0;
}

#define DEFLT_MULTI_PREFIX	'.'

void autodir_option_multiprefix( char ch, char *arg, int valid )
{
	if( ! valid )
		self.multi_prefix = DEFLT_MULTI_PREFIX;
	else
	{
		if( arg[1] )
			msglog( MSG_FATAL, "Invalid argument for " \
				"multi prefix -%c option", ch );
		self.multi_prefix = *arg;
	}
}

/*************** end of option handling functions *****************/
