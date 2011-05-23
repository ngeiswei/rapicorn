/* Rapicorn
 * Copyright (C) 2005-2006 Tim Janik
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * A copy of the GNU Lesser General Public License should ship along
 * with this library; if not, see http://www.gnu.org/copyleft/.
 */
#include "viewp0rt.hh"
#include <rcore/rapicornthread.hh>
#include <ui/utilities.hh>

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <cairo/cairo-xlib.h>

#include <cstring>

static Rapicorn::Logging gtk_events ("gtk-events", Rapicorn::Logging::CURTLY);
#define DEBUG(...)       RAPICORN_DEBUG (gtk_events, __VA_ARGS__)

namespace { // Anon
using namespace Rapicorn;

/* --- prototypes --- */
class Viewp0rtGtk;
static gboolean     rapicorn_viewp0rt_ancestor_event (GtkWidget  *ancestor,
                                                      GdkEvent   *event,
                                                      GtkWidget  *widget);
static bool         gdk_window_has_ancestor          (GdkWindow  *window,
                                                      GdkWindow  *ancestor);
static bool         gtk_widget_has_local_display     (GtkWidget  *widget);
typedef enum {
  BACKING_STORE_NOT_USEFUL,
  BACKING_STORE_WHEN_MAPPED,
  BACKING_STORE_ALWAYS
} BackingStore;
static BackingStore gdk_window_enable_backing        (GdkWindow  *window,
                                                      BackingStore bs_type);

/* --- GDK_THREADS_* / Mutex glue --- */
struct RapicronGdkSyncLock {
  void
  lock()
  {
    GDK_THREADS_ENTER();
    /* protect execution of any Gtk/Gdk functions */
  }
  void
  unlock()
  {
    /* unprotect Gtk/Gdk */
    GDK_THREADS_LEAVE();
    /* X commands enqueued by any Gtk/Gdk functions so far
     * may still be queued and need to be flushed. also, any
     * X events that may have arrived already need to be
     * handled by the gtk_main() loop. waking up the default
     * main context will do exactly this, wake up gtk_main(),
     * and call XPending() which flushes any queued events.
     */
    g_main_context_wakeup (NULL);
  }
};
static RapicronGdkSyncLock GTK_GDK_THREAD_SYNC;

/* --- RapicornViewp0rt --- */
#define RAPICORN_TYPE_VIEWP0RT              (rapicorn_viewp0rt_get_type ())
#define RAPICORN_VIEWP0RT(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), RAPICORN_TYPE_VIEWP0RT, RapicornViewp0rt))
#define RAPICORN_VIEWP0RT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), RAPICORN_TYPE_VIEWP0RT, RapicornViewp0rtClass))
#define RAPICORN_IS_VIEWP0RT(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), RAPICORN_TYPE_VIEWP0RT))
#define RAPICORN_IS_VIEWP0RT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), RAPICORN_TYPE_VIEWP0RT))
#define RAPICORN_VIEWP0RT_GET_CLASS(object) (G_TYPE_INSTANCE_GET_CLASS ((object), RAPICORN_TYPE_VIEWP0RT, RapicornViewp0rtClass))
struct RapicornViewp0rtClass : public GtkContainerClass {};
struct RapicornViewp0rt : public GtkContainer {
  Viewp0rtGtk       *viewp0rt;
  GdkVisibilityState visibility_state;
  bool               fast_local_blitting;
  guint32            last_time;
  guint32            last_motion_time;
  BackingStore       backing_store;
  GdkModifierType    last_modifier;
  double             last_x, last_y;
};
G_DEFINE_TYPE (RapicornViewp0rt, rapicorn_viewp0rt, GTK_TYPE_CONTAINER);
static Viewp0rtGtk *rapicorn_viewp0rt__cxxinit_viewp0rt;        // protected by global GDK lock
static uint         rapicorn_viewp0rt__alive_counter;           // protected by global GDK lock
static bool         rapicorn_viewp0rt__auto_quit_gtk = false;   // protected by global GDK lock

/* --- Viewp0rtGtk class --- */
struct Viewp0rtGtk : public virtual Viewp0rt {
  union {
    RapicornViewp0rt   *m_viewp0rt;
    GtkWidget          *m_widget;
  };
  Info                  m_info;
  EventReceiver        &m_receiver;
  bool                  m_ignore_exposes;
  bool                  m_splash_screen;
  float                 m_root_x, m_root_y;
  float                 m_request_width, m_request_height;
  WindowState           m_window_state;
  Color                 m_average_background;
  explicit              Viewp0rtGtk             (const String   &backend_name,
                                                 WindowType      viewp0rt_type,
                                                 EventReceiver  &receiver);
  virtual void          present_viewp0rt        ();
  virtual void          trigger_hint_action     (WindowHint     hint);
  virtual void          start_user_move         (uint           button,
                                                 double         root_x,
                                                 double         root_y);
  virtual void          start_user_resize       (uint           button,
                                                 double         root_x,
                                                 double         root_y,
                                                 AnchorType     edge);
  virtual void          show                    (void);
  virtual bool          visible                 (void);
  virtual bool          viewable                (void);
  virtual void          hide                    (void);
  virtual void          blit_display            (Rapicorn::Display &display);
  virtual void          create_display_backing  (Rapicorn::Display &display);
  virtual void          copy_area               (double          src_x,
                                                 double          src_y,
                                                 double          width,
                                                 double          height,
                                                 double          dest_x,
                                                 double          dest_y);
  virtual void          enqueue_win_draws       (void);
  virtual uint          last_draw_stamp         ();
  virtual Info          get_info                ();
  virtual State         get_state               ();
  virtual void          set_config              (const Config  &config,
                                                 bool           force_resize_draw);
  virtual void          beep                    (void);
  void                  enqueue_locked          (Event         *event);
};
static Viewp0rt::Factory<Viewp0rtGtk> viewp0rt_gtk_factory ("GtkWindow"); // FIXME: should register only after gtk_init() has been called

/* --- Viewp0rtGtk methods --- */
Viewp0rtGtk::Viewp0rtGtk (const String  &backend_name,
                          WindowType     viewp0rt_type,
                          EventReceiver &receiver) :
  m_viewp0rt (NULL),
  m_receiver (receiver), m_ignore_exposes (false),
  m_root_x (NAN), m_root_y (NAN),
  m_request_width (33), m_request_height (33),
  m_window_state (WindowState (0)), m_average_background (0xff808080)
{
  m_info.window_type = viewp0rt_type;
  ScopedLock<RapicronGdkSyncLock> locker (GTK_GDK_THREAD_SYNC);
  rapicorn_viewp0rt__alive_counter++;
  bool is_override_redirect = (m_info.window_type == WINDOW_TYPE_DESKTOP ||
                               m_info.window_type == WINDOW_TYPE_DROPDOWN_MENU ||
                               m_info.window_type == WINDOW_TYPE_POPUP_MENU ||
                               m_info.window_type == WINDOW_TYPE_TOOLTIP ||
                               m_info.window_type == WINDOW_TYPE_NOTIFICATION ||
                               m_info.window_type == WINDOW_TYPE_COMBO ||
                               m_info.window_type == WINDOW_TYPE_DND);
  GtkWindow *window = NULL;
  if (backend_name == "GtkWindow")
    window = (GtkWindow*) gtk_window_new (is_override_redirect ? GTK_WINDOW_POPUP : GTK_WINDOW_TOPLEVEL);
  /* set GdkWindowTypeHint */
  switch (m_info.window_type)
    {
    case WINDOW_TYPE_NORMAL:            gtk_window_set_type_hint (window, GDK_WINDOW_TYPE_HINT_NORMAL);		break;
    case WINDOW_TYPE_DESKTOP:           gtk_window_set_type_hint (window, GDK_WINDOW_TYPE_HINT_DESKTOP);	break;
    case WINDOW_TYPE_DOCK:              gtk_window_set_type_hint (window, GDK_WINDOW_TYPE_HINT_DOCK);		break;
    case WINDOW_TYPE_TOOLBAR:           gtk_window_set_type_hint (window, GDK_WINDOW_TYPE_HINT_TOOLBAR);	break;
    case WINDOW_TYPE_MENU:              gtk_window_set_type_hint (window, GDK_WINDOW_TYPE_HINT_MENU);		break;
    case WINDOW_TYPE_UTILITY:           gtk_window_set_type_hint (window, GDK_WINDOW_TYPE_HINT_UTILITY);	break;
    case WINDOW_TYPE_SPLASH:            gtk_window_set_type_hint (window, GDK_WINDOW_TYPE_HINT_SPLASHSCREEN);	break;
    case WINDOW_TYPE_DIALOG:            gtk_window_set_type_hint (window, GDK_WINDOW_TYPE_HINT_DIALOG);		break;
#if GTK_CHECK_VERSION (2, 9, 0)
    case WINDOW_TYPE_DROPDOWN_MENU:     gtk_window_set_type_hint (window, GDK_WINDOW_TYPE_HINT_DROPDOWN_MENU);  break;
    case WINDOW_TYPE_POPUP_MENU:        gtk_window_set_type_hint (window, GDK_WINDOW_TYPE_HINT_POPUP_MENU);	break;
    case WINDOW_TYPE_TOOLTIP:           gtk_window_set_type_hint (window, GDK_WINDOW_TYPE_HINT_TOOLTIP);	break;
    case WINDOW_TYPE_NOTIFICATION:      gtk_window_set_type_hint (window, GDK_WINDOW_TYPE_HINT_NOTIFICATION);	break;
    case WINDOW_TYPE_COMBO:             gtk_window_set_type_hint (window, GDK_WINDOW_TYPE_HINT_COMBO);		break;
    case WINDOW_TYPE_DND:               gtk_window_set_type_hint (window, GDK_WINDOW_TYPE_HINT_DND);		break;
#endif
    default: break;
    }
  RAPICORN_ASSERT (rapicorn_viewp0rt__cxxinit_viewp0rt == NULL);
  rapicorn_viewp0rt__cxxinit_viewp0rt = this;
  m_viewp0rt = RAPICORN_VIEWP0RT (g_object_ref (g_object_new (RAPICORN_TYPE_VIEWP0RT, "can-focus", TRUE, "parent", window, NULL)));
  gtk_widget_grab_focus (GTK_WIDGET (m_viewp0rt));
  rapicorn_viewp0rt__cxxinit_viewp0rt = NULL;
  RAPICORN_ASSERT (m_viewp0rt->viewp0rt == this);
  g_object_set_data (G_OBJECT (m_viewp0rt), "RapicornViewp0rt-my-GtkWindow", window); // flag to indicate the window is owned by RapicornViewp0rt
}
// FIXME: add rapicorn_viewp0rt__alive_counter--; to ~Viewp0rtGtk and gtk_main_quit() via idle if 0

static GtkWindow*
rapicorn_viewp0rt_get_toplevel_window (RapicornViewp0rt *rviewp0rt)
{
  GtkWidget *widget = GTK_WIDGET (rviewp0rt);
  if (widget)
    while (widget->parent)
      widget = widget->parent;
  return GTK_IS_WINDOW (widget) ? (GtkWindow*) widget : NULL;
}

static GtkWindow*
rapicorn_viewp0rt_get_my_window (RapicornViewp0rt *rviewp0rt)
{
  GtkWindow *window = rapicorn_viewp0rt_get_toplevel_window (rviewp0rt);
  if (window && window == g_object_get_data (G_OBJECT (rviewp0rt), "RapicornViewp0rt-my-GtkWindow"))
    return window;
  return NULL;
}

void
Viewp0rtGtk::present_viewp0rt ()
{
  ScopedLock<RapicronGdkSyncLock> locker (GTK_GDK_THREAD_SYNC);
  GtkWindow *window = rapicorn_viewp0rt_get_toplevel_window (m_viewp0rt);
  if (window && GTK_WIDGET_DRAWABLE (window))
    gtk_window_present (window);
}

void
Viewp0rtGtk::enqueue_locked (Event *event)
{
  // ScopedLock<RapicronGdkSyncLock> locker (GTK_GDK_THREAD_SYNC);
  m_receiver.enqueue_async (event);
}

void
Viewp0rtGtk::start_user_move (uint           button,
                              double         root_x,
                              double         root_y)
{
  ScopedLock<RapicronGdkSyncLock> locker (GTK_GDK_THREAD_SYNC);
  GtkWindow *window = rapicorn_viewp0rt_get_toplevel_window (m_viewp0rt);
  if (window && GTK_WIDGET_DRAWABLE (window))
    gtk_window_begin_move_drag (window, button, iround (root_x), iround (root_y), GDK_CURRENT_TIME);
}

static GdkWindowEdge
get_gdk_window_edge (AnchorType anchor)
{
  switch (anchor)
    {
    case ANCHOR_NONE:
    case ANCHOR_CENTER:         return GDK_WINDOW_EDGE_SOUTH_EAST;
    case ANCHOR_EAST:           return GDK_WINDOW_EDGE_EAST;
    case ANCHOR_NORTH_EAST:     return GDK_WINDOW_EDGE_NORTH_EAST;
    case ANCHOR_NORTH:          return GDK_WINDOW_EDGE_NORTH;
    case ANCHOR_NORTH_WEST:     return GDK_WINDOW_EDGE_NORTH_WEST;
    case ANCHOR_WEST:           return GDK_WINDOW_EDGE_WEST;
    case ANCHOR_SOUTH_WEST:     return GDK_WINDOW_EDGE_SOUTH_WEST;
    case ANCHOR_SOUTH:          return GDK_WINDOW_EDGE_SOUTH;
    case ANCHOR_SOUTH_EAST:     return GDK_WINDOW_EDGE_SOUTH_EAST;
    }
  g_assert_not_reached();
}

void
Viewp0rtGtk::start_user_resize (uint           button,
                                double         root_x,
                                double         root_y,
                                AnchorType     edge)
{
  ScopedLock<RapicronGdkSyncLock> locker (GTK_GDK_THREAD_SYNC);
  GtkWindow *window = rapicorn_viewp0rt_get_toplevel_window (m_viewp0rt);
  if (window && GTK_WIDGET_DRAWABLE (window))
    gtk_window_begin_resize_drag (window, get_gdk_window_edge (edge), button, iround (root_x), iround (root_y), GDK_CURRENT_TIME);
}

void
Viewp0rtGtk::show (void)
{
  ScopedLock<RapicronGdkSyncLock> locker (GTK_GDK_THREAD_SYNC);
  if (m_viewp0rt)
    {
      gtk_widget_show (m_widget);
      GtkWindow *window = rapicorn_viewp0rt_get_my_window (m_viewp0rt);
      if (window)
        gtk_widget_show (GTK_WIDGET (window));
    }
}

bool
Viewp0rtGtk::visible (void)
{
  ScopedLock<RapicronGdkSyncLock> locker (GTK_GDK_THREAD_SYNC);
  if (m_viewp0rt)
    {
      GtkWindow *window = rapicorn_viewp0rt_get_my_window (m_viewp0rt);
      if (GTK_WIDGET_DRAWABLE (m_widget) &&
          (!window || GTK_WIDGET_DRAWABLE (window)))
        return TRUE;
    }
  return FALSE;
}

bool
Viewp0rtGtk::viewable (void)
{
  ScopedLock<RapicronGdkSyncLock> locker (GTK_GDK_THREAD_SYNC);
  if (m_viewp0rt)
    {
      if (!m_widget->window)
        return FALSE;
      GtkWidget *widget = m_widget;
      while (widget)
        {
          if (!GTK_WIDGET_DRAWABLE (widget) || !gdk_window_is_viewable (widget->window))
            return FALSE;
          widget = widget->parent;
        }
      return TRUE;
    }
  return FALSE;
}

void
Viewp0rtGtk::hide (void)
{
  ScopedLock<RapicronGdkSyncLock> locker (GTK_GDK_THREAD_SYNC);
  if (m_viewp0rt)
    {
      GtkWindow *window = rapicorn_viewp0rt_get_my_window (m_viewp0rt);
      if (window)
        {
          gtk_widget_hide (GTK_WIDGET (window));
          gtk_widget_unrealize (GTK_WIDGET (window));
        }
      gtk_widget_hide (m_widget);
    }
}

void
Viewp0rtGtk::create_display_backing (Rapicorn::Display &display)
{
  if (display.empty())
    return;
  const Rect e = display.extents();
  cairo_surface_t *bsurface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, e.width, e.height);
  cairo_t *cr = cairo_create (bsurface);
  cairo_translate (cr, -e.x, -e.y);
  display.set_backing (cr);
  cairo_surface_destroy (bsurface);
  cairo_destroy (cr);
}

