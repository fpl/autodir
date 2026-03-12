DB2HTML_XSL = /usr/share/xml/docbook/stylesheet/docbook-xsl-ns/xhtml/docbook.xsl

dist_doc_DATA = Autodir-HOWTO.html Autodir-HOWTO.xml

CLEANFILES = Autodir-HOWTO.html

Autodir-HOWTO.html: Autodir-HOWTO.xml
	$(XSLTPROC) --nonet -o $@ $(DB2HTML_XSL) $<
