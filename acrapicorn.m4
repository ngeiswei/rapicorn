dnl # Rapicorn

## Portability defines that help interoperate with classic and modern autoconfs
ifdef([AC_TR_SH],[
define([GLIB_TR_SH],[AC_TR_SH([$1])])
define([GLIB_TR_CPP],[AC_TR_CPP([$1])])
], [
define([GLIB_TR_SH],
       [patsubst(translit([[$1]], [*+], [pp]), [[^a-zA-Z0-9_]], [_])])
define([GLIB_TR_CPP],
       [patsubst(translit([[$1]],
  	                  [*abcdefghijklmnopqrstuvwxyz],
 			  [PABCDEFGHIJKLMNOPQRSTUVWXYZ]),
		 [[^A-Z0-9_]], [_])])
])

# AC_DIVERT_BEFORE_HELP(STUFF)
# ---------------------------------
# Put STUFF early enough so that they are available for $ac_help expansion.
# Handle both classic (<= v2.13) and modern autoconf
AC_DEFUN([AC_DIVERT_BEFORE_HELP],
[ifdef([m4_divert_text], [m4_divert_text([NOTICE],[$1])],
       [ifdef([AC_DIVERT], [AC_DIVERT([NOTICE],[$1])],
              [AC_DIVERT_PUSH(AC_DIVERSION_NOTICE)dnl
$1
AC_DIVERT_POP()])])])


dnl # GLIB_SIZEOF (INCLUDES, TYPE, ALIAS [, CROSS-SIZE])
AC_DEFUN([GLIB_SIZEOF],
[pushdef([glib_Sizeof], GLIB_TR_SH([glib_cv_sizeof_$3]))dnl
AC_CACHE_CHECK([size of $2], glib_Sizeof,
[AC_TRY_RUN([#include <stdio.h>
#if STDC_HEADERS
#include <stdlib.h>
#include <stddef.h>
#endif
$1
main()
{
  FILE *f=fopen("conftestval", "w");
  if (!f) exit(1);
  fprintf(f, "%d\n", sizeof($2));
  exit(0);
}],
  [glib_Sizeof=`cat conftestval`  dnl''
],
  [glib_Sizeof=0],
  ifelse([$4], [], [], [glib_Sizeof=$4]))])
AC_DEFINE_UNQUOTED(GLIB_TR_CPP(glib_sizeof_$3), [$[]glib_Sizeof], [Size of $3])
popdef([glib_Sizeof])dnl
])


dnl # MC_IF_VAR_EQ(environment-variable, value [, equals-action] [, else-action])
AC_DEFUN([MC_IF_VAR_EQ], [
	case "$[$1]" in
	"[$2]"[)]
		[$3]
		;;
	*[)]
		[$4]
		;;
	esac
])


dnl # MC_STR_CONTAINS(src-string, sub-string [, contains-action] [, else-action])
AC_DEFUN([MC_STR_CONTAINS], [
	case "[$1]" in
	*"m4_bpatsubst([$2], ["], [\\"])"*[)]
		[$3]
		;;
	*[)]
		[$4]
		;;
	esac
])

dnl # MC_EVAR_ADD(environment-variable, find-or-add-string)
AC_DEFUN([MC_EVAR_ADD], [
	MC_STR_CONTAINS($[$1], [$2], :, [$1]="$[$1] [$2]")
])
dnl # MC_EVAR_SUPPLEMENT(environment-variable, check-string, add-string)
AC_DEFUN([MC_EVAR_SUPPLEMENT], [
	MC_STR_CONTAINS($[$1], [$2], [$1]="$[$1] [$3]", :)
])


