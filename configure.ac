#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
#

#
# This is not supposed to be 'widely' portable across all unices 
# as this package depends on specific features provided
# by autofs linux kernel module.  -- Author
#

AC_PREREQ([2.71])

# Move auxiliary files in their own dir to avoid polluting the 
# base dir with autotools-specific files.

AC_CONFIG_AUX_DIR(autotools)
AC_CONFIG_MACRO_DIR(autotools)

AC_INIT([Autodir],[1.0.0])
AC_CONFIG_SRCDIR([src/autodir.c])
AM_INIT_AUTOMAKE

AC_PREFIX_DEFAULT([/usr])

# Checks for programs.

AC_PROG_CC
LT_INIT([dlopen])

if test "$GCC" = yes
then
	CFLAGS="-O2 -Wall -Wwrite-strings \
		-Wpointer-arith -Wcast-qual -Wcast-align \
		-Wstrict-prototypes -Wmissing-prototypes \
		-Wnested-externs -Winline -Wshadow"
fi

# Checks for libraries.

LIBS_SAVE=$LIBS

AC_CHECK_LIB([dl],[dlopen],,
		[AC_MSG_ERROR([[libdl not found]])])

AC_CHECK_LIB([ltdl],[lt_dlinit],,
		[AC_MSG_ERROR([[libltdl not found]])])

AC_CHECK_LIB([pthread],[pthread_create],,
		[AC_MSG_ERROR([[libpthread not found]])])

AC_CHECK_LIB([cap],[cap_set_proc],,
		[AC_MSG_ERROR([[libcap not found]])])

AC_CHECK_LIB([rt], [clock_gettime],,
                [AC_MSG_ERROR([[librt not found]])])

LIBS=$LIBS_SAVE

# Autoupdate added the next two lines to ensure that your configure
# script's behavior did not change.  They are probably safe to remove.
AC_CHECK_INCLUDES_DEFAULT
AC_PROG_EGREP

# Checks for header files.
AC_CHECK_HEADERS([ errno.h \
		sys/poll.h \
		sys/stat.h \
		sys/types.h \
		dlfcn.h \
		signal.h \
		dirent.h \
		pthread.h \
		fcntl.h \
		limits.h \
		malloc.h \
		stdlib.h \
		string.h \
		sys/ioctl.h \
		sys/mount.h \
		sys/time.h \
		syslog.h \
		unistd.h \
		grp.h \
		stdarg.h \
		pwd.h ],
	[],[AC_MSG_ERROR([[required header not found]])])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

AC_CHECK_FUNCS([vsyslog \
		getsubopt \
		getopt \
		sigwait \
		poll \
		gethostname \
		localtime_r \
		memchr \
		memmove \
		stpcpy],
		[], [AC_MSG_ERROR([[required function not found]])])

AC_CONFIG_FILES([Makefile src/Makefile src/modules/Makefile])
AC_CONFIG_HEADERS([src/config.h])
AC_OUTPUT
