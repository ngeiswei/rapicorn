// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
#ifndef __RAPICORN_MAIN_HH__
#define __RAPICORN_MAIN_HH__

#include <rcore/utilities.hh>

#if !defined __RAPICORN_CORE_HH__ && !defined __RAPICORN_BUILD__
#error Only <rapicorn-core.hh> can be included directly.
#endif

namespace Rapicorn {

// == initialization ==
bool parse_init_args (int *argcp, char **argv, const StringVector &args = StringVector());

class InitSettings {
  static const InitSettings &is;
protected:
  uint64 test_codes_;
  bool   autonomous_;
public:
  static bool   autonomous()    { return is.autonomous_; } ///< Indicates self-contained runtime, no rcfiles, boot scripts, etc.
  static uint64 test_codes()    { return is.test_codes_; } ///< Internal test flags.
};

bool    arg_parse_option        (uint         argc,
                                 char       **argv,
                                 size_t      *i,
                                 const char  *arg);
bool    arg_parse_string_option (uint         argc,
                                 char       **argv,
                                 size_t      *i,
                                 const char  *arg,
                                 const char **strp);
int     arg_parse_collapse      (int         *argcp,
                                 char       **argv);
String  rapicorn_version        ();
String  rapicorn_buildid        ();

// == locale ==

/// Class to push a specific locale_t for the scope of its lifetime.
class ScopedLocale {
  locale_t      locale_;
  /*copy*/      ScopedLocale (const ScopedLocale&) = delete;
  ScopedLocale& operator=    (const ScopedLocale&) = delete;
protected:
  explicit      ScopedLocale (locale_t scope_locale);
public:
  // explicit   ScopedLocale (const String &locale_name = ""); // not supported
  /*dtor*/     ~ScopedLocale ();
};

/// Class to push the POSIX/C locale_t (UTF-8) for the scope of its lifetime.
class ScopedPosixLocale : public ScopedLocale {
public:
  explicit        ScopedPosixLocale ();
  static locale_t posix_locale      (); ///< Retrieve the (UTF-8) POSIX/C locale_t.
};

// == process info ==
String       program_argv0      ();
void         program_argv0_init (const char *argv0);
String       program_name       ();
void         program_alias      (String customname);
String       program_alias      ();
String       program_cwd        ();
String       application_name   ();

} // Rapicorn

namespace RapicornInternal {
using namespace Rapicorn;
bool    application_setup (const String &application_name, const String &program_name);
void    inject_init_args  (const char *const internal_args);

} // RapicornInternal

#endif /* __RAPICORN_MAIN_HH__ */