#define CHECK_CAIRO_STATUS(status)      do {    \
            cairo_status_t ___s = (status);     \
            if (___s != CAIRO_STATUS_SUCCESS)   \
              RAPICORN_LOG (DIAG, "%s: %s", #status, cairo_status_to_string (___s)); \
          } while (0)

void
Viewp0rtGtk::blit_display (Rapicorn::Display &display)
{
  ScopedLock<RapicronGdkSyncLock> locker (GTK_GDK_THREAD_SYNC);
  if (m_viewp0rt && !display.empty())
    {
      int priority;
      if (m_viewp0rt->fast_local_blitting)
        priority = -G_MAXINT / 2;       /* run with PRIORITY_NOW to blit immediately on local displays */
      else
        priority = GTK_PRIORITY_REDRAW; /* allow event processing to interrupt blitting on remote displays */
      if (GTK_WIDGET_DRAWABLE (m_widget))
        {
          GdkVisual *gvisual = gdk_drawable_get_visual (m_widget->window);
          int gwidth, gheight;
          gdk_drawable_get_size (m_widget->window, &gwidth, &gheight);
          cairo_surface_t *xsurface = cairo_xlib_surface_create (GDK_WINDOW_XDISPLAY (m_widget->window),
                                                                 GDK_WINDOW_XID (m_widget->window),
                                                                 GDK_VISUAL_XVISUAL (gvisual),
                                                                 gwidth, gheight);
          return_if_fail (xsurface);
          CHECK_CAIRO_STATUS (cairo_surface_status (xsurface));
          return_if_fail (cairo_surface_status (xsurface) == CAIRO_STATUS_SUCCESS);
          cairo_xlib_surface_set_size (xsurface, gwidth, gheight);
          return_if_fail (cairo_surface_status (xsurface) == CAIRO_STATUS_SUCCESS);

          cairo_t *xcr = cairo_create (xsurface);
          return_if_fail (xcr);
          return_if_fail (CAIRO_STATUS_SUCCESS == cairo_status (xcr));
          cairo_scale (xcr, 1, -1);
          cairo_translate (xcr, 0, -gheight);
          display.render_backing (xcr);
          assert (CAIRO_STATUS_SUCCESS == cairo_status (xcr));
          gdk_flush();
          if (xcr)
            {
              cairo_destroy (xcr);
              xcr = NULL;
            }
          if (xsurface)
            {
              cairo_surface_destroy (xsurface);
              xsurface = NULL;
            }
        }
    }
}

