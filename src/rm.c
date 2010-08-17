/* `rm' file deletion utility for GNU.
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

/* Initially written by Paul Rubin, David MacKenzie, and Richard Stallman.
   Reworked to use chdir and avoid recursion, and later, rewritten
   once again, to use fts, by Jim Meyering.  */

#include "config.h"

#include <stdio.h>
#include <getopt.h>
#include <sys/types.h>
#include <assert.h>

#include "system.h"
#include "argmatch.h"
#include "concat-filename.h"
#include "error.h"
#include "quote.h"
#include "quotearg.h"
#include "remove.h"
#include "root-dev-ino.h"
#include "yesno.h"
#include "priv-set.h"

/* The official name of this program (e.g., no `g' prefix).  */
#define PROGRAM_NAME "rmfd"

#define AUTHORS \
  proper_name ("Paul Rubin"), \
  proper_name ("David MacKenzie"), \
  proper_name ("Richard M. Stallman"), \
  proper_name ("Jim Meyering"), \
  proper_name ("Dan Hipschman")

/* For long options that have no equivalent short option, use a
   non-character as a pseudo short option, starting with CHAR_MAX + 1.  */
enum
{
  INTERACTIVE_OPTION = CHAR_MAX + 1,
  ONE_FILE_SYSTEM,
  NO_PRESERVE_ROOT,
  PRESERVE_ROOT,
  PRESUME_INPUT_TTY_OPTION,
  WARNINGS
};

enum interactive_type
  {
    interactive_never,		/* 0: no option or --interactive=never */
    interactive_once,		/* 1: -I or --interactive=once */
    interactive_always		/* 2: default, -i or --interactive=always */
  };

static struct option const long_opts[] =
{
  {"directory", no_argument, NULL, 'd'},
  {"force", no_argument, NULL, 'f'},
  {"interactive", optional_argument, NULL, INTERACTIVE_OPTION},

  {"one-file-system", no_argument, NULL, ONE_FILE_SYSTEM},
  {"no-preserve-root", no_argument, NULL, NO_PRESERVE_ROOT},
  {"preserve-root", no_argument, NULL, PRESERVE_ROOT},

  /* This is solely for testing.  Do not document.  */
  /* It is relatively difficult to ensure that there is a tty on stdin.
     Since rm acts differently depending on that, without this option,
     it'd be harder to test the parts of rm that depend on that setting.  */
  {"-presume-input-tty", no_argument, NULL, PRESUME_INPUT_TTY_OPTION},

  {"recursive", no_argument, NULL, 'r'},
  {"verbose", no_argument, NULL, 'v'},
  {"warnings", no_argument, NULL, 'w'},
  {GETOPT_HELP_OPTION_DECL},
  {GETOPT_VERSION_OPTION_DECL},
  {NULL, 0, NULL, 0}
};

static char const *const interactive_args[] =
{
  "never", "no", "none",
  "once",
  "always", "yes", NULL
};
static enum interactive_type const interactive_types[] =
{
  interactive_never, interactive_never, interactive_never,
  interactive_once,
  interactive_always, interactive_always
};
ARGMATCH_VERIFY (interactive_args, interactive_types);

/* Advise the user about invalid usages like "rm -foo" if the file
   "-foo" exists, assuming ARGC and ARGV are as with `main'.  */

static void
diagnose_leading_hyphen (int argc, char **argv)
{
  /* OPTIND is unreliable, so iterate through the arguments looking
     for a file name that looks like an option.  */
  int i;

  for (i = 1; i < argc; i++)
    {
      char const *arg = argv[i];
      struct stat st;

      if (arg[0] == '-' && arg[1] && lstat (arg, &st) == 0)
        {
          fprintf (stderr,
                   _("Try `%s ./%s' to remove the file %s.\n"),
                   argv[0],
                   quotearg_n_style (1, shell_quoting_style, arg),
                   quote (arg));
          break;
        }
    }
}

static size_t
warnings_table_hash (void const *p, size_t n_buckets)
{
  struct warnings_entry const *entry = p;
  return entry->ino % n_buckets;
}

static bool
warnings_table_comparator (void const *p1, void const *p2)
{
  struct warnings_entry const *e1 = p1, *e2 = p2;
  return e1->dev == e2->dev && e1->ino == e2->ino;
}

