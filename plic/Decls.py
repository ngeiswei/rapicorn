#!/usr/bin/env python
# plic - Pluggable IDL Compiler                                -*-mode:python-*-
# Copyright (C) 2008 Tim Janik
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
import os, sys, re, shutil, hashlib;
true, false, length = (True, False, len)

# --- types ---
VOID, INT, FLOAT, STRING, ENUM, SEQUENCE, RECORD, INTERFACE, FUNC, TYPE_REFERENCE, ANY = [ord (x) for x in 'vidsEQRCFTY']
def storage_name (storage):
  name = {
    VOID      : 'VOID',
    INT       : 'INT',
    FLOAT     : 'FLOAT',
    STRING    : 'STRING',
    ENUM      : 'ENUM',
    RECORD    : 'RECORD',
    SEQUENCE  : 'SEQUENCE',
    FUNC      : 'FUNC',
    INTERFACE : 'INTERFACE',
    ANY       : 'ANY',
  }.get (storage, None)
  if not name:
    raise RuntimeError ("Invalid storage type: " + storage)
  return name

# --- declarations ---
class BaseDecl (object):
  def __init__ (self):
    self.name = None
    self.namespace = None
    self.loc = ()
    self.hint = ''
    self.docu = ()
  def list_namespaces (self):
    nslist = []
    ns = self.namespace
    while ns:
      nslist = [ ns ] + nslist
      ns = ns.namespace
    return nslist

class Namespace (BaseDecl):
  def __init__ (self, name, outer, impl_list):
    super (Namespace, self).__init__()
    self.name = name
    self.namespace = outer
    self.cmembers = [] # holds: (name, content)
    self.tmembers = [] # holds: (name, content)
    self.type_dict = {}
    self.const_dict = {}
    self.impl_set = set()
    self.global_impl_list = impl_list
    self.full_name = "::". join ([x.name for x in self.list_namespaces()] + [self.name])
  def add_const (self, name, content, isimpl):
    self.cmembers += [ (name, content) ]
    self.const_dict[name] = self.cmembers[-1]
    if isimpl:
      self.impl_set.add (name)
  def add_type (self, type):
    assert isinstance (type, TypeInfo)
    type.namespace = self
    self.tmembers += [ (type.name, type) ]
    self.type_dict[type.name] = type
    self.global_impl_list += [ type ]
  def types (self):
    return [mb[1] for mb in self.tmembers]
  def consts (self):
    return self.cmembers
  def unknown (self, name):
    return not (self.const_dict.has_key (name) or self.type_dict.has_key (name))
  def find_const (self, name):
    nc = self.const_dict.get (name, None)
    return nc and (nc[1],) or ()
  def find_type (self, name, fallback = None):
    return self.type_dict.get (name, fallback)

