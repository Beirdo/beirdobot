BOTNETCFG	= ../../bin/botnet-config
MYSQLCFG	= mysql_config
PKGCONFIGPATH	= ../../lib/pkgconfig

CC	= gcc

CFLAGS	= -O6 -g -Wall -Werror -I../../include -std=c99 -pedantic
# -DDEBUG
CFLAGS	+= $(shell ${MYSQLCFG} --cflags)
CFLAGS  += $(shell ${BOTNETCFG} --cflags)

CFLAGS_rssfeed  = $(shell PKG_CONFIG_PATH=${PKGCONFIGPATH} pkg-config --cflags nxml)
CFLAGS_rssfeed += $(shell PKG_CONFIG_PATH=${PKGCONFIGPATH} pkg-config --cflags mrss)

LDFLAGS = -g
LIBS	= $(shell ${MYSQLCFG} --libs_r)
LIBS	+= $(shell ${BOTNETCFG} --libs-static)

DLLIBS_dns	= -lresolv
DLLIBS_rssfeed	= $(shell PKG_CONFIG_PATH=${PKGCONFIGPATH} pkg-config --libs nxml)
DLLIBS_rssfeed += $(shell PKG_CONFIG_PATH=${PKGCONFIGPATH} pkg-config --libs mrss)

SRCS	= trout.c fart.c core.c dns.c rssfeed.c
OBJS	= ${SRCS:%.c=plugin_%.so}
DEPS	= ${SRCS:.c=.d}

.PHONY:	clean cleanall

all:	${OBJS}

plugin_%.so:	%.c
	${CC} ${CFLAGS} ${CFLAGS_${<:%.c=%}} -fPIC -shared -o $@ $< ${DLLIBS_${<:%.c=%}}

depend:		${DEPS}
${DEPS}:	Makefile

%.d: %.c
	@echo "Making $@ (dependencies for $<)"
	@${CC} -MM ${CFLAGS} $< | sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g'  > $@

clean:
	${RM} ${OBJS}

cleanall:	clean
	${RM} ${DEPS}

-include ${DEPS}