void
Viewp0rtGtk::copy_area (double          src_x,
                        double          src_y,
                        double          width,
                        double          height,
                        double          dest_x,
                        double          dest_y)
{
  ScopedLock<RapicronGdkSyncLock> locker (GTK_GDK_THREAD_SYNC);
  if (GTK_WIDGET_DRAWABLE (m_widget))
    {
      /* copy the area */
      int window_height;
      gdk_window_get_size (m_widget->window, NULL, &window_height);
      gdk_gc_set_exposures (m_widget->style->black_gc, TRUE);
      gdk_draw_drawable (m_widget->window, m_widget->style->black_gc, // FIXME: use gdk_window_move_region() with newer Gtk+
                         m_widget->window, iround (src_x), iround (window_height - src_y - height),
                         iround (dest_x), iround (window_height - dest_y - height), iround (width), iround (height));
      gdk_gc_set_exposures (m_widget->style->black_gc, FALSE);
      /* ensure last GraphicsExpose events are processed before the next copy */
      GdkEvent *event = gdk_event_get_graphics_expose (m_widget->window);
      while (event)
        {
          gtk_widget_send_expose (m_widget, event);
          bool last = event->expose.count == 0;
          gdk_event_free (event);
          event = last || !m_widget->window ? NULL : gdk_event_get_graphics_expose (m_widget->window);
        }
    }
}

void
Viewp0rtGtk::enqueue_win_draws (void)
{
  ScopedLock<RapicronGdkSyncLock> locker (GTK_GDK_THREAD_SYNC);
  if (GTK_WIDGET_DRAWABLE (m_widget))
    gdk_window_process_updates (m_widget->window, TRUE);
}

uint
Viewp0rtGtk::last_draw_stamp ()
{
  return 0;
}

Viewp0rt::Info
Viewp0rtGtk::get_info ()
{
  return m_info;
}

Viewp0rt::State
Viewp0rtGtk::get_state ()
{
  ScopedLock<RapicronGdkSyncLock> locker (GTK_GDK_THREAD_SYNC);
  GtkWindow *window = rapicorn_viewp0rt_get_toplevel_window (m_viewp0rt);
  State state;
  state.local_blitting = m_viewp0rt->fast_local_blitting;
  state.is_active = window && gtk_window_is_active (window);
  state.has_toplevel_focus = window && gtk_window_has_toplevel_focus (window);
  state.window_state = m_window_state;
  bool has_real_allocation = GTK_WIDGET_REALIZED (m_widget) && GTK_WIDGET_VISIBLE (m_widget);
  state.width = has_real_allocation ? m_widget->allocation.width : 0;
  state.height = has_real_allocation ? m_widget->allocation.height : 0;
  return state;
}

