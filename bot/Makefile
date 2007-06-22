EMPTY_AUTOMAKE_TARGETS  = install-info installdirs check installcheck dvi pdf
EMPTY_AUTOMAKE_TARGETS += ps info html tags ctags distdir
RECURSE_AUTOMAKE_TARGETS  = install install-exec install-data
.PHONY:	FORCE ${EMPTY_AUTOMAKE_TARGETS} ${RECURSE_AUTOMAKE_TARGETS}

all:	include/release.h
	${MAKE} -C src/botnet-1.6.4 -f Makefile.gjh all
	${MAKE} -C src/libnxml-0.11 -f Makefile.gjh all
	${MAKE} -C src/libmrss-0.12 -f Makefile.gjh all
	${MAKE} -C src/opie-2.32    -f Makefile.gjh all
	${MAKE} -C src all

distclean:	clean
clean:
	${MAKE} -C src clean
	${MAKE} -C src/botnet-1.6.4 -f Makefile.gjh clean
	${MAKE} -C src/libnxml-0.11 -f Makefile.gjh clean
	${MAKE} -C src/libmrss-0.12 -f Makefile.gjh clean
	${MAKE} -C src/opie-2.32    -f Makefile.gjh clean
	${RM} include/release.h

release:
	../scripts/mkrelease

include/release.h:	FORCE
	../scripts/tagversion

${RECURSE_AUTOMAKE_TARGETS}:
	${MAKE} -C src $@

${EMPTY_AUTOMAKE_TARGETS}:
