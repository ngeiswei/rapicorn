// This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
#include "container.hh"
#include "container.hh"
#include "sizegroup.hh"
#include "window.hh"
#include "factory.hh"
#include <algorithm>
#include <stdio.h>

#define DEBUG_RESIZE(...)     RAPICORN_KEY_DEBUG ("Resize", __VA_ARGS__)

namespace Rapicorn {

// == CrossLinks ==
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
static inline void container_uncross_link_R (ContainerImpl *container, CrossLink **clinkp, bool notify_callback = true);
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

// == ContainerImpl ==
size_t
ContainerImpl::widget_cross_link (WidgetImpl &owner, WidgetImpl &link, const WidgetSlot &uncross)
{
  assert_return (&owner != &link, 0);
  assert_return (owner.common_ancestor (link) == this, 0); // could be disabled for performance
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
  const WidgetImplP guard_this = shared_ptr_cast<WidgetImpl*> (this);
  const WidgetImplP guard_owner = shared_ptr_cast<WidgetImpl*> (&owner);
  const WidgetImplP guard_link = shared_ptr_cast<WidgetImpl*> (&link);
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
    critical ("no cross link from \"%s\" to \"%s\" on \"%s\" to remove", owner.id(), link.id(), id());
}

void
ContainerImpl::widget_uncross_links (WidgetImpl &owner, WidgetImpl &link)
{
  const WidgetImplP guard_this = shared_ptr_cast<WidgetImpl*> (this);
  const WidgetImplP guard_owner = shared_ptr_cast<WidgetImpl*> (&owner);
  const WidgetImplP guard_link = shared_ptr_cast<WidgetImpl*> (&link);
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
}

static inline bool
widget_has_ancestor (const WidgetImpl *widget, const WidgetImpl *ancestor)
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
  const WidgetImplP guard_this = shared_ptr_cast<WidgetImpl*> (this);
  const WidgetImplP guard_widget = shared_ptr_cast<WidgetImpl*> (widget);
  ContainerImpl *cc = widget->as_container_impl();
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
      ContainerImpl *saved_parent = NULL;
      std::swap (saved_parent, this->parent_);
      for (CrossLink *last = NULL, *clink = clinks ? clinks->links : NULL; clink; last = clink, clink = last->next)
        if (widget_has_ancestor (clink->owner, widget) ||
            widget_has_ancestor (clink->link, widget))
          {
            std::swap (saved_parent, this->parent_);
            container_uncross_link_R (clinks->container, last ? &last->next : &clinks->links);
            goto restart_search;
          }
      std::swap (saved_parent, this->parent_);
    }
}

static inline void
container_uncross_link_R (ContainerImpl *container, CrossLink **clinkp, bool notify_callback)
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

void
ContainerImpl::fabricated ()
{
  WidgetImpl::fabricated();
  for (auto child : *this)
    child->fabricated();
}

typedef vector<WidgetGroupP> WidgetGroups;
class WidgetGroupsKey : public DataKey<WidgetGroups*> {
  virtual void destroy (WidgetGroups *widget_groups) override
  {
    while (!widget_groups->empty())
      {
        WidgetGroupP widget_group = widget_groups->back();
        widget_groups->pop_back();
      }
    delete widget_groups;
  }
};
static WidgetGroupsKey widget_groups_key;

WidgetGroup*
ContainerImpl::retrieve_widget_group (const String &group_name, WidgetGroupType group_type, bool force_create)
{
  WidgetGroups *widget_groups = get_data (&widget_groups_key);
  if (!widget_groups)
    {
      if (!force_create)
        return NULL;
      widget_groups = new WidgetGroups;
      set_data (&widget_groups_key, widget_groups);
    }
  else
    for (auto it : *widget_groups)
      if (it->name() == group_name && it->type() == group_type)
        return &*it;
  if (force_create)
    {
      WidgetGroupP widget_group = WidgetGroup::create (group_name, group_type);
      widget_groups->push_back (widget_group);
      return &*widget_group;
    }
  return NULL;
}

