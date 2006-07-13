# Process this file with autoconf to produce a configure script.
AC_PREREQ(2.59)

AC_INIT(beirdobot, [SVN Version 200M], [gjhurlbu@gmail.com])
AC_CONFIG_AUX_DIR(scripts)
AC_CONFIG_SRCDIR(bot/src/main.c)
AM_CONFIG_HEADER(bot/include/config.h)
AC_CANONICAL_SYSTEM

AM_INIT_AUTOMAKE(beirdobot, "SVN Version 200M")



AC_ARG_VAR(CUT, [location of the cut command])
AC_PATH_PROG(CUT, cut)

dnl check package version - formatted like: "1.11.23 July 12, 2004"
AC_MSG_CHECKING(version of the package)

VERSION=`cat bot/include/release.h | grep "SVN_Version =" | $CUT -d '"' -f 2`
AC_MSG_RESULT(${VERSION})

AC_SUBST(VERSION)

AC_GNU_SOURCE

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB([pcre], [pcre_config])
AC_CHECK_LIB([curl], [Curl_connect])
AC_CHECK_LIB([mysql], [mysql_connect])


# Checks for header files.
#AC_HEADER_DIRENT
#AC_HEADER_STDC
#AC_HEADER_SYS_WAIT
#AC_CHECK_HEADERS([fcntl.h limits.h malloc.h netdb.h netinet/in.h stdlib.h string.h strings.h sys/file.h sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
#AC_HEADER_STDBOOL
AC_C_CONST
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_STRUCT_TM

#AC_CHECK_TYPES(socklen_t)

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_FUNC_SELECT_ARGTYPES
AC_TYPE_SIGNAL
AC_FUNC_STRNLEN
#AC_FUNC_STRTOD
AC_FUNC_VPRINTF
#AC_CHECK_FUNCS([bzero gethostbyaddr gethostbyname gethostname getpagesize gettimeofday inet_ntoa isascii memset select socket strcasecmp strchr strdup strncasecmp strstr strsep])


AC_CONFIG_FILES([Makefile web/Makefile bot/src/Makefile 
                 bot/src/plugins/Makefile])
AC_OUTPUT
