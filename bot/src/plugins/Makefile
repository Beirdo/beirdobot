BOTNETCFG	= ../../bin/botnet-config
MYSQLCFG	= mysql_config

CC	= gcc

CFLAGS	= -O6 -g -Wall -Werror -I../../include
# -DDEBUG
#-ansi -pedantic 
CFLAGS	+= $(shell ${MYSQLCFG} --cflags)
CFLAGS  += $(shell ${BOTNETCFG} --cflags)

LDFLAGS = -g
LIBS	= $(shell ${MYSQLCFG} --libs_r)
LIBS	+= $(shell ${BOTNETCFG} --libs-static)

SRCS	= 
OBJS	= ${SRCS:.c=.o}
PLUGINS	= plugin_trout.so

all:	${PLUGINS}

%.o:	%.c
	${CC} ${CFLAGS} -c -o $@ $<

plugin_%.so:	%.c
	${CC} ${CFLAGS} -fPIC -shared -o $@ $<

clean:
	${RM} ${OBJS} ${PLUGINS}