static DataKey<ContainerImpl*> child_container_key;

void
ContainerImpl::child_container (ContainerImpl *child_container)
{
  if (child_container && !child_container->has_ancestor (*this))
    throw Exception ("child container is not descendant of container \"", id(), "\": ", child_container->id());
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

String
ContainerImpl::try_add (WidgetImplP widget)
{
  ContainerImpl &container = child_container();
  if (this != &container)
    return container.try_add (widget);
  assert_return (widget != NULL, "widget != NULL");
  assert_return (this->isconstructed(), "!isconstructed");
  assert_return (widget->isconstructed(), "!isconstructed");
  if (widget->parent())
    return "child already has a parent";
  const String error = container.add_child (widget);
  if (widget->parent())
    {
      const PackInfo &pa = widget->pack_info();
      PackInfo po = pa;
      po.hspan = po.vspan = 0; // indicate initial repack_child()
      widget->parent()->repack_child (*widget, po, pa);
    }
  return error;
}

void
ContainerImpl::add (WidgetImpl &widget)
{
  String error = try_add (shared_ptr_cast<WidgetImpl> (&widget));
  if (!error.empty())
    critical ("failed to add container child: %s", __func__, error);
}

bool
ContainerImpl::remove (WidgetImpl &widget)
{
  const WidgetImplP guard_widget = shared_ptr_cast<WidgetImpl*> (&widget);
  ContainerImpl *container = widget.parent();
  if (!container)
    return false;
  ContainerImpl *dcontainer = container;
  while (dcontainer)
    {
      dcontainer->dispose_widget (widget);
      dcontainer = dcontainer->parent();
    }
  container->remove_child (widget);
  return true;
}

void
ContainerImpl::foreach_recursive (const std::function<void (WidgetImpl&)> &f)
{
  f (*this);
  for (auto child : *this)
    child->foreach_recursive (f);
}

void
ContainerImpl::hierarchy_changed (WindowImpl *old_toplevel)
{
  WidgetImpl::hierarchy_changed (old_toplevel);
  for (auto child : *this)
    child->sig_hierarchy_changed.emit (old_toplevel);
}

void
ContainerImpl::dispose_widget (WidgetImpl &widget)
{
  if (&widget == get_data (&child_container_key))
    child_container (NULL);
}

void
ContainerImpl::repack_child (WidgetImpl &widget, const PackInfo &orig, const PackInfo &pnew)
{
  // we must check the requisition if child packing properties changed
  invalidate_requisition();
}

static DataKey<vector<FocusIndicator*>> focus_indicator_key;

void
ContainerImpl::register_focus_indicator (FocusIndicator &focus_indicator)
{
  assert_return (test_any (NEEDS_FOCUS_INDICATOR));
  vector<FocusIndicator*> vfi = get_data (&focus_indicator_key);
  for (auto &existing_indicator : vfi)
    assert_return (existing_indicator != &focus_indicator);
  vfi.push_back (&focus_indicator);
  set_data (&focus_indicator_key, vfi);
  set_flag (HAS_FOCUS_INDICATOR, true);
}

void
ContainerImpl::unregister_focus_indicator (FocusIndicator &focus_indicator)
{
  vector<FocusIndicator*> vfi = get_data (&focus_indicator_key);
  for (size_t i = 0; i < vfi.size(); i++)
    if (vfi[i] == &focus_indicator)
      {
        vfi.erase (vfi.begin() + i);
        if (vfi.size())
          set_data (&focus_indicator_key, vfi);
        else
          {
            delete_data (&focus_indicator_key);
            set_flag (HAS_FOCUS_INDICATOR, false);
          }
        return;
      }
  assert_return (!"failed to find focus_indicator!");
}

void
ContainerImpl::do_changed (const String &name)
{
  WidgetImpl::do_changed (name);
  if (test_all (NEEDS_FOCUS_INDICATOR | HAS_FOCUS_INDICATOR) && name == "flags")
    {
      vector<FocusIndicator*> vfi = get_data (&focus_indicator_key);
      for (size_t i = 0; i < vfi.size(); i++)
        vfi[i]->focusable_container_change (*this);
    }
}

static DataKey<WidgetImpl*> focus_child_key;

void
ContainerImpl::unparent_child (WidgetImpl &widget)
{
  const WidgetImplP guard_this = shared_ptr_cast<WidgetImpl*> (this);
  if (widget.visible())
    invalidate_requisition();
  if (&widget == get_data (&focus_child_key))
    delete_data (&focus_child_key);
  ContainerImpl *ancestor = this;
  do
    {
      ancestor->uncross_descendant (widget);
      ancestor = ancestor->parent();
    }
  while (ancestor);
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
  operator() (WidgetImpl *const &child1, WidgetImpl *const &child2) const
  {
    // using child_allocation() assumes child1 and child2 have the same parent
    const Allocation &a1 = child1->child_allocation();
    const Allocation &a2 = child2->child_allocation();
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

/// Return the visible widget allocation area useful for consideraiton during directional focus movement.
static inline Allocation
focus_view_area (WidgetImpl *widget)
{
  /* Note, widgets may be (partially) invisible because they can be obscured by other
   * contents, or their allocation may exceed their ancestors boundaries (overflown)
   * or they may be (paritally) scrolled out of sight within any of their ancestors.
   * For directional focus movement, i.e moving to the "nearest left/right/up/down",
   * what we do is consider the nearest *onscreen* *visible* child, or the direction
   * (edge) of the ancestor that is closest to the invisible child. Any other
   * movements across multiple nested scrolled areas probably become too complicated
   * to yield non-surprising results.
   */
  Allocation farea = widget->child_allocation();
  WidgetImpl *p = widget->parent();
  while (p)
    {
      // Intersect focusable area with parent area, or map it onto
      // one of the parent edges if they are non-intersecting.
      farea = farea.mapped_onto (p->allocation());
      const Allocation parea = p->child_allocation();
      farea.x += parea.x;
      farea.y += parea.y;
      p = p->parent();
    }
  return farea;
}

struct LesserWidgetByDirection {
  FocusDir     dir;
  Point        anchor;
  LesserWidgetByDirection (FocusDir d, const Point &p) :
    dir (d), anchor (p)
  {}
  double
  directional_distance (const Allocation &a) const
  {
    switch (dir)
      {
      case FocusDir::RIGHT:
        return a.x - anchor.x;
      case FocusDir::UP:
        return anchor.y - (a.y + a.height);
      case FocusDir::LEFT:
        return anchor.x - (a.x + a.width);
      case FocusDir::DOWN:
        return a.y - anchor.y;
      default:
        return -1;      // unused
      }
  }
  bool
  operator() (WidgetImpl *const &child1, WidgetImpl *const &child2) const
  {
    // calculate widget distances along dir, dist >= 0 lies ahead
    const Allocation &a1 = focus_view_area (child1);
    const Allocation &a2 = focus_view_area (child2);
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
    if (dd1 != dd2)
      return dd1 < dd2;
    // same edge and center distance, probably because focus_view_area() collapsed
    // area coordinates, now resort to real allocation
    dd1 = directional_distance (child1->rect_to_viewport (child1->allocation()));
    dd2 = directional_distance (child2->rect_to_viewport (child2->allocation()));
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
    if (dd1 != dd2)
      return dd1 < dd2;
    return true; // the real allocations still have the same anchor distances
  }
};

static inline Point
rect_center (const Allocation &a)
{
  return Point (a.x + a.width * 0.5, a.y + a.height * 0.5);
}

static inline bool
has_allocation (WidgetImpl *widget)
{
  const Allocation area = widget->allocation();
  return area.width > 0 && area.height > 0;
}

bool
ContainerImpl::move_focus (FocusDir fdir)
{
  // check focus ability
  if (!visible() || !sensitive())
    return false;
  // focus self
  if (!has_focus() && focusable() && !test_any (FOCUS_CHAIN))
    return grab_focus();
  // allow last focus descendant to handle movement
  WidgetImpl *last_child = get_data (&focus_child_key);
  if (last_child && last_child->move_focus (fdir))
    return true;
  // copy children
  vector<WidgetImpl*> children;
  for (auto child : *this)
    if (child->visible() && has_allocation (&*child))
      children.push_back (&*child);
  if (children.empty())
    return false;
  // sort children according to direction and current focus
  const Allocation &area = focus_view_area (this);
  Point refpoint;
  switch (fdir)
    {
      WidgetImpl *current;
    case FocusDir::NEXT:
      stable_sort (children.begin(), children.end(), LesserWidgetByHBand());
      break;
    case FocusDir::PREV:
      stable_sort (children.begin(), children.end(), LesserWidgetByHBand());
      reverse (children.begin(), children.end());
      break;
    case FocusDir::UP:    case FocusDir::LEFT:
    case FocusDir::RIGHT: case FocusDir::DOWN:
      current = get_window()->get_focus_widget();
      if (current)
        refpoint = rect_center (focus_view_area (current));
      else // use an entering corner as reference point
        switch (fdir)
          {
          case FocusDir::UP:   case FocusDir::LEFT:  // use lower right as reference point
            refpoint = Point (area.x + area.width, area.y + area.height);
            break;
          case FocusDir::DOWN: case FocusDir::RIGHT: // use upper left as reference point
            refpoint = Point (area.x, area.y);
            break;
          default: ; // silence compiler
          }
      { // filter widgets with negative distance (not ahead in focus direction)
        LesserWidgetByDirection lesseribd = LesserWidgetByDirection (fdir, refpoint);
        vector<WidgetImpl*> children2;
        for (vector<WidgetImpl*>::const_iterator it = children.begin(); it != children.end(); it++)
          if (lesseribd.directional_distance (focus_view_area (*it)) >= 0)
            children2.push_back (*it);
        children.swap (children2);
        stable_sort (children.begin(), children.end(), lesseribd);
      }
      break;
    case FocusDir::NONE: ;
    }
  // skip children beyond last focus descendant
  auto citer = children.begin();
  if (last_child && (fdir == FocusDir::NEXT || fdir == FocusDir::PREV))
    while (citer < children.end())
      if (last_child == *citer++)
        break;
  // let remaining descendants handle movement
  while (citer < children.end())
    {
      WidgetImpl *child = *citer;
      if (child->move_focus (fdir))
        return true;
      citer++;
    }
  return false;
}

/// Method for container implementation to switch a child on- or off-screen, e.g. in a scrolling context.
void
ContainerImpl::stash_child (WidgetImpl &child, bool b)
{
  child.widget_adjust_state (WidgetState::STASHED, b);
}

/// List all descendants in stacking order which contain Point @a widget_point.
void
ContainerImpl::point_descendants (Point widget_point, std::vector<WidgetImplP> &stack) const
{
  for (auto childp : *this)
    {
      const WidgetImpl &child = *childp;
      const Allocation child_allocation = child.child_allocation();
      const Point p (widget_point.x - child_allocation.x, widget_point.y - child_allocation.y);
      if (child.point (p))
        {
          stack.push_back (shared_ptr_cast<WidgetImpl> (&child));
          const ContainerImpl *cc = const_cast<WidgetImpl*> (&child)->as_container_impl();
          if (cc)
            cc->point_descendants (p, stack);
        }
    }
}

void
ContainerImpl::debug_tree (String indent)
{
  printerr ("%s%s(%p) (%fx%f%+f%+f)\n", indent, this->id(), this,
            allocation().width, allocation().height, allocation().x, allocation().y);
  for (auto childp : *this)
    {
      WidgetImpl &child = *childp;
      ContainerImpl *c = child.as_container_impl();
      if (c)
        c->debug_tree (indent + "  ");
      else
        printerr ("  %s%s(%p) (%fx%f%+f%+f)\n", indent, child.id(), &child,
                  child.allocation().width, child.allocation().height, child.allocation().x, child.allocation().y);
    }
}

void
ContainerImpl::dump_test_data (TestStream &tstream)
{
  WidgetImpl::dump_test_data (tstream);
  for (auto child : *this)
    child->make_test_dump (tstream);
}

void
ContainerImpl::selectable_child_changed (WidgetChain &chain)
{
  ContainerImpl *container = parent();
  if (container)
    {
      WidgetChain this_chain;
      this_chain.widget = this;
      this_chain.next = &chain;
      container->selectable_child_changed (this_chain);
    }
}

WidgetIfaceP
ContainerImpl::create_widget (const String &widget_identifier, const StringSeq &args) // ContainerIface
{
  WidgetImplP widget = Factory::create_ui_widget (widget_identifier, args);
  try {
    add (*widget);
  } catch (...) {
    return NULL;
  }
  return widget;
}

void
ContainerImpl::remove_widget (WidgetIface &child) // ContainerIface method
{
  WidgetImpl *widget = dynamic_cast<WidgetImpl*> (&child);
  if (widget)
    remove (widget);
}

/// Calculate real requisition of a widget, including spacing/padding/alignment/etc.
Requisition
ContainerImpl::size_request_child (WidgetImpl &child, bool *hspread, bool *vspread)
{
  if (hspread)
    *hspread = child.hspread();
  if (vspread)
    *vspread = child.vspread();
  const PackInfo &pi = child.pack_info();
  const Requisition rq = child.requisition();
  return Requisition (pi.left_spacing + rq.width + pi.right_spacing,
                      pi.top_spacing + rq.height + pi.bottom_spacing);
}

/// Layout a widget with an area @a carea, including spacing/padding/alignment/etc.
Allocation
ContainerImpl::layout_child (WidgetImpl &child, const Allocation &carea)
{
  Requisition rq = child.requisition();
  const PackInfo &pi = child.pack_info();
  Allocation area = carea;
  /* pad allocation */
  area.x += min (pi.left_spacing, area.width);
  area.width -= min (pi.left_spacing + pi.right_spacing, area.width);
  area.y += min (pi.top_spacing, area.height);
  area.height -= min (pi.bottom_spacing + pi.top_spacing, area.height);
  /* expand/scale child */
  if (area.width > rq.width)
    {
      int width = iround (rq.width + pi.hscale * (area.width - rq.width));
      area.x += iround (pi.halign * (area.width - width));
      area.width = width;
    }
  if (area.height > rq.height)
    {
      int height = iround (rq.height + pi.vscale * (area.height - rq.height));
      area.y += iround (pi.valign * (area.height - height));
      area.height = height;
    }
  return area;
}

/// Shorthand for set_child_allocation() and layout_child()
void
ContainerImpl::layout_child_allocation (WidgetImpl &child, const Allocation &carea)
{
  const Allocation child_area = layout_child (child, carea);
  child.set_child_allocation (child_area);
}

SingleContainerImpl::SingleContainerImpl () :
  child_widget_ (NULL)
{}

WidgetImplP*
SingleContainerImpl::begin () const
{
  WidgetImplP *iter = const_cast<WidgetImplP*> (&child_widget_);
  return iter;
}

WidgetImplP*
SingleContainerImpl::end () const
{
  WidgetImplP *iter = const_cast<WidgetImplP*> (&child_widget_);
  if (child_widget_)
    iter++;
  return iter;
}

String
SingleContainerImpl::add_child (WidgetImplP widget)
{
  if (child_widget_)
    return "invalid attempt to add multiple children to SingleContainerImpl";
  child_widget_ = widget;
  set_child_parent (*widget, this);
  return "";
}

void
SingleContainerImpl::remove_child (WidgetImpl &widget)
{
  assert_return (&widget == child_widget_.get());
  const WidgetImplP guard_widget = child_widget_;
  child_widget_.reset();
  set_child_parent (widget, NULL);
}

void
SingleContainerImpl::size_request (Requisition &requisition)
{
  bool hspread = false, vspread = false;
  if (has_visible_child())
    requisition = size_request_child (get_child(), &hspread, &vspread);
  set_flag (HSPREAD_CONTAINER, hspread);
  set_flag (VSPREAD_CONTAINER, vspread);
}

void
SingleContainerImpl::size_allocate (Allocation area)
{
  if (has_visible_child())
    {
      WidgetImpl &child = get_child();
      Allocation child_area = layout_child (child, area);
      child.set_child_allocation (child_area);
    }
}

SingleContainerImpl::~SingleContainerImpl()
{
  while (child_widget_)
    remove (*child_widget_);
}

// == ResizeContainerImpl ==
ResizeContainerImpl::ResizeContainerImpl()
{}

ResizeContainerImpl::~ResizeContainerImpl()
{
  ancestry_cache_.resize_container = NULL;
}

const WidgetImpl::AncestryCache*
ResizeContainerImpl::fetch_ancestry_cache ()
{
  ancestry_cache_.resize_container = this;
  if (parent())
    {
      ancestry_cache_.viewport = parent()->get_viewport();
      ancestry_cache_.window = parent()->get_window();
    }
  else
    {
      ancestry_cache_.viewport = NULL;
      ancestry_cache_.window = NULL;
    }
  return &ancestry_cache_;
}

void
ResizeContainerImpl::hierarchy_changed (WindowImpl *old_toplevel)
{
  SingleContainerImpl::hierarchy_changed (old_toplevel);
}

void
ResizeContainerImpl::widget_invalidate (WidgetFlag mask)
{
  this->SingleContainerImpl::widget_invalidate (mask);
  WindowImpl *w = get_window();
  if (w)
    w->queue_resize_redraw();
}

// == MultiContainerImpl ==
MultiContainerImpl::MultiContainerImpl ()
{}

WidgetImplP*
MultiContainerImpl::begin () const
{
  const WidgetImplP *iter = widgets.data();
  return const_cast<WidgetImplP*> (iter);
}

WidgetImplP*
MultiContainerImpl::end () const
{
  const WidgetImplP *iter = widgets.data();
  iter += widgets.end() - widgets.begin();
  return const_cast<WidgetImplP*> (iter);
}

String
MultiContainerImpl::add_child (WidgetImplP widget)
{
  widgets.push_back (widget);
  set_child_parent (*widget, this);
  return "";
}

void
MultiContainerImpl::remove_child (WidgetImpl &widget)
{
  size_t index = ~size_t (0);
  // often the last child is removed, so loop while looking at both ends
  for (size_t i = 0; i < (widgets.size() + 1) / 2; i++)
    {
      const int i1 = i;
      if (widgets[i1].get() == &widget)
        {
          index = i1;
          break;
        }
      const int i2 = widgets.size() - (i + 1);
      if (i2 <= i1)
        break;
      if (widgets[i2].get() == &widget)
        {
          index = i2;
          break;
        }
    }
  // actual removal
  if (index < widgets.size())
    {
      const WidgetImplP guard_widget = widgets[index];
      widgets.erase (widgets.begin() + index);
      set_child_parent (widget, NULL);
    }
  else
    assert_unreached();
}

void
MultiContainerImpl::raise_child (WidgetImpl &widget)
{
  for (uint i = 0; i < widgets.size(); i++)
    if (widgets[i].get() == &widget)
      {
        if (i + 1 != widgets.size())
          {
            std::shared_ptr<WidgetImpl> widgetp = widgets[i];
            widgets.erase (widgets.begin() + i);
            widgets.push_back (widgetp);
            if (widget.viewable())
              widget.expose();
          }
        break;
      }
}

void
MultiContainerImpl::lower_child (WidgetImpl &widget)
{
  for (uint i = 0; i < widgets.size(); i++)
    if (widgets[i].get() == &widget)
      {
        if (i != 0)
          {
            std::shared_ptr<WidgetImpl> widgetp = widgets[i];
            widgets.erase (widgets.begin() + i);
            widgets.insert (widgets.begin(), widgetp);
            if (widget.viewable())
              widget.expose();
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

MultiContainerImpl::~MultiContainerImpl()
{
  remove_all_children();
}

} // Rapicorn