static void
add_warnings_entry (Hash_table *table, struct stat const *st, char const *path,
                    size_t path_length)
{
  struct warnings_entry *entry = xmalloc (sizeof *entry + path_length);
  entry->dev = st->st_dev;
  entry->ino = st->st_ino;
  entry->response = T_UNKNOWN;
  strcpy(entry->given_path, path);
  if (! hash_insert (table, entry))
    xalloc_die ();
}

static Hash_table *
create_warnings_table (void)
{
  char const *home_dir = getenv ("HOME");
  if (! home_dir)
    return NULL;

  char *path = xconcatenated_filename (home_dir, ".rmfd/warn.list", NULL);
  FILE *fp = fopen (path, "r");
  if (! fp)
    {
      free (path);
      return NULL;
    }

  Hash_table *table = hash_initialize (41, NULL, warnings_table_hash,
                                       warnings_table_comparator, NULL);
  if (! table)
    xalloc_die ();

  char *line = NULL;
  size_t length;
  ssize_t read;
  while (-1 != (read = getline (&line, &length, fp)))
    {
      if (line[read - 1] == '\n')
        line[--read] = '\0';
      if (line[0] != '/')
        error (1, 0, "warn.list: %s: must be an absolute path", quote (line));

      struct stat st;

      if (0 == lstat (line, &st))
        add_warnings_entry (table, &st, line, read);

      if (S_ISLNK (st.st_mode) && 0 == stat (line, &st))
        add_warnings_entry (table, &st, line, read);
    }

  free (line);
  fclose (fp);
  free (path);

  return table;
}

void
usage (int status)
{
  if (status != EXIT_SUCCESS)
    fprintf (stderr, _("Try `%s --help' for more information.\n"),
             program_name);
  else
    {
      printf (_("Usage: %s [OPTION]... FILE...\n"), program_name);
      fputs (_("\
Remove (unlink) the FILE(s).\n\
\n\
  -f, --force           ignore nonexistent files, never prompt unless\n\
                          overridden with --warnings\n\
  -i                    prompt before every removal\n\
"), stdout);
      fputs (_("\
  -I                    prompt once before removing more than three files, or\n\
                          when removing recursively.  Less intrusive than -i,\n\
                          while still giving protection against most mistakes\n\
      --interactive[=WHEN]  prompt according to WHEN: never, once (-I), or\n\
                          always (-i).  Without WHEN, prompt always\n\
"), stdout);
      fputs (_("\
      --one-file-system  when removing a hierarchy recursively, skip any\n\
                          directory that is on a file system different from\n\
                          that of the corresponding command line argument\n\
"), stdout);
      fputs (_("\
      --no-preserve-root  do not treat `/' specially\n\
      --preserve-root   do not remove `/' (default)\n\
  -r, -R, --recursive   remove directories and their contents recursively\n\
  -v, --verbose         explain what is being done\n\
  -w, --warnings        read ~/.rmfd/warn.list and issue a prompt if any\n\
                          file in that list is going to be removed.\n\
"), stdout);
      fputs (HELP_OPTION_DESCRIPTION, stdout);
      fputs (VERSION_OPTION_DESCRIPTION, stdout);
      fputs (_("\
\n\
By default, rm does not remove directories.  Use the --recursive (-r or -R)\n\
option to remove each listed directory, too, along with all of its contents.\n\
"), stdout);
      printf (_("\
\n\
To remove a file whose name starts with a `-', for example `-foo',\n\
use one of these commands:\n\
  %s -- -foo\n\
\n\
  %s ./-foo\n\
"),
              program_name, program_name);
      fputs (_("\
\n\
Note that if you use rm to remove a file, it might be possible to recover\n\
some of its contents, given sufficient expertise and/or time.  For greater\n\
assurance that the contents are truly unrecoverable, consider using shred.\n\
"), stdout);
      emit_ancillary_info ();
    }
  exit (status);
}

