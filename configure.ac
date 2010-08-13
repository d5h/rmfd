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

# Checks for header files.
# AC_HEADER_STDC
# AC_CHECK_HEADERS([float.h limits.h stdlib.h string.h])

# Checks for typedefs, structures, and compiler characteristics.
# AC_HEADER_STDBOOL
# AC_C_CONST
# AC_TYPE_SIZE_T

# Checks for library functions.
# AC_FUNC_MALLOC
# AC_FUNC_VPRINTF
# AC_CHECK_FUNCS([atexit memmove memset modf sqrt strchr strerror strrchr strtoul])

AC_CONFIG_FILES([Makefile
                 lib/Makefile
                 src/Makefile])
AC_OUTPUT
