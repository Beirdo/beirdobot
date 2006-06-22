AC_INIT(src/nxml_parser.c)
AM_SANITY_CHECK
AM_INIT_AUTOMAKE(libnxml, 0.11)
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
AC_CHECK_HEADER(curl/curl.h,,AC_MSG_ERROR(libcurl is required to compile libnxml))
AC_CHECK_LIB(curl,curl_version,,AC_MSG_ERROR(libcurl is required to compile libnxml))

CFLAGS="$CFLAGS -Wall"

AC_OUTPUT([
Makefile
src/Makefile
test/Makefile 
doc/Makefile 
debian/Makefile
nxml.pc
doxy.conf
])
