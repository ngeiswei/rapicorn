// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
#ifndef __RAPICORN_WINDOW_HH__
#define __RAPICORN_WINDOW_HH__

#include <ui/viewport.hh>

namespace Rapicorn {

// == WindowImpl ==
class WindowImpl;
typedef std::shared_ptr<WindowImpl> WindowImplP;
typedef std::weak_ptr<WindowImpl>   WindowImplW;

class WindowImpl : public virtual ViewportImpl, public virtual WindowIface {
protected:
  virtual void                 construct            () override;
  virtual void                 dispose              () override;
  virtual void                 set_parent           (ContainerImpl *parent) override;
  virtual const AncestryCache* fetch_ancestry_cache () override;
public:
  explicit              WindowImpl            ();
  virtual              ~WindowImpl            ();
  virtual WindowImpl*   as_window_impl        () override       { return this; }
  static  void          forcefully_close_all  ();
public: // tailored member access for WidgetImpl
  /// @cond INTERNAL
  class WidgetImplFriend {
    friend                class WidgetImpl; // only friends can access private class members
    static DisplayWindow* display_window     (WindowImpl &window)                     { return window.display_window(); }
    static void           set_focus          (WindowImpl &window, WidgetImpl *widget) { window.set_focus (widget); }
    static bool           widget_is_anchored (WidgetImpl &widget);
  };
  /// @endcond
};

} // Rapicorn

#endif  /* __RAPICORN_WINDOW_HH__ */
