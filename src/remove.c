/* remove.c -- core functions for removing files and directories
   Copyright (C) 1988, 1990-1991, 1994-2010 Free Software Foundation, Inc.

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

/* Extracted from rm.c and librarified, then rewritten twice by Jim Meyering.  */

#include <config.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>

#include "system.h"
#include "concat-filename.h"
#include "error.h"
#include "euidaccess-stat.h"
#include "file-type.h"
#include "quote.h"
#include "hash-pjw.h"
#include "remove.h"
#include "root-dev-ino.h"
#include "write-any-file.h"
#include "xfts.h"
#include "yesno.h"

typedef enum Ternary Ternary;

/* The prompt function may be called twice for a given directory.
   The first time, we ask whether to descend into it, and the
   second time, we ask whether to remove it.  */
enum Prompt_action
  {
    PA_DESCEND_INTO_DIR = 2,
    PA_REMOVE_DIR
  };

/* D_TYPE(D) is the type of directory entry D if known, DT_UNKNOWN
   otherwise.  */
#if ! HAVE_STRUCT_DIRENT_D_TYPE
/* Any int values will do here, so long as they're distinct.
   Undef any existing macros out of the way.  */
# undef DT_UNKNOWN
# undef DT_DIR
# undef DT_LNK
# define DT_UNKNOWN 0
# define DT_DIR 1
# define DT_LNK 2
#endif

/* Like fstatat, but cache the result.  If ST->st_size is -1, the
   status has not been gotten yet.  If less than -1, fstatat failed
   with errno == ST->st_ino.  Otherwise, the status has already
   been gotten, so return 0.  */
static int
cache_fstatat (int fd, char const *file, struct stat *st, int flag)
{
  if (st->st_size == -1 && fstatat (fd, file, st, flag) != 0)
    {
      st->st_size = -2;
      st->st_ino = errno;
    }
  if (0 <= st->st_size)
    return 0;
  errno = (int) st->st_ino;
  return -1;
}

/* Initialize a fstatat cache *ST.  Return ST for convenience.  */
static inline struct stat *
cache_stat_init (struct stat *st)
{
  st->st_size = -1;
  return st;
}

/* Return true if *ST has been statted.  */
static inline bool
cache_statted (struct stat *st)
{
  return (st->st_size != -1);
}

/* Return true if *ST has been statted successfully.  */
static inline bool
cache_stat_ok (struct stat *st)
{
  return (0 <= st->st_size);
}

/* Return 1 if FILE is an unwritable non-symlink,
   0 if it is writable or some other type of file,
   -1 and set errno if there is some problem in determining the answer.
   Use FULL_NAME only if necessary.
   Set *BUF to the file status.
   This is to avoid calling euidaccess when FILE is a symlink.  */
static int
write_protected_non_symlink (int fd_cwd,
                             char const *file,
                             char const *full_name,
                             struct stat *buf)
{
  if (can_write_any_file ())
    return 0;
  if (cache_fstatat (fd_cwd, file, buf, AT_SYMLINK_NOFOLLOW) != 0)
    return -1;
  if (S_ISLNK (buf->st_mode))
    return 0;
  /* Here, we know FILE is not a symbolic link.  */

  /* In order to be reentrant -- i.e., to avoid changing the working
     directory, and at the same time to be able to deal with alternate
     access control mechanisms (ACLs, xattr-style attributes) and
     arbitrarily deep trees -- we need a function like eaccessat, i.e.,
     like Solaris' eaccess, but fd-relative, in the spirit of openat.  */

