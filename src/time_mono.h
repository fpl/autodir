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

#ifndef TIME_MONO_H
#define TIME_MONO_H

#include <time.h>

/*INITIALIZE*/
extern void time_mono_init(void);
/*INITIALIZE*/

#if defined(_POSIX_TIMERS) && (_POSIX_TIMERS > 0)
extern clockid_t clockid;
#endif

time_t time_mono(void);
#if 0
struct timespec *mono_timespec(struct timespec *tp, time_t sec, long nsec);
#endif
struct timespec *thread_cond_timespec(struct timespec *, time_t);
void mono_nanosleep( long nsec );

#endif
