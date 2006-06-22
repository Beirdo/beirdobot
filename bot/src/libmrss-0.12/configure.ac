AC_INIT(src/mrss_generic.c)
AM_SANITY_CHECK
AM_INIT_AUTOMAKE(libmrss,0.12)
AM_CONFIG_HEADER(config.h)
AM_MAINTAINER_MODE

AC_PROG_CC
AC_ISC_POSIX
AC_PROG_RANLIB
AC_PROG_LIBTOOL
AC_PROG_INSTALL
AM_PROG_CC_STDC

AC_HEADER_STDC
AC_HEADER_DIRENT

AC_CHECK_HEADER(curl/curl.h,, AC_MSG_ERROR(libcurl is required to compile libmrss))
AC_CHECK_LIB(curl,curl_version,, AC_MSG_ERROR(libcurl is required to compile libmrss))

PKG_CHECK_MODULES(NXML, nxml, nxml=yes, nxml=no)

if test "$nxml" = no; then
  AC_MSG_ERROR(libnXML is required to compile libmrss!)
fi

LDFLAGS="$LDFLAGS $NXML_LIBS"
CFLAGS="$CFLAGS $NXML_CFLAGS -Wall"

AC_OUTPUT([
Makefile
src/Makefile
test/Makefile
doc/Makefile
phpmrss/Makefile
debian/Makefile
mrss.pc
doxy.conf
])