  /* In the absence of a native eaccessat function, here are some of
     the implementation choices [#4 and #5 were suggested by Paul Eggert]:
     1) call openat with O_WRONLY|O_NOCTTY
        Disadvantage: may create the file and doesn't work for directory,
        may mistakenly report `unwritable' for EROFS or ACLs even though
        perm bits say the file is writable.

     2) fake eaccessat (save_cwd, fchdir, call euidaccess, restore_cwd)
        Disadvantage: changes working directory (not reentrant) and can't
        work if save_cwd fails.

     3) if (euidaccess (full_name, W_OK) == 0)
        Disadvantage: doesn't work if full_name is too long.
        Inefficient for very deep trees (O(Depth^2)).

     4) If the full pathname is sufficiently short (say, less than
        PATH_MAX or 8192 bytes, whichever is shorter):
        use method (3) (i.e., euidaccess (full_name, W_OK));
        Otherwise: vfork, fchdir in the child, run euidaccess in the
        child, then the child exits with a status that tells the parent
        whether euidaccess succeeded.

        This avoids the O(N**2) algorithm of method (3), and it also avoids
        the failure-due-to-too-long-file-names of method (3), but it's fast
        in the normal shallow case.  It also avoids the lack-of-reentrancy
        and the save_cwd problems.
        Disadvantage; it uses a process slot for very-long file names,
        and would be very slow for hierarchies with many such files.

     5) If the full file name is sufficiently short (say, less than
        PATH_MAX or 8192 bytes, whichever is shorter):
        use method (3) (i.e., euidaccess (full_name, W_OK));
        Otherwise: look just at the file bits.  Perhaps issue a warning
        the first time this occurs.

        This is like (4), except for the "Otherwise" case where it isn't as
        "perfect" as (4) but is considerably faster.  It conforms to current
        POSIX, and is uniformly better than what Solaris and FreeBSD do (they
        mess up with long file names). */

  {
    /* This implements #1: on decent systems, either faccessat is
       native or /proc/self/fd allows us to skip a chdir.  */
    if (!openat_needs_fchdir ())
      {
        if (faccessat (fd_cwd, file, W_OK, AT_EACCESS) == 0)
          return 0;

        return errno == EACCES ? 1 : -1;
      }

    /* This implements #5: */
    size_t file_name_len = strlen (full_name);

    if (MIN (PATH_MAX, 8192) <= file_name_len)
      return ! euidaccess_stat (buf, W_OK);
    if (euidaccess (full_name, W_OK) == 0)
      return 0;
    if (errno == EACCES)
      {
        errno = 0;
        return 1;
      }

    /* Perhaps some other process has removed the file, or perhaps this
       is a buggy NFS client.  */
    return -1;
  }
}

/* When a function like unlink, rmdir, or fstatat fails with an errno
   value of ERRNUM, return true if the specified file system object
   is guaranteed not to exist;  otherwise, return false.  */
static inline bool
nonexistent_file_errno (int errnum)
{
  /* Do not include ELOOP here, since the specified file may indeed
     exist, but be (in)accessible only via too long a symlink chain.
     Likewise for ENAMETOOLONG, since rm -f ./././.../foo may fail
     if the "..." part expands to a long enough sequence of "./"s,
     even though ./foo does indeed exist.  */

  switch (errnum)
    {
    case ENOENT:
    case ENOTDIR:
      return true;
    default:
      return false;
    }
}

/* Encapsulate the test for whether the errno value, ERRNUM, is ignorable.  */
static inline bool
ignorable_missing (struct rm_options const *x, int errnum)
{
  return x->ignore_missing_files && nonexistent_file_errno (errnum);
}

enum warn_status
{
  WARN_OK = RM_OK,
  WARN_USER_DECLINED = RM_USER_DECLINED,
  WARN_ERROR = RM_ERROR,
  WARN_NOT_FOUND = (RM_OK + RM_USER_DECLINED + RM_ERROR)
};

static struct warnings_entry *
warnings_table_lookup (Hash_table const *table, struct stat const *st_key)
{
  struct warnings_entry we;
  we.dev = st_key->st_dev;
  we.ino = st_key->st_ino;
  return hash_lookup (table, &we);
}

static void
issue_warning (char const *format, ...)
{
  static Ternary use_colors = T_UNKNOWN;

  va_list args;

  if (use_colors == T_UNKNOWN)
    use_colors = isatty (STDERR_FILENO) ? T_YES : T_NO;

  if (use_colors == T_YES)
    fprintf (stderr, _("%s: \033[01;31mWARNING:\033[0m "), program_name);
  else
    fprintf (stderr, _("%s: WARNING: "), program_name);

  va_start (args, format);
  vfprintf (stderr, format, args);
  va_end (args);
}

