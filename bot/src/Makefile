BOTNETCFG	=../bin/botnet-config
MYSQLCFG	= mysql_config

CC	= gcc

CFLAGS	= -O6 -g -Wall -Werror -I../include
# -DDEBUG
#-ansi -pedantic 
CFLAGS	+= $(shell ${MYSQLCFG} --cflags)
CFLAGS  += $(shell ${BOTNETCFG} --cflags)

LDFLAGS = -g
LIBS	= $(shell ${MYSQLCFG} --libs_r)
LIBS	+= $(shell ${BOTNETCFG} --libs-static)

SRCS	= main.c bot.c mysql.c linked_list.c
OBJS	= ${SRCS:.c=.o}
EXE	= beirdobot

all:	${EXE}

%.o:	%.c
	${CC} ${CFLAGS} -c -o $@ $<

${EXE}:	${OBJS}
	${CC} ${LDFLAGS} -o $@ $+ ${LIBS}

clean:
	${RM} ${OBJS} ${EXE}

main.o:	main.c ../include/release.h
