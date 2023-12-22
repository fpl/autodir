SUBDIRS =		modules

sbin_PROGRAMS	= 	autodir

autodir_SOURCES	=	autodir.c \
			autodir.h \
			miscfuncs.c \
			miscfuncs.h \
			mpacket.c \
			mpacket.h \
			msg.c \
			msg.h \
			options.c \
			options.h \
			thread_cache.c \
			thread_cache.h \
			thread.c \
			thread.h \
			workon.c \
			workon.h \
			module.c \
			module.h \
			dropcap.c \
			dropcap.h \
			lockfile.c \
			lockfile.h \
			multipath.c \
			multipath.h \
			backup.c \
			backup.h \
			backup_queue.c \
			backup_queue.h \
			backup_child.c \
			backup_child.h \
			backup_fork.c \
			backup_fork.h \
			backup_argv.c \
			backup_argv.h \
			backup_pid.c \
			backup_pid.h \
                        time_mono.c \
                        time_mono.h \
			expire.c \
			expire.h

autodir_LDFLAGS =	-export-dynamic
autodir_LDADD =	-lltdl -lpthread -ldl -lcap -lrt