/* Look up the file referenced by ENT->fts_accpath.  Follow symlinks
   if the target is a directory and we are in recursive mode.  Warn
   and propmt the user if the object given by the device and inode
   numbers are found in the warnings table.  Return WARN_NOT_FOUND if
   the file was not in the warnings table and so the default prompt,
   if any, should be given.  If the user allows removal, return
   WARN_OK.  If they decline return WARN_USER_DECLINED.  If an error
   occurs return WARN_ERROR.  Any value except WARN_NOT_FOUND has the
   same value as its corresponding RM_status.

   Use FD_CWD and CACHED_LSTAT for cache_fstatat calls.  */
static enum warn_status
warn (FTSENT const *ent, int fd_cwd, struct stat *cached_lstat,
      struct rm_options const *x)
{
  if (-1 == cache_fstatat (fd_cwd, ent->fts_accpath, cached_lstat,
                           AT_SYMLINK_NOFOLLOW))
    return ignorable_missing (x, errno) ? WARN_NOT_FOUND : WARN_ERROR;

  struct warnings_entry *found =
    warnings_table_lookup (x->warnings_table, cached_lstat);
  if (found)
    {
      if (found->response == T_UNKNOWN)
        {
          issue_warning(_("you are about to remove %s; continue? "),
                        quote (found->given_path));

          found->response = yesno () ? T_YES : T_NO;
        }
      return (found->response == T_YES) ? WARN_OK : WARN_USER_DECLINED;
    }

  if (! S_ISLNK (cached_lstat->st_mode) || ! x->recursive)
    return WARN_NOT_FOUND;

  struct stat st;
  if (-1 == stat (ent->fts_accpath, &st))
    return ignorable_missing (x, errno) ? WARN_NOT_FOUND : WARN_ERROR;

  if (! S_ISDIR (st.st_mode))
    return WARN_NOT_FOUND;

  found = warnings_table_lookup (x->warnings_table, &st);
  if (! found)
    return WARN_NOT_FOUND;

  if (found->response == T_UNKNOWN)
    {
      issue_warning(_("you are about to recursively remove"
                      " the contents of %s "),
                    quote (found->given_path));
      fprintf (stderr, _("through symbolic link %s; continue? "),
               quote (ent->fts_path));

      found->response = yesno () ? T_YES : T_NO;
    }

  return (found->response == T_YES) ? WARN_OK : WARN_USER_DECLINED;
}

/* Prompt whether to remove FILENAME (ent->, if required via a combination of
   the options specified by X and/or file attributes.  If the file may
   be removed, return RM_OK.  If the user declines to remove the file,
   return RM_USER_DECLINED.  If not ignoring missing files and we
   cannot lstat FILENAME, then return RM_ERROR.

   IS_DIR is true if ENT designates a directory, false otherwise.

   Depending on MODE, ask whether to `descend into' or to `remove' the
   directory FILENAME.  MODE is ignored when FILENAME is not a directory.
   Set *IS_EMPTY_P to T_YES if FILENAME is an empty directory, and it is
   appropriate to try to remove it with rmdir (e.g. recursive mode).
   Don't even try to set *IS_EMPTY_P when MODE == PA_REMOVE_DIR.  */
