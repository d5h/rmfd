# Sort in traditional ASCII order, regardless of the current locale;
# otherwise we may get into trouble with distinct strings that the
# current locale considers to be equal.
ASSORT = LC_ALL=C sort

EXTRA_DIST =            \
  Coreutils.pm          \
  CuTmpdir.pm           \
  check.mk              \
  envvar-check          \
  init.cfg              \
  init.sh               \
  lang-default          \
  other-fs-tmpdir       \
  require-perl          \
  sample-test           \
  test-lib.sh

root_tests =            \
  rm/fail-2eperm        \
  rm/no-give-up         \
  rm/one-file-system    \
  rm/read-only

.PHONY: check-root
check-root:
	$(MAKE) check TESTS='$(root_tests)'

check-recursive: root-hint

# Advertise `check-root' target.
.PHONY: root-hint
root-hint:
	@echo '***********************************************************'
	@echo "NOTICE: Some tests may be run only as root."
	@echo "  See the 'Running tests as root' section in README."
	@echo '***********************************************************'

EXTRA_DIST += $(TESTS)

# Do not choose a name that is a shell keyword like 'if', or a
# commonly-used utility like 'cat' or 'test', as the name of a test.
# Otherwise, VPATH builds will fail on hosts like Solaris, since they
# will expand 'if test ...' to 'if .../test ...', and the '.../test'
# will execute the test script rather than the standard utility.

TESTS = \
  rm/cycle \
  rm/dangling-symlink \
  rm/deep-1 \
  rm/deep-2 \
  rm/dir-nonrecur \
  rm/dir-no-w \
  rm/dot-rel \
  rm/empty-inacc \
  rm/empty-name \
  rm/ext3-perf \
  rm/f-1 \
  rm/fail-2eperm \
  rm/fail-eacces \
  rm/fail-eperm \
  rm/hash \
  rm/i-1 \
  rm/ignorable \
  rm/inaccessible \
  rm/i-never \
  rm/i-no-r \
  rm/interactive-always \
  rm/interactive-once \
  rm/ir-1 \
  rm/isatty \
  rm/no-give-up \
  rm/one-file-system \
  rm/one-file-system2 \
  rm/r-1 \
  rm/r-2 \
  rm/r-3 \
  rm/r-4 \
  rm/readdir-bug \
  rm/read-only \
  rm/rm1 \
  rm/rm2 \
  rm/rm3 \
  rm/rm4 \
  rm/rm5 \
  rm/sunos-1 \
  rm/unread2 \
  rm/unread3 \
  rm/unreadable \
  rm/v-slash

include $(srcdir)/check.mk
