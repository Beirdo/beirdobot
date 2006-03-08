BOTNETCFG	= ../../bin/botnet-config
MYSQLCFG	= mysql_config

CC	= gcc

CFLAGS	= -O6 -g -Wall -Werror -I../../include -std=c99 -pedantic
# -DDEBUG
CFLAGS	+= $(shell ${MYSQLCFG} --cflags)
CFLAGS  += $(shell ${BOTNETCFG} --cflags)

LDFLAGS = -g
LIBS	= $(shell ${MYSQLCFG} --libs_r)
LIBS	+= $(shell ${BOTNETCFG} --libs-static)

SRCS	= trout.c fart.c core.c
OBJS	= ${SRCS:%.c=plugin_%.so}
DEPS	= ${SRCS:.c=.d}

.PHONY:	clean cleanall

all:	${OBJS}

plugin_%.so:	%.c
	${CC} ${CFLAGS} -fPIC -shared -o $@ $<

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

