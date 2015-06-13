// This Source Code Form is licensed MPLv2: http://mozilla.org/MPL/2.0
#include "clientapi.hh"
#include "internal.hh"
#include <stdlib.h>

namespace Rapicorn {

static struct __StaticCTorTest { int v; __StaticCTorTest() : v (0x120caca0) { v += 0x300000; } } __staticctortest;
static ApplicationH app_cached;

/// Check and return if init_app() or init_test_app() has already been called.
bool
init_app_initialized ()
{
  return ApplicationH::the() != NULL;
}

/**
 * Initialize Rapicorn core via init_core(), and then starts a seperately
 * running UI thread. This UI thread initializes all UI related components
 * and the global Application object. After initialization, it enters the
 * main event loop for UI processing.
 * @param app_ident     Identifier for this application, this is used to distinguish
 *                      persistent application resources and window configurations
 *                      from other applications.
 * @param argcp         Pointer to @a argc as passed into main().
 * @param argv          The @a argv argument as passed into main().
 * @param args          Internal initialization arguments, see init_core() for details.
 */
ApplicationH
init_app (const String &app_ident, int *argcp, char **argv, const StringVector &args)
{
  assert_return (init_app_initialized() == false, app_cached);
  // assert global_ctors work
  if (__staticctortest.v != 0x123caca0)
    fatal ("librapicornui: link error: C++ constructors have not been executed");
  // initialize core
  if (!init_core_initialized())
    init_core (app_ident, argcp, argv, args);
  else if (app_ident != program_ident())
    fatal ("librapicornui: application identifier changed during ui initialization");
  // boot up UI thread
  const bool boot_ok = uithread_bootup (argcp, argv, args);
  assert (boot_ok);
  // connect to remote UIThread and fetch main handle
  ApplicationH app = Aida::ObjectBroker::connect<ApplicationHandle> ("inproc://Rapicorn-" RAPICORN_VERSION);
  assert (app != NULL);
  // assign global remote handle
  app_cached = app;
  return app_cached;
}

ApplicationH
ApplicationH::the ()
{
  return app_cached;
}

/**
 * This function calls Application::shutdown() first, to properly terminate Rapicorn's
 * concurrently running ui-thread, and then terminates the program via
 * exit(3posix). This function does not return.
 * @param status        The exit status returned to the parent process.
 */
void
exit_app (int status)
{
  uithread_shutdown();
  ::exit (status);
}

class AppSource;
typedef std::shared_ptr<AppSource> AppSourceP;

class AppSource : public EventSource {
  Rapicorn::Aida::BaseConnection &connection_;
  PollFD pfd_;
  bool   last_seen_primary, need_check_primary;
  void
  check_primaries()
  {
    // seen_primary is merely a hint, need to check local and remote states
    if (ApplicationH::the().finishable() &&  // remote
        main_loop()->finishable() && loop_)            // local
      main_loop()->quit();
  }
  AppSource (Rapicorn::Aida::BaseConnection &connection) :
    connection_ (connection), last_seen_primary (false), need_check_primary (false)
  {
    pfd_.fd = connection_.notify_fd();
    pfd_.events = PollFD::IN;
    pfd_.revents = 0;
    add_poll (&pfd_);
    primary (false);
    ApplicationH::the().sig_missing_primary() += [this]() { queue_check_primaries(); };
  }
  virtual bool
  prepare (const LoopState &state, int64 *timeout_usecs_p)
  {
    return need_check_primary || connection_.pending();
  }
  virtual bool
  check (const LoopState &state)
  {
    if (UNLIKELY (last_seen_primary && !state.seen_primary && !need_check_primary))
      need_check_primary = true;
    last_seen_primary = state.seen_primary;
    return need_check_primary || connection_.pending();
  }
  virtual bool
  dispatch (const LoopState &state)
  {
    connection_.dispatch();
    if (need_check_primary)
      {
        need_check_primary = false;
        queue_check_primaries();
      }
    return true;
  }
  friend class FriendAllocator<AppSource>;
public:
  static AppSourceP create (Rapicorn::Aida::BaseConnection &connection)
  { return FriendAllocator<AppSource>::make_shared (connection); }
  void
  queue_check_primaries()
  {
    if (loop_)
      loop_->exec_idle (Aida::slot (*this, &AppSource::check_primaries));
  }
};

} // Rapicorn

// compile client-side API
#include "clientapi.cc"

#include <rcore/testutils.hh>
namespace Rapicorn {
MainLoopP
ApplicationH::main_loop()
{
  assert_return (the() != NULL, NULL);
  static MainLoopP app_loop = NULL;
  do_once
    {
      app_loop = MainLoop::create();
      AppSourceP source = AppSource::create (*ApplicationHandle::__aida_connection__());
      app_loop->add (source);
      source->queue_check_primaries();
    }
  return app_loop;
}

/**
 * Initialize Rapicorn like init_app(), and boots up the test suite framework.
 * Normally, Test::run() should be called next to execute all unit tests.
 */
ApplicationH
init_test_app (const String &app_ident, int *argcp, char **argv, const StringVector &args)
{
  init_core_test (app_ident, argcp, argv, args);
  return init_app (app_ident, argcp, argv, args);
}

/**
 * Cause the application's main loop to quit, and run() to return @a quit_code.
 */
void
ApplicationH::quit (int quit_code)
{
  main_loop()->quit (quit_code);
}

/**
 * Run the main event loop until all primary sources ceased to exist
 * (see MainLoop::finishable()) or until the loop is quit.
 * @returns the @a quit_code passed in to loop_quit() or 0.
 */
int
ApplicationH::run ()
{
  return main_loop()->run();
}

/**
 * This function runs the Application main loop via loop_run(), and exits
 * the running process once the loop has quit. The loop_quit() status is
 * passed on to exit() and thus to the parent process.
 */
int
ApplicationH::run_and_exit ()
{
  int status = run();
  shutdown();
  ::exit (status);
}

/// Perform one loop iteration and return whether more iterations are needed.
bool
ApplicationH::iterate (bool block)
{
  return main_loop()->iterate (block);
}

/**
 * This function causes proper termination of Rapicorn's concurrently running
 * ui-thread and needs to be called before exit(3posix), to avoid parallel
 * execution of the ui-thread while atexit(3posix) handlers or while global
 * destructors are releasing process resources.
 */
void
ApplicationH::shutdown()
{
  uithread_shutdown();
}

// internal function for tests
int64
client_app_test_hook ()
{
  return ApplicationH::the().test_hook();
}

} // Rapicorn
