
SUBDIRS = Windows

lib_LTLIBRARIES = libbotnet.la

include_HEADERS = botnet.h

libbotnet_la_LIBADD = @BN_PTHREAD_LIB@

libbotnet_la_SOURCES = \
	botnet.c  \
	botnet.h  \
	dcc_chat.c  \
	dcc_send.c  \
	includes.h  \
	output.c  \
	server.c  \
	utils.c

libbotnet_la_LDFLAGS = \
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
	-release $(LT_RELEASE)