dnl # MC_CHECK_VERSION() extracts up to 6 decimal numbers out of given-version
dnl and required-version, using any non-number letters as delimiters. it then
dnl compares each of those 6 numbers in order 1..6 to each other, requirering
dnl all of the 6 given-version numbers to be greater than, or at least equal
dnl to the corresponding number of required-version.
dnl MC_CHECK_VERSION(given-version, required-version [, match-action] [, else-action])
AC_DEFUN([MC_CHECK_VERSION], [
[eval `echo "$1:0:0:0:0:0:0" | sed -e 's/^[^0-9]*//' -e 's/[^0-9]\+/:/g' \
 -e 's/\([^:]*\):\([^:]*\):\([^:]*\):\([^:]*\):\([^:]*\):\(.*\)/ac_v1=\1 ac_v2=\2 ac_v3=\3 ac_v4=\4 ac_v5=\5 ac_v6=\6/' \
`]
[eval `echo "$2:0:0:0:0:0:0" | sed -e 's/^[^0-9]*//' -e 's/[^0-9]\+/:/g' \
 -e 's/\([^:]*\):\([^:]*\):\([^:]*\):\([^:]*\):\([^:]*\):\(.*\)/ac_r1=\1 ac_r2=\2 ac_r3=\3 ac_r4=\4 ac_r5=\5 ac_r6=\6/' \
`]
ac_vm=[`expr \( $ac_v1 \> $ac_r1 \) \| \( \( $ac_v1 \= $ac_r1 \) \& \(		\
	      \( $ac_v2 \> $ac_r2 \) \| \( \( $ac_v2 \= $ac_r2 \) \& \(		\
	       \( $ac_v3 \> $ac_r3 \) \| \( \( $ac_v3 \= $ac_r3 \) \& \(	\
	        \( $ac_v4 \> $ac_r4 \) \| \( \( $ac_v4 \= $ac_r4 \) \& \(	\
	         \( $ac_v5 \> $ac_r5 \) \| \( \( $ac_v5 \= $ac_r5 \) \& \(	\
	          \( $ac_v6 \>= $ac_r6 \)					\
		 \) \)	\
		\) \)	\
	       \) \)	\
	      \) \)	\
	     \) \)	`]
case $ac_vm in
[1)]
	$3
	;;
*[)]
	$4
	;;
esac
])

dnl # MC_DEFINE_SPINLOCK_INITIALIZER(includes)
AC_DEFUN([MC_DEFINE_SPINLOCK_INITIALIZER],
[AC_REQUIRE([AC_PROG_CC])
AC_LANG_PUSH([C])
LDFLAGS_SAVE="$LDFLAGS" ; LDFLAGS="-pthread $LDFLAGS"
AC_CACHE_CHECK(for Spinlock initializer, mc_cv_spinlock_initializer,
 [AC_RUN_IFELSE([AC_LANG_PROGRAM([
  #include <stdio.h>
  #include <stddef.h>
  #include <assert.h>
  #include <pthread.h>
  #include <inttypes.h>
  $1], [[
  static char xmem[1024] = { 0, };
  pthread_spinlock_t *slock1 = (pthread_spinlock_t*) (((ptrdiff_t) xmem + (sizeof xmem)/2) & 0xffffffffffffff00);
  int i;
  for (i = 0; i < (sizeof (pthread_spinlock_t) + 3) / 4; i++)
    assert (0 == ((int*) slock1)[i]);
  int ret = pthread_spin_init (slock1, 0);
  assert (ret == 0);
  char ymem[1024] = { 0, };
  pthread_spinlock_t *slock2 = (pthread_spinlock_t*) (((ptrdiff_t) ymem + (sizeof ymem)/2) & 0xffffffffffffff00);
  ret = pthread_spin_init (slock2, 0);
  assert (ret == 0);
  assert (*slock1 == *slock2);
  FILE *f = fopen ("conftest_spinlock_initializer", "w");
  assert (f);
  fprintf (f, "{ ");
  for (i = 0; i < (sizeof (pthread_spinlock_t) + 3) / 4; i++)
    fprintf (f, "%s0x%04x", i ? ", " : "", ((int*) slock2)[i]);
  fprintf (f, " }\n");
  assert (sizeof (pthread_spinlock_t) == 4); // implementation assumption
  fclose (f);
  return 0;
]])],
     [mc_cv_spinlock_initializer=`cat conftest_spinlock_initializer`; rm -f conftest_spinlock_initializer],
     AC_MSG_FAILURE([spinlock check failed to execute]))])
LDFLAGS="$LDFLAGS_SAVE"
AC_LANG_POP([C])
])