static void
configure_gtk_window (GtkWindow              *window,
                      const Viewp0rt::Config &config)
{
  GtkWidget *widget = GTK_WIDGET (window);
  
  /* simple settings */
  gtk_window_set_modal (window, config.modal);
  gtk_window_set_title (window, config.title.c_str());
  gtk_window_set_role (window, config.session_role.c_str());
  if (config.initial_width > 0 && config.initial_height > 0)
    gtk_window_set_default_size (window, iround (config.initial_width), iround (config.initial_height));
  else
    gtk_window_set_default_size (window, -1, -1);
  window->need_default_size = TRUE; /* work-around for gtk_window_set_geometry_hints() breaking basic resizing functionality */

  /* geometry handling */
  if (GTK_WIDGET_REALIZED (window))
    gdk_window_set_geometry_hints (widget->window, NULL, GdkWindowHints (0));   // clear geometry on XServer
  GdkGeometry geometry = { 0, };
  uint geometry_mask = 0;
  if (config.min_width > 0 && config.min_height > 0)
    {
      geometry.min_width = iround (config.min_width);
      geometry.min_height = iround (config.min_height);
      geometry_mask |= GDK_HINT_MIN_SIZE;
    }
  if (config.max_width > 0 && config.max_height > 0)
    {
      geometry.max_width = iround (config.max_width);
      geometry.max_height = iround (config.max_height);
      geometry_mask |= GDK_HINT_MAX_SIZE;
    }
  if (config.base_width > 0 && config.base_height > 0)
    {
      geometry.base_width = iround (config.base_width);
      geometry.base_height = iround (config.base_height);
      geometry_mask |= GDK_HINT_BASE_SIZE;
    }
  if (config.width_inc > 0 && config.height_inc > 0)
    {
      geometry.width_inc = iround (config.width_inc);
      geometry.height_inc = iround (config.height_inc);
      geometry_mask |= GDK_HINT_RESIZE_INC;
    }
  if (config.min_aspect > 0 && config.max_aspect > 0)
    {
      geometry.min_aspect = config.min_aspect;
      geometry.max_aspect = config.max_aspect;
      geometry_mask |= GDK_HINT_ASPECT;
    }
  gtk_window_set_geometry_hints (window, NULL, &geometry, GdkWindowHints (geometry_mask));
  
  /* window_hint handling */
  gtk_window_set_decorated (window, bool (config.window_hint & Viewp0rt::HINT_DECORATED));
#if GTK_CHECK_VERSION (2, 8, 0)
  gtk_window_set_urgency_hint (window, bool (config.window_hint & Viewp0rt::HINT_URGENT));
#endif
#if GTK_CHECK_VERSION (99999, 9999, 0) // FIXME: gtk_window_set_shaded()
  gtk_window_set_shaded (window, bool (config.window_hint & Viewp0rt::HINT_SHADED));
#endif
  gtk_window_set_keep_above (window, bool (config.window_hint & Viewp0rt::HINT_ABOVE_ALL));
  gtk_window_set_keep_below (window, bool (config.window_hint & Viewp0rt::HINT_BELOW_ALL));
  gtk_window_set_skip_taskbar_hint (window, bool (config.window_hint & Viewp0rt::HINT_SKIP_TASKBAR));
  gtk_window_set_skip_pager_hint (window, bool (config.window_hint & Viewp0rt::HINT_SKIP_PAGER));
  gtk_window_set_accept_focus (window, bool (config.window_hint & Viewp0rt::HINT_ACCEPT_FOCUS));
  gtk_window_set_focus_on_map (window, !bool (config.window_hint & Viewp0rt::HINT_UNFOCUSED));
#if GTK_CHECK_VERSION (2, 10, 0)
  gtk_window_set_deletable (window, bool (config.window_hint & Viewp0rt::HINT_DELETABLE));
#endif
}

static void
adjust_gtk_window (GtkWindow            *window,
                   Viewp0rt::WindowHint  window_hint)
{
  /* actively alter window state */
  if (window_hint & Viewp0rt::HINT_STICKY)
    gtk_window_stick (window);
  else
    gtk_window_unstick (window);
  if (window_hint & Viewp0rt::HINT_ICONIFY)
    gtk_window_iconify (window);
  else
    gtk_window_deiconify (window);
  if (window_hint & Viewp0rt::HINT_FULLSCREEN)
    gtk_window_fullscreen (window);
  else
    gtk_window_unfullscreen (window);
  uint fully_maximized = Viewp0rt::HINT_HMAXIMIZED | Viewp0rt::HINT_VMAXIMIZED;
  if ((window_hint & fully_maximized) == fully_maximized)
    gtk_window_maximize (window);
  else if ((window_hint & fully_maximized) == 0)
    gtk_window_unmaximize (window);
  else
    {
#if GTK_CHECK_VERSION (99999, 9999, 0) // FIXME: gtk_window_hmaximize() gtk_window_vmaximize()
      if (window_hint & Viewp0rt::HINT_HMAXIMIZED)
        gtk_window_hmaximize (window);
      if (window_hint & Viewp0rt::HINT_VMAXIMIZED)
        gtk_window_vmaximize (window);
#endif
    }
}

void
Viewp0rtGtk::set_config (const Config &config,
                         bool          force_resize_draw)
{
  ScopedLock<RapicronGdkSyncLock> locker (GTK_GDK_THREAD_SYNC);
  GtkWindow *window = rapicorn_viewp0rt_get_my_window (m_viewp0rt);
  m_root_x = config.root_x;
  m_root_y = config.root_y;
  if (m_request_width != config.request_width ||
      m_request_height != config.request_height ||
      force_resize_draw)
    {
      m_request_width = config.request_width;
      m_request_height = config.request_height;
      /* guarantee a WIN_DRAW event upon size changes */
      gtk_widget_queue_resize (m_widget);
      m_ignore_exposes = true;
    }
  m_average_background = config.average_background;
  if (window)
    {
      configure_gtk_window (window, config);
      if (!GTK_WIDGET_DRAWABLE (window)) /* when unmapped or unrealized */
        {
          adjust_gtk_window (window, config.window_hint);
          if (isfinite (config.root_x) && isfinite (config.root_y))
            gtk_window_move (window, iround (config.root_x), iround (config.root_y));
        }
    }
}

void
Viewp0rtGtk::beep()
{
  ScopedLock<RapicronGdkSyncLock> locker (GTK_GDK_THREAD_SYNC);
  if (GTK_WIDGET_DRAWABLE (m_widget))
    {
#if GTK_CHECK_VERSION (2, 12, 0)
      gdk_window_beep (m_widget->window);
#else
      gdk_display_beep (gtk_widget_get_display (m_widget));
#endif
    }
}

void
Viewp0rtGtk::trigger_hint_action (WindowHint window_hint)
{
  ScopedLock<RapicronGdkSyncLock> locker (GTK_GDK_THREAD_SYNC);
  GtkWindow *window = rapicorn_viewp0rt_get_my_window (m_viewp0rt);
  if (window)
    adjust_gtk_window (window, window_hint);
}

/* --- Gdk/Gtk+/Rapicorn utilities --- */
static Viewp0rt::WindowState
get_rapicorn_window_state (GdkWindowState window_state)
{
  uint wstate = 0;
  if (window_state & GDK_WINDOW_STATE_WITHDRAWN)
    wstate |= Viewp0rt::STATE_WITHDRAWN;
  if (window_state & GDK_WINDOW_STATE_ICONIFIED)
    wstate |= Viewp0rt::STATE_ICONIFIED;
  if (window_state & GDK_WINDOW_STATE_MAXIMIZED)
    wstate |= Viewp0rt::STATE_HMAXIMIZED | Viewp0rt::STATE_VMAXIMIZED;
  if (window_state & GDK_WINDOW_STATE_STICKY)
    wstate |= Viewp0rt::STATE_STICKY;
  if (window_state & GDK_WINDOW_STATE_FULLSCREEN)
    wstate |= Viewp0rt::STATE_FULLSCREEN;
  if (window_state & GDK_WINDOW_STATE_ABOVE)
    wstate |= Viewp0rt::STATE_ABOVE;
  if (window_state & GDK_WINDOW_STATE_BELOW)
    wstate |= Viewp0rt::STATE_BELOW;
  return Viewp0rt::WindowState (wstate);
}

static bool
gdk_window_has_ancestor (GdkWindow *window,
                         GdkWindow *ancestor)
{
  while (window)
    if (window == ancestor)
      return true;
    else
      window = gdk_window_get_parent (window);
  return false;
}

