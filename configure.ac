# Process this file with autoconf to produce a configure script.
AC_PREREQ(2.59)

AC_INIT(beirdobot, [SVN], [gjhurlbu@gmail.com])
AC_CONFIG_AUX_DIR(scripts)
AC_CONFIG_SRCDIR(bot/src/main.c)
AM_CONFIG_HEADER(bot/include/config.h)
AC_CANONICAL_SYSTEM

AM_INIT_AUTOMAKE(beirdobot, "SVN")



AC_ARG_VAR(CUT, [location of the cut command])
AC_PATH_PROG(CUT, cut)

AC_MSG_CHECKING(version of the package)

VERSION=`cat bot/include/release.h | grep "SVN_Version =" | $CUT -d '"' -f 2`
AC_MSG_RESULT(${VERSION})

AC_SUBST(VERSION)

AC_GNU_SOURCE

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB([pcre], [pcre_config],,[AC_MSG_ERROR(libpcre not installed)])
AC_CHECK_LIB([curl], [Curl_connect])
AC_CHECK_LIB([mysqlclient], [mysql_connect])

# Check for mysql having --libs_r in mysql_config
if mysql_config --libs_r > /dev/null ; then
	ac_cv_mysql_libs="--libs_r"
else
	ac_cv_mysql_libs="--libs"
fi
AC_SUBST(MYSQL_LIBS, ${ac_cv_mysql_libs})

dnl HAVOK_SWITCH(arg-name, arg_name, ARG_NAME, help_LHS, help_RHS, cache_string,
dnl              header_comment, default, not_default)
AC_DEFUN([ENABLE_SWITCH],
[AC_ARG_ENABLE($1, AS_HELP_STRING([$4], [$5]))
if test "$enable_$2" = "$9"; then
        ac_cv_option_$2=$9
else    
        ac_cv_option_$2=$8
fi

AC_CACHE_CHECK([$6], ac_cv_option_$2)
if test "$ac_cv_option_$2" = "yes" ; then
        AC_DEFINE($3, 1, [$7])
fi

AC_SUBST($3, ${ac_cv_option_$2})
] )

ENABLE_SWITCH([plugin-dns],[plugin_dns],[PLUGIN_DNS],[--disable-plugin-dns],
              [Disable the dns plugin],
              [to enable the compilation of the dns plugin],
              [To enable the compilation of the dns plugin],
              [yes],[no])

ENABLE_SWITCH([plugin-fart],[plugin_fart],[PLUGIN_FART],[--disable-plugin-fart],
              [Disable the fart plugin],
              [to enable the compilation of the fart plugin],
              [To enable the compilation of the fart plugin],
              [yes],[no])

ENABLE_SWITCH([plugin-luascript],[plugin_luascript],[PLUGIN_LUASCRIPT],
	      [--disable-plugin-luascript],[Disable the luascript plugin],
              [to enable the compilation of the luascript plugin],
              [To enable the compilation of the luascript plugin],
              [yes],[no])

ENABLE_SWITCH([plugin-mailbox],[plugin_mailbox],[PLUGIN_MAILBOX],
              [--disable-plugin-mailbox], [Disable the mailbox plugin],
              [to enable the compilation of the mailbox plugin],
              [To enable the compilation of the mailbox plugin],
              [yes],[no])

ENABLE_SWITCH([plugin-rssfeed],[plugin_rssfeed],[PLUGIN_RSSFEED],
              [--disable-plugin-rssfeed],[Disable the rssfeed plugin],
              [to enable the compilation of the rssfeed plugin],
              [To enable the compilation of the rssfeed plugin],
              [yes],[no])

ENABLE_SWITCH([plugin-trac],[plugin_trac],[PLUGIN_TRAC],[--disable-plugin-trac],
              [Disable the trac plugin],
              [to enable the compilation of the trac plugin],
              [To enable the compilation of the trac plugin],
              [yes],[no])

ENABLE_SWITCH([plugin-trout],[plugin_trout],[PLUGIN_TROUT],
              [--disable-plugin-trout],[Disable the trout plugin],
              [to enable the compilation of the trout plugin],
              [To enable the compilation of the trout plugin],
              [yes],[no])

ENABLE_SWITCH([plugin-url],[plugin_url],[PLUGIN_URL],[--disable-plugin-url],
              [Disable the url plugin],
              [to enable the compilation of the url plugin],
              [To enable the compilation of the url plugin],
              [yes],[no])


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
