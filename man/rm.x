'\" Copyright (C) 1998-1999, 2001, 2003-2004, 2006, 2009-2010 Free Software
'\" Foundation, Inc.
'\"
'\" This is free software.  You may redistribute copies of it under the terms
'\" of the GNU General Public License <http://www.gnu.org/licenses/gpl.html>.
'\" There is NO WARRANTY, to the extent permitted by law.
[NAME]
rmfd \- remove files or directories
[DESCRIPTION]
This manual page
documents
.BR rmfd .
.B rmfd
is a replacement for GNU
.B rm
that adds a \fI\-\-warnings\fR (short name, \fI\-w\fR) option.
Throughout the remainder of the manual we will refer to
.B rmfd
as
.B rm
, since that is usually how it is invoked.
.P
.B rm
removes each specified file.  By default, it does not remove
directories.
.P
If the \fI\-I\fR or \fI\-\-interactive\=once\fR option is given,
and there are more than three files or the \fI\-r\fR, \fI\-R\fR,
or \fI\-\-recursive\fR are given, then
.B rm
prompts the user for whether to proceed with the entire operation.  If
the response is not affirmative, the entire command is aborted.
.P
Otherwise, if a file is unwritable, standard input is a terminal, and
the \fI\-f\fR or \fI\-\-force\fR option is not given, or the
\fI\-i\fR or \fI\-\-interactive\=always\fR option is given,
.B rm
prompts the user for whether to remove the file.  If the response is
not affirmative, the file is skipped.
.P
If the \fI\-w\fR or \fI\-\-warnings\fR option is given,
.B rm
will read the contents \fI~/.rmfd/warn.list\fR, which should consist of
one absolute file path per line.  Before removing any files,
.B rm
will determine if the invocation will remove any files in the list, and
if so will warn and prompt the user for whether to continue with the
operation.  If the user declines, the entire command is aborted.  If a
path listed is a symbolic link, then
.B rm
will warn if either the link or the target of the link will be removed.
If a directory is listed and \fI\-r\fR, \fI\-R\fR, or \fI\-\-recursive\fR
are given,
.B rm
will also warn if the contents of the directory will be removed through
a symbolic link.  If a directory \fID\fR is listed,
.B rm
will attempt to deduce when either
.B ``rm-w D/*''
or
.B ``cd D; rm *''
has been performed and will issue a warning and prompt.
.SH OPTIONS
[SEE ALSO]
unlink(1), unlink(2), chattr(1), shred(1)