class TypeInfo (BaseDecl):
  collector = 'void'
  def __init__ (self, name, storage, isimpl):
    super (TypeInfo, self).__init__()
    assert storage in (VOID, INT, FLOAT, STRING, ENUM, RECORD, SEQUENCE, FUNC, INTERFACE, ANY)
    self.name = name
    self.storage = storage
    self.isimpl = isimpl
    # clonable fields:
    self.typedef_origin = None
    self.is_forward = False
    self.options = []           # holds: (ident, label, blurb, number)
    if (self.storage == RECORD or
        self.storage == INTERFACE):
      self.fields = []          # holds: (ident, TypeInfo)
    if self.storage == SEQUENCE:
      self.elements = None      # holds: ident, TypeInfo
    if self.storage == FUNC:
      self.args = []            # holds: (ident, TypeInfo, defaultinit)
      self.rtype = None         # holds: TypeInfo
      self.ownertype = None    # TypeInfo
      self.pure = False
      self.issignal = False
    if self.storage == INTERFACE:
      self.prerequisites = []
      self.methods = []         # holds: TypeInfo
      self.signals = []         # holds: TypeInfo
    self.auxdata = {}
  def string_digest (self):
    typelist, arglist = [], [] # owner, self, rtype, arg*type, ...
    if self.__dict__.get ('ownertype', None): typelist += [self.ownertype]
    typelist += [self]
    if self.__dict__.get ('rtype', None): arglist += [self.rtype]
    if self.__dict__.get ('args', []): arglist += [a[1] for a in self.args]
    typelist = '::'.join ([tp.full_name() for tp in typelist]) # MethodObject method_name
    arglist  =  '+'.join ([tp.full_name() for tp in arglist])  # void float int string
    return typelist + (' ' if arglist else '') + arglist
  def ident_digest (self):
    digest = self.string_digest()
    digest = re.sub ('[^a-zA-Z0-9]', '_', digest)
    return digest
  def hash128digest (self, highnibble, prefix, postfix = ''):
    sha256 = hashlib.sha256()
    hash_feed = 'fc4676dd-248d-4958-a7fa-e170a4d8a68c | ' + prefix + ' | ' + self.string_digest() + postfix
    # print >>sys.stderr, "HASH:", hash_feed
    sha256.update (hash_feed)
    hash120 = sha256.digest()[7:17] + sha256.digest()[26:31]
    hash8 = (highnibble & 0xf0) + (ord (sha256.digest()[3]) & 0x0f)
    return chr (hash8) + hash120
  def type_hash (self):
    if self.storage == FUNC:
      if self.issignal:
        highnibble, tag = 0x50, "sigcon"
      elif self.rtype.storage == VOID:
        highnibble, tag = 0x20, "oneway"
      else:
        highnibble, tag = 0x30, "twoway"
    else:
      highnibble, tag = 0x00, "type"
    bytes = self.hash128digest (highnibble, tag)
    t = tuple ([ord (c) for c in bytes])
    return t
  def twoway_hash (self, special = ''):
    highnibble, tag = 0x30, "twoway"
    tag = '%s/%s' % (tag, special) if special else tag
    bytes = self.hash128digest (highnibble, tag)
    return tuple ([ord (c) for c in bytes])
  def property_hash (self, field, setter):
    if setter:
      highnibble, tag = 0x20, "setter" # oneway
    else:
      highnibble, tag = 0x30, "getter" # twoway
    postfix = '::' + field[0] + ' ' + field[1].full_name()
    bytes = self.hash128digest (highnibble, tag, postfix)
    t = tuple ([ord (c) for c in bytes])
    return t
  def clone (self, newname, isimpl):
    if newname == None: newname = self.name
    ti = TypeInfo (newname, self.storage, isimpl)
    ti.typedef_origin = self.typedef_origin
    ti.is_forward = self.is_forward
    ti.options += self.options
    if hasattr (self, 'namespace'):
      ti.namespace = self.namespace
    if hasattr (self, 'fields'):
      ti.fields += self.fields
    if hasattr (self, 'args'):
      ti.args += self.args
    if hasattr (self, 'rtype'):
      ti.rtype = self.rtype
    if hasattr (self, 'pure'):
      ti.pure = self.pure
    if hasattr (self, 'issignal'):
      ti.issignal = self.issignal
    if hasattr (self, 'elements'):
      ti.elements = self.elements
    if hasattr (self, 'prerequisites'):
      ti.prerequisites += self.prerequisites
    if hasattr (self, 'methods'):
      ti.methods += self.methods
    if hasattr (self, 'ownertype'):
      ti.ownertype = self.ownertype
    if hasattr (self, 'signals'):
      ti.signals += self.signals
    ti.auxdata.update (self.auxdata)
    return ti
  def update_auxdata (self, auxdict):
    self.auxdata.update (auxdict)
  def add_option (self, ident, label, blurb, number):
    assert self.storage == ENUM
    assert isinstance (ident, str)
    assert isinstance (label, str)
    assert isinstance (blurb, str)
    assert isinstance (number, int)
    self.options += [ (ident, label, blurb, number) ]
  def has_option (self, ident = None, label = None, blurb = None, number = None):
    assert self.storage == ENUM
    if (ident, label, blurb, number) == (None, None, None, None):
      return len (self.options) > 0
    for o in self.options:
      if ((ident  == None or o[0] == ident) and
          (label  == None or o[1] == label) and
          (blurb  == None or o[2] == blurb) and
          (number == None or o[3] == number)):
        return true
    return false
  def add_field (self, ident, type):
    assert self.storage == RECORD or self.storage == INTERFACE
    assert isinstance (ident, str)
    assert isinstance (type, TypeInfo)
    self.fields += [ (ident, type) ]
  def add_arg (self, ident, type, defaultinit):
    assert self.storage == FUNC
    assert isinstance (ident, str)
    assert isinstance (type, TypeInfo)
    self.args += [ (ident, type, defaultinit) ]
  def set_rtype (self, type):
    assert self.storage == FUNC
    assert isinstance (type, TypeInfo)
    assert self.rtype == None
    self.rtype = type
  def set_pure (self, vbool):
    assert self.storage == FUNC
    self.pure = bool (vbool)
  def set_collector (self, collkind):
    self.collector = collkind
  def add_method (self, ftype, issignal = False):
    assert self.storage == INTERFACE
    assert isinstance (ftype, TypeInfo)
    assert ftype.storage == FUNC
    assert isinstance (ftype.rtype, TypeInfo)
    ftype.ownertype = self
    ftype.issignal = issignal
    if issignal:
      self.signals += [ ftype ]
    else:
      self.methods += [ ftype ]
  def add_prerequisite (self, type):
    assert self.storage == INTERFACE
    assert isinstance (type, TypeInfo)
    assert type.storage == INTERFACE
    self.prerequisites += [ type ]
  def set_elements (self, ident, type):
    assert self.storage == SEQUENCE
    assert isinstance (ident, str)
    assert isinstance (type, TypeInfo)
    self.elements = (ident, type)
  def full_name (self):
    s = self.name
    namespace = self.namespace
    while namespace:
      s = namespace.name + '::' + s
      namespace = namespace.namespace
    return s
