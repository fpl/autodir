
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

/* autofs packet malloc cache management*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "msg.h"
#include "thread.h"
#include "mpacket.h"

/*cache size of used memory.*/
#define PACKET_CACHE_MAX 150 /*OPTIMIZE*/

static struct {
	Packet *list;
	int count;
	pthread_mutex_t lock;
} pcache;

/*use junk if available otherwise malloc
  returns more then one packet if available*/
static Packet *packet_get_free_list( void )
{
	Packet *list;

	if( pcache.count > 0 )
	{
		pthread_mutex_lock( &pcache.lock );
		if( pcache.count <= 0 )
		{
			pthread_mutex_unlock( &pcache.lock );
			goto dalloc;
		}
		list = pcache.list;
		pcache.list = NULL;
		pcache.count = 0;
		pthread_mutex_unlock( &pcache.lock );
		return list;
	}
dalloc:
	list = ( Packet * ) malloc( sizeof(Packet) );
	if( list )
		list->next = NULL;
	return list;
}

/*PERFORMANCE:
Not thread safe. To be called from single thread only.*/
Packet *packet_allocate( void )
{
	static Packet *list = NULL;
	Packet *pkt;

	while( ! list && ! ( list = packet_get_free_list() ) )
	{
		msglog( MSG_ALERT, "packet_allocate: " \
				"could not get free packet list" );
		sleep( 1 );
	}
	pkt = list;
	list = list->next;

	return pkt;
}

/*thread safe*/
void packet_free( Packet *pk )
{
	Packet *tmp;

	if( ! pk ) return;

	if( pcache.count < PACKET_CACHE_MAX )
	{
		pthread_mutex_lock( &pcache.lock );
		tmp = pcache.list;
		pcache.list = pk;
		pk->next = tmp;
		pcache.count++;
		pthread_mutex_unlock( &pcache.lock );
		return;
	}
	else free( pk );
}

static void packet_clean( void )
{
	Packet *pk, *tmp;

	pk = pcache.list;
	while( pk )
	{
		tmp = pk;
		pk = pk->next;
		free( tmp );
	}
	pthread_mutex_destroy( &pcache.lock );
}

void packet_init( void )
{
	pcache.list = NULL;
	pcache.count = 0;
        thread_mutex_init(&pcache.lock);

	if( atexit( packet_clean ) )
	{
		packet_clean();
		msglog( MSG_FATAL, "packet_init: " \
				"could not register cleanup method" );
	}
}
