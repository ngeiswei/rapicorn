// Licensed GNU LGPL v3 or later: http://www.gnu.org/licenses/lgpl.html
#include "container.hh"
#include "container.hh"
#include "window.hh"
#include "factory.hh"
#include <algorithm>
#include <stdio.h>

#define DEBUG_RESIZE(...)     RAPICORN_KEY_DEBUG ("Resize", __VA_ARGS__)

namespace Rapicorn {

struct ClassDoctor {
  static void widget_set_parent (WidgetImpl &widget, ContainerImpl *parent) { widget.set_parent (parent); }
};

/* --- CrossLinks --- */
struct CrossLink {
  WidgetImpl           *owner, *link;
  WidgetImpl::WidgetSlot  uncross;
  CrossLink          *next;
  CrossLink (WidgetImpl *o, WidgetImpl *l, const WidgetImpl::WidgetSlot &slot) :
    owner (o), link (l),
    uncross (slot), next (NULL)
  {}
  RAPICORN_CLASS_NON_COPYABLE (CrossLink);
};
struct CrossLinks {
  ContainerImpl *container;
  CrossLink *links;
};
static inline void      container_uncross_link_R        (ContainerImpl *container,
                                                         CrossLink **clinkp,
                                                         bool        notify_callback = true);
struct CrossLinksKey : public DataKey<CrossLinks*> {
  virtual void
  destroy (CrossLinks *clinks)
  {
    while (clinks->links)
      container_uncross_link_R (clinks->container, &clinks->links);
    delete (clinks);
  }
};
static CrossLinksKey cross_links_key;

struct UncrossNode {
  UncrossNode   *next;
  ContainerImpl *mutable_container;
  CrossLink     *clink;
  UncrossNode (ContainerImpl *xcontainer, CrossLink *xclink) :
    next (NULL), mutable_container (xcontainer), clink (xclink)
  {}
  RAPICORN_CLASS_NON_COPYABLE (UncrossNode);
};
static UncrossNode *uncross_callback_stack = NULL;
static Mutex        uncross_callback_stack_mutex;

size_t
ContainerImpl::widget_cross_link (WidgetImpl &owner, WidgetImpl &link, const WidgetSlot &uncross)
{
  assert (&owner != &link);
  assert (owner.common_ancestor (link) == this); // could be disabled for performance
  CrossLinks *clinks = get_data (&cross_links_key);
  if (!clinks)
    {
      clinks = new CrossLinks();
      clinks->container = this;
      clinks->links = NULL;
      set_data (&cross_links_key, clinks);
    }
  CrossLink *clink = new CrossLink (&owner, &link, uncross);
  clink->next = clinks->links;
  clinks->links = clink;
  return size_t (clink);
}

void
ContainerImpl::widget_cross_unlink (WidgetImpl &owner, WidgetImpl &link, size_t link_id)
{
  bool found_one = false;
  ref (this);
  ref (owner);
  ref (link);
  /* _first_ check whether a currently uncrossing link (recursing from
   * container_uncross_link_R()) needs to be unlinked.
   */
  uncross_callback_stack_mutex.lock();
  for (UncrossNode *unode = uncross_callback_stack; unode; unode = unode->next)
    if (unode->mutable_container == this && link_id == size_t (unode->clink) &&
        unode->clink->owner == &owner && unode->clink->link == &link)
      {
        unode->mutable_container = NULL; /* prevent more cross_unlink() calls */
        found_one = true;
        break;
      }
  uncross_callback_stack_mutex.unlock();
  if (!found_one)
    {
      CrossLinks *clinks = get_data (&cross_links_key);
      for (CrossLink *last = NULL, *clink = clinks ? clinks->links : NULL; clink; last = clink, clink = last->next)
        if (link_id == size_t (clink) && clink->owner == &owner && clink->link == &link)
          {
            container_uncross_link_R (clinks->container, last ? &last->next : &clinks->links, false);
            found_one = true;
            break;
          }
    }
  if (!found_one)
    throw Exception ("no cross link from \"" + owner.name() + "\" to \"" + link.name() + "\" on \"" + name() + "\" to remove");
  unref (link);
  unref (owner);
  unref (this);
}

void
ContainerImpl::widget_uncross_links (WidgetImpl &owner,
                                   WidgetImpl &link)
{
  ref (this);
  ref (owner);
  ref (link);
 restart_search:
  CrossLinks *clinks = get_data (&cross_links_key);
  for (CrossLink *last = NULL, *clink = clinks ? clinks->links : NULL; clink; last = clink, clink = last->next)
    if (clink->owner == &owner &&
        clink->link == &link)
      {
        container_uncross_link_R (clinks->container, last ? &last->next : &clinks->links);
        clinks = get_data (&cross_links_key);
        goto restart_search;
      }
  unref (link);
  unref (owner);
  unref (this);
}

static inline bool
widget_has_ancestor (const WidgetImpl *widget,
                   const WidgetImpl *ancestor)
{
  /* this duplicates widget->has_ancestor() to optimize speed and
   * to cover the case where widget == ancestor.
   */
  do
    if (widget == ancestor)
      return true;
    else
      widget = widget->parent();
  while (widget);
  return false;
}

void
ContainerImpl::uncross_descendant (WidgetImpl &descendant)
{
  assert (descendant.has_ancestor (*this)); // could be disabled for performance
  WidgetImpl *widget = &descendant;
  ref (this);
  ref (widget);
  ContainerImpl *cc = dynamic_cast<ContainerImpl*> (widget);
 restart_search:
  CrossLinks *clinks = get_data (&cross_links_key);
  if (!cc || !cc->has_children()) /* suppress tree walks where possible */
    for (CrossLink *last = NULL, *clink = clinks ? clinks->links : NULL; clink; last = clink, clink = last->next)
      {
        if (clink->owner == widget || clink->link == widget)
          {
            container_uncross_link_R (clinks->container, last ? &last->next : &clinks->links);
            goto restart_search;
          }
      }
  else /* need to check whether widget is ancestor of any of our cross-link widgets */
    {
      /* we do some minor hackery here, for optimization purposes. since widget
       * is a descendant of this container, we don't need to walk ->owner's or
       * ->link's ancestor lists any further than up to reaching this container.
       * to suppress extra checks in widget_has_ancestor() in this regard, we
       * simply set parent() to NULL temporarily and with that cause
       * widget_has_ancestor() to return earlier.
       */
      ContainerImpl *saved_parent = *_parent_loc();
      *_parent_loc() = NULL;
      for (CrossLink *last = NULL, *clink = clinks ? clinks->links : NULL; clink; last = clink, clink = last->next)
        if (widget_has_ancestor (clink->owner, widget) ||
            widget_has_ancestor (clink->link, widget))
          {
            *_parent_loc() = saved_parent;
            container_uncross_link_R (clinks->container, last ? &last->next : &clinks->links);
            goto restart_search;
          }
      *_parent_loc() = saved_parent;
    }
  unref (widget);
  unref (this);
}

static inline void
container_uncross_link_R (ContainerImpl *container,
                          CrossLink **clinkp,
                          bool        notify_callback)
{
  CrossLink *clink = *clinkp;
  /* remove cross link */
  *clinkp = clink->next;
  /* notify */
  if (notify_callback)
    {
      /* record execution */
      UncrossNode unode (container, clink);
      uncross_callback_stack_mutex.lock();
      unode.next = uncross_callback_stack;
      uncross_callback_stack = &unode;
      uncross_callback_stack_mutex.unlock();
      /* exec callback, note that this may recurse */
      clink->uncross (*clink->link);
      /* unrecord execution */
      uncross_callback_stack_mutex.lock();
      UncrossNode *walk, *last = NULL;
      for (walk = uncross_callback_stack; walk; last = walk, walk = last->next)
        if (walk == &unode)
          {
            if (!last)
              uncross_callback_stack = unode.next;
            else
              last->next = unode.next;
            break;
          }
      uncross_callback_stack_mutex.unlock();
      assert (walk != NULL); /* paranoid */
    }
  /* delete cross link */
  delete clink;
}

/* --- ContainerImpl --- */
ContainerImpl::~ContainerImpl ()
{}

const CommandList&
ContainerImpl::list_commands()
{
  static Command *commands[] = {
  };
  static const CommandList command_list (commands, WidgetImpl::list_commands());
  return command_list;
}

static DataKey<ContainerImpl*> child_container_key;

void
ContainerImpl::child_container (ContainerImpl *child_container)
{
  if (child_container && !child_container->has_ancestor (*this))
    throw Exception ("child container is not descendant of container \"", name(), "\": ", child_container->name());
  set_data (&child_container_key, child_container);
}

ContainerImpl&
ContainerImpl::child_container ()
{
  ContainerImpl *container = get_data (&child_container_key);
  if (!container)
    container = this;
  return *container;
}

void
ContainerImpl::add (WidgetImpl &widget)
{
  if (widget.parent())
    throw Exception ("not adding widget with parent: ", widget.name());
  ContainerImpl &container = child_container();
  if (this != &container)
    {
      container.add (widget);
      return;
    }
  widget.ref();
  try {
    container.add_child (widget);
    const PackInfo &pa = widget.pack_info();
    PackInfo po = pa;
    po.hspan = po.vspan = 0; // indicate initial repack_child()
    container.repack_child (widget, po, pa);
  } catch (...) {
    widget.unref();
    throw;
  }
  /* can invalidate etc. the fully setup widget now */
  widget.invalidate();
  invalidate();
  widget.unref();
}

void
ContainerImpl::add (WidgetImpl *widget)
{
  if (!widget)
    throw NullPointer();
  add (*widget);
}

void
ContainerImpl::remove (WidgetImpl &widget)
{
  ContainerImpl *container = widget.parent();
  if (!container)
    throw NullPointer();
  widget.ref();
  if (widget.visible())
    {
      widget.invalidate();
      invalidate();
    }
  ContainerImpl *dcontainer = container;
  while (dcontainer)
    {
      dcontainer->dispose_widget (widget);
      dcontainer = dcontainer->parent();
    }
  container->remove_child (widget);
  widget.invalidate();
  widget.unref();
}

Affine
ContainerImpl::child_affine (const WidgetImpl &widget)
{
  return Affine(); // Identity
}

void
ContainerImpl::hierarchy_changed (WidgetImpl *old_toplevel)
{
  WidgetImpl::hierarchy_changed (old_toplevel);
  for (ChildWalker cw = local_children(); cw.has_next(); cw++)
    cw->sig_hierarchy_changed.emit (old_toplevel);
}

void
ContainerImpl::dispose_widget (WidgetImpl &widget)
{
  if (&widget == get_data (&child_container_key))
    child_container (NULL);
}

void
ContainerImpl::repack_child (WidgetImpl       &widget,
                             const PackInfo &orig,
                             const PackInfo &pnew)
{
  widget.invalidate_parent();
}

static DataKey<WidgetImpl*> focus_child_key;

void
ContainerImpl::unparent_child (WidgetImpl &widget)
{
  ref (this);
  if (&widget == get_data (&focus_child_key))
    delete_data (&focus_child_key);
  ContainerImpl *ancestor = this;
  do
    {
      ancestor->uncross_descendant (widget);
      ancestor = ancestor->parent();
    }
  while (ancestor);
  unref (this);
}

void
ContainerImpl::set_focus_child (WidgetImpl *widget)
{
  if (!widget)
    delete_data (&focus_child_key);
  else
    {
      assert (widget->parent() == this);
      set_data (&focus_child_key, widget);
    }
}

void
ContainerImpl::scroll_to_child (WidgetImpl &widget)
{}

WidgetImpl*
ContainerImpl::get_focus_child () const
{
  return get_data (&focus_child_key);
}

struct LesserWidgetByHBand {
  bool
  operator() (WidgetImpl *const &i1,
              WidgetImpl *const &i2) const
  {
    const Allocation &a1 = i1->allocation();
    const Allocation &a2 = i2->allocation();
    // sort widgets by horizontal bands first
    if (a1.y + a1.height <= a2.y)
      return true;
    if (a1.y >= a2.y + a2.height)
      return false;
    // sort vertically overlapping widgets by horizontal position
    if (a1.x != a2.x)
      return a1.x < a2.x;
    // resort to center
    Point m1 (a1.x + a1.width * 0.5, a1.y + a1.height * 0.5);
    Point m2 (a2.x + a2.width * 0.5, a2.y + a2.height * 0.5);
    if (m1.y != m2.y)
      return m1.y < m2.y;
    else
      return m1.x < m2.x;
  }
};

struct LesserWidgetByDirection {
  FocusDirType dir;
  Point        anchor;
  LesserWidgetByDirection (FocusDirType d,
                         const Point &p) :
    dir (d), anchor (p)
  {}
  double
  directional_distance (const Allocation &a) const
  {
    switch (dir)
      {
      case FOCUS_RIGHT:
        return a.x - anchor.x;
      case FOCUS_UP:
        return anchor.y - (a.y + a.height);
      case FOCUS_LEFT:
        return anchor.x - (a.x + a.width);
      case FOCUS_DOWN:
        return a.y - anchor.y;
      default:
        return -1;      // unused
      }
  }
  bool
  operator() (WidgetImpl *const &i1,
              WidgetImpl *const &i2) const
  {
    // calculate widget distances along dir, dist >= 0 lies ahead
    const Allocation &a1 = i1->allocation();
    const Allocation &a2 = i2->allocation();
    double dd1 = directional_distance (a1);
    double dd2 = directional_distance (a2);
    // sort widgets along dir
    if (dd1 != dd2)
      return dd1 < dd2;
    // same horizontal/vertical band distance, sort by closest edge distance
    dd1 = a1.dist (anchor);
    dd2 = a2.dist (anchor);
    if (dd1 != dd2)
      return dd1 < dd2;
    // same edge distance, resort to center distance
    dd1 = anchor.dist (Point (a1.x + a1.width * 0.5, a1.y + a1.height * 0.5));
    dd2 = anchor.dist (Point (a2.x + a2.width * 0.5, a2.y + a2.height * 0.5));
    return dd1 < dd2;
  }
};

static inline Point
rect_center (const Allocation &a)
{
  return Point (a.x + a.width * 0.5, a.y + a.height * 0.5);
}

bool
ContainerImpl::move_focus (FocusDirType fdir)
{
  /* check focus ability */
  if (!visible() || !sensitive())
    return false;
  /* focus self */
  if (!has_focus() && can_focus())
    return grab_focus();
  WidgetImpl *last_child = get_data (&focus_child_key);
  /* let last focus descendant handle movement */
  if (last_child && last_child->move_focus (fdir))
    return true;
  /* copy children */
  vector<WidgetImpl*> children;
  ChildWalker lw = local_children();
  while (lw.has_next())
    children.push_back (&*lw++);
  /* sort children according to direction and current focus */
  const Allocation &area = allocation();
  Point upper_left (area.x, area.y + area.height);
  Point lower_right (area.x + area.width, area.y);
  Point refpoint;
  switch (fdir)
    {
      WidgetImpl *current;
    case FOCUS_NEXT:
      stable_sort (children.begin(), children.end(), LesserWidgetByHBand());
      break;
    case FOCUS_PREV:
      stable_sort (children.begin(), children.end(), LesserWidgetByHBand());
      reverse (children.begin(), children.end());
      break;
    case FOCUS_UP:
    case FOCUS_LEFT:
      current = get_window()->get_focus();
      refpoint = current ? rect_center (current->allocation()) : lower_right;
      { /* filter widgets with negative distance (not ahead in focus direction) */
        LesserWidgetByDirection lesseribd = LesserWidgetByDirection (fdir, refpoint);
        vector<WidgetImpl*> children2;
        for (vector<WidgetImpl*>::const_iterator it = children.begin(); it != children.end(); it++)
          if (lesseribd.directional_distance ((*it)->allocation()) >= 0)
            children2.push_back (*it);
        children.swap (children2);
        stable_sort (children.begin(), children.end(), lesseribd);
      }
      break;
    case FOCUS_RIGHT:
    case FOCUS_DOWN:
      current = get_window()->get_focus();
      refpoint = current ? rect_center (current->allocation()) : upper_left;
      { /* filter widgets with negative distance (not ahead in focus direction) */
        LesserWidgetByDirection lesseribd = LesserWidgetByDirection (fdir, refpoint);
        vector<WidgetImpl*> children2;
        for (vector<WidgetImpl*>::const_iterator it = children.begin(); it != children.end(); it++)
          if (lesseribd.directional_distance ((*it)->allocation()) >= 0)
            children2.push_back (*it);
        children.swap (children2);
        stable_sort (children.begin(), children.end(), lesseribd);
      }
      break;
    }
  /* skip children beyond last focus descendant */
  Walker<WidgetImpl*> cw = walker (children);
  if (last_child && (fdir == FOCUS_NEXT || fdir == FOCUS_PREV))
    while (cw.has_next())
      if (last_child == *cw++)
        break;
  /* let remaining descendants handle movement */
  while (cw.has_next())
    {
      WidgetImpl *child = *cw;
      if (child->move_focus (fdir))
        return true;
      cw++;
    }
  return false;
}

void
ContainerImpl::expose_enclosure ()
{
  /* expose without children */
  Region region (allocation());
  for (ChildWalker cw = local_children(); cw.has_next(); cw++)
    if (cw->drawable())
      {
        WidgetImpl &child = *cw;
        Region cregion (child.allocation());
        cregion.affine (child_affine (child).invert());
        region.subtract (cregion);
      }
  expose (region);
}

void
ContainerImpl::point_children (Point               p, /* window coordinates relative */
                               std::vector<WidgetImpl*> &stack)
{
  for (ChildWalker cw = local_children(); cw.has_next(); cw++)
    {
      WidgetImpl &child = *cw;
      Point cp = child_affine (child).point (p);
      if (child.point (cp))
        {
          child.ref();
          stack.push_back (&child);
          ContainerImpl *cc = dynamic_cast<ContainerImpl*> (&child);
          if (cc)
            cc->point_children (cp, stack);
        }
    }
}

void
ContainerImpl::screen_window_point_children (Point                   p, /* screen_window coordinates relative */
                                             std::vector<WidgetImpl*> &stack)
{
  point_children (point_from_screen_window (p), stack);
}

void
ContainerImpl::render_recursive (RenderContext &rcontext)
{
  for (ChildWalker cw = local_children(); cw.has_next(); cw++)
    {
      WidgetImpl &child = *cw;
      if (child.drawable() && rendering_region (rcontext).contains (child.allocation()) != Region::OUTSIDE)
        {
          if (child.test_flags (INVALID_REQUISITION))
            critical ("rendering widget with invalid %s: %s (%p)", "requisition", cw->name().c_str(), &child);
          if (child.test_flags (INVALID_ALLOCATION))
            critical ("rendering widget with invalid %s: %s (%p)", "allocation", cw->name().c_str(), &child);
          child.render_widget (rcontext);
        }
    }
}

void
ContainerImpl::debug_tree (String indent)
{
  printerr ("%s%s(%p) (%fx%f%+f%+f)\n", indent.c_str(), this->name().c_str(), this,
            allocation().width, allocation().height, allocation().x, allocation().y);
  for (ChildWalker cw = local_children(); cw.has_next(); cw++)
    {
      WidgetImpl &child = *cw;
      ContainerImpl *c = dynamic_cast<ContainerImpl*> (&child);
      if (c)
        c->debug_tree (indent + "  ");
      else
        printerr ("  %s%s(%p) (%fx%f%+f%+f)\n", indent.c_str(), child.name().c_str(), &child,
                  child.allocation().width, child.allocation().height, child.allocation().x, child.allocation().y);
    }
}

void
ContainerImpl::dump_test_data (TestStream &tstream)
{
  WidgetImpl::dump_test_data (tstream);
  for (ChildWalker cw = local_children(); cw.has_next(); cw++)
    cw->make_test_dump (tstream);
}

WidgetIface*
ContainerImpl::create_child (const std::string   &widget_identifier,
                             const StringSeq &args)
{
  WidgetImpl &widget = Factory::create_ui_widget (widget_identifier, args);
  ref_sink (widget);
  try {
    add (widget);
  } catch (...) {
    unref (widget);
    return NULL;
  }
  unref (widget);
  return &widget;
}

SingleContainerImpl::SingleContainerImpl () :
  child_widget (NULL)
{}

ContainerImpl::ChildWalker
SingleContainerImpl::local_children () const
{
  WidgetImpl **iter = const_cast<WidgetImpl**> (&child_widget), **iend = iter;
  if (child_widget)
    iend++;
  return value_walker (PointerIterator<WidgetImpl*> (iter), PointerIterator<WidgetImpl*> (iend));
}

void
SingleContainerImpl::add_child (WidgetImpl &widget)
{
  if (child_widget)
    throw Exception ("invalid attempt to add child \"", widget.name(), "\" to single-child container \"", name(), "\" ",
                     "which already has a child \"", child_widget->name(), "\"");
  widget.ref_sink();
  ClassDoctor::widget_set_parent (widget, this);
  child_widget = &widget;
}

void
SingleContainerImpl::remove_child (WidgetImpl &widget)
{
  assert (child_widget == &widget); /* ensured by remove() */
  child_widget = NULL;
  ClassDoctor::widget_set_parent (widget, NULL);
  widget.unref();
}

void
SingleContainerImpl::size_request_child (Requisition &requisition, bool *hspread, bool *vspread)
{
  bool chspread = false, cvspread = false;
  if (has_allocatable_child())
    {
      WidgetImpl &child = get_child();
      Requisition cr = child.requisition ();
      const PackInfo &pi = child.pack_info();
      requisition.width = pi.left_spacing + cr.width + pi.right_spacing;
      requisition.height = pi.bottom_spacing + cr.height + pi.top_spacing;
      chspread = child.hspread();
      cvspread = child.vspread();
    }
  if (hspread)
    *hspread = chspread;
  else
    set_flag (HSPREAD_CONTAINER, chspread);
  if (vspread)
    *vspread = cvspread;
  else
    set_flag (VSPREAD_CONTAINER, cvspread);
}

void
SingleContainerImpl::size_request (Requisition &requisition)
{
  size_request_child (requisition, NULL, NULL);
}

Allocation
ContainerImpl::layout_child (WidgetImpl         &child,
                             const Allocation &carea)
{
  Requisition rq = child.requisition();
  const PackInfo &pi = child.pack_info();
  Allocation area = carea;
  /* pad allocation */
  area.x += pi.left_spacing;
  area.width -= pi.left_spacing + pi.right_spacing;
  area.y += pi.bottom_spacing;
  area.height -= pi.bottom_spacing + pi.top_spacing;
  /* expand/scale child */
  if (area.width > rq.width && !child.hexpand())
    {
      int width = iround (rq.width + pi.hscale * (area.width - rq.width));
      area.x += iround (pi.halign * (area.width - width));
      area.width = width;
    }
  if (area.height > rq.height && !child.vexpand())
    {
      int height = iround (rq.height + pi.vscale * (area.height - rq.height));
      area.y += iround (pi.valign * (area.height - height));
      area.height = height;
    }
  return area;
}

void
SingleContainerImpl::size_allocate (Allocation area, bool changed)
{
  if (has_allocatable_child())
    {
      WidgetImpl &child = get_child();
      Allocation child_area = layout_child (child, area);
      child.set_allocation (child_area);
    }
}

void
SingleContainerImpl::pre_finalize()
{
  while (child_widget)
    remove (child_widget);
  ContainerImpl::pre_finalize();
}

SingleContainerImpl::~SingleContainerImpl()
{
  while (child_widget)
    remove (child_widget);
}

ResizeContainerImpl::ResizeContainerImpl() :
  tunable_requisition_counter_ (0), resizer_ (0)
{
  anchor_info_.resize_container = this;
  update_anchor_info();
}

ResizeContainerImpl::~ResizeContainerImpl()
{
  clear_exec (&resizer_);
  anchor_info_.resize_container = NULL;
}

void
ResizeContainerImpl::hierarchy_changed (WidgetImpl *old_toplevel)
{
  update_anchor_info();
  SingleContainerImpl::hierarchy_changed (old_toplevel);
}

void
ResizeContainerImpl::update_anchor_info ()
{
  WidgetImpl *last, *widget;
  anchor_info_.viewport = NULL;
  // find first ViewportImpl
  for (last = widget = this; widget && !anchor_info_.viewport; last = widget, widget = last->parent())
    anchor_info_.viewport = dynamic_cast<ViewportImpl*> (widget);
  // find topmost parent
  widget = last;
  while (widget)
    last = widget, widget = last->parent();
  widget = last;
  // assign window iff one is found
  anchor_info_.window = dynamic_cast<WindowImpl*> (widget);
}

static inline String
impl_type (WidgetImpl *widget)
{
  String tag;
  if (widget)
    tag = Factory::factory_context_impl_type (widget->factory_context());
  const size_t cpos = tag.rfind (':');
  return cpos != String::npos ? tag.c_str() + cpos + 1 : tag;
}

void
ResizeContainerImpl::idle_sizing ()
{
  assert_return (resizer_ != 0);
  resizer_ = 0;
  if (anchored() && drawable())
    {
      ContainerImpl *pc = parent();
      if (pc && pc->test_any_flag (INVALID_REQUISITION | INVALID_ALLOCATION))
        DEBUG_RESIZE ("%12s 0x%016zx, %s", impl_type (this).c_str(), size_t (this), "pass upwards...");
      else
        {
          Allocation area = allocation();
          negotiate_size (&area);
        }
    }
}

void
ResizeContainerImpl::negotiate_size (const Allocation *carea)
{
  assert_return (requisitions_tunable() == false); // prevent recursion
  const bool have_allocation = carea != NULL;
  Allocation area;
  if (have_allocation)
    {
      area = *carea;
      change_flags_silently (INVALID_ALLOCATION, true);
    }
  const bool need_debugging = debug_enabled() && test_flags (INVALID_REQUISITION | INVALID_ALLOCATION);
  if (need_debugging)
    DEBUG_RESIZE ("%12s 0x%016zx, %s", impl_type (this).c_str(), size_t (this),
                  !carea ? "probe..." : String ("assign: " + carea->string()).c_str());
  /* this is the core of the resizing loop. via Widget.tune_requisition(), we
   * allow widgets to adjust the requisition from within size_allocate().
   * whether the tuned requisition is honored at all, depends on
   * tunable_requisition_counter_.
   * currently, we simply freeze the allocation after 3 iterations. for the
   * future it's possible to honor the tuned requisition only partially or
   * proportionally as tunable_requisition_counter_ decreases, so to mimick
   * a simulated annealing process yielding the final layout.
   */
  tunable_requisition_counter_ = 3;
  while (test_flags (INVALID_REQUISITION | INVALID_ALLOCATION))
    {
      const Requisition creq = requisition(); // unsets INVALID_REQUISITION
      if (!have_allocation)
        {
          // seed allocation from requisition
          area.width = creq.width;
          area.height = creq.height;
        }
      set_allocation (area); // unsets INVALID_ALLOCATION, may re-::invalidate_size()
      if (tunable_requisition_counter_)
        tunable_requisition_counter_--;
    }
  tunable_requisition_counter_ = 0;
  if (need_debugging && !carea)
    DEBUG_RESIZE ("%12s 0x%016zx, %s", impl_type (this).c_str(), size_t (this), String ("result: " + area.string()).c_str());
}

void
ResizeContainerImpl::invalidate_parent ()
{
  if (anchored() && drawable())
    {
      if (!resizer_)
        {
          WindowImpl *w = get_window();
          EventLoop *loop = w ? w->get_loop() : NULL;
          if (loop)
            resizer_ = loop->exec_timer (0, Aida::slot (*this, &ResizeContainerImpl::idle_sizing), WindowImpl::PRIORITY_RESIZE);
        }
      return;
    }
  SingleContainerImpl::invalidate_parent();
}

MultiContainerImpl::MultiContainerImpl ()
{}

void
MultiContainerImpl::add_child (WidgetImpl &widget)
{
  widget.ref_sink();
  ClassDoctor::widget_set_parent (widget, this);
  widgets.push_back (&widget);
}

void
MultiContainerImpl::remove_child (WidgetImpl &widget)
{
  vector<WidgetImpl*>::iterator it;
  for (it = widgets.begin(); it != widgets.end(); it++)
    if (*it == &widget)
      {
        widgets.erase (it);
        ClassDoctor::widget_set_parent (widget, NULL);
        widget.unref();
        return;
      }
  assert_unreached();
}

void
MultiContainerImpl::raise_child (WidgetImpl &widget)
{
  for (uint i = 0; i < widgets.size(); i++)
    if (widgets[i] == &widget)
      {
        if (i + 1 != widgets.size())
          {
            widgets.erase (widgets.begin() + i);
            widgets.push_back (&widget);
            invalidate();
          }
        break;
      }
}

void
MultiContainerImpl::lower_child (WidgetImpl &widget)
{
  for (uint i = 0; i < widgets.size(); i++)
    if (widgets[i] == &widget)
      {
        if (i != 0)
          {
            widgets.erase (widgets.begin() + i);
            widgets.insert (widgets.begin(), &widget);
            invalidate();
          }
        break;
      }
}

void
MultiContainerImpl::remove_all_children ()
{
  while (widgets.size())
    remove (*widgets[widgets.size() - 1]);
}

void
MultiContainerImpl::pre_finalize()
{
  remove_all_children();
  ContainerImpl::pre_finalize();
}

MultiContainerImpl::~MultiContainerImpl()
{
  while (widgets.size())
    remove (*widgets[widgets.size() - 1]);
}

} // Rapicorn
