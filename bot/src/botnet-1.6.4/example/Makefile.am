SUBDIRS = Windows

noinst_PROGRAMS = bot @O_SERV@
EXTRA_PROGRAMS = serv
bot_SOURCES = bot.c
bot_LDADD = @BN_PTHREAD_LIB@ -lc ../makelib/.libs/libbotnet.a

serv_SOURCES = serv.c
serv_LDADD = @BN_PTHREAD_LIB@ -lc ../makelib/.libs/libbotnet.a
