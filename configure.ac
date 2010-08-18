#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(rmfd, 0.5, http://github.com/d5h/rmfd/issues)
AC_CONFIG_SRCDIR([src/rm.c])

AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([lib/config.h])

AM_INIT_AUTOMAKE([1.11.1 dist-xz color-tests parallel-tests])

# Checks for programs.
AC_PROG_CC
gl_EARLY
gl_INIT

AC_PROG_RANLIB
AC_PROG_LN_S

AC_CONFIG_FILES([Makefile
                 lib/Makefile
                 man/Makefile
                 src/Makefile
                 tests/Makefile])
AC_OUTPUT
