/*

Copyright (C) (2004 - 2006) (Venkata Ramana Enaganti) <ramana@intraperson.com>

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

/*
 For dropping root capabilities and to
 keep only those that are required

 all forked procs -- all backup procs will have only CAP_DAC_READ_SEARCH.
*/

#include <sys/capability.h>
#include "msg.h"
#include "dropcap.h"

#ifdef DISPLAY_CAP

static void dropcap_discap( void )
{
	char *txt;
        cap_t ct;

	if( ! ( ct = cap_get_proc() ) )
	{
		msglog( MSG_ERR|LOG_ERRNO, "dropcap_discap: cap_get_proc" );
		return;
	}
	if( ! ( txt = cap_to_text( ct, NULL ) ) )
	{
		msglog( MSG_ERR, "dropcap_discap: cap_to_text" );
		cap_free( ct );
		return;
	}
	msglog( MSG_INFO, "remaining capabilities: %s", txt );
	cap_free( txt );
	cap_free( ct );
}
#endif

void dropcap_drop( void )
{
        cap_t ct;
        const char *caps = 

		/* CAP_CHOWN

		   This overrides the restriction of changing file
		   ownership and group ownership.
		   */
		"cap_chown," \

		/* CAP_DAC_OVERRIDE

		   Override all DAC access.
		   */
		"cap_dac_override," \

		/* CAP_FOWNER

		   Overrides all restrictions about allowed operations
		   on files, where file owner ID must be equal to the user ID,
		   except where CAP_FSETID is applicable.
		   It doesn't override MAC and DAC restrictions.
		 */
		"cap_fowner," \

		/* CAP_CHOWN

		   Overrides the following restrictions that the
		   effective user ID shall match the file owner ID
		   when setting the S_ISUID and S_ISGID bits on that
		   file; that the effective group ID (or one of
		   the supplementary group IDs) shall match the file
		   owner ID when setting the S_ISGID bit on that file
		   */
		"cap_fsetid," \

		/* CAP_KILL

		   Overrides the restriction that the real or effective
		   user ID of a process sending a signal must match
		   the real or effective user ID of the process receiving the signal.
		"cap_kill," \
		   */

		/* CAP_SYS_ADMIN -- too much power but
		   we are interested only in the following,

		   Allow mount() and umount()
		   Allow some autofs root ioctls
		*/
		"cap_sys_admin+ep" \

		" " \

		/* CAP_DAC_READ_SEARCH -- for backup;

		   Overrides all DAC restrictions regarding read and
		   search on files and directories.
		   */
		"cap_dac_read_search+epi";
                                                                                
        if( ! ( ct = cap_from_text( caps ) ) )
		msglog( MSG_FATAL|LOG_ERRNO, "dropcap_drop: cap_from_text" );

	msglog( MSG_INFO, "giving up unnecessary root privileges" );
        if( cap_set_proc( ct ) )
	{
		msglog( MSG_ERR|LOG_ERRNO, "dropcap_drop: cap_set_proc" );
		msglog( MSG_WARNING, "could not drop root privileges" );
	}

	cap_free( ct );

#ifdef DISPLAY_CAP
	/*enable this if you are too paranoid*/
	dropcap_discap();
#endif
}
