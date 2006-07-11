.PHONY:	FORCE

all:	bot web

bot:	FORCE
	${MAKE} -C bot all

web:	FORCE
	${MAKE} -C web all

clean:	bot-clean web-clean

bot-clean:
	${MAKE} -C bot clean

web-clean:
	${MAKE} -C web clean

release:
	scripts/mkrelease