static enum RM_status
prompt (FTS const *fts, FTSENT const *ent, bool is_dir,
        struct rm_options const *x, enum Prompt_action mode,
        Ternary *is_empty_p)
{
  int fd_cwd = fts->fts_cwd_fd;
  char const *full_name = ent->fts_path;
  char const *filename = ent->fts_accpath;
  if (is_empty_p)
    *is_empty_p = T_UNKNOWN;

  struct stat st;
  struct stat *sbuf = &st;
  cache_stat_init (sbuf);

  if (x->warnings_table)
    {
      enum warn_status ws = warn (ent, fd_cwd, sbuf, x);
      if (ws != WARN_NOT_FOUND)
        return (enum RM_status) ws;
    }

  int dirent_type = is_dir ? DT_DIR : DT_UNKNOWN;
  int write_protected = 0;

  /* When nonzero, this indicates that we failed to remove a child entry,
     either because the user declined an interactive prompt, or due to
     some other failure, like permissions.  */
  if (ent->fts_number)
    return RM_USER_DECLINED;

  if (x->interactive == RMI_NEVER)
    return RM_OK;

  int wp_errno = 0;
  if (!x->ignore_missing_files
      && ((x->interactive == RMI_ALWAYS) || x->stdin_tty)
      && dirent_type != DT_LNK)
    {
      write_protected = write_protected_non_symlink (fd_cwd, filename,
                                                     full_name, sbuf);
      wp_errno = errno;
    }

  if (write_protected || x->interactive == RMI_ALWAYS)
    {
      if (0 <= write_protected && dirent_type == DT_UNKNOWN)
        {
          if (cache_fstatat (fd_cwd, filename, sbuf, AT_SYMLINK_NOFOLLOW) == 0)
            {
              if (S_ISLNK (sbuf->st_mode))
                dirent_type = DT_LNK;
              else if (S_ISDIR (sbuf->st_mode))
                dirent_type = DT_DIR;
              /* Otherwise it doesn't matter, so leave it DT_UNKNOWN.  */
            }
          else
            {
              /* This happens, e.g., with `rm '''.  */
              write_protected = -1;
              wp_errno = errno;
            }
        }

      if (0 <= write_protected)
        switch (dirent_type)
          {
          case DT_LNK:
            /* Using permissions doesn't make sense for symlinks.  */
            if (x->interactive != RMI_ALWAYS)
              return RM_OK;
            break;

          case DT_DIR:
            if (!x->recursive)
              {
                write_protected = -1;
                wp_errno = EISDIR;
              }
            break;
          }

      char const *quoted_name = quote (full_name);

      if (write_protected < 0)
        {
          error (0, wp_errno, _("cannot remove %s"), quoted_name);
          return RM_ERROR;
        }

      bool is_empty;
      if (is_empty_p)
        {
          is_empty = is_empty_dir (fd_cwd, filename);
          *is_empty_p = is_empty ? T_YES : T_NO;
        }
      else
        is_empty = false;

      /* Issue the prompt.  */
      if (dirent_type == DT_DIR
          && mode == PA_DESCEND_INTO_DIR
          && !is_empty)
        fprintf (stderr,
                 (write_protected
                  ? _("%s: descend into write-protected directory %s? ")
                  : _("%s: descend into directory %s? ")),
                 program_name, quoted_name);
      else
        {
          if (cache_fstatat (fd_cwd, filename, sbuf, AT_SYMLINK_NOFOLLOW) != 0)
            {
              error (0, errno, _("cannot remove %s"), quoted_name);
              return RM_ERROR;
            }

          fprintf (stderr,
                   (write_protected
                    /* TRANSLATORS: You may find it more convenient to
                       translate "%s: remove %s (write-protected) %s? "
                       instead.  It should avoid grammatical problems
                       with the output of file_type.  */
                    ? _("%s: remove write-protected %s %s? ")
                    : _("%s: remove %s %s? ")),
                   program_name, file_type (sbuf), quoted_name);
        }

      if (!yesno ())
        return RM_USER_DECLINED;
    }
  return RM_OK;
}

/* Return true if FILENAME is a directory (and not a symlink to a directory).
   Otherwise, including the case in which lstat fails, return false.
   *ST is FILENAME's tstatus.
   Do not modify errno.  */
static inline bool
is_dir_lstat (int fd_cwd, char const *filename, struct stat *st)
{
  int saved_errno = errno;
  bool is_dir =
    (cache_fstatat (fd_cwd, filename, st, AT_SYMLINK_NOFOLLOW) == 0
     && S_ISDIR (st->st_mode));
  errno = saved_errno;
  return is_dir;
}

/* Return true if FILENAME is a non-directory.
   Otherwise, including the case in which lstat fails, return false.
   *ST is FILENAME's tstatus.
   Do not modify errno.  */
