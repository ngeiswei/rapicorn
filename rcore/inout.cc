// Licensed GNU LGPL v3 or later: http://www.gnu.org/licenses/lgpl.html
#include "inout.hh"
#include "utilities.hh"
#include "strings.hh"
#include "thread.hh"
#include "main.hh"
#include <unistd.h>
#include <syslog.h>
#include <sys/types.h>
#include <fcntl.h>
#include <cstring>

namespace Rapicorn {

// == debug_handler ==
static String
dbg_prefix (const String &fileline)
{
  // reduce "foo/bar.c:77" to "bar"
  String cxxstring = fileline;
  char *string = &cxxstring[0];
  char *d = strrchr (string, '.');
  if (d)
    {
      char *s = strrchr (string, DIR_SEPARATOR);
      if (d > s) // strip ".c:77"
        {
          *d = 0;
          return s ? s + 1 : string;
        }
    }
  return fileline;
}

/** @def RAPICORN_DEBUG_OPTION(option, blurb)
 * Create a Rapicorn::DebugOption object that can be used to query, cache and document debugging options,
 * configured through #$RAPICORN_DEBUG.
 * Typical use: @code
 * static auto dbg_test_feature = RAPICORN_DEBUG_OPTION ("test-feature", "Switch use of the test-feature during debugging on or off.");
 * if (dbg_test_feature)
 *   enable_test_feature();
 * @endcode
 */

static DebugOption dbe_syslog = RAPICORN_DEBUG_OPTION ("syslog", "Enable logging of general purpose messages through syslog(3).");
static DebugOption dbe_fatal_syslog = RAPICORN_DEBUG_OPTION ("fatal-syslog", "Enable logging of fatal conditions through syslog(3).");
static DebugOption dbe_fatal_warnings = RAPICORN_DEBUG_OPTION ("fatal-warnings", "Cast all warning messages into fatal errors.");

static __attribute__ ((noinline)) void // internal function, this + caller are skipped in backtraces
debug_handler (const char dkind, const String &file_line, const String &message, const char *key = NULL)
{
  /* The logging system must work before Rapicorn is initialized, and possibly even during
   * global_ctor phase. So any initialization needed here needs to be handled on demand.
   */
  String msg = message;
  const char kind = toupper (dkind);
  enum { DO_STDERR = 1, DO_SYSLOG = 2, DO_ABORT = 4, DO_DEBUG = 8, DO_ERRNO = 16,
         DO_STAMP = 32, DO_LOGFILE = 64, DO_FIXIT = 128, DO_BACKTRACE = 256, };
  static int conftest_logfd = 0;
  const String conftest_logfile = conftest_logfd == 0 ? debug_config_get ("logfile") : "";
  const int FATAL_SYSLOG = dbe_fatal_syslog ? DO_SYSLOG : 0;
  const int MAY_SYSLOG = dbe_syslog ? DO_SYSLOG : 0;
  const int MAY_ABORT  = dbe_fatal_warnings ? DO_ABORT  : 0;
  const char *what = "DEBUG";
  int f = islower (dkind) ? DO_ERRNO : 0;                       // errno checks for lower-letter kinds
  switch (kind)
    {
    case 'F': what = "FATAL";    f |= DO_STDERR | FATAL_SYSLOG | DO_ABORT;  break;      // fatal, assertions
    case 'C': what = "CRITICAL"; f |= DO_STDERR | MAY_SYSLOG   | MAY_ABORT; break;      // critical
    case 'X': what = "FIX""ME";  f |= DO_FIXIT  | DO_STAMP;                 break;      // fixing needed
    case 'D': what = "DEBUG";    f |= DO_DEBUG  | DO_STAMP;                 break;      // debug
    }
  f |= conftest_logfd > 0 || !conftest_logfile.empty() ? DO_LOGFILE : 0;
  f |= (f & DO_ABORT) ? DO_BACKTRACE : 0;
  const String where = file_line + (file_line.empty() ? "" : ": ");
  const String wherewhat = where + what + ": ";
  String emsg = "\n"; // (f & DO_ERRNO ? ": " + string_from_errno (saved_errno) : "")
  if (kind == 'A')
    msg = "assertion failed: " + msg;
  else if (kind == 'U')
    msg = "assertion must not be reached" + String (msg.empty() ? "" : ": ") + msg;
  else if (kind == 'I')
    msg = "condition failed: " + msg;
  const uint64 start = timestamp_startup(), delta = max (timestamp_realtime(), start) - start;
  if (f & DO_STAMP)
    do_once {
      printerr ("[%s] %s[%u]: program started at: %.6f\n",
                timestamp_format (delta).c_str(),
                program_alias().c_str(), ThisThread::thread_pid(),
                start / 1000000.0);
    }
  if (f & DO_DEBUG)
    {
      String intro, prefix = key ? key : dbg_prefix (file_line);
      if (prefix.size())
        prefix = prefix + ": ";
      if (f & DO_STAMP)
        intro = string_printf ("[%s]", timestamp_format (delta).c_str());
      printerr ("%s %s%s%s", intro.c_str(), prefix.c_str(), msg.c_str(), emsg.c_str());
    }
  if (f & DO_FIXIT)
    {
      printerr ("%s: %s%s%s", what, where.c_str(), msg.c_str(), emsg.c_str());
    }
  if (f & DO_STDERR)
    {
      using namespace AnsiColors;
      const char *cy1 = color (FG_CYAN), *cy0 = color (FG_DEFAULT);
      const char *bo1 = color (BOLD), *bo0 = color (BOLD_OFF);
      const char *fgw = color (FG_WHITE), *fgc1 = fgw, *fgc0 = color (FG_DEFAULT), *fbo1 = "", *fbo0 = "";
      switch (kind)
        {
        case 'F':
          fgc1 = color (FG_RED);
          fbo1 = bo1;                   fbo0 = bo0;
          break;
        case 'C':
          fgc1 = color (FG_YELLOW);
          break;
        }
      printerr ("%s%s%s%s%s%s%s%s[%u] %s%s%s:%s%s %s%s",
                cy1, where.c_str(), cy0,
                bo1, fgw, program_alias().c_str(), fgc0, bo0, ThisThread::thread_pid(),
                fbo1, fgc1, what,
                fgc0, fbo0,
                msg.c_str(), emsg.c_str());
      if (f & DO_ABORT)
        printerr ("Aborting...\n");
    }
  if (f & DO_SYSLOG)
    {
      do_once { openlog (NULL, LOG_PID, LOG_USER); } // force pid logging
      String end = emsg;
      if (!end.empty() && end[end.size()-1] == '\n')
        end.resize (end.size()-1);
      const int level = f & DO_ABORT ? LOG_ERR : LOG_WARNING;
      const String severity = (f & DO_ABORT) ? "ABORTING: " : "";
      syslog (level, "%s%s%s", severity.c_str(), msg.c_str(), end.c_str());
    }
  String btmsg;
  if (f & DO_BACKTRACE)
    {
      size_t addr;
      const vector<String> syms = pretty_backtrace (2, &addr);
      btmsg = string_printf ("%sBacktrace at 0x%08zx (stackframe at 0x%08zx):\n", where.c_str(),
                             addr, size_t (__builtin_frame_address (0)) /*size_t (&addr)*/);
      for (size_t i = 0; i < syms.size(); i++)
        btmsg += string_printf ("  %s\n", syms[i].c_str());
    }
  if (f & DO_LOGFILE)
    {
      String out;
      do_once
        {
          int fd;
          do
            fd = open (Path::abspath (conftest_logfile).c_str(), O_WRONLY | O_CREAT | O_APPEND | O_NOCTTY, 0666);
          while (conftest_logfd < 0 && errno == EINTR);
          if (fd == 0) // invalid initialization value
            {
              fd = dup (fd);
              close (0);
            }
          out = string_printf ("[%s] %s[%u]: program started at: %s\n",
                               timestamp_format (delta).c_str(), program_alias().c_str(), ThisThread::thread_pid(),
                               timestamp_format (start).c_str());
          conftest_logfd = fd;
        }
      out += string_printf ("[%s] %s[%u]:%s%s%s",
                            timestamp_format (delta).c_str(), program_alias().c_str(), ThisThread::thread_pid(),
                            wherewhat.c_str(), msg.c_str(), emsg.c_str());
      if (f & DO_ABORT)
        out += "aborting...\n";
      int err;
      do
        err = write (conftest_logfd, out.data(), out.size());
      while (err < 0 && (errno == EINTR || errno == EAGAIN));
      if (f & DO_BACKTRACE)
        do
          err = write (conftest_logfd, btmsg.data(), btmsg.size());
        while (err < 0 && (errno == EINTR || errno == EAGAIN));
    }
  if (f & DO_BACKTRACE)
    printerr ("%s", btmsg.c_str());
  if (f & DO_ABORT)
    {
      ::abort();
    }
}

// == envkey_ functions ==
static int
cstring_option_sense (const char *option_string, const char *option, char *value, const int offset = 0)
{
  const char *haystack = option_string + offset;
  const char *p = strstr (haystack, option);
  if (p)                                // found possible match
    {
      const int l = strlen (option);
      if (p == haystack || (p > haystack && (p[-1] == ':' || p[-1] == ';')))
        {                               // start matches (word boundary)
          const char *d1 = strchr (p + l, ':'), *d2 = strchr (p + l, ';'), *d = MAX (d1, d2);
          d = d ? d : p + l + strlen (p + l);
          bool match = true;
          if (p[l] == '=')              // found value
            {
              const char *v = p + l + 1;
              strncpy (value, v, d - v);
              value[d - v] = 0;
            }
          else if (p[l] == 0 || p[l] == ':' || p[l] == ';')
            {                           // option present
              strcpy (value, "1");
            }
          else
            match = false;              // no match
          if (match)
            {
              const int pos = d - option_string;
              if (d[0])
                {
                  const int next = cstring_option_sense (option_string, option, value, pos);
                  if (next >= 0)        // found overriding match
                    return next;
                }
              return pos;               // this match is last match
            }
        }                               // unmatched, keep searching
      return cstring_option_sense (option_string, option, value, p + l - option_string);
    }
  return -1;                            // not present in haystack
}

static bool
fast_envkey_check (const char *option_string, const char *key)
{
  const int l = max (size_t (64), strlen (option_string) + 1);
  char kvalue[l];
  strcpy (kvalue, "0");
  const int keypos = !key ? -1 : cstring_option_sense (option_string, key, kvalue);
  char avalue[l];
  strcpy (avalue, "0");
  const int allpos = cstring_option_sense (option_string, "all", avalue);
  if (keypos > allpos)
    return cstring_to_bool (kvalue, false);
  else if (allpos > keypos)
    return cstring_to_bool (avalue, false);
  else
    return false;       // neither key nor "all" found
}

/** Check whether a flipper (feature toggle) is enabled.
 * This function first checks the environment variable @a env_var for @a key, if it is present or if
 * @a with_all_toggle is true and 'all' is present, the function returns @a true, otherwise @a false.
 * The @a cachep argument may point to a caching variable which is reset to 0 if @a env_var is
 * empty (i.e. no features can be enabled), so the caching variable can be used to prevent
 * unneccessary future envkey_flipper_check() calls.
 */
bool
envkey_flipper_check (const char *env_var, const char *key, bool with_all_toggle, volatile bool *cachep)
{
  if (env_var)          // require explicit activation
    {
      const char *val = getenv (env_var);
      if (!val || val[0] == 0)
        {
          if (cachep)
            *cachep = 0;
        }
      else if (key && fast_envkey_check (val, key))
        return true;
    }
  return false;
}

/** Check whether to print debugging message.
 * This function first checks the environment variable @a env_var for @a key, if the key is present,
 * 'all' is present or if @a env_var is NULL, the debugging message will be printed.
 * A NULL @a key checks for general debugging, it's equivalent to passing "debug" as @a key.
 * The @a cachep argument may point to a caching variable which is reset to 0 if @a env_var is
 * empty (so no debugging is enabled), so the caching variable can be used to prevent unneccessary
 * future debugging calls, e.g. to envkey_debug_message().
 */
bool
envkey_debug_check (const char *env_var, const char *key, volatile bool *cachep)
{
  if (!env_var)
    return true;        // unconditional debugging
  return envkey_flipper_check (env_var, key ? key : "debug", true, cachep);
}

/** Conditionally print debugging message.
 * This function first checks whether debugging is enabled via envkey_debug_check() and returns if not.
 * The arguments @a file_path and @a line are used to denote the debugging message source location,
 * @a format and @a va_args are formatting the message analogously to vprintf().
 */
void
envkey_debug_message (const char *env_var, const char *key, const char *file_path, const int line,
                      const char *format, va_list va_args, volatile bool *cachep)
{
  if (!envkey_debug_check (env_var, key, cachep))
    return;
  String msg = string_vprintf (format, va_args);
  debug_handler ('D', string_printf ("%s:%d", file_path, line), msg, key);
}

// == debug_* functions ==
void
debug_assert (const char *file_path, const int line, const char *message)
{
  debug_handler ('C', string_printf ("%s:%d", file_path, line), string_printf ("assertion failed: %s", message));
}

void
debug_fassert (const char *file_path, const int line, const char *message)
{
  debug_handler ('F', string_printf ("%s:%d", file_path, line), string_printf ("assertion failed: %s", message));
  ::abort();
}

void
debug_fatal (const char *file_path, const int line, const char *format, ...)
{
  va_list vargs;
  va_start (vargs, format);
  String msg = string_vprintf (format, vargs);
  va_end (vargs);
  debug_handler ('F', string_printf ("%s:%d", file_path, line), msg);
  ::abort();
}

void
debug_critical (const char *file_path, const int line, const char *format, ...)
{
  va_list vargs;
  va_start (vargs, format);
  String msg = string_vprintf (format, vargs);
  va_end (vargs);
  debug_handler ('C', string_printf ("%s:%d", file_path, line), msg);
}

void
debug_fixit (const char *file_path, const int line, const char *format, ...)
{
  va_list vargs;
  va_start (vargs, format);
  String msg = string_vprintf (format, vargs);
  va_end (vargs);
  debug_handler ('X', string_printf ("%s:%d", file_path, line), msg);
}

static Mutex              dbg_mutex;
static String             dbg_envvar = "";
static map<String,String> dbg_map;

/// Parse environment variable @a name for debug configuration.
void
debug_envvar (const String &name)
{
  ScopedLock<Mutex> locker (dbg_mutex);
  dbg_envvar = name;
}

/// Set debug configuration override.
void
debug_config_add (const String &option)
{
  ScopedLock<Mutex> locker (dbg_mutex);
  String key, value;
  const size_t eq = option.find ('=');
  if (eq != std::string::npos)
    {
      key = option.substr (0, eq);
      value = option.substr (eq + 1);
    }
  else
    {
      key = option;
      value = "1";
    }
  if (!key.empty())
    dbg_map[key] = value;
}

/// Unset debug configuration override.
void
debug_config_del (const String &key)
{
  ScopedLock<Mutex> locker (dbg_mutex);
  dbg_map.erase (key);
}

/// Query debug configuration for option @a key, defaulting to @a default_value.
String
debug_config_get (const String &key, const String &default_value)
{
  ScopedLock<Mutex> locker (dbg_mutex);
  auto pair = dbg_map.find (key);
  if (pair != dbg_map.end())
    return pair->second;
  auto envstring = [] (const char *name) {
    const char *c = getenv (name);
    return c ? c : "";
  };
  const String options[3] = {
    envstring (dbg_envvar.c_str()),
    envstring ("RAPICORN_DEBUG"),
  };
  for (size_t i = 0; i < ARRAY_SIZE (options); i++)
    {
      const int l = max (size_t (64), options[i].size());
      char value[l];
      strcpy (value, "0");
      const int keypos = cstring_option_sense (options[i].c_str(), key.c_str(), value);
      if (keypos >= 0)
        return value;
    }
  return default_value;
}

bool
debug_config_bool (const String &key, bool default_value)
{
  return string_to_bool (debug_config_get (key, string_from_int (default_value)));
}

// == AnsiColors ==
namespace AnsiColors {

const char*
color_code (Colors acolor)
{
  switch (acolor)
    {
    default: ;
    case NONE:             return "";
    case RESET:            return "\033[0m";
    case BOLD:             return "\033[1m";
    case BOLD_OFF:         return "\033[22m";
    case ITALICS:          return "\033[3m";
    case ITALICS_OFF:      return "\033[23m";
    case UNDERLINE:        return "\033[4m";
    case UNDERLINE_OFF:    return "\033[24m";
    case INVERSE:          return "\033[7m";
    case INVERSE_OFF:      return "\033[27m";
    case STRIKETHROUGH:    return "\033[9m";
    case STRIKETHROUGH_OFF:return "\033[29m";
    case FG_BLACK:         return "\033[30m";
    case FG_RED:           return "\033[31m";
    case FG_GREEN:         return "\033[32m";
    case FG_YELLOW:        return "\033[33m";
    case FG_BLUE:          return "\033[34m";
    case FG_MAGENTA:       return "\033[35m";
    case FG_CYAN:          return "\033[36m";
    case FG_WHITE:         return "\033[37m";
    case FG_DEFAULT:       return "\033[39m";
    case BG_BLACK:         return "\033[40m";
    case BG_RED:           return "\033[41m";
    case BG_GREEN:         return "\033[42m";
    case BG_YELLOW:        return "\033[43m";
    case BG_BLUE:          return "\033[44m";
    case BG_MAGENTA:       return "\033[45m";
    case BG_CYAN:          return "\033[46m";
    case BG_WHITE:         return "\033[47m";
    case BG_DEFAULT:       return "\033[49m";
    }
}

struct EnvKey {
  String var, key;
  EnvKey() : var (""), key ("") {}
};

static constexpr int     UNCHECKED = 2;
static Atomic<int>       colorize_stdout = UNCHECKED;   // cache stdout colorization check
static Exclusive<EnvKey> env_key;

void
color_envkey (const String &env_var, const String &key)
{
  EnvKey ekey;
  ekey.var = env_var;
  ekey.key = key;
  env_key = ekey; // Atomic access
  colorize_stdout = UNCHECKED; // Atomic access
}

bool
colorize_tty (int fd)
{
  EnvKey ekey = env_key;
  const char *ev = getenv (ekey.var.c_str());
  if (ev)
    {
      if (ekey.key.empty() == false)
        {
          const int l = max (size_t (64), strlen (ev) + 1);
          char value[l];
          strcpy (value, "auto");
          cstring_option_sense (ev, ekey.key.c_str(), value);
          if (strncasecmp (value, "always", 6) == 0)
            return true;
          else if (strncasecmp (value, "never", 5) == 0)
            return false;
          else if (strncasecmp (value, "auto", 4) != 0)
            return string_to_bool (value, 0);
        }
      else if (strncasecmp (ev, "always", 6) == 0)
        return true;
      else if (strncasecmp (ev, "never", 5) == 0)
        return false;
      else if (strncasecmp (ev, "auto", 4) != 0)
        return string_to_bool (ev, 0);
    }
  // found 'auto', sense arbitrary fd
  if (fd >= 3)
    return isatty (fd);
  // sense stdin/stdout/stderr
  if (isatty (1) && isatty (2))
    {
      char *term = getenv ("TERM");
      if (term && strcmp (term, "dumb") != 0)
        return true;
    }
  return false;
}

const char*
color (Colors acolor)
{
  if (colorize_stdout == UNCHECKED)
    colorize_stdout = colorize_tty();
  if (!colorize_stdout)
    return "";
  return color_code (acolor);
}

} // AnsiColors

} // Rapicorn