static void
rm_option_init (struct rm_options *x)
{
  x->ignore_missing_files = false;
  x->interactive = RMI_SOMETIMES;
  x->one_file_system = false;
  x->recursive = false;
  x->root_dev_ino = NULL;
  x->stdin_tty = isatty (STDIN_FILENO);
  x->verbose = false;
  x->warnings_table = NULL;

  /* Since this program exits immediately after calling `rm', rm need not
     expend unnecessary effort to preserve the initial working directory.  */
  x->require_restore_cwd = false;
}

int
main (int argc, char **argv)
{
  bool preserve_root = true;
  struct rm_options x;
  bool prompt_once = false;
  int c;

  initialize_main (&argc, &argv);
  set_program_name (argv[0]);
  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  atexit (close_stdin);

  rm_option_init (&x);

  /* Try to disable the ability to unlink a directory.  */
  priv_set_remove_linkdir ();

  while ((c = getopt_long (argc, argv, "dfirvIRw", long_opts, NULL)) != -1)
    {
      switch (c)
        {
        case 'd':
          /* Ignore this option, for backward compatibility with
             coreutils 5.92.  FIXME: Some time after 2005, change this
             to report an error (or perhaps behave like FreeBSD does)
             instead of ignoring the option.  */
          break;

        case 'f':
          x.interactive = RMI_NEVER;
          x.ignore_missing_files = true;
          prompt_once = false;
          break;

        case 'i':
          x.interactive = RMI_ALWAYS;
          x.ignore_missing_files = false;
          prompt_once = false;
          break;

        case 'I':
          x.interactive = RMI_NEVER;
          x.ignore_missing_files = false;
          prompt_once = true;
          break;

        case 'r':
        case 'R':
          x.recursive = true;
          break;

        case 'w':
          if (! x.warnings_table)
            x.warnings_table = create_warnings_table ();
          break;

        case INTERACTIVE_OPTION:
          {
            int i;
            if (optarg)
              i = XARGMATCH ("--interactive", optarg, interactive_args,
                             interactive_types);
            else
              i = interactive_always;
            switch (i)
              {
              case interactive_never:
                x.interactive = RMI_NEVER;
                prompt_once = false;
                break;

              case interactive_once:
                x.interactive = RMI_SOMETIMES;
                x.ignore_missing_files = false;
                prompt_once = true;
                break;

              case interactive_always:
                x.interactive = RMI_ALWAYS;
                x.ignore_missing_files = false;
                prompt_once = false;
                break;
              }
            break;
          }

        case ONE_FILE_SYSTEM:
          x.one_file_system = true;
          break;

        case NO_PRESERVE_ROOT:
          preserve_root = false;
          break;

        case PRESERVE_ROOT:
          preserve_root = true;
          break;

        case PRESUME_INPUT_TTY_OPTION:
          x.stdin_tty = true;
          break;

        case 'v':
          x.verbose = true;
          break;

        case_GETOPT_HELP_CHAR;
        case_GETOPT_VERSION_CHAR (PROGRAM_NAME, AUTHORS);
        default:
          diagnose_leading_hyphen (argc, argv);
          usage (EXIT_FAILURE);
        }
    }

  if (argc <= optind)
    {
      if (x.ignore_missing_files)
        exit (EXIT_SUCCESS);
      else
        {
          error (0, 0, _("missing operand"));
          usage (EXIT_FAILURE);
        }
    }

  if (x.recursive && preserve_root)
    {
      static struct dev_ino dev_ino_buf;
      x.root_dev_ino = get_root_dev_ino (&dev_ino_buf);
      if (x.root_dev_ino == NULL)
        error (EXIT_FAILURE, errno, _("failed to get attributes of %s"),
               quote ("/"));
    }

  size_t n_files = argc - optind;
  char **file =  argv + optind;

  if (prompt_once && (x.recursive || 3 < n_files))
    {
      fprintf (stderr,
               (x.recursive
                ? _("%s: remove all arguments recursively? ")
                : _("%s: remove all arguments? ")),
               program_name);
      if (!yesno ())
        exit (EXIT_SUCCESS);
    }

  if (x.warnings_table && ! check (file, &x))
    exit (EXIT_FAILURE);

  enum RM_status status = rm (file, &x);
  assert (VALID_STATUS (status));
  exit (status == RM_ERROR ? EXIT_FAILURE : EXIT_SUCCESS);
}