static inline bool
is_nondir_lstat (int fd_cwd, char const *filename, struct stat *st)
{
  int saved_errno = errno;
  bool is_non_dir =
    (cache_fstatat (fd_cwd, filename, st, AT_SYMLINK_NOFOLLOW) == 0
     && !S_ISDIR (st->st_mode));
  errno = saved_errno;
  return is_non_dir;
}

/* Tell fts not to traverse into the hierarchy at ENT.  */
static void
fts_skip_tree (FTS *fts, FTSENT *ent)
{
  fts_set (fts, ent, FTS_SKIP);
  /* Ensure that we do not process ENT a second time.  */
  ent = fts_read (fts);
}

/* Upon unlink failure, or when the user declines to remove ENT, mark
   each of its ancestor directories, so that we know not to prompt for
   its removal.  */
static void
mark_ancestor_dirs (FTSENT *ent)
{
  FTSENT *p;
  for (p = ent->fts_parent; FTS_ROOTLEVEL <= p->fts_level; p = p->fts_parent)
    {
      if (p->fts_number)
        break;
      p->fts_number = 1;
    }
}

/* Remove the file system object specified by ENT.  IS_DIR specifies
   whether it is expected to be a directory or non-directory.
   Return RM_OK upon success, else RM_ERROR.  */
static enum RM_status
excise (FTS *fts, FTSENT *ent, struct rm_options const *x, bool is_dir)
{
  int flag = is_dir ? AT_REMOVEDIR : 0;
  if (unlinkat (fts->fts_cwd_fd, ent->fts_accpath, flag) == 0)
    {
      if (x->verbose)
        {
          printf ((is_dir
                   ? _("removed directory: %s\n")
                   : _("removed %s\n")), quote (ent->fts_path));
        }
      return RM_OK;
    }

  /* The unlinkat from kernels like linux-2.6.32 reports EROFS even for
     nonexistent files.  When the file is indeed missing, map that to ENOENT,
     so that rm -f ignores it, as required.  Even without -f, this is useful
     because it makes rm print the more precise diagnostic.  */
  if (errno == EROFS)
    {
      struct stat st;
      if ( ! (lstatat (fts->fts_cwd_fd, ent->fts_accpath, &st)
                       && errno == ENOENT))
        errno = EROFS;
    }

  if (ignorable_missing (x, errno))
    return RM_OK;

  /* When failing to rmdir an unreadable directory, the typical
     errno value is EISDIR, but that is not as useful to the user
     as the errno value from the failed open (probably EPERM).
     Use the earlier, more descriptive errno value.  */
  if (ent->fts_info == FTS_DNR)
    errno = ent->fts_errno;
  error (0, errno, _("cannot remove %s"), quote (ent->fts_path));
  mark_ancestor_dirs (ent);
  return RM_ERROR;
}

/* This function is called once for every file system object that fts
   encounters.  fts performs a depth-first traversal.
   A directory is usually processed twice, first with fts_info == FTS_D,
   and later, after all of its entries have been processed, with FTS_DP.
   Return RM_ERROR upon error, RM_USER_DECLINED for a negative response
   to an interactive prompt, and otherwise, RM_OK.  */