static BackingStore
gdk_window_enable_backing (GdkWindow   *window,
                           BackingStore bs_type)
{
  g_return_val_if_fail (GDK_IS_WINDOW (window), BACKING_STORE_NOT_USEFUL);
  if (GDK_WINDOW_DESTROYED (window))
    return BACKING_STORE_NOT_USEFUL;
  
  XSetWindowAttributes attr = { 0, };
  attr.backing_store = NotUseful;
  if (bs_type == BACKING_STORE_WHEN_MAPPED)
    attr.backing_store = WhenMapped;
  else if (bs_type == BACKING_STORE_ALWAYS)
    attr.backing_store = Always;
  XChangeWindowAttributes (GDK_WINDOW_XDISPLAY (window),
                           GDK_WINDOW_XID (window),
                           CWBackingStore,
                           &attr);
  GdkScreen *screen = gdk_colormap_get_screen (gdk_drawable_get_colormap (window));
  int bs_screen = XDoesBackingStore (GDK_SCREEN_XSCREEN (screen));
  /* return backing store state for this window */
  if (bs_screen == Always &&
      bs_type   == BACKING_STORE_ALWAYS)
    return BACKING_STORE_ALWAYS;
  else if ((bs_screen == Always               || bs_screen == WhenMapped) &&
           (bs_type   == BACKING_STORE_ALWAYS || bs_type   == BACKING_STORE_WHEN_MAPPED))
    return BACKING_STORE_WHEN_MAPPED;
  else /* bs_screen == NotUseful || bs_type == BACKING_STORE_NOT_USEFUL */
    return BACKING_STORE_NOT_USEFUL;
}

static bool
gtk_widget_has_local_display (GtkWidget *widget)
{
  GtkWidget *toplevel = gtk_widget_get_toplevel (widget);
  if (GTK_WIDGET_TOPLEVEL (toplevel) && toplevel->window)
    {
      GdkDisplay *display = gtk_widget_get_display (toplevel);
      gpointer visual_flag = g_object_get_data (G_OBJECT (display), "GdkDisplay-local-display-flag");
      if (visual_flag == (void*) 0)
        {
          GdkImage *image = gdk_image_new (GDK_IMAGE_SHARED, gtk_widget_get_visual (toplevel), 1, 1);
          visual_flag = image ? (void*) 0x1 : (void*) 0x2;
          if (image)
            gdk_image_destroy (image);
          g_object_set_data (G_OBJECT (display), "GdkDisplay-local-display-flag", visual_flag);
        }
      return visual_flag == (void*) 0x1;
    }
  return false;
}

/* --- RapicornViewp0rt methods --- */
static void
rapicorn_viewp0rt_init (RapicornViewp0rt *self)
{
  GtkWidget *widget = GTK_WIDGET (self);
  self->viewp0rt = rapicorn_viewp0rt__cxxinit_viewp0rt;
  RAPICORN_ASSERT (self->viewp0rt != NULL);
  self->visibility_state = GDK_VISIBILITY_FULLY_OBSCURED;
  self->backing_store = BACKING_STORE_NOT_USEFUL;
  self->last_time = 0;
  self->last_motion_time = 0;
  self->last_x = -1;
  self->last_y = -1;
  self->last_modifier = GdkModifierType (0);
  gtk_widget_set_redraw_on_allocate (widget, TRUE);
  gtk_widget_set_double_buffered (widget, FALSE);
  gtk_widget_show (widget);
}

static bool
translate_along_ancestry (GdkWindow *window1, /* from-window */
                          GdkWindow *window2, /* to-window */
                          int       *deltax,
                          int       *deltay)
{
  /* translate in case window1 is a child of window2 or vice versa */
  GdkWindow *c1 = window1, *c2 = window2;
  int dx1 = 0, dy1 = 0, dx2 = 0, dy2 = 0, px, py;
  if (c1 == c2)
    {
      *deltax = 0;
      *deltay = 0;
      return true;
    }
  while (c1 && c2)
    {
      gdk_window_get_position (c1, &px, &py);
      dx1 += px;
      dy1 += py;
      c1 = gdk_window_get_parent (c1);
      if (c1 == window2)
        {
          *deltax = -dx1;
          *deltay = -dy1;
          return true;
        }
      gdk_window_get_position (c2, &px, &py);
      dx2 += px;
      dy2 += py;
      c2 = gdk_window_get_parent (c2);
      if (c2 == window1)
        {
          *deltax = dx2;
          *deltay = dy2;
          return true;
        }
    }
  while (c1)
    {
      gdk_window_get_position (c1, &px, &py);
      dx1 += px;
      dy1 += py;
      c1 = gdk_window_get_parent (c1);
      if (c1 == window2)
        {
          *deltax = -dx1;
          *deltay = -dy1;
          return true;
        }
    }
  while (c2)
    {
      gdk_window_get_position (c2, &px, &py);
      dx2 += px;
      dy2 += py;
      c2 = gdk_window_get_parent (c2);
      if (c2 == window1)
        {
          *deltax = dx2;
          *deltay = dy2;
          return true;
        }
    }
  return false;
}

static EventContext
rapicorn_viewp0rt_event_context (RapicornViewp0rt *self,
                                 GdkEvent         *event = NULL,
                                 int              *window_height = NULL,
                                 GdkTimeCoord     *core_coords = NULL)
{
  /* extract common event information */
  EventContext econtext;
  double doublex, doubley;
  GdkModifierType modifier_type;
  GdkWindow *gdkwindow = GTK_WIDGET (self)->window;
  gint wh = 0;
  if (gdkwindow)
    gdk_window_get_size (gdkwindow, NULL, &wh);
  if (event && event->any.window == gdkwindow && core_coords)
    {
      self->last_time = core_coords->time;
      self->last_x = core_coords->axes[0];
      /* vertical Rapicorn axis extends upwards */
      self->last_y = wh - core_coords->axes[1];;
      econtext.synthesized = true;
    }
  else if (event)
    {
      guint32 evt = gdk_event_get_time (event);
      if (evt != GDK_CURRENT_TIME)
        self->last_time = evt;
      int dx, dy;
      if (event->type != GDK_CONFIGURE && // ConfigureNotify events don't contain window-relative pointer coordinates
          gdk_event_get_coords (event, &doublex, &doubley) &&
          translate_along_ancestry (event->any.window, gdkwindow, &dx, &dy))
        {
          self->last_x = doublex - dx;
          self->last_y = wh - (doubley - dy); // vertical Rapicorn axis extends upwards
        }
      if (event->type != GDK_PROPERTY_NOTIFY && // some X servers send 0-state on PropertyNotify
          gdk_event_get_state (event, &modifier_type))
        self->last_modifier = modifier_type;
      econtext.synthesized = event->any.send_event;
    }
  else
    econtext.synthesized = true;
  econtext.time = self->last_time;
  if (gdkwindow)
    {
      doublex = self->last_x;
      doubley = self->last_y;
      modifier_type = self->last_modifier;
    }
  else
    {
      // unrealized
      doublex = doubley = -1;
      modifier_type = GdkModifierType (0);
    }
  econtext.x = iround (doublex);
  econtext.y = iround (doubley);
  econtext.modifiers = ModifierState (modifier_type);
  /* ensure modifier compatibility */
  RAPICORN_STATIC_ASSERT (GDK_SHIFT_MASK   == (int) MOD_SHIFT);
  RAPICORN_STATIC_ASSERT (GDK_LOCK_MASK    == (int) MOD_CAPS_LOCK);
  RAPICORN_STATIC_ASSERT (GDK_CONTROL_MASK == (int) MOD_CONTROL);
  RAPICORN_STATIC_ASSERT (GDK_MOD1_MASK    == (int) MOD_MOD1);
  RAPICORN_STATIC_ASSERT (GDK_MOD2_MASK    == (int) MOD_MOD2);
  RAPICORN_STATIC_ASSERT (GDK_MOD3_MASK    == (int) MOD_MOD3);
  RAPICORN_STATIC_ASSERT (GDK_MOD4_MASK    == (int) MOD_MOD4);
  RAPICORN_STATIC_ASSERT (GDK_MOD5_MASK    == (int) MOD_MOD5);
  RAPICORN_STATIC_ASSERT (GDK_BUTTON1_MASK == (int) MOD_BUTTON1);
  RAPICORN_STATIC_ASSERT (GDK_BUTTON2_MASK == (int) MOD_BUTTON2);
  RAPICORN_STATIC_ASSERT (GDK_BUTTON3_MASK == (int) MOD_BUTTON3);
  if (window_height)
    *window_height = wh;
  return econtext;
}

