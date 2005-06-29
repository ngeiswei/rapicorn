/* Rapicorn
 * Copyright (C) 2005 Tim Janik
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#ifndef __RAPICORN_CONTAINER_IMPL_HH__
#define __RAPICORN_CONTAINER_IMPL_HH__

#include <rapicorn/container.hh>
#include <rapicorn/itemimpl.hh>

namespace Rapicorn {

/* --- Single Child Container Impl --- */
class SingleContainerImpl : public virtual ItemImpl, public virtual Container {
  Item                  *child_item;
protected:
  virtual void          size_request            (Requisition &requisition);
  virtual void          size_allocate           (Allocation area);
  Item&                 get_child               () { if (!child_item) throw NullPointer(); return *child_item; }
  virtual               ~SingleContainerImpl    ();
  virtual ChildWalker   local_children          ();
  virtual bool          has_children            () { return child_item != NULL; }
  bool                  has_visible_child       () { return child_item && child_item->visible(); }
  bool                  has_drawable_child      () { return child_item && child_item->drawable(); }
  virtual bool          add_child               (Item   &item, const PackPropertyList &pack_plist = PackPropertyList());
  virtual void          remove_child            (Item   &item);
  explicit              SingleContainerImpl     ();
public:
  virtual Packer        create_packer           (Item &item);
};

/* --- Multi Child Container Impl --- */
class MultiContainerImpl : public virtual ItemImpl, public virtual Container {
  std::vector<Item*>    items;
protected:
  virtual               ~MultiContainerImpl();
  virtual ChildWalker   local_children          () { return value_walker (items); }
  virtual bool          has_children            () { return items.size() > 0; }
  virtual bool          add_child               (Item   &item, const PackPropertyList &pack_plist = PackPropertyList());
  virtual void          remove_child            (Item   &item);
  explicit              MultiContainerImpl      ();
};

} // Rapicorn

#endif  /* __RAPICORN_CONTAINER_IMPL_HH__ */