.PHONY:	FORCE

all:	include/release.h
	${MAKE} -C src/botnet-1.6.4 -f Makefile.gjh all
	${MAKE} -C src all

clean:
	${MAKE} -C src/botnet-1.6.4 -f Makefile.gjh clean
	${RM} include/release.h
	${MAKE} -C src clean

release:
	scripts/mkrelease

include/release.h:	FORCE
	scripts/tagversion

