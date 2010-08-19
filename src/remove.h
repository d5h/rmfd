/* Remove directory entries.

   Copyright (C) 1998, 2000, 2002-2010 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef REMOVE_H
# define REMOVE_H

# include "dev-ino.h"
# include "hash.h"

enum rm_interactive
{
  /* Start with any number larger than 1, so that any legacy tests
     against values of 0 or 1 will fail.  */
  RMI_ALWAYS = 3,
  RMI_SOMETIMES,
  RMI_NEVER
};

enum Ternary
{
  T_UNKNOWN = 2,
  T_NO,
  T_YES
};

struct warnings_entry
{
  dev_t dev;
  ino_t ino;
  /* Cache the response of the user so we don't prompt for the same file
     twice.  */
  enum Ternary response;
  /* The path given by the user in warn.list, used for prompting.  */
  char given_path[1];
};

struct rm_options
{
  /* If true, ignore nonexistent files.  */
  bool ignore_missing_files;

  /* If true, query the user about whether to remove each file.  */
  enum rm_interactive interactive;

  // FIXME: remove
  /* If true, do not traverse into (or remove) any directory that is
     on a file system (i.e., that has a different device number) other
     than that of the corresponding command line argument.  Note that
     even without this option, rm will fail in the end, due to its
     probable inability to remove the mount point.  But there, the
     diagnostic comes too late -- after removing all contents.  */
  bool one_file_system;

  /* If true, recursively remove directories.  */
  bool recursive;

  /* Pointer to the device and inode numbers of `/', when --recursive
     and preserving `/'.  Otherwise NULL.  */
  struct dev_ino *root_dev_ino;

  /* If nonzero, stdin is a tty.  */
  bool stdin_tty;

  /* If true, display the name of each file removed.  */
  bool verbose;

  /* If not NULL, warn and prompt the user whenever any file in this table will
     be removed.  This overrides any interactive options.  The table contains
     warnings_entrys, so it is not the filename that is checked, it's the
     device and inode numbers.  Symbolic links should be dereferenced.  */
  Hash_table *warnings_table;

  /* If true, treat the failure by the rm function to restore the
     current working directory as a fatal error.  I.e., if this field
     is true and the rm function cannot restore cwd, it must exit with
     a nonzero status.  Some applications require that the rm function
     restore cwd (e.g., mv) and some others do not (e.g., rm,
     in many cases).  */
  bool require_restore_cwd;
};

enum RM_status
{
  /* These must be listed in order of increasing seriousness. */
  RM_OK = 2,
  RM_USER_DECLINED,
  RM_ERROR,
  RM_NONEMPTY_DIR
};

# define VALID_STATUS(S) \
  ((S) == RM_OK || (S) == RM_USER_DECLINED || (S) == RM_ERROR)

# define UPDATE_STATUS(S, New_value)				\
  do								\
    {								\
      if ((New_value) == RM_ERROR				\
          || ((New_value) == RM_USER_DECLINED && (S) == RM_OK))	\
        (S) = (New_value);					\
    }								\
  while (0)

extern bool check_globs (char *const *file, struct rm_options const *x);
extern bool check (char *const *file, struct rm_options const *x);
extern enum RM_status rm (char *const *file, struct rm_options const *x);

#endif
