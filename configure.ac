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

m4_define([autodir_major], 1)
m4_define([autodir_minor], 0)
m4_define([autodir_micro], 0)
m4_define([autodir_version],[autodir_major.autodir_minor.autodir_micro])
m4_define([autodir_name], [autodir])

AC_INIT([Autodir],autodir_version,[frankie@debian.org],autodir_name,[https://github.com/fpl/autodir/])
AC_CONFIG_SRCDIR([src/autodir.c])
AM_INIT_AUTOMAKE

AC_PREFIX_DEFAULT([/usr])

PACKAGE=autodir_name
VERSION=autodir_version
AC_SUBST(PACKAGE)
AC_SUBST(VERSION)

INSTALL="$srcdir/scripts/shtool install -c"
ECHO="$srcdir/scripts/shtool echo -e"
MKDIR="$srcdir/scripts/shtool mkdir -p"
AC_SUBST(INSTALL)
AC_SUBST(ECHO)
AC_SUBST(MKDIR)

# Checks for programs.

AC_PROG_CC
LT_INIT([dlopen])

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
		getopt_long \
		sigwait \
		poll \
		gethostname \
		localtime_r \
		memchr \
		memmove \
		stpcpy],
		[], [AC_MSG_ERROR([[required function not found]])])

AC_CONFIG_FILES([Makefile src/Makefile src/modules/Makefile man/Makefile doc/Makefile doc/docbook/Makefile])
AC_CONFIG_HEADERS([src/config.h])

AC_OUTPUT
