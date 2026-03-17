# Autodir daemon

Autodir is a modular daemon for creating directories transparently, based on autofs.

It is a thread-enabled tool to create and/or mounting and managing automagically
and transparently user/group home directories, on demand. It can work with any
authentication framework (e.g. system files, NIS, LDAP or SQL) and does not
require PAM, which is a required feature for session-less service such as email
servers.

Automounter version 4 (autofs4) has to be enabled when compiling the kernel.
Debian packaged kernels have it enabled as module.

For more information, consult online docs at
[Autodir-HOWTO](https://tldp.org/HOWTO/html_single/Autodir-HOWTO/index.html)
or the up-to-date version under the doc/docbook directory.

Note that in order to generate the HTML version of the DocBook documentation
it is mandatory having xlstproc installed with required styling sheets.
On Debian you need to install the following packages:

```
xsltproc 
libxml2-utils (for xmlcatalog)
docbook-xml 
docbook-xsl 
docbook-xsl-ns 
```
