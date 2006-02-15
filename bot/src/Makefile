BOTNETCFG	=../bin/botnet-config
MYSQLCFG	= mysql_config

CC	= gcc

CFLAGS	= -O6 -g -Wall -Werror -I../include
# -DDEBUG
#-ansi -pedantic 
CFLAGS	+= $(shell ${MYSQLCFG} --cflags)
CFLAGS  += $(shell ${BOTNETCFG} --cflags)

LDFLAGS = -g -rdynamic
LIBS	= $(shell ${MYSQLCFG} --libs_r)
LIBS	+= $(shell ${BOTNETCFG} --libs-static)
LIBS    += -ldl

SRCS	= main.c plugin_api.c bot.c bot_cmds.c mysql.c linked_list.c
SRCS    += balanced_btree.c
OBJS	= ${SRCS:.c=.o}
EXE	= beirdobot

.PHONY:	plugins

all:	${EXE} plugins

plugins:
	${MAKE} -C plugins all

%.o:	%.c
	${CC} ${CFLAGS} -c -o $@ $<

${EXE}:	${OBJS}
	${CC} ${LDFLAGS} -o $@ $+ ${LIBS}

clean:
	${RM} ${OBJS} ${EXE}
	${MAKE} -C plugins clean

main.o:	main.c ../include/release.h
