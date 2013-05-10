// Licensed GNU LGPL v3 or later: http://www.gnu.org/licenses/lgpl.html
#include <ui/utilities.hh>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/XShm.h>
#include <sys/shm.h>

#define XDEBUG(...)     RAPICORN_KEY_DEBUG ("X11", __VA_ARGS__)

namespace { // Anon
using namespace Rapicorn;

// == X11 Error Handling ==
static XErrorHandler xlib_error_handler = NULL;
static XErrorEvent *xlib_error_trap = NULL;

static int
x11_error (Display *error_display, XErrorEvent *error_event)
{
  if (xlib_error_trap)
    {
      *xlib_error_trap = *error_event;
      return 0;
    }
  size_t addr;
  const vector<String> syms = pretty_backtrace (0, &addr);
  String btmsg = string_format ("%s:%d: Backtrace at 0x%08zx (stackframe at 0x%08zx):\n", __FILE__, __LINE__,
                                addr, size_t (__builtin_frame_address (0)) /*size_t (&addr)*/);
  for (size_t i = 0; i < syms.size(); i++)
    btmsg += string_format ("  %s\n", syms[i].c_str());
  printerr ("X11: received an XErrorEvent ($RAPICORN_DEBUG=%s), aborting...\n%s",
            CQUOTE (dbe_x11sync.key + String (dbe_x11sync ? "=1" : "=0")), btmsg.c_str());
  atexit (abort); // prevents other atexit() handlers from complaining about improper shutdown
  xlib_error_handler (error_display, error_event);
  abort(); // usually _XPrintDefaultError already aborted
  return -1;
}

static bool
x11_trap_errors (XErrorEvent *trapped_event)
{
  assert_return (xlib_error_trap == NULL, false);
  xlib_error_trap = trapped_event;
  trapped_event->error_code = 0;
  return true;
}

static int
x11_untrap_errors()
{
  assert_return (xlib_error_trap != NULL, -1);
  const int error_code = xlib_error_trap->error_code;
  xlib_error_trap = NULL;
  return error_code;
}

static bool
x11_check_shared_image (Display *display, Visual *visual, int depth)
{
  bool has_shared_mem = 0;
  XShmSegmentInfo shminfo = { 0 /*shmseg*/, -1 /*shmid*/, (char*) -1 /*shmaddr*/, True /*readOnly*/ };
  XImage *ximage = XShmCreateImage (display, visual, depth, ZPixmap, NULL, &shminfo, 1, 1);
  if (ximage)
    {
      shminfo.shmid = shmget (IPC_PRIVATE, ximage->bytes_per_line * ximage->height, IPC_CREAT | 0600);
      if (shminfo.shmid != -1)
        {
          shminfo.shmaddr = (char*) shmat (shminfo.shmid, NULL, SHM_RDONLY);
          if (ptrdiff_t (shminfo.shmaddr) != -1)
            {
              XErrorEvent dummy = { 0, };
              x11_trap_errors (&dummy);
              Bool result = XShmAttach (display, &shminfo);
              XSync (display, False); // forces error delivery
              if (!x11_untrap_errors())
                {
                  // if we got here uccessfully, shared memory works
                  has_shared_mem = result != 0;
                }
            }
          shmctl (shminfo.shmid, IPC_RMID, NULL); // delete the shm segment upon last detaching process
          // cleanup
          if (has_shared_mem)
            XShmDetach (display, &shminfo);
          if (ptrdiff_t (shminfo.shmaddr) != -1)
            shmdt (shminfo.shmaddr);
        }
      XDestroyImage (ximage);
    }
  return has_shared_mem;
}

static __attribute__ ((unused)) const char*
window_state (int wm_state)
{
  switch (wm_state)
    {
    case WithdrawnState:        return "WithdrawnState";
    case NormalState:           return "NormalState";
    case IconicState:           return "IconicState";
    default:                    return "Unknown";
    }
}

static const char*
notify_mode (int notify_type)
{
  switch (notify_type)
    {
    case NotifyNormal:          return "Normal";
    case NotifyGrab:            return "Grab";
    case NotifyUngrab:          return "Ungrab";
    case NotifyWhileGrabbed:    return "WhileGrabbed";
    default:                    return "Unknown";
    }
}

static const char*
notify_detail (int notify_type)
{
  switch (notify_type)
    {
    case NotifyAncestor:         return "Ancestor";
    case NotifyVirtual:          return "Virtual";
    case NotifyInferior:         return "Inferior";
    case NotifyNonlinear:        return "Nonlinear";
    case NotifyNonlinearVirtual: return "NonlinearVirtual";
    case NotifyPointer:          return "NotifyPointer";
    case NotifyPointerRoot:      return "NotifyPointerRoot";
    case NotifyDetailNone:       return "NotifyDetailNone";
    default:                     return "Unknown";
    }
}

static const char*
visibility_state (int visibility_type)
{
  switch (visibility_type)
    {
    case VisibilityUnobscured:          return "VisibilityUnobscured";
    case VisibilityPartiallyObscured:   return "VisibilityPartiallyObscured";
    case VisibilityFullyObscured:       return "VisibilityFullyObscured";
    default:                            return "Unknown";
    }
}

static void
load_atom_cache (Display *display)
{
  const char *cached_atoms[] = {
    "ATOM_PAIR", "CLIPBOARD_MANAGER", "COMPOUND_TEXT", "DESKTOP_STARTUP_ID", "DISPLAY", "ENLIGHTENMENT_DESKTOP",
    "GDK_SELECTION", "_GTK_HIDE_TITLEBAR_WHEN_MAXIMIZED", "_GTK_THEME_VARIANT", "MANAGER", "_KDE_NET_WM_FRAME_STRUT",
    "_MOTIF_DRAG_AND_DROP_MESSAGE", "_MOTIF_DRAG_INITIATOR_INFO", "_MOTIF_DRAG_RECEIVER_INFO", "_MOTIF_DRAG_TARGETS", "_MOTIF_DRAG_WINDOW",
    "_NET_ACTIVE_WINDOW", "_NET_CLIENT_LIST_STACKING", "_NET_CURRENT_DESKTOP", "_NET_FRAME_EXTENTS", "_NET_STARTUP_ID",
    "_NET_STARTUP_INFO", "_NET_STARTUP_INFO_BEGIN", "_NET_SUPPORTED", "_NET_SUPPORTING_WM_CHECK", "_NET_VIRTUAL_ROOTS",
    "_NET_WM_CM_S0", "_NET_WM_DESKTOP", "_NET_WM_ICON", "_NET_WM_ICON_NAME", "_NET_WM_MOVERESIZE", "_NET_WM_NAME", "_NET_WM_PID",
    "_NET_WM_PING", "_NET_WM_STATE", "_NET_WM_STATE_ABOVE", "_NET_WM_STATE_BELOW", "_NET_WM_STATE_DEMANDS_ATTENTION",
    "_NET_WM_STATE_FOCUSED", "_NET_WM_STATE_FULLSCREEN", "_NET_WM_STATE_HIDDEN", "_NET_WM_STATE_MAXIMIZED_HORZ",
    "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MODAL", "_NET_WM_STATE_SHADED", "_NET_WM_STATE_SKIP_PAGER",
    "_NET_WM_STATE_SKIP_TASKBAR", "_NET_WM_STATE_STICKY", "_NET_WM_SYNC_REQUEST", "_NET_WM_SYNC_REQUEST_COUNTER",
    "_NET_WM_USER_TIME", "_NET_WM_USER_TIME_WINDOW", "_NET_WM_WINDOW_OPACITY", "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_COMBO",
    "_NET_WM_WINDOW_TYPE_DESKTOP", "_NET_WM_WINDOW_TYPE_DIALOG", "_NET_WM_WINDOW_TYPE_DND", "_NET_WM_WINDOW_TYPE_DOCK",
    "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", "_NET_WM_WINDOW_TYPE_MENU", "_NET_WM_WINDOW_TYPE_NORMAL", "_NET_WM_WINDOW_TYPE_NOTIFICATION",
    "_NET_WM_WINDOW_TYPE_POPUP_MENU", "_NET_WM_WINDOW_TYPE_SPLASH", "_NET_WM_WINDOW_TYPE_TOOLBAR", "_NET_WM_WINDOW_TYPE_TOOLTIP",
    "_NET_WM_WINDOW_TYPE_UTILITY", "_NET_WORKAREA", "SAVE_TARGETS", "SM_CLIENT_ID", "__SWM_VROOT", "UTF8_STRING",
    "WM_CLIENT_LEADER", "WM_DELETE_WINDOW", "WM_ICON_NAME", "WM_LOCALE_NAME", "WM_NAME", "WM_PROTOCOLS", "WM_STATE", "WM_TAKE_FOCUS",
    "WM_WINDOW_ROLE", "_XSETTINGS_SETTINGS",
  };
  const bool force_create = false;
  Atom atoms[ARRAY_SIZE (cached_atoms)];
  XInternAtoms (display, const_cast<char**> (cached_atoms), ARRAY_SIZE (cached_atoms), !force_create, atoms);
}

static Atom
x11_atom (Display *display, const String &text)
{
  return XInternAtom (display, text.c_str(), False);
}

static String
x11_atom_name (Display *display, Atom atom)
{
  char *res = XGetAtomName (display, atom);
  if (res)
    {
      String result = res;
      XFree (res);
      return result;
    }
  return "";
}

template<class Data> static vector<Data>
x11_get_property_data (Display *display, Window window, Atom property_atom, Atom *property_type = NULL)
{
  XErrorEvent dummy = { 0, };
  x11_trap_errors (&dummy);
  int format_returned = 0;
  Atom type_returned = 0;
  unsigned long nwidgets_return = 0, bytes_after_return = 0;
  uint8 *prop_data = NULL;
  int abort = XGetWindowProperty (display, window, property_atom, 0, 8 * 1024, False,
                                  AnyPropertyType, &type_returned, &format_returned, &nwidgets_return,
                                  &bytes_after_return, &prop_data) != Success;
  if (x11_untrap_errors() || type_returned == None)
    abort++;
  if (!abort && bytes_after_return)
    {
      XDEBUG ("XGetWindowProperty(%s): property size exceeds buffer by %lu bytes", CQUOTE (x11_atom_name (display, property_atom)), bytes_after_return);
      abort++;
    }
  if (!abort && sizeof (Data) * 8 != format_returned)
    {
      XDEBUG ("XGetWindowProperty(%s): property format mismatch: expected=%zu returned=%u", CQUOTE (x11_atom_name (display, property_atom)), sizeof (Data) * 8, format_returned);
      abort++;
    }
  vector<Data> datav;
  if (!abort && prop_data && nwidgets_return && format_returned)
    {
      if (format_returned == 32)
        {
          const unsigned long *pulong = (const unsigned long*) prop_data;
          for (uint i = 0; i < nwidgets_return; i++)
            datav.push_back (pulong[i]);
        }
      else if (format_returned == 16)
        {
          const uint16 *puint16 = (const uint16*) prop_data;
          for (uint i = 0; i < nwidgets_return; i++)
            datav.push_back (puint16[i]);
        }
      else if (format_returned == 8)
        {
          const uint8 *puint8 = (const uint8*) prop_data;
          for (uint i = 0; i < nwidgets_return; i++)
            datav.push_back (puint8[i]);
        }
      else
        XDEBUG ("XGetWindowProperty(%s): unknown property data format with %d bits", CQUOTE (x11_atom_name (display, property_atom)), format_returned);
    }
  if (prop_data)
    XFree (prop_data);
  if (property_type)
    *property_type = type_returned;
  return datav;
}

static String
x11_get_string_property (Display *display, Window window, Atom property_atom, Atom *property_type = NULL)
{
  Atom ptype = 0;
  vector<char> datav = x11_get_property_data<char> (display, window, property_atom, &ptype);
  String rstring;
  if (datav.size() && (ptype == XA_STRING || ptype == x11_atom (display, "COMPOUND_TEXT")))
    {
      XTextProperty xtp;
      xtp.format = 8;
      xtp.nitems = datav.size();
      xtp.value = (uint8*) datav.data();
      xtp.encoding = ptype;
      char **tlist = NULL;
      int count = 0, res = Xutf8TextPropertyToTextList (display, &xtp, &tlist, &count);
      if (res != XNoMemory && res != XLocaleNotSupported && res != XConverterNotFound && count && tlist && tlist[0])
        rstring = String (tlist[0]);
      if (tlist)
        XFreeStringList (tlist);
    }
  else if (datav.size() && ptype == x11_atom (display, "UTF8_STRING"))
    rstring = String (datav.data(), datav.size());
  else
    XDEBUG ("XGetWindowProperty(%s): unknown string property format: %s", CQUOTE (x11_atom_name (display, property_atom)), x11_atom_name (display, ptype).c_str());
  if (property_type)
    *property_type = ptype;
  return rstring;
}

enum XPEmpty { KEEP_EMPTY, DELETE_EMPTY };

static bool
set_text_property (Display *display, Window window, Atom property_atom, XICCEncodingStyle ecstyle,
                   const String &value, XPEmpty when_empty = KEEP_EMPTY)
{
  bool success = true;
  if (when_empty == DELETE_EMPTY && value.empty())
    XDeleteProperty (display, window, property_atom);
  else if (ecstyle == XUTF8StringStyle)
    XChangeProperty (display, window, property_atom, x11_atom (display, "UTF8_STRING"), 8,
                     PropModeReplace, (uint8*) value.c_str(), value.size());
  else
    {
      char *text = const_cast<char*> (value.c_str());
      XTextProperty xtp = { 0, };
      const int result = Xutf8TextListToTextProperty (display, &text, 1, ecstyle, &xtp);
      if (0)
        printerr ("XUTF8CONVERT: target=%s len=%zd result=%d: %s -> %s\n", x11_atom_name (display, xtp.encoding).c_str(), value.size(), result, text, xtp.value);
      if (result >= 0 && xtp.nitems && xtp.value)
        XChangeProperty (display, window, property_atom, xtp.encoding, xtp.format,
                         PropModeReplace, xtp.value, xtp.nitems);
      else
        success = false;
      if (xtp.value)
        XFree (xtp.value);
    }
  return success;
}

enum Mwm {
  MWM_UNSPECIFIED = -1, // leaves FUNC/DECOR unset
  FUNC_ALL = 0x01,      // combining ALL with other flags has adverse effects with mwm
  FUNC_RESIZE = 0x02, FUNC_MOVE = 0x04, FUNC_MINIMIZE = 0x08, FUNC_MAXIMIZE = 0x10, FUNC_CLOSE = 0x20,
  DECOR_ALL = 0x01,     // combining ALL with other flags has adverse effects with mwm
  DECOR_BORDER = 0x02, DECOR_RESIZEH = 0x04, DECOR_TITLE = 0x08, DECOR_MENU = 0x10, DECOR_MINIMIZE = 0x20, DECOR_MAXIMIZE = 0x40,
  DECOR_CLOSE = 0x80,   // CLOSE is fvwm specific
};
struct MwmHints { unsigned long flags, functions, decorations, input_mode, status; };

static bool
get_mwm_hints (Display *display, Window window, Mwm *funcs, Mwm *deco)
{
  const Atom xa_mwm_hints = x11_atom (display, "_MOTIF_WM_HINTS");
  unsigned long nitems = 0, bytes_left = 0;
  uint8 *data = NULL;
  int format = 0;
  Atom type = 0;
  XGetWindowProperty (display, window, xa_mwm_hints, 0, 5, False, xa_mwm_hints, &type, &format, &nitems, &bytes_left, &data);
  const MwmHints dummy = { 0, }, &mwm_hints = data ? *(MwmHints*) data : dummy;
  if (data)
    {
      if (funcs)
        *funcs = Mwm (mwm_hints.flags & 1 ? mwm_hints.functions : 0);
      if (deco)
        *deco = Mwm (mwm_hints.flags & 2 ? mwm_hints.decorations : 0);
      XFree (data);
    }
  return data != NULL;
}

static void
adjust_mwm_hints (Display *display, Window window, Mwm funcs, Mwm deco)
{
  const Atom xa_mwm_hints = x11_atom (display, "_MOTIF_WM_HINTS");
  unsigned long nitems = 0, bytes_left = 0;
  uint8 *data = NULL;
  int format = 0;
  Atom type = 0;
  XGetWindowProperty (display, window, xa_mwm_hints, 0, 5, False, xa_mwm_hints, &type, &format, &nitems, &bytes_left, &data);
  MwmHints dummy = { 0, }, &mwm_hints = data ? *(MwmHints*) data : dummy;
  const int mwm_funcs = int (funcs), mwm_deco = int (deco);
  if (mwm_funcs >= 0)
    {
      mwm_hints.flags |= 1; // MWM-FUNCTIONS
      mwm_hints.functions = mwm_funcs;
    }
  if (mwm_deco >= 0)
    {
      mwm_hints.flags |= 2; // MWM-DECORATIONS
      mwm_hints.decorations = mwm_deco;
    }
  XChangeProperty (display, window, xa_mwm_hints, xa_mwm_hints, 32, PropModeReplace, (uint8*) &mwm_hints, 5);
  if (data)
    XFree (data);
}

static bool
_window_net_frame_extents (Display *display, Window window, int *dx, int *dy, int *dw, int *dh, int *fx, int *fy, int *fw, int *fh)
{
  bool success = false;
  Atom type = 0;
  int format = 0;
  unsigned long nitems = 0, remains = 0;
  uint8 *data = NULL;
  if (XGetWindowProperty (display, window, x11_atom (display, "_NET_FRAME_EXTENTS"), 0, 4, False,
                          XA_CARDINAL, &type, &format, &nitems, &remains, &data) == Success &&
      type == XA_CARDINAL && format == 32 && nitems == 4 && data)
    {
      struct FrameExtents { unsigned long left, right, top, bottom; };
      FrameExtents &frame = *(FrameExtents*) data;
      Window root = 0, child = 0;
      int gx = 0, gy = 0, tx = 0, ty = 0;
      uint gw = 0, gh = 0, gb = 0, gd = 0;
      if (XGetGeometry (display, window, &root, &gx, &gy, &gw, &gh, &gb, &gd) != 0 &&
          XTranslateCoordinates (display, window, root, 0, 0, &tx, &ty, &child))
        {
          *dx = tx; *dy = ty; *dw = gw; *dh = gh;
          *fx = tx - frame.left;
          *fy = ty - frame.top;
          *fw = gw + frame.left + frame.right;
          *fh = gh + frame.top + frame.bottom;
          success = true;
        }
    }
  if (data)
    XFree (data);
  return success;
}

static bool
_window_frame_origin (Display *display, Window window, int *dx, int *dy, int *fx, int *fy)
{
  const Atom ENLIGHTENMENT_DESKTOP = x11_atom (display, "ENLIGHTENMENT_DESKTOP");
  Window root = 0, *children = NULL, ancestor, vparent = window;
  uint nchildren = 0;
  do
    {
      ancestor = vparent;
      if (!XQueryTree (display, ancestor, &root, &vparent /*parent*/, &children, &nchildren))
        return false;
      if (children)
        XFree (children);
      Atom type = 0; int format = 0; unsigned long nitems = 0, remains = 0; uint8 *data = NULL;
      if (XGetWindowProperty (display, vparent, ENLIGHTENMENT_DESKTOP, 0, 1, False,
                              XA_CARDINAL, &type, &format, &nitems, &remains, &data) == Success && type == XA_CARDINAL)
        {
          if (data)
            XFree (data);
          break;
        }
    }
  while (vparent != root);
  Window child = 0;
  if (vparent != window && // vparent is virtual-root, ancestor is its sibling and an ancestor of window
      XTranslateCoordinates (display, ancestor, vparent, 0, 0, fx, fy, &child) &&
      XTranslateCoordinates (display, window, root, 0, 0, dx, dy, &child))
    return true;
  return false;
}

static bool
window_deco_origin (Display *display, Window window, int *dx, int *dy, int *fx, int *fy)
{
  XErrorEvent errevent;
  int dw, dh, fw, fh;
  *dx = 0; *dy = 0; *fx = 0; *fy = 0;
  x11_trap_errors (&errevent);  // guard against destroyed windows
  bool good = _window_net_frame_extents (display, window, dx, dy, &dw, &dh, fx, fy, &fw, &fh);
  good = good || _window_frame_origin (display, window, dx, dy, fx, fy);
  x11_untrap_errors();
  return good; // tell solid from falback info
}

static String
x11_input_method (Display *display, XIM *ximp, XIMStyle *bestp, const char *locale_modifiers = "")
{
  *ximp = NULL;
  *bestp = 0;
  if (!XSupportsLocale() ||                     // checks if locale is supported
      !XSetLocaleModifiers (locale_modifiers))  // set X11 locale for XIM
    return string_format ("locale not supported: %s", setlocale (LC_ALL, NULL));
  XIM xim = XOpenIM (display, NULL, NULL, NULL);
  if (!xim)
    return "failed to find input method";
  XIMStyles *xim_styles = NULL;
  if (XGetIMValues (xim, XNQueryInputStyle, &xim_styles, NULL) == NULL && xim_styles && xim_styles->count_styles)
    {
      XIMStyle xs[4] { 0, };
      for (uint i = 0; i < xim_styles->count_styles; i++)
        {
          const XIMStyle s = xim_styles->supported_styles[i];
          if      (s & XIMPreeditNothing && s & XIMStatusNothing)
            xs[0] = s;
          else if (s & XIMPreeditNothing && s & XIMStatusNone)
            xs[1] = s;
          else if (s & XIMPreeditNone    && s & XIMStatusNothing)
            xs[2] = s;
          else if (s & XIMPreeditNone    && s & XIMStatusNone)
            xs[3] = s;
        }
      for (uint i = 0; i < ARRAY_SIZE (xs); i++)
        if (xs[i])
          {
            *bestp = xs[i];
            break;
          }
    }
  if (xim_styles)
    XFree (xim_styles);
  if (!*bestp)
    {
      XCloseIM (xim);
      return "failed to find input style";
    }
  *ximp = xim;
  return "";
}

static String
x11_input_context (Display *display, Window window, unsigned long events, XIM xim, XIMStyle imstyle, XIC *xicp)
{
  *xicp = 0;
  XIC xic = xim ? XCreateIC (xim, XNInputStyle, imstyle, XNClientWindow, window, XNFocusWindow, window, nullptr) : NULL;
  if (!xic)
    return "failed to create input context";
  // extend window event mask as needed
  unsigned long imevents = 0;
  if (XGetICValues (xic, XNFilterEvents, &imevents, nullptr) == NULL && imevents != (events & imevents))
    XSelectInput (display, window, events | imevents);
  *xicp = xic;
  return "";
}

} // Anon