static void
rapicorn_viewp0rt_change_visibility (RapicornViewp0rt  *self,
                                     GdkVisibilityState visibility)
{
  Viewp0rtGtk *viewp0rt = self->viewp0rt;
  self->visibility_state = visibility;
  if (self->visibility_state == GDK_VISIBILITY_FULLY_OBSCURED)
    {
      EventContext econtext = rapicorn_viewp0rt_event_context (self);
      viewp0rt->enqueue_locked (create_event_cancellation (econtext));
    }
}

static void
rapicorn_viewp0rt_size_request (GtkWidget      *widget,
                                GtkRequisition *requisition)
{
  RapicornViewp0rt *self = RAPICORN_VIEWP0RT (widget);
  Viewp0rtGtk *viewp0rt = self->viewp0rt;
  if (viewp0rt)
    {
      requisition->width = iceil (viewp0rt->m_request_width);
      requisition->height = iceil (viewp0rt->m_request_height);
    }
}

static void
rapicorn_viewp0rt_size_allocate (GtkWidget     *widget,
                                 GtkAllocation *allocation)
{
  widget->allocation = *allocation;
  RapicornViewp0rt *self = RAPICORN_VIEWP0RT (widget);
  Viewp0rtGtk *viewp0rt = self->viewp0rt;
  if (GTK_WIDGET_REALIZED (widget))
    {
      gdk_window_move_resize (widget->window, allocation->x, allocation->y, allocation->width, allocation->height);
      gdk_flush(); /* resize now, so gravity settings take effect immediately */
    }
  if (viewp0rt)
    {
      EventContext econtext = rapicorn_viewp0rt_event_context (self); /* relies on proper GdkWindow size */
      viewp0rt->enqueue_locked (create_event_win_size (econtext, 0, allocation->width, allocation->height));
      gtk_widget_queue_draw (widget); /* make sure we *always* redraw when sending win_size */
    }
  viewp0rt->m_ignore_exposes = false;
}

static void
rapicorn_viewp0rt_realize (GtkWidget *widget)
{
  RapicornViewp0rt *self = RAPICORN_VIEWP0RT (widget);
  Viewp0rtGtk *viewp0rt = self->viewp0rt;
  GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);
  GdkWindowAttr attributes = { 0, };
  attributes.window_type = GDK_WINDOW_CHILD;
  attributes.x = widget->allocation.x;
  attributes.y = widget->allocation.y;
  attributes.width = widget->allocation.width;
  attributes.height = widget->allocation.height;
  attributes.wclass = GDK_INPUT_OUTPUT;
  attributes.visual = gtk_widget_get_visual (widget);
  attributes.colormap = gtk_widget_get_colormap (widget);
  attributes.event_mask = GDK_EXPOSURE_MASK | GDK_ALL_EVENTS_MASK;
  int attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
  widget->window = gdk_window_new (gtk_widget_get_parent_window (widget), &attributes, attributes_mask);
  gdk_window_set_user_data (widget->window, widget);
  widget->style = gtk_style_attach (widget->style, widget->window);
  Color argb_color = viewp0rt ? viewp0rt->m_average_background : Color (0xff808080);
  GdkColor gdkcolor = { 0, };
  gdkcolor.red = argb_color.red() * 0x0101;
  gdkcolor.green = argb_color.green() * 0x0101;
  gdkcolor.blue = argb_color.blue() * 0x0101;
  if (gdk_colormap_alloc_color (attributes.colormap, &gdkcolor, FALSE, TRUE))
    {
      gdk_window_set_background (widget->window, &gdkcolor);
      gdk_colormap_free_colors (attributes.colormap, &gdkcolor, 1);
    }
  /* for flicker-free static gravity, we must set static-gravities on the complete ancestry */
  for (GdkWindow *twindow = widget->window; twindow; twindow = gdk_window_get_parent (twindow))
    gdk_window_set_static_gravities (twindow, TRUE);
  /* catch toplevel unmap events */
  GtkWidget *toplevel = gtk_widget_get_toplevel (widget);
  gtk_widget_add_events (toplevel, GDK_STRUCTURE_MASK | GDK_VISIBILITY_NOTIFY_MASK);
  g_signal_connect (toplevel, "event", G_CALLBACK (rapicorn_viewp0rt_ancestor_event), self);
  /* optimize GtkWindow for flicker-free child window moves if it is our immediate parent */
  if (widget->parent == toplevel && gdk_colormap_alloc_color (attributes.colormap, &gdkcolor, FALSE, TRUE))
    {
      gtk_widget_set_app_paintable (toplevel, TRUE);
      gdk_window_set_background (toplevel->window, &gdkcolor);
      gdk_colormap_free_colors (attributes.colormap, &gdkcolor, 1);
    }
  rapicorn_viewp0rt_change_visibility (self, GDK_VISIBILITY_FULLY_OBSCURED);
  self->fast_local_blitting = gtk_widget_has_local_display (widget);
  if (!self->fast_local_blitting)
    self->backing_store = gdk_window_enable_backing (widget->window, BACKING_STORE_ALWAYS);
}

static void
rapicorn_viewp0rt_map (GtkWidget *widget)
{
  RapicornViewp0rt *self = RAPICORN_VIEWP0RT (widget);
  Viewp0rtGtk *viewp0rt = self->viewp0rt;
  bool block_auto_startup_notification = viewp0rt && viewp0rt->m_splash_screen;
  if (block_auto_startup_notification)
    gtk_window_set_auto_startup_notification (false);
  GTK_WIDGET_CLASS (rapicorn_viewp0rt_parent_class)->map (widget); // chain
  if (block_auto_startup_notification)
    gtk_window_set_auto_startup_notification (true);
  rapicorn_viewp0rt_change_visibility (self, GDK_VISIBILITY_FULLY_OBSCURED);
}

static void
rapicorn_viewp0rt_unmap (GtkWidget *widget)
{
  RapicornViewp0rt *self = RAPICORN_VIEWP0RT (widget);
  rapicorn_viewp0rt_change_visibility (self, GDK_VISIBILITY_FULLY_OBSCURED);
  GTK_WIDGET_CLASS (rapicorn_viewp0rt_parent_class)->unmap (widget); // chain
}

static void
rapicorn_viewp0rt_unrealize (GtkWidget *widget)
{
  RapicornViewp0rt *self = RAPICORN_VIEWP0RT (widget);
  Viewp0rtGtk *viewp0rt = self->viewp0rt;
  GtkWidget *toplevel = gtk_widget_get_toplevel (widget);
  g_signal_handlers_disconnect_by_func (toplevel, (void*) rapicorn_viewp0rt_ancestor_event, self);
  if (viewp0rt)
    viewp0rt->enqueue_locked (create_event_cancellation (rapicorn_viewp0rt_event_context (self)));
  GTK_WIDGET_CLASS (rapicorn_viewp0rt_parent_class)->unrealize (widget); // chain
  rapicorn_viewp0rt_change_visibility (self, GDK_VISIBILITY_FULLY_OBSCURED);
  self->fast_local_blitting = false;
}

