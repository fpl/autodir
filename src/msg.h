
/*

Copyright (C) (2004) (Venkata Ramana Enaganti) <ramana@intraperson.com>

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

#ifndef MSG_H
#define MSG_H

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>

#define LOG_ERRNO 128

#define MSG_BUFSZ 1024

enum {
	MSG_FATAL = 0,
	MSG_EMERG,
	MSG_ALERT,
	MSG_CRIT,
	MSG_ERR,
	MSG_WARNING,
	MSG_NOTICE,
	MSG_INFO,
	MSG_DEBUG
};

void msg_init(void);
void msg_console_on(void);
void msg_console_off(void);
void msg_syslog_on(void);
void msg_syslog_off(void);
void msg_modname_prefix( const char *modname );
void msglog( int mprio, const char *fmt, ... );
void msg_option_verbose( char ch, char *arg, int valid );

#endif
