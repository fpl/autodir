PACKAGE = @PACKAGE@
VERSION = @VERSION@

man_MANS = autodir.8

distclean-local:
	-rm -f autodir.8 

clean-local:
	-rm -f autodir.8 

autodir.8: autodir.pod
	pod2man -s 8 -c "System Administration" -r "$(PACKAGE) $(VERSION)" autodir.pod >autodir.8
