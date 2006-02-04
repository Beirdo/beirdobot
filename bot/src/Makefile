BOTNETCFG	=../bin/botnet-config

CC	= gcc

CFLAGS	= -O6 -g -Wall -Werror -DDEBUG
#-ansi -pedantic 
CFLAGS  += $(shell ${BOTNETCFG} --cflags)

LDFLAGS = -g
LIBS	= -L/usr/lib/mysql -lmysqlclient
LIBS	+= $(shell ${BOTNETCFG} --libs-static)

SRCS	= bot.c
OBJS	= ${SRCS:.c=.o}
EXE	= bot

all:	${EXE}

%.o:	%.c
	${CC} ${CFLAGS} -c -o $@ $+

${EXE}:	${OBJS}
	${CC} ${LDFLAGS} -o $@ $+ ${LIBS}

clean:
	${RM} ${OBJS} ${EXE}
