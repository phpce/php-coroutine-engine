dnl $Id$
dnl config.m4 for extension coro_http

dnl Comments in this file start with the string 'dnl'.
dnl Remove where necessary. This file will not work
dnl without editing.

dnl If your extension references something external, use with:

PHP_ARG_WITH(coro_http, for coro_http support,
dnl Make sure that the comment is aligned:
[  --with-coro_http             Include coro_http support])

dnl Otherwise use enable:

PHP_ARG_ENABLE(coro_http, whether to enable coro_http support,
dnl Make sure that the comment is aligned:
[  --enable-coro_http           Enable coro_http support])

if test "$PHP_CORO_HTTP" != "no"; then
  dnl Write more examples of tests here...

  dnl # --with-coro_http -> check with-path
  dnl SEARCH_PATH="/usr/local /usr"     # you might want to change this
  dnl SEARCH_FOR="/include/coro_http.h"  # you most likely want to change this
  dnl if test -r $PHP_CORO_HTTP/$SEARCH_FOR; then # path given as parameter
  dnl   CORO_HTTP_DIR=$PHP_CORO_HTTP
  dnl else # search default path list
  dnl   AC_MSG_CHECKING([for coro_http files in default path])
  dnl   for i in $SEARCH_PATH ; do
  dnl     if test -r $i/$SEARCH_FOR; then
  dnl       CORO_HTTP_DIR=$i
  dnl       AC_MSG_RESULT(found in $i)
  dnl     fi
  dnl   done
  dnl fi
  dnl
  dnl if test -z "$CORO_HTTP_DIR"; then
  dnl   AC_MSG_RESULT([not found])
  dnl   AC_MSG_ERROR([Please reinstall the coro_http distribution])
  dnl fi

  dnl # --with-coro_http -> add include path
  dnl PHP_ADD_INCLUDE($CORO_HTTP_DIR/include)

  dnl # --with-coro_http -> check for lib and symbol presence
  dnl LIBNAME=coro_http # you may want to change this
  dnl LIBSYMBOL=coro_http # you most likely want to change this 

  dnl PHP_CHECK_LIBRARY($LIBNAME,$LIBSYMBOL,
  dnl [
  dnl   PHP_ADD_LIBRARY_WITH_PATH($LIBNAME, $CORO_HTTP_DIR/$PHP_LIBDIR, CORO_HTTP_SHARED_LIBADD)
  dnl   AC_DEFINE(HAVE_CORO_HTTPLIB,1,[ ])
  dnl ],[
  dnl   AC_MSG_ERROR([wrong coro_http lib version or lib not found])
  dnl ],[
  dnl   -L$CORO_HTTP_DIR/$PHP_LIBDIR -lm
  dnl ])
  dnl
  dnl PHP_SUBST(CORO_HTTP_SHARED_LIBADD)

  PHP_NEW_EXTENSION(coro_http, coro_http.c, $ext_shared,, -DZEND_ENABLE_STATIC_TSRMLS_CACHE=1)
fi
