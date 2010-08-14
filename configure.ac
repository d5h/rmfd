#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(rmfd, 0.1)
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src/rm.c])

AC_CONFIG_HEADERS([lib/config.h])

# Checks for programs.
AC_PROG_CC
gl_EARLY
gl_INIT

AC_PROG_RANLIB

AC_CONFIG_FILES([Makefile
                 lib/Makefile
                 src/Makefile
                 tests/Makefile])
AC_OUTPUT