static enum RM_status
rm_fts (FTS *fts, FTSENT *ent, struct rm_options const *x)
{
  switch (ent->fts_info)
    {
    case FTS_D:			/* preorder directory */
      if (! x->recursive)
        {
          /* This is the first (pre-order) encounter with a directory.
             Not recursive, so arrange to skip contents.  */
          error (0, EISDIR, _("cannot remove %s"), quote (ent->fts_path));
          mark_ancestor_dirs (ent);
          fts_skip_tree (fts, ent);
          return RM_ERROR;
        }

      /* Perform checks that can apply only for command-line arguments.  */
      if (ent->fts_level == FTS_ROOTLEVEL)
        {
          if (strip_trailing_slashes (ent->fts_path))
            ent->fts_pathlen = strlen (ent->fts_path);

          /* If the basename of a command line argument is "." or "..",
             diagnose it and do nothing more with that argument.  */
          if (dot_or_dotdot (last_component (ent->fts_accpath)))
            {
              error (0, 0, _("cannot remove directory: %s"),
                     quote (ent->fts_path));
              fts_skip_tree (fts, ent);
              return RM_ERROR;
            }

          /* If a command line argument resolves to "/" (and --preserve-root
             is in effect -- default) diagnose and skip it.  */
          if (ROOT_DEV_INO_CHECK (x->root_dev_ino, ent->fts_statp))
            {
              ROOT_DEV_INO_WARN (ent->fts_path);
              fts_skip_tree (fts, ent);
              return RM_ERROR;
            }
        }

      {
        Ternary is_empty_directory;
        enum RM_status s = prompt (fts, ent, true /*is_dir*/, x,
                                   PA_DESCEND_INTO_DIR, &is_empty_directory);

        if (s == RM_OK && is_empty_directory == T_YES)
          {
            /* When we know (from prompt when in interactive mode)
               that this is an empty directory, don't prompt twice.  */
            s = excise (fts, ent, x, true);
            fts_skip_tree (fts, ent);
          }

        if (s != RM_OK)
          {
            mark_ancestor_dirs (ent);
            fts_skip_tree (fts, ent);
          }

        return s;
      }

    case FTS_F:			/* regular file */
    case FTS_NS:		/* stat(2) failed */
    case FTS_SL:		/* symbolic link */
    case FTS_SLNONE:		/* symbolic link without target */
    case FTS_DP:		/* postorder directory */
    case FTS_DNR:		/* unreadable directory */
    case FTS_NSOK:		/* e.g., dangling symlink */
    case FTS_DEFAULT:		/* none of the above */
      {
        /* With --one-file-system, do not attempt to remove a mount point.
           fts' FTS_XDEV ensures that we don't process any entries under
           the mount point.  */
        if (ent->fts_info == FTS_DP
            && x->one_file_system
            && FTS_ROOTLEVEL < ent->fts_level
            && ent->fts_statp->st_dev != fts->fts_dev)
          {
            mark_ancestor_dirs (ent);
            error (0, 0, _("skipping %s, since it's on a different device"),
                   quote (ent->fts_path));
            return RM_ERROR;
          }

        bool is_dir = ent->fts_info == FTS_DP || ent->fts_info == FTS_DNR;
        enum RM_status s = prompt (fts, ent, is_dir, x, PA_REMOVE_DIR, NULL);
        if (s != RM_OK)
          return s;
        return excise (fts, ent, x, is_dir);
      }

    case FTS_DC:		/* directory that causes cycles */
      emit_cycle_warning (ent->fts_path);
      fts_skip_tree (fts, ent);
      return RM_ERROR;

    case FTS_ERR:
      /* Various failures, from opendir to ENOMEM, to failure to "return"
         to preceding directory, can provoke this.  */
      error (0, ent->fts_errno, _("traversal failed: %s"),
             quote (ent->fts_path));
      fts_skip_tree (fts, ent);
      return RM_ERROR;

    default:
      error (0, 0, _("unexpected failure: fts_info=%d: %s\n"
                     "please report to %s"),
             ent->fts_info,
             quote (ent->fts_path),
             PACKAGE_BUGREPORT);
      abort ();
    }
}

/* Check for ENT->fts_accpath in the warnings table and prompt the user if
   found.  Return false if the user declines to continue.  Responses are
   cached so if we see the file again while removing we won't prompt.  */
bool
check_fts (FTS *fts, FTSENT *ent, struct rm_options const *x)
{
  switch (ent->fts_info)
    {
    case FTS_D:			/* preorder directory */
      if (! x->recursive)
        {
          fts_skip_tree (fts, ent);
          return true;
        }

      /* Fall through.  */

    case FTS_F:			/* regular file */
    case FTS_NS:		/* stat(2) failed */
    case FTS_SL:		/* symbolic link */
    case FTS_SLNONE:		/* symbolic link without target */
    case FTS_DNR:		/* unreadable directory */
    case FTS_NSOK:		/* e.g., dangling symlink */
    case FTS_DEFAULT:		/* none of the above */
      {
        struct stat st;
        cache_stat_init (&st);

        enum warn_status status = warn (ent, fts->fts_cwd_fd, &st, x);
        return (status == WARN_OK) | (status == WARN_NOT_FOUND);
      }

    case FTS_DC:
    case FTS_ERR:
      fts_skip_tree (fts, ent);
      return true;

    case FTS_DP:
      return true;
    }
}

