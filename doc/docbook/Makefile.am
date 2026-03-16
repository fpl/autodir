dist_doc_DATA = Autodir-HOWTO.html Autodir-HOWTO.xml

CLEANFILES = Autodir-HOWTO.html

Autodir-HOWTO.html: Autodir-HOWTO.xml
	$(XSLTPROC) --nonet \
	    --param section.autolabel 1 \
    	--param section.label.includes.component.label 1 \
	-o $@ $(DOCBOOK_XSL_PATH) $<

