BOTNETCFG	= ../bin/botnet-config
MYSQLCFG	= mysql_config
PCRECFG		= pcre-config
PKGCONFIGPATH	= ../lib/pkgconfig

CC	= gcc

CFLAGS	= -O6 -g -Wall -Werror -I../include -std=c99 -pedantic 
# -DDEBUG
CFLAGS	+= $(shell ${MYSQLCFG} --cflags)
CFLAGS  += $(shell ${BOTNETCFG} --cflags)
CFLAGS  += $(shell ${PCRECFG} --cflags)
CFLAGS  += $(shell PKG_CONFIG_PATH=${PKGCONFIGPATH} pkg-config --cflags nxml)
CFLAGS  += $(shell PKG_CONFIG_PATH=${PKGCONFIGPATH} pkg-config --cflags mrss)

LDFLAGS = -g -rdynamic
LIBS	= $(shell ${MYSQLCFG} --libs_r)
LIBS	+= $(shell ${BOTNETCFG} --libs-static)
LIBS	+= $(shell ${PCRECFG} --libs)
LIBS	+= $(shell PKG_CONFIG_PATH=${PKGCONFIGPATH} pkg-config --libs nxml)
LIBS	+= $(shell PKG_CONFIG_PATH=${PKGCONFIGPATH} pkg-config --libs mrss)
LIBS    += -ldl -lopie -lresolv

SRCS	= main.c plugin_api.c bot.c bot_cmds.c mysql.c linked_list.c regexp.c
SRCS    += balanced_btree.c protected_data.c mysql_upgrade.c queue.c notice.c
SRCS    += authenticate.c logging.c thread_api.c
OBJS	= ${SRCS:.c=.o}
DEPS	= ${SRCS:.c=.d}
EXE	= beirdobot

.PHONY:	plugins clean cleanall

all:	${EXE} plugins

plugins:
	${MAKE} -C plugins all

${EXE}:	${OBJS}
	${CC} ${LDFLAGS} -o $@ $+ ${LIBS}

depend:		${DEPS}
${DEPS}:	Makefile

%.d: %.c
	@echo "Making $@ (dependencies for $<)"
	@${CC} -MM ${CFLAGS} $< | sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g'  > $@

clean:
	${RM} ${OBJS} ${EXE}
	${MAKE} -C plugins clean

cleanall:	clean
	${RM} ${DEPS}
	${MAKE} -C plugins cleanall

-include ${DEPS}

