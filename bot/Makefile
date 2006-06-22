.PHONY:	FORCE

all:	include/release.h
	${MAKE} -C src/botnet-1.6.4 -f Makefile.gjh all
	${MAKE} -C src/libnxml-0.11 -f Makefile.gjh all
	${MAKE} -C src/libmrss-0.12 -f Makefile.gjh all
	${MAKE} -C src all

clean:
	${MAKE} -C src clean
	${MAKE} -C src/botnet-1.6.4 -f Makefile.gjh clean
	${MAKE} -C src/libnxml-0.11 -f Makefile.gjh clean
	${MAKE} -C src/libmrss-0.12 -f Makefile.gjh clean
	${RM} include/release.h

release:
	scripts/mkrelease

include/release.h:	FORCE
	scripts/tagversion

