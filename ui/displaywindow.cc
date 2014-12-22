// This Source Code Form is licensed MPLv2: http://mozilla.org/MPL/2.0
#include "displaywindow.hh"
#include <list>
#include <algorithm>

#define SDEBUG(...)     RAPICORN_KEY_DEBUG ("ScreenDriver", __VA_ARGS__)

namespace Rapicorn {

// == DisplayWindow ==
DisplayWindow::DisplayWindow () :
  async_state_accessed_ (0)
{}

DisplayWindow::~DisplayWindow ()
{
  std::list<Event*> events;
  {
    ScopedLock<Spinlock> sl (async_spin_);
    events.swap (async_event_queue_);
  }
  while (!events.empty())
    {
      Event *e = events.front ();
      events.pop_front();
      delete e;
    }
  critical_unless (async_event_queue_.empty()); // this queue must not be accessed at this point
}

void
DisplayWindow::enqueue_event (Event *event)
{
  critical_unless (event);
  ScopedLock<Spinlock> sl (async_spin_);
  const bool notify = async_event_queue_.empty();
  async_event_queue_.push_back (event);
  if (notify && async_wakeup_)
    async_wakeup_();
}

void
DisplayWindow::push_event (Event *event)
{
  critical_unless (event);
  ScopedLock<Spinlock> sl (async_spin_);
  const bool notify = async_event_queue_.empty();
  async_event_queue_.push_front (event);
  if (notify && async_wakeup_)
    async_wakeup_();
}

Event*
DisplayWindow::pop_event ()
{
  ScopedLock<Spinlock> sl (async_spin_);
  if (async_event_queue_.empty())
    return NULL;
  Event *event = async_event_queue_.front();
  async_event_queue_.pop_front();
  return event;
}

bool
DisplayWindow::has_event ()
{
  ScopedLock<Spinlock> sl (async_spin_);
  return !async_event_queue_.empty();
}

bool
DisplayWindow::peek_events (const std::function<bool (Event*)> &pred)
{
  ScopedLock<Spinlock> sl (async_spin_);
  for (auto ep : async_event_queue_)
    if (pred (ep))
      return true;
  return false;
}

void
DisplayWindow::set_event_wakeup (const std::function<void()> &wakeup)
{
  ScopedLock<Spinlock> sl (async_spin_);
  async_wakeup_ = wakeup;
}

bool
DisplayWindow::update_state (const State &state)
{
  ScopedLock<Spinlock> sl (async_spin_);
  const bool accessed = async_state_accessed_;
  async_state_accessed_ = false;
  async_state_ = state;
  return accessed;
}

DisplayWindow::State
DisplayWindow::get_state ()
{
  ScopedLock<Spinlock> sl (async_spin_);
  return async_state_;
}

bool
DisplayWindow::viewable ()
{
  ScopedLock<Spinlock> sl (async_spin_);
  bool viewable = async_state_.visible;
  viewable = viewable && !(async_state_.window_flags & (ICONIFY | HIDDEN | SHADED));
  return viewable;
}

void
DisplayWindow::configure (const Config &config, bool sizeevent)
{
  ScreenCommand *cmd = new ScreenCommand (ScreenCommand::CONFIGURE, this);
  cmd->config = new DisplayWindow::Config (config);
  cmd->need_resize = sizeevent;
  queue_command (cmd);
}

void
DisplayWindow::beep ()
{
  queue_command (new ScreenCommand (ScreenCommand::BEEP, this));
}

void
DisplayWindow::show ()
{
  queue_command (new ScreenCommand (ScreenCommand::SHOW, this));
}

void
DisplayWindow::present (bool user_activation)
{
  ScreenCommand *cmd = new ScreenCommand (ScreenCommand::PRESENT, this);
  cmd->u64 = user_activation;
  queue_command (cmd);
}

void
DisplayWindow::blit_surface (cairo_surface_t *surface, const Rapicorn::Region &region)
{
  ScreenCommand *cmd = new ScreenCommand (ScreenCommand::BLIT, this);
  cmd->surface = cairo_surface_reference (surface);
  cmd->region = new Rapicorn::Region (region);
  queue_command (cmd);
}

void
DisplayWindow::start_user_move (uint button, double root_x, double root_y)
{
  ScreenCommand *cmd = new ScreenCommand (ScreenCommand::UMOVE, this);
  cmd->button = button;
  cmd->root_x = root_x;
  cmd->root_y = root_y;
  queue_command (cmd);
}

void
DisplayWindow::start_user_resize (uint button, double root_x, double root_y, AnchorType edge)
{
  ScreenCommand *cmd = new ScreenCommand (ScreenCommand::URESIZE, this);
  cmd->button = button;
  cmd->root_x = root_x;
  cmd->root_y = root_y;
  queue_command (cmd);
}

void
DisplayWindow::set_content_owner (ContentSourceType source, uint64 nonce, const StringVector &data_types)
{
  ScreenCommand *cmd = new ScreenCommand (ScreenCommand::OWNER, this);
  cmd->source = source;
  cmd->nonce = nonce;
  cmd->string_list = data_types;
  queue_command (cmd);
}

void
DisplayWindow::provide_content (const String &data_type, const String &data, uint64 request_id)
{
  ScreenCommand *cmd = new ScreenCommand (ScreenCommand::PROVIDE, this);
  cmd->nonce = request_id;
  cmd->string_list.push_back (data_type);
  cmd->string_list.push_back (data);
  queue_command (cmd);
}

void
DisplayWindow::request_content (ContentSourceType source, uint64 nonce, const String &data_type)
{
  ScreenCommand *cmd = new ScreenCommand (ScreenCommand::CONTENT, this);
  cmd->source = source;
  cmd->nonce = nonce;
  cmd->string_list.push_back (data_type);
  queue_command (cmd);
}

void
DisplayWindow::destroy ()
{
  queue_command (new ScreenCommand (ScreenCommand::DESTROY, this));
}

void
DisplayWindow::queue_command (ScreenCommand *command)
{
  ScreenDriver::Friends::queue_command (screen_driver_async(), command);
}

static const char*
flag_name (uint64 flag)
{
  switch (DisplayWindow::Flags (flag))
    {
    case DisplayWindow::MODAL:		return "MODAL";
    case DisplayWindow::STICKY:	        return "STICKY";
    case DisplayWindow::VMAXIMIZED:	return "VMAXIMIZED";
    case DisplayWindow::HMAXIMIZED:	return "HMAXIMIZED";
    case DisplayWindow::SHADED:	        return "SHADED";
    case DisplayWindow::SKIP_TASKBAR:	return "SKIP_TASKBAR";
    case DisplayWindow::SKIP_PAGER:	return "SKIP_PAGER";
    case DisplayWindow::HIDDEN:	        return "HIDDEN";
    case DisplayWindow::FULLSCREEN:	return "FULLSCREEN";
    case DisplayWindow::ABOVE_ALL:	return "ABOVE_ALL";
    case DisplayWindow::BELOW_ALL:	return "BELOW_ALL";
    case DisplayWindow::ATTENTION:	return "ATTENTION";
    case DisplayWindow::FOCUS_DECO:	return "FOCUS_DECO";
    case DisplayWindow::DECORATED:	return "DECORATED";
    case DisplayWindow::MINIMIZABLE:	return "MINIMIZABLE";
    case DisplayWindow::MAXIMIZABLE:	return "MAXIMIZABLE";
    case DisplayWindow::DELETABLE:	return "DELETABLE";
    case DisplayWindow::ACCEPT_FOCUS:	return "ACCEPT_FOCUS";
    case DisplayWindow::UNFOCUSED:	return "UNFOCUSED";
    case DisplayWindow::ICONIFY:	return "ICONIFY";
    case DisplayWindow::_WM_STATE_MASK:
    case DisplayWindow::_DECO_MASK:
      ; // pass
    }
 return "UNKNOWN";
}

String
DisplayWindow::flags_name (uint64 flags, String combo)
{
  const uint64 I = 1;
  String result;
  for (size_t i = 0; i < 64; i++)
    if (flags & (I << i))
      result += (result.empty() ? "" : combo) + flag_name (I << i);
  return result;
}

// == ScreenCommand ==
ScreenCommand::ScreenCommand (Type ctype, DisplayWindow *window) :
  type (ctype), screen_window (window), config (NULL), setup (NULL), surface (NULL), region (NULL),
  nonce (0), root_x (-1), root_y (-1), button (-1), source (ContentSourceType (0)), need_resize (false)
{}

ScreenCommand::~ScreenCommand()
{
  if (surface)
    cairo_surface_destroy (surface);
  if (config)
    delete config;
  if (setup)
    delete setup;
  if (region)
    delete region;
}

bool
ScreenCommand::reply_type (Type type)
{
  switch (type)
    {
    case CREATE: case SHUTDOWN: return true; // has reply
    case OK: case ERROR:        return true; // is reply
    case CONFIGURE: case BLIT:
    case UMOVE: case URESIZE:
    case OWNER:
    case PROVIDE: case CONTENT:
    case BEEP: case SHOW:
    case PRESENT: case DESTROY:
      return false;
    }
  return false; // silence compiler
}

// == ScreenDriver ==
static Mutex                    screen_driver_mutex;
static ScreenDriver            *screen_driver_chain = NULL;

ScreenDriver::ScreenDriver (const String &name, int priority) :
  sibling_ (NULL), name_ (name), priority_ (priority)
{
  ScopedLock<Mutex> locker (screen_driver_mutex);
  sibling_ = screen_driver_chain;
  screen_driver_chain = this;
}

ScreenDriver::~ScreenDriver ()
{
  assert_return (command_queue_.pending() == false);
  assert_return (reply_queue_.pending() == false);
  assert_return (thread_handle_.get_id() == std::thread::id());
}

bool
ScreenDriver::open_L ()
{
  assert_return (screen_driver_mutex.debug_locked(), false);
  assert_return (thread_handle_.get_id() == std::thread::id(), false);
  assert_return (reply_queue_.pending() == false, false);
  thread_handle_ = std::thread (&ScreenDriver::run, this, std::ref (command_queue_), std::ref (reply_queue_));
  ScreenCommand *reply = reply_queue_.pop();
  if (reply->type == ScreenCommand::OK)
    {
      delete reply;
      return true;
    }
  else if (reply->type == ScreenCommand::ERROR)
    {
      delete reply;
      thread_handle_.join();
      return false;
    }
  else
    assert_unreached();
}

void
ScreenDriver::close_L ()
{
  assert_return (screen_driver_mutex.debug_locked());
  assert_return (thread_handle_.joinable());
  assert_return (reply_queue_.pending() == false);
  command_queue_.push (new ScreenCommand (ScreenCommand::SHUTDOWN, NULL));
  ScreenCommand *reply = reply_queue_.pop();
  assert (reply->type == ScreenCommand::OK);
  delete reply;
  thread_handle_.join();
}

ScreenDriver*
ScreenDriver::retrieve_screen_driver (const String &backend_name)
{
  ScopedLock<Mutex> locker (screen_driver_mutex);
  vector<ScreenDriver*> screen_driver_array;
  for (ScreenDriver *it = screen_driver_chain; it; it = it->sibling_)
    screen_driver_array.push_back (it);
  SDEBUG ("trying to open 1/%d screen drivers...", screen_driver_array.size());
  sort (screen_driver_array.begin(), screen_driver_array.end(), driver_priority_lesser);
  for (auto it : screen_driver_array)
    {
      const char *r;
      if (it->name_ != backend_name && backend_name != "auto")
        r = "not selected";
      else if (it->thread_handle_.joinable() || it->open_L())
        r = NULL;
      else
        r = "failed to open";
      SDEBUG ("screen driver %s: %s", CQUOTE (it->name_), r ? r : "success");
      if (r == NULL)
        return it;
    }
  return NULL;
}

void
ScreenDriver::forcefully_close_all ()
{
  ScopedLock<Mutex> locker (screen_driver_mutex);
  for (ScreenDriver *screen_driver = screen_driver_chain; screen_driver; screen_driver = screen_driver->sibling_)
    if (screen_driver->thread_handle_.joinable())
      screen_driver->close_L();
}

bool
ScreenDriver::driver_priority_lesser (const ScreenDriver *d1, const ScreenDriver *d2)
{
  return d1->priority_ < d2->priority_;
}

void
ScreenDriver::queue_command (ScreenCommand *screen_command)
{
  assert_return (thread_handle_.joinable());
  assert_return (screen_command->screen_window != NULL);
  assert_return (ScreenCommand::reply_type (screen_command->type) == false);
  command_queue_.push (screen_command);
}

DisplayWindow*
ScreenDriver::create_screen_window (const DisplayWindow::Setup &setup, const DisplayWindow::Config &config)
{
  assert_return (thread_handle_.joinable(), NULL);
  assert_return (reply_queue_.pending() == false, NULL);

  ScreenCommand *cmd = new ScreenCommand (ScreenCommand::CREATE, NULL);
  cmd->setup = new DisplayWindow::Setup (setup);
  cmd->config = new DisplayWindow::Config (config);
  command_queue_.push (cmd);
  ScreenCommand *reply = reply_queue_.pop();
  assert (reply->type == ScreenCommand::OK && reply->screen_window);
  DisplayWindow *screen_window = reply->screen_window;
  delete reply;
  return screen_window;
}

} // Rapicorn
