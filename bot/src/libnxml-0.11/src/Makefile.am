lib_LTLIBRARIES = libnxml.la

libnxml_la_SOURCES = \
	nxml_parser.c \
	nxml_download.c \
	nxml_init.c \
	nxml_error.c \
	nxml_free.c \
	nxml_write.c \
	nxml_edit.c \
	nxml_utf.c \
	nxml_string.c \
	nxml_easy.c \
	nxml_tools.c \
	nxml_dtd.c \
	nxml_namespace.c \
	nxml_entity.c

libnxml_la_LDFLAGS = -version-info 0:10:0 

EXTRA_DIST = nxml.h nxml_internal.h

incdir = $(includedir)
inc_DATA = nxml.h