struct dir_prefix
{
  /* The directory name, not NULL-terminated.  */
  char *dirname;
  /* The length of the directory name.  */
  size_t len;
  /* A table of file names found in with this directory prefix.  */
  Hash_table *filenames;
};

static bool
streq (void const *p1, void const *p2)
{
  return 0 == strcmp (p1, p2);
}

/* Checks the list FILEs given on the command line for cases such as
   "cd important_dir; rm *".  We don't assume the "*" is the only
   thing on the command line, or that it's specifically "*" and not
   "foo/*", but we do only check for "[prefix/dirs/]*" and not things
   like "foo/* /bar" (without the space), "f*oo/bar", names that would
   have multiple globs in them, or dot files.  This seems to cover the
   majority of mistakes.

   To do this we group arguments that could come from the same glob
   and for each of those groups check to see if the containing
   directory is in X->warnings_table, and every file of the directory
   is in the arguments list.  Prompt the user if the above conditions
   are met, and return true only if the user permits us to continue,
   or we didn't prompt.  */
bool
check_globs (char *const *file, struct rm_options const *x)
{
  /* There tends to be very few distinct directory prefixes on the
     command line, at least when used by a human.  Hence we use a
     resizable array rather than a hash table for performance.  */
  size_t n_prefixes = 0, prefixes_alloced = 16;
  struct dir_prefix *prefixes = xmalloc (prefixes_alloced * sizeof *prefixes);
  struct dir_prefix *pfix;
  size_t i;
  bool check_ok = true;

  /* Store each argument basename in a table under its directory
     prefix.  I.e., for argument "foo/bar/baz", store "baz" in the
     table under prefix "foo/bar".  */
  for ( ; *file; ++file)
    {
      size_t len = dir_len (*file);
      for (i = 0; i < n_prefixes; ++i)
        {
          pfix = &prefixes[i];
          if (len == pfix->len && 0 == strncmp (*file, pfix->dirname, len))
            break;
        }
      if (i == n_prefixes)
        {
          if (n_prefixes == prefixes_alloced)
            {
              prefixes_alloced *= 2;
              prefixes = xrealloc (prefixes,
                                   prefixes_alloced * sizeof *prefixes);
            }
          pfix = &prefixes[n_prefixes++];
          pfix->dirname = *file;
          pfix->len = len;
          pfix->filenames = hash_initialize (17, NULL, hash_pjw, streq, NULL);
          if (! pfix->filenames)
            xalloc_die ();
        }
      char const *filename = *file + len;
      while (ISSLASH (*filename))
        ++filename;
      if (! hash_insert (pfix->filenames, filename))
        xalloc_die ();
    }

  /* For each directory prefix in the argument list, check if it's in
     the warnings table, and if so, read the directory and see if
     every non-dot file in the directory is listed in the arguments.
     This will catch a glob in that directory, sans race conditions,
     which is better than nothing.  */
  for (i = 0; check_ok && i < n_prefixes; ++i)
    {
      struct stat st;
      char const *dirname;
      pfix = &prefixes[i];
      if (pfix->len == 0)
        dirname = ".";
      else
        {
          assert (ISSLASH (pfix->dirname[pfix->len]));
          pfix->dirname[pfix->len] = '\0';
          dirname = pfix->dirname;
        }

      if (-1 == stat (dirname, &st))
        {
          if (! ignorable_missing (x, errno))
            error (EXIT_FAILURE, errno, _("cannot stat %s"), quote (dirname));
        }
      else
        {
          /* Check if the directory is in the warnings table and if so
             check the contents.  */
          struct warnings_entry *found =
            warnings_table_lookup (x->warnings_table, &st);
          if (found)
            {
              DIR *dir = opendir (dirname);
              if (! dir)
                error (EXIT_FAILURE, errno, _("cannot open directory %s"),
                       quote (dirname));

              struct dirent *dirent;
              size_t n_files = 0;

              /* readdir sets errno on failure but not on success.  */
              errno = 0;
              while ((dirent = readdir (dir)))
                {
                  if (dirent->d_name[0] == '.')
                    continue;
                  if (! hash_lookup (pfix->filenames, dirent->d_name))
                    break;
                  n_files++;
                }
              if (dirent == NULL)
                {
                  if (errno)
                    error (EXIT_FAILURE, errno,
                           _("error reading directory %s"), quote (dirname));

                  if (n_files)
                    {
                      char *glob =
                        xconcatenated_filename (found->given_path, "*", NULL);
                      char const *s = (n_files == 1) ? "" : "s";
                      issue_warning(_("you are about to remove"
                                      " %zd file%s via %s; continue? "),
                                    n_files, s, quote (glob));
                      free (glob);
                      /* If they want to remove the glob contents,
                         don't bother them later about whether they
                         want to remove the directory.  */
                      found->response = yesno () ? T_YES : T_NO;

                      if (found->response == T_NO)
                        check_ok = false;
                    }
                } /* end if (dirent == NULL) */

              if (-1 == closedir (dir))
                error (EXIT_FAILURE, errno, _("cannot close directory %s"),
                       quote (dirname));

            } /* end if (found) */

        } /* end if (-1 != stat (dirname, &st)) */

      if (pfix->len)
        pfix->dirname[pfix->len] = DIRECTORY_SEPARATOR;

    } /* end for (check_ok && i < n_prefixes) */

  while (0 < n_prefixes)
    hash_free (prefixes[--n_prefixes].filenames);
  free (prefixes);

  return check_ok;
}

