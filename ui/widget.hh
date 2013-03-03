// Licensed GNU LGPL v3 or later: http://www.gnu.org/licenses/lgpl.html
#include <ui/serverapi.hh>
// serverapi.hh includes widget.hh after WidgetIface declaration

#ifndef __RAPICORN_WIDGET_HH_
#define __RAPICORN_WIDGET_HH_

#include <ui/events.hh>
#include <ui/region.hh>
#include <ui/commands.hh>
#include <ui/heritage.hh>

namespace Rapicorn {

/* --- Widget structures and forward decls --- */
typedef Rect Allocation;
class WidgetImpl;
class AnchorInfo;
class SizeGroup;
class Adjustment;
class ContainerImpl;
class ResizeContainerImpl;
class WindowImpl;
class ViewportImpl;
namespace Selector { class Selob; }

/* --- event handler --- */
class EventHandler : public virtual ReferenceCountable {
  typedef Aida::Signal<bool (const Event&), Aida::CollectorWhile0<bool>> EventSignal;
protected:
  virtual bool  handle_event    (const Event    &event);
public:
  explicit      EventHandler    ();
  EventSignal   sig_event;
  typedef enum {
    RESET_ALL
  } ResetMode;
  virtual void  reset           (ResetMode       mode = RESET_ALL) = 0;
};

/// WidgetImpl is the base type for all UI element implementations and implements the Widget interface.
/// More details about widgets are covered in @ref Widget.
class WidgetImpl : public virtual WidgetIface, public virtual DataListContainer {
  friend                      class ClassDoctor;
  friend                      class ContainerImpl;
  friend                      class SizeGroup;
  uint64                      flags_;  // inlined for fast access
  ContainerImpl              *parent_; // inlined for fast access
  const AnchorInfo           *ainfo_;
  Heritage                   *heritage_;
  FactoryContext             *factory_context_;
  Allocation                  allocation_;
  Requisition                 requisition_;
  Requisition                 inner_size_request (); // ungrouped size requisition
  void                        propagate_state    (bool notify_changed);
  ContainerImpl**             _parent_loc        () { return &parent_; }
  void                        propagate_heritage ();
  void                        heritage           (Heritage  *heritage);
  void                        expose_internal    (const Region &region);
protected:
  const AnchorInfo*           force_anchor_info  () const;
  virtual void                constructed        ();
  /* flag handling */
  bool                        change_flags_silently (uint64 mask, bool on);
  enum {
    ANCHORED                  = 1 <<  0,
    VISIBLE                   = 1 <<  1,
    SENSITIVE                 = 1 <<  2,
    UNVIEWABLE                = 1 <<  3,
    PARENT_SENSITIVE          = 1 <<  4,
    PARENT_UNVIEWABLE         = 1 <<  5,
    PRELIGHT                  = 1 <<  6,
    IMPRESSED                 = 1 <<  7,
    HAS_DEFAULT               = 1 <<  8,
    FOCUS_CHAIN               = 1 <<  9,
    HSHRINK                   = 1 << 10,
    VSHRINK                   = 1 << 11,
    HEXPAND                   = 1 << 12,
    VEXPAND                   = 1 << 13,
    HSPREAD                   = 1 << 14,
    VSPREAD                   = 1 << 15,
    HSPREAD_CONTAINER         = 1 << 16,
    VSPREAD_CONTAINER         = 1 << 17,
    INVALID_REQUISITION       = 1 << 18,
    INVALID_ALLOCATION        = 1 << 19,
    INVALID_CONTENT           = 1 << 20,
  };
  void                        set_flag          (uint64 flag, bool on = true);
  void                        unset_flag        (uint64 flag)   { set_flag (flag, false); }
  virtual Selector::Selob*    pseudo_selector   (Selector::Selob &selob, const String &ident, const String &arg, String &error) { return NULL; }
  // resizing, requisition and allocation
  virtual void                size_request      (Requisition &requisition) = 0;
  virtual void                size_allocate     (Allocation   area, bool changed) = 0;
  virtual void                invalidate_parent ();
  void                        clip_area         (const Allocation *clip);
  bool                        tune_requisition  (Requisition  requisition);
  bool                        tune_requisition  (double       new_width,
                                                 double       new_height);
  /* signal methods */
  virtual void                do_invalidate     ();
  virtual void                do_changed        ();
  /* idlers & timers */
  uint                        exec_fast_repeater   (const EventLoop::BoolSlot &sl);
  uint                        exec_slow_repeater   (const EventLoop::BoolSlot &sl);
  uint                        exec_key_repeater    (const EventLoop::BoolSlot &sl);
  bool                        remove_exec          (uint            exec_id);
  bool                        clear_exec           (uint           *exec_id);
  virtual void                visual_update        ();
  /* misc */
  virtual                     ~WidgetImpl       ();
  virtual void                finalize          ();
  virtual void                set_parent        (ContainerImpl *parent);
  virtual void                hierarchy_changed (WidgetImpl *old_toplevel);
  virtual bool                activate_widget   ();
  virtual bool                custom_command    (const String       &command_name,
                                                 const StringSeq    &command_args);
  void                        anchored          (bool b) { set_flag (ANCHORED, b); }
  void                        notify_key_error  ();
public:
  explicit                    WidgetImpl        ();
  virtual WindowImpl*         as_window_impl    ()              { return NULL; }
  virtual ContainerImpl*      as_container_impl ()              { return NULL; }
  bool                        test_all_flags    (uint64 mask) const { return (flags_ & mask) == mask; }
  bool                        test_any_flag     (uint64 mask) const { return (flags_ & mask) != 0; }
  bool                        anchored          () const { return test_all_flags (ANCHORED); }
  virtual bool                visible           () const { return test_all_flags (VISIBLE); }
  virtual void                visible           (bool b) { set_flag (VISIBLE, b); }
  bool                        ancestry_visible  () const; ///< Check if ancestry is fully visible.
  virtual bool                viewable          () const; // visible() && !UNVIEWABLE && !PARENT_UNVIEWABLE
  bool                        drawable          () const; // viewable() && clipped_allocation > 0
  virtual bool                sensitive         () const { return test_all_flags (SENSITIVE | PARENT_SENSITIVE); }
  virtual void                sensitive         (bool b) { set_flag (SENSITIVE, b); }
  bool                        insensitive       () const { return !sensitive(); }
  void                        insensitive       (bool b) { sensitive (!b); }
  bool                        key_sensitive     () const;
  bool                        pointer_sensitive () const;
  bool                        prelight          () const { return test_any_flag (PRELIGHT); }
  virtual void                prelight          (bool b) { set_flag (PRELIGHT, b); }
  bool                        ancestry_prelight () const; ///< Check if ancestry contains prelight().
  bool                        impressed         () const { return test_any_flag (IMPRESSED); }
  virtual void                impressed         (bool b) { set_flag (IMPRESSED, b); }
  bool                        ancestry_impressed() const; ///< Check if ancestry contains impressed().
  bool                        has_default       () const { return test_any_flag (HAS_DEFAULT); }
  bool                        grab_default      () const;
  virtual bool                can_focus         () const;
  bool                        has_focus         () const;
  bool                        grab_focus        ();
  void                        unset_focus       ();
  virtual bool                move_focus        (FocusDirType fdir);
  virtual bool                activate          ();
  virtual bool                hexpand           () const { return test_any_flag (HEXPAND | HSPREAD | HSPREAD_CONTAINER); }
  virtual void                hexpand           (bool b) { set_flag (HEXPAND, b); }
  virtual bool                vexpand           () const { return test_any_flag (VEXPAND | VSPREAD | VSPREAD_CONTAINER); }
  virtual void                vexpand           (bool b) { set_flag (VEXPAND, b); }
  virtual bool                hspread           () const { return test_any_flag (HSPREAD | HSPREAD_CONTAINER); }
  virtual void                hspread           (bool b) { set_flag (HSPREAD, b); }
  virtual bool                vspread           () const { return test_any_flag (VSPREAD | VSPREAD_CONTAINER); }
  virtual void                vspread           (bool b) { set_flag (VSPREAD, b); }
  virtual bool                hshrink           () const { return test_any_flag (HSHRINK); }
  virtual void                hshrink           (bool b) { set_flag (HSHRINK, b); }
  virtual bool                vshrink           () const { return test_any_flag (VSHRINK); }
  virtual void                vshrink           (bool b) { set_flag (VSHRINK, b); }
  virtual String              name              () const;
  virtual void                name              (const String &str);
  FactoryContext*             factory_context   () const;
  void                        factory_context   (FactoryContext *fc);
  UserSource                  user_source       () const;
  ColorSchemeType             color_scheme      () const;
  void                        color_scheme      (ColorSchemeType cst);
  /* override requisition */
  double                      width             () const;
  void                        width             (double w);
  double                      height            () const;
  void                        height            (double h);
  /* properties */
  Property*                   lookup_property   (const String    &property_name);
  String                      get_property      (const String    &property_name);
  void                        set_property      (const String    &property_name,
                                                 const String    &value);
  bool                        try_set_property  (const String    &property_name,
                                                 const String    &value);
  const PropertyList&         list_properties   ();
  /* commands */
  bool                        exec_command      (const String    &command_call_string);
  Command*                    lookup_command    (const String    &command_name);
  virtual const CommandList&  list_commands     ();
  /* parents */
  ContainerImpl*              parent            () const { return parent_; }
  ContainerImpl*              root              () const;
  bool                        has_ancestor      (const WidgetImpl &ancestor) const;
  WidgetImpl*                 common_ancestor   (const WidgetImpl &other) const;
  WidgetImpl*                 common_ancestor   (const WidgetImpl *other) const { return common_ancestor (*other); }
  const AnchorInfo*           anchor_info       () const { return RAPICORN_UNLIKELY (!anchored()) ? NULL : RAPICORN_LIKELY (ainfo_) ? ainfo_ : force_anchor_info(); }
  WindowImpl*                 get_window           () const;
  ViewportImpl*               get_viewport         () const;
  ResizeContainerImpl*        get_resize_container () const;
  /* cross links */
  typedef std::function<void (WidgetImpl&)> WidgetSlot;
  size_t                      cross_link        (WidgetImpl &link, const WidgetSlot &uncross);
  void                        cross_unlink      (WidgetImpl &link, size_t link_id);
  void                        uncross_links     (WidgetImpl &link);
  /* invalidation / changes */
  void                        invalidate        (uint64 mask = INVALID_REQUISITION | INVALID_ALLOCATION | INVALID_CONTENT);
  void                        invalidate_size   ()                      { invalidate (INVALID_REQUISITION | INVALID_ALLOCATION); }
  void                        changed           ();
  void                        expose            ()                      { expose (allocation()); }
  void                        expose            (const Rect &rect)      { expose (Region (rect)); }
  void                        expose            (const Region &region);
  void                        queue_visual_update  ();
  void                        force_visual_update  ();
  /* public signals */
  Aida::Signal<void ()>                 sig_finalize;
  Aida::Signal<void ()>                 sig_changed;
  Aida::Signal<void ()>                 sig_invalidate;
  Aida::Signal<void (WidgetImpl *old)>    sig_hierarchy_changed;
  /* event handling */
  bool                       process_event               (const Event &event);  // widget coordinates relative
  bool                       process_screen_window_event (const Event &event);  // screen_window coordinates relative
  /* coordinate handling */
protected:
  Affine                     affine_to_screen_window   ();                    // widget => screen_window affine
  Affine                     affine_from_screen_window ();                    // screen_window => widget affine
  // rendering
  class RenderContext;
  virtual void               render_widget             (RenderContext    &rcontext);
  virtual void               render_recursive          (RenderContext    &rcontext);
  virtual void               render                    (RenderContext    &rcontext, const Rect &rect) = 0;
  const Region&              rendering_region          (RenderContext    &rcontext) const;
  virtual cairo_t*           cairo_context             (RenderContext    &rcontext,
                                                        const Allocation &area = Allocation (-1, -1, 0, 0));
public:
  void                       render_into               (cairo_t *cr, const Region &region);
  virtual bool               point                     (Point        p);            // widget coordinates relative
  Point                      point_to_screen_window    (Point        widget_point);   // widget coordinates relative
  Point                      point_from_screen_window  (Point        window_point); // screen_window coordinates relative
  virtual bool               translate_from         (const WidgetImpl   &src_widget,
                                                     const uint    n_points,
                                                     Point        *points) const;
  bool                       translate_to           (const uint    n_points,
                                                     Point        *points,
                                                     const WidgetImpl   &target_widget) const;
  bool                       translate_from         (const WidgetImpl   &src_widget,
                                                     const uint    n_rects,
                                                     Rect         *rects) const;
  bool                       translate_to           (const uint    n_rects,
                                                     Rect         *rects,
                                                     const WidgetImpl   &target_widget) const;
  bool                       screen_window_point    (Point        p);           // screen_window coordinates relative
  /* public size accessors */
  Requisition                requisition        ();                             // effective size requisition
  void                       set_allocation     (const Allocation &area,
                                                 const Allocation *clip = NULL); // assign new allocation
  const Allocation&          allocation         () const { return allocation_; } // current allocation
  Allocation                 clipped_allocation () const;                        // clipped allocation
  const Allocation*          clip_area          () const;                        // widget clipping
  /* heritage / appearance */
  StateType             state                   () const;
  Heritage*             heritage                () const { return heritage_; }
  Color                 foreground              () { return heritage()->foreground (state()); }
  Color                 background              () { return heritage()->background (state()); }
  Color                 dark_color              () { return heritage()->dark_color (state()); }
  Color                 dark_shadow             () { return heritage()->dark_shadow (state()); }
  Color                 dark_glint              () { return heritage()->dark_glint (state()); }
  Color                 light_color             () { return heritage()->light_color (state()); }
  Color                 light_shadow            () { return heritage()->light_shadow (state()); }
  Color                 light_glint             () { return heritage()->light_glint (state()); }
  Color                 focus_color             () { return heritage()->focus_color (state()); }
  /* debugging/testing */
  virtual String        test_dump               ();
  String                debug_dump              (const String &flags = String());
protected:
  void                  make_test_dump          (TestStream   &tstream);
  virtual void          dump_test_data          (TestStream   &tstream);
  virtual void          dump_private_data       (TestStream   &tstream);
  /* convenience */
public:
  void                  find_adjustments        (AdjustmentSourceType adjsrc1,
                                                 Adjustment         **adj1,
                                                 AdjustmentSourceType adjsrc2 = ADJUSTMENT_SOURCE_NONE,
                                                 Adjustment         **adj2 = NULL,
                                                 AdjustmentSourceType adjsrc3 = ADJUSTMENT_SOURCE_NONE,
                                                 Adjustment         **adj3 = NULL,
                                                 AdjustmentSourceType adjsrc4 = ADJUSTMENT_SOURCE_NONE,
                                                 Adjustment         **adj4 = NULL);
public: /* packing */
  struct PackInfo {
    double hposition, hspan, vposition, vspan;
    uint left_spacing, right_spacing, bottom_spacing, top_spacing;
    double halign, hscale, valign, vscale;
  };
  const PackInfo&    pack_info       () const   { return const_cast<WidgetImpl*> (this)->pack_info (false); }
  double             hposition       () const   { return pack_info ().hposition; }
  void               hposition       (double d);
  double             hspan           () const   { return pack_info ().hspan; }
  void               hspan           (double d);
  double             vposition       () const   { return pack_info ().vposition; }
  void               vposition       (double d);
  double             vspan           () const   { return pack_info ().vspan; }
  void               vspan           (double d);
  int                left_spacing    () const   { return pack_info ().left_spacing; }
  void               left_spacing    (int s);
  int                right_spacing   () const   { return pack_info ().right_spacing; }
  void               right_spacing   (int s);
  int                bottom_spacing  () const   { return pack_info ().bottom_spacing; }
  void               bottom_spacing  (int s);
  int                top_spacing     () const   { return pack_info ().top_spacing; }
  void               top_spacing     (int s);
  double             halign          () const   { return pack_info ().halign; }
  void               halign          (double f);
  double             hscale          () const   { return pack_info ().hscale; }
  void               hscale          (double f);
  double             valign          () const   { return pack_info ().valign; }
  void               valign          (double f);
  double             vscale          () const   { return pack_info ().vscale; }
  void               vscale          (double f);
  double             hanchor         () const   { return halign(); } // mirrors halign
  void               hanchor         (double a) { halign (a); }      // mirrors halign
  double             vanchor         () const   { return valign(); } // mirrors valign
  void               vanchor         (double a) { valign (a); }      // mirrors valign
private:
  void               repack          (const PackInfo &orig, const PackInfo &pnew);
  PackInfo&          pack_info       (bool create);
public:
  virtual bool       match_selector        (const String &selector);
  virtual WidgetIface* query_selector        (const String &selector);
  virtual WidgetSeq    query_selector_all    (const String &selector);
  virtual WidgetIface* query_selector_unique (const String &selector);
  template<class C> typename
  InterfaceMatch<C>::Result interface        (const String &ident = String(),
                                              const std::nothrow_t &nt = dothrow) const;
  template<class C> typename
  InterfaceMatch<C>::Result parent_interface (const String &ident = String(),
                                              const std::nothrow_t &nt = dothrow) const;
protected:
  virtual bool          do_event        (const Event &event);
  static ContainerImpl* container_cast  (WidgetImpl *widget)    { return widget ? widget->as_container_impl() : NULL; }
  static WindowImpl*    window_cast     (WidgetImpl *widget)    { return widget ? widget->as_window_impl() : NULL; }
private:
  void                  type_cast_error (const char *dest_type) RAPICORN_NORETURN;
  bool                  match_interface (bool wself, bool wparent, bool children, InterfaceMatcher &imatcher) const;
};
inline bool operator== (const WidgetImpl &widget1, const WidgetImpl &widget2) { return &widget1 == &widget2; }
inline bool operator!= (const WidgetImpl &widget1, const WidgetImpl &widget2) { return &widget1 != &widget2; }

// == WidgetIfaceVector ==
struct WidgetIfaceVector : public std::vector<WidgetIface*> {
  explicit WidgetIfaceVector (const WidgetSeq &widgetseq);
  /*ctor*/ WidgetIfaceVector () {}
  WidgetSeq  to_widget_seq     () const;
};

// == Implementations ==
template<class C> typename WidgetImpl::InterfaceMatch<C>::Result
WidgetImpl::interface (const String         &ident,
                     const std::nothrow_t &nt) const
{
  InterfaceMatch<C> interface_match (ident);
  match_interface (1, 0, 1, interface_match);
  return interface_match.result (&nt == &dothrow);
}

template<class C> typename WidgetImpl::InterfaceMatch<C>::Result
WidgetImpl::parent_interface (const String         &ident,
                            const std::nothrow_t &nt) const
{
  InterfaceMatch<C> interface_match (ident);
  match_interface (0, 1, 0, interface_match);
  return interface_match.result (&nt == &dothrow);
}

} // Rapicorn

#endif  /* __RAPICORN_WIDGET_HH_ */
