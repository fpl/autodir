# Stuff of interest for distributions

The `dist/` directory contains contributed stuff useful for GNU/Linux distributions.

That includes systemd unit files and an init SysV script taken from Debian,
which can be adopted by other distributions. The Debian package and its
derivatives already use systemd to startup autodir and its modules (and used to
run it via init script until release 11).

While it is not mandatory to use such systems to run autodir it is generally
considered a best practice.

