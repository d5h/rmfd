#!/bin/sh

# If we find foo.in.h in .gitignore (added by gnulib), also add foo.h.
find . -name .gitignore -print0 |
xargs -r0 sed -i 's/^\(.*\)\.in\.h$/\1.in.h\n\1.h/'
