
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

#ifndef MISCFUNCS_H
#define MISCFUNCS_H

#include <sys/types.h>

typedef struct cary{
	char *array;
	int max;
	int cur;
} Cary;

#define cary_len(c)	((c)->cur)

char *string_n_copy(char *str1, const char *str2, int len);
int check_abs_path(char *path);
void cary_init(Cary *c, char *buf, int max);
int cary_add(Cary *c, char ch);
int cary_add_str(Cary *ca, const char *str);
int string_to_number(const char *str, int *num);
int create_dir(const char *dir, mode_t md);
int octal_string2dec( const char *str, unsigned int *oct );
int write_all( int fd, const char *buf, int buf_sz );
unsigned int string_hash( const char *str );
void string_safe( char *str, int rep );
int rename_dir( const char *from, const char *to_dir, const char *to_name,
						    const char *tme_format );

#endif