/* Check for any FILEs in the warnings table that will be removed and give the
   user a chance for early exit.  Return true if it is OK to proceed, false if
   rm should be skipped.  */
bool
check (char *const *file, struct rm_options const *x)
{
  bool status = true;

  if (*file)
    {
      int bit_flags = (FTS_CWDFD | FTS_NOSTAT | FTS_PHYSICAL);

      if (x->one_file_system)
        bit_flags |= FTS_XDEV;

      FTS *fts = xfts_open (file, bit_flags, NULL);

      while (1)
        {
          FTSENT *ent = fts_read (fts);
          if (ent == NULL)
            {
              if (errno != 0)
                {
                  error (0, errno, _("fts_read failed"));
                  status = false;
                }
              break;
            }

          if (! check_fts (fts, ent, x))
            {
              status = false;
              break;
            }
        }

      if (fts_close (fts) != 0)
        {
          error (0, errno, _("fts_close failed"));
          status = false;
        }
    }

  return status;
}

/* Remove FILEs, honoring options specified via X.
   Return RM_OK if successful.  */
enum RM_status
rm (char *const *file, struct rm_options const *x)
{
  enum RM_status rm_status = RM_OK;

  if (*file)
    {
      int bit_flags = (FTS_CWDFD
                       | FTS_NOSTAT
                       | FTS_PHYSICAL);

      if (x->one_file_system)
        bit_flags |= FTS_XDEV;

      FTS *fts = xfts_open (file, bit_flags, NULL);

      while (1)
        {
          FTSENT *ent;

          ent = fts_read (fts);
          if (ent == NULL)
            {
              if (errno != 0)
                {
                  error (0, errno, _("fts_read failed"));
                  rm_status = RM_ERROR;
                }
              break;
            }

          enum RM_status s = rm_fts (fts, ent, x);

          assert (VALID_STATUS (s));
          UPDATE_STATUS (rm_status, s);
        }

      if (fts_close (fts) != 0)
        {
          error (0, errno, _("fts_close failed"));
          rm_status = RM_ERROR;
        }
    }

  return rm_status;
}
