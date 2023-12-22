
/*

Copyright (C) (2004 - 2005) (Venkata Ramana Enaganti) <ramana@intraperson.com>

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

#ifndef THREADS_H
#define THREADS_H

#include <pthread.h>

extern pthread_mutexattr_t common_mutex_attr;
extern pthread_condattr_t common_cond_attr;

#define thread_mutex_init(mutex) (pthread_mutex_init(mutex, &common_mutex_attr))
#define thread_cond_init(cond) (pthread_cond_init(cond, &common_cond_attr))

void thread_init( void );
int thread_new( void *( *th )( void * ), void *data, pthread_t *pt );
pthread_t thread_new_wait( void *( *th )( void * ), void *data, time_t retry );
int thread_new_joinable( void *( *th )( void * ), void *data, pthread_t *pt );

#endif