static void
debug_dump_event (GtkWidget          *widget,
                  const gchar        *prefix,
                  GdkEvent           *event,
                  const EventContext &econtext)
{
  GEnumClass *eclass = (GEnumClass*) g_type_class_ref (GDK_TYPE_EVENT_TYPE);
  const gchar *ename = g_enum_get_value (eclass, event->type)->value_name;
  if (strncmp (ename, "GDK_", 4) == 0)
    ename += 4;
  String name = ename + String (")");
  gint wx, wy, ww, wh;
  gdk_window_get_position (widget->window, &wx, &wy);
  gdk_window_get_size (widget->window, &ww, &wh);
  String dmsg = string_printf ("%s%-14s t=0x%08x xy=%+7.2f,%+7.2f state=0x%04x ewin=%p",
                               prefix, name.c_str(), econtext.time,
                               econtext.x, econtext.y, econtext.modifiers,
                               event->any.window);
  switch (event->type)
    {
      const gchar *mode, *detail;
      GEnumClass *ec1;
    case GDK_ENTER_NOTIFY:
    case GDK_LEAVE_NOTIFY:
      switch (event->crossing.mode) {
      case GDK_CROSSING_NORMAL:             mode = "normal";  break;
      case GDK_CROSSING_GRAB:               mode = "grab";    break;
      case GDK_CROSSING_UNGRAB:             mode = "ungrab";  break;
      default:                              mode = "unknown"; break;
      }
      switch (event->crossing.detail) {
      case GDK_NOTIFY_INFERIOR:             detail = "inferior";  break;
      case GDK_NOTIFY_ANCESTOR:             detail = "ancestor";  break;
      case GDK_NOTIFY_VIRTUAL:              detail = "virtual";   break;
      case GDK_NOTIFY_NONLINEAR:            detail = "nonlinear"; break;
      case GDK_NOTIFY_NONLINEAR_VIRTUAL:    detail = "nolinvirt"; break;
      default:                              detail = "unknown";   break;
      }
      dmsg += string_printf (" %s/%s sub=%p", mode, detail, event->crossing.subwindow);
      break;
    case GDK_MOTION_NOTIFY:
      dmsg += string_printf (" is_hint=%d", event->motion.is_hint);
      break;
    case GDK_EXPOSE:
      {
        GdkRectangle &area = event->expose.area;
        dmsg += string_printf (" (bbox: %d,%d %dx%d)", area.x, area.y, area.width, area.height);
      }
      break;
    case GDK_SCROLL:
      ec1 = (GEnumClass*) g_type_class_ref (GDK_TYPE_SCROLL_DIRECTION);
      detail = g_enum_get_value (ec1, event->scroll.direction)->value_name;
      if (strncmp (detail, "GDK_", 4) == 0)
        detail += 4;
      dmsg += string_printf (" %s", detail);
      g_type_class_unref (ec1);
      break;
    case GDK_VISIBILITY_NOTIFY:
      switch (event->visibility.state)
        {
        case GDK_VISIBILITY_UNOBSCURED:         dmsg += string_printf (" unobscured"); break;
        case GDK_VISIBILITY_FULLY_OBSCURED:     dmsg += string_printf (" fully-obscured"); break;
        case GDK_VISIBILITY_PARTIAL:            dmsg += string_printf (" partial"); break;
        }
      break;
    default: ;
    }
  dmsg += string_printf (" (wwin=%p %d,%d %dx%d)%s", widget->window, wx, wy, ww, wh,
                         econtext.synthesized ? " (synth)" : "");
  DEBUG ("%s", dmsg.c_str());
  g_type_class_unref (eclass);
}

static gboolean
rapicorn_viewp0rt_event (GtkWidget *widget,
                         GdkEvent  *event)
{
  if (!widget->window)
    return FALSE; // protected against events when unrealized, e.g. FOCUS_CHANGE
  RapicornViewp0rt *self = RAPICORN_VIEWP0RT (widget);
  Viewp0rtGtk *viewp0rt = self->viewp0rt;
  bool handled = false;
  int window_height = 0;
  EventContext econtext = rapicorn_viewp0rt_event_context (self, event, &window_height);
  if (Rapicorn::Logging::debug_enabled()) /* debug events */
    debug_dump_event (widget, ".", event, econtext);
  if (!viewp0rt)
    return false;
  /* translate events */
  switch (event->type)
    {
    case GDK_ENTER_NOTIFY:
      viewp0rt->enqueue_locked (create_event_mouse (event->crossing.detail == GDK_NOTIFY_INFERIOR ? MOUSE_MOVE : MOUSE_ENTER, econtext));
      break;
    case GDK_MOTION_NOTIFY:
      viewp0rt->enqueue_locked (create_event_mouse (MOUSE_MOVE, econtext));
      self->last_motion_time = self->last_time;
      /* retrieve and enqueue intermediate moves */
      if (true)
        {
          GdkDevice *device = gdk_display_get_core_pointer (gtk_widget_get_display (widget));
          assert (device->num_axes >= 2);
          GdkTimeCoord **tcoords = NULL;
          gint           n = 0;
          gdk_device_get_history  (device, widget->window, self->last_motion_time, GDK_CURRENT_TIME, &tcoords, &n);
          for (int i = 0; i < n; i++)
            {
              econtext = rapicorn_viewp0rt_event_context (self, event, NULL, tcoords[i]);
              self->last_motion_time = self->last_time;
              DEBUG ("MOTION-HISTORY: time=0x%08x x=%+7.2f y=%+7.2f", tcoords[i]->time, tcoords[i]->axes[0], tcoords[i]->axes[1]);
              viewp0rt->enqueue_locked (create_event_mouse (MOUSE_MOVE, econtext));
            }
          gdk_device_free_history (tcoords, n);
        }
      else /* only rough motion tracking */
        {
          /* request more motion events */
          if (event->any.window == widget->window && event->motion.is_hint)
            gdk_window_get_pointer (event->any.window, NULL, NULL, NULL);
        }
      break;
    case GDK_LEAVE_NOTIFY:
      viewp0rt->enqueue_locked (create_event_mouse (event->crossing.detail == GDK_NOTIFY_INFERIOR ? MOUSE_MOVE : MOUSE_LEAVE, econtext));
      break;
    case GDK_FOCUS_CHANGE:
      viewp0rt->enqueue_locked (create_event_focus (event->focus_change.in ? FOCUS_IN : FOCUS_OUT, econtext));
      handled = TRUE; // prevent Gtk+ from queueing a shallow draw
      break;
    case GDK_KEY_PRESS:
    case GDK_KEY_RELEASE:
      char *key_name;
      key_name = g_strndup (event->key.string, event->key.length);
      /* KeyValue is modelled to match X keysyms */
      // FIXME: handled = root->dispatch_key_event (econtext, event->type == GDK_KEY_PRESS, KeyValue (event->key.keyval), key_name);
      viewp0rt->enqueue_locked (create_event_key (event->type == GDK_KEY_PRESS ? KEY_PRESS : KEY_RELEASE, econtext, KeyValue (event->key.keyval), key_name));
      handled = TRUE;
      g_free (key_name);
      break;
    case GDK_BUTTON_PRESS:
      viewp0rt->enqueue_locked (create_event_button (BUTTON_PRESS, econtext, event->button.button));
      handled = TRUE;
      // FIXME: handled = root->dispatch_button_event (econtext, true, event->button.button);
      break;
    case GDK_2BUTTON_PRESS:
      viewp0rt->enqueue_locked (create_event_button (BUTTON_PRESS, econtext, event->button.button));
      handled = TRUE;
      break;
    case GDK_3BUTTON_PRESS:
      viewp0rt->enqueue_locked (create_event_button (BUTTON_PRESS, econtext, event->button.button));
      handled = TRUE;
      break;
    case GDK_BUTTON_RELEASE:
      viewp0rt->enqueue_locked (create_event_button (BUTTON_RELEASE, econtext, event->button.button));
      handled = TRUE;
      break;
    case GDK_SCROLL:
      if (event->scroll.direction == GDK_SCROLL_UP)
        viewp0rt->enqueue_locked (create_event_scroll (SCROLL_UP, econtext));
      else if (event->scroll.direction == GDK_SCROLL_LEFT)
        viewp0rt->enqueue_locked (create_event_scroll (SCROLL_LEFT, econtext));
      else if (event->scroll.direction == GDK_SCROLL_RIGHT)
        viewp0rt->enqueue_locked (create_event_scroll (SCROLL_RIGHT, econtext));
      else if (event->scroll.direction == GDK_SCROLL_DOWN)
        viewp0rt->enqueue_locked (create_event_scroll (SCROLL_DOWN, econtext));
      handled = TRUE;
      break;
#if GTK_CHECK_VERSION (2, 8, 0)
    case GDK_GRAB_BROKEN:
      viewp0rt->enqueue_locked (create_event_cancellation (econtext));
      break;
#endif
    case GDK_DELETE:
      viewp0rt->enqueue_locked (create_event_win_delete (econtext));
      handled = TRUE;
      break;
    case GDK_DESTROY:
    case GDK_UNMAP:
      rapicorn_viewp0rt_change_visibility (self, GDK_VISIBILITY_FULLY_OBSCURED);
      break;
    case GDK_VISIBILITY_NOTIFY:
      rapicorn_viewp0rt_change_visibility (self, event->visibility.state);
      break;
    case GDK_WINDOW_STATE:
      viewp0rt->m_window_state = get_rapicorn_window_state (event->window_state.new_window_state);
      if ((event->window_state.new_window_state & (GDK_WINDOW_STATE_WITHDRAWN | GDK_WINDOW_STATE_ICONIFIED)) &&
          widget->window && gdk_window_has_ancestor (widget->window, event->window_state.window))
        viewp0rt->enqueue_locked (create_event_cancellation (econtext));
      break;
    case GDK_EXPOSE:
      {
        std::vector<Rect> rectangles;
        if (event->expose.region)
          {
            GdkRectangle *areas;
            gint n_areas;
            gdk_region_get_rectangles (event->expose.region, &areas, &n_areas);
            for (int i = 0; i < n_areas; i++)
              {
                gint realy = window_height - (areas[i].y + areas[i].height);
                rectangles.push_back (Rect (Point (areas[i].x, realy), areas[i].width, areas[i].height));
              }
            g_free (areas);
          }
        else
          {
            GdkRectangle &area = event->expose.area;
            gint realy = window_height - (area.y + area.height);
            rectangles.push_back (Rect (Point (area.x, realy), area.width, area.height));
          }
        if (!viewp0rt->m_ignore_exposes)
          viewp0rt->enqueue_locked (create_event_win_draw (econtext, 0, rectangles));
      }
      break;
    default:
    case GDK_NOTHING:
    case GDK_NO_EXPOSE:
    case GDK_PROPERTY_NOTIFY:   case GDK_CLIENT_EVENT:
    case GDK_CONFIGURE:         case GDK_MAP:
    case GDK_SELECTION_CLEAR:   case GDK_SELECTION_REQUEST:     case GDK_SELECTION_NOTIFY:
    case GDK_OWNER_CHANGE:
    case GDK_PROXIMITY_IN:      case GDK_PROXIMITY_OUT:
      /* nothing to forward */
      break;
    }
  return handled;
}