dnl # MC_ASSERT_NONEMPTY(variable, program, srcpackage)
AC_DEFUN([MC_ASSERT_NONEMPTY], [
    case "x$[$1]"y in
    xy)
	AC_MSG_ERROR([failed to find $2 which is required for a functional build. $3])
	;;
    esac
])

dnl # MC_ASSERT_PROG(variable, program, srcpackage) - Find program
AC_DEFUN([MC_ASSERT_PROG], [
    AC_PATH_PROG([$1], [$2], missing!)
    case "_$[$1]" in
    '_missing!')
	AC_MSG_ERROR([failed to find $2 which is required for a functional build. $3])
	;;
    esac
])
dnl # MC_ASSERT_PROGS(variable, programs, srcpackage)
AC_DEFUN([MC_ASSERT_PROGS], [
    AC_PATH_PROGS([$1], [$2], missing!)
    case "_$[$1]" in
    '_missing!')
	AC_MSG_ERROR([failed to find any of ($2) which is required for a functional build. $3])
	;;
    esac
])

dnl # MC_PKG_CONFIG_REQUIRE(package, version, clfgas-var, libs-var)
dnl # Find package through $PKG_CONFIG
AC_DEFUN([MC_PKG_CONFIG_REQUIRE], [
    mc_PACKAGE="[$1]"
    mc_VERSION="[$2]"
    AC_MSG_CHECKING([for $mc_PACKAGE - version >= $mc_VERSION])
    if $PKG_CONFIG --atleast-version="$mc_VERSION" $mc_PACKAGE 2>/dev/null ; then
      mc_VERSION=`$PKG_CONFIG --modversion $mc_PACKAGE`
      AC_MSG_RESULT([yes ($mc_VERSION)])
    else
      AC_MSG_RESULT([no])
      AC_MSG_ERROR([pkg-config failed to find "$mc_PACKAGE" v"$mc_VERSION"])
    fi
    [$3]=`$PKG_CONFIG $mc_PACKAGE --cflags`
    [$4]=`$PKG_CONFIG $mc_PACKAGE --libs`
    unset mc_PACKAGE
    unset mc_VERSION
])

dnl # MC_CC_TRY_OPTION(ENVVARIABLE, OPTION)
dnl # Check whether CC accepts OPTION and add to ENVVARIABLE
AC_DEFUN([MC_CC_TRY_OPTION], [
AC_MSG_CHECKING([if ${CC-cc} supports [$2]])
echo >conftest.c;
if ${CC-cc} $CFLAGS [$2] -c conftest.c >conftest.log 2>&1 &&
   ! grep -qi 'warn.*[$2]' conftest.log ; then
    AC_MSG_RESULT(yes)
    MC_STR_CONTAINS($[$1], [$2], :, [$1]="$[$1] [$2]")
else
    AC_MSG_RESULT(no)
fi
rm -f conftest*
])dnl
dnl # MC_CXX_TRY_OPTION(ENVVARIABLE, OPTION)
dnl # Check whether CXX accepts OPTION and add to ENVVARIABLE
AC_DEFUN([MC_CXX_TRY_OPTION], [
AC_MSG_CHECKING([if ${CXX-c++} supports [$2]])
echo >conftest.c;
if ${CXX-c++} $CXXFLAGS [$2] -c conftest.c >/dev/null 2>&1 ; then
    AC_MSG_RESULT(yes)
    MC_STR_CONTAINS($[$1], [$2], :, [$1]="$[$1] [$2]")
else
    AC_MSG_RESULT(no)
fi
rm -fr conftest*
])dnl
