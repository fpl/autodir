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

#ifndef MPACKET_H
#define MPACKET_H

typedef struct packet Packet;

#include <limits.h>
#include <linux/types.h>
#include <linux/auto_fs4.h>
#include "thread_cache.h"

struct packet {
	union autofs_packet_union ap;
	struct packet *next;
	thread_cache *tc;
};

void packet_init( void );
Packet *packet_allocate( void );
void packet_free( Packet *pk );

#endif