static gboolean
rapicorn_viewp0rt_ancestor_event (GtkWidget *ancestor,
                                  GdkEvent  *event,
                                  GtkWidget *widget)
{
  RapicornViewp0rt *self = RAPICORN_VIEWP0RT (widget);
  Viewp0rtGtk *viewp0rt = self->viewp0rt;
  EventContext econtext = rapicorn_viewp0rt_event_context (self, event);
  if (!viewp0rt)
    return false;
  if (Rapicorn::Logging::debug_enabled()) /* debug events */
    debug_dump_event (widget, "+", event, econtext);
  bool handled = false;
  switch (event->type)
    {
    case GDK_VISIBILITY_NOTIFY:
      if (event->visibility.state == GDK_VISIBILITY_FULLY_OBSCURED)
        rapicorn_viewp0rt_change_visibility (self, GDK_VISIBILITY_FULLY_OBSCURED);
      break;
    case GDK_WINDOW_STATE:
      viewp0rt->m_window_state = get_rapicorn_window_state (event->window_state.new_window_state);
      if ((event->window_state.new_window_state & (GDK_WINDOW_STATE_WITHDRAWN | GDK_WINDOW_STATE_ICONIFIED)) &&
          widget->window && gdk_window_has_ancestor (widget->window, event->window_state.window))
        viewp0rt->enqueue_locked (create_event_cancellation (econtext));
      break;
#if GTK_CHECK_VERSION (2, 8, 0)
    case GDK_GRAB_BROKEN:
      viewp0rt->enqueue_locked (create_event_cancellation (econtext));
      break;
#endif
    case GDK_DELETE:
      if (ancestor == (GtkWidget*) rapicorn_viewp0rt_get_my_window (self))
        {
          viewp0rt->enqueue_locked (create_event_win_delete (econtext));
          handled = true;
        }
      break;
    case GDK_DESTROY:
    case GDK_UNMAP:
      rapicorn_viewp0rt_change_visibility (self, GDK_VISIBILITY_FULLY_OBSCURED);
      break;
    default: break;
    }
  return handled;
}

static void
rapicorn_viewp0rt_dispose (GObject *object)
{
  RapicornViewp0rt *self = RAPICORN_VIEWP0RT (object);
  self->viewp0rt = NULL;
  G_OBJECT_CLASS (rapicorn_viewp0rt_parent_class)->dispose (object); // chain
}

static void
rapicorn_viewp0rt_class_init (RapicornViewp0rtClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gobject_class->dispose = rapicorn_viewp0rt_dispose;

  widget_class->size_request = rapicorn_viewp0rt_size_request;
  widget_class->size_allocate = rapicorn_viewp0rt_size_allocate;
  widget_class->realize = rapicorn_viewp0rt_realize;
  widget_class->map = rapicorn_viewp0rt_map;
  widget_class->unmap = rapicorn_viewp0rt_unmap;
  widget_class->unrealize = rapicorn_viewp0rt_unrealize;
  widget_class->event = rapicorn_viewp0rt_event;
}

/* --- RapicornGtkThread --- */
class RapicornGtkThread : public virtual Thread {
public:
  RapicornGtkThread() :
    Thread ("RapicornGtkThread")
  {}
  virtual void
  run ()
  {
    ScopedLock<RapicronGdkSyncLock> locker (GTK_GDK_THREAD_SYNC);
    gtk_main(); /* does GDK_THREADS_LEAVE(); ... GDK_THREADS_ENTER(); */
  }
};

} // Anon

namespace Rapicorn {

static Thread *gtkthread = NULL;

void
rapicorn_init_with_gtk_thread (int        *argcp,
                               char     ***argvp,
                               const char *app_name)
{
  /* non-GTK initialization */
  rapicorn_init (argcp, *argvp, app_name);
  g_type_init();
  /* setup GDK_THREADS_ENTER/GDK_THREADS_LEAVE */
  gdk_threads_init();
  ScopedLock<RapicronGdkSyncLock> locker (GTK_GDK_THREAD_SYNC);
  /* GTK intilization */
  gtk_init (argcp, argvp);
  gtkthread = new RapicornGtkThread ();
  ref_sink (gtkthread);
  gtkthread->start();
}

bool
rapicorn_init_with_foreign_gtk (int        *argcp,
                                char     ***argvp,
                                const char *app_name,
                                bool        auto_quit_gtk)
{
  /* non-GTK initialization */
  rapicorn_init (argcp, *argvp, app_name);
  g_type_init();
  /* setup GDK_THREADS_ENTER/GDK_THREADS_LEAVE */
  gdk_threads_init();
  ScopedLock<RapicronGdkSyncLock> locker (GTK_GDK_THREAD_SYNC);
  rapicorn_viewp0rt__auto_quit_gtk = auto_quit_gtk;
  /* GTK intilization */
  return gtk_init_check (argcp, argvp); // allow $DISPLAY initialisation to fail
}

void
rapicorn_gtk_threads_enter ()
{
  GTK_GDK_THREAD_SYNC.lock();
}

void
rapicorn_gtk_threads_leave ()
{
  GTK_GDK_THREAD_SYNC.unlock();
}

} // Rapicorn