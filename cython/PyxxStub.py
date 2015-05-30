# This Source Code Form is licensed MPLv2: http://mozilla.org/MPL/2.0
"""AidaPyxxStub - Aida Cython Code Generator

More details at http://www.rapicorn.org/
"""
import Decls, re, sys, os

def reindent (prefix, lines):
  return re.compile (r'^(?=.)', re.M).sub (prefix, lines)
def strcquote (string):
  result = ''
  for c in string:
    oc = ord (c)
    ec = { 92 : r'\\',
            7 : r'\a',
            8 : r'\b',
            9 : r'\t',
           10 : r'\n',
           11 : r'\v',
           12 : r'\f',
           13 : r'\r',
           12 : r'\f'
         }.get (oc, '')
    if ec:
      result += ec
      continue
    if oc <= 31 or oc >= 127:
      result += '\\' + oct (oc)[-3:]
    elif c == '"':
      result += r'\"'
    else:
      result += c
  return '"' + result + '"'

# == Utilities ==
def type_namespace_names (tp):
  namespaces = tp.list_namespaces() # [ Namespace... ]
  return [d.name for d in namespaces if d.name]
def underscore_namespace (tp):
  return '__'.join (type_namespace_names (tp))
def colon_namespace (tp):
  return '::'.join (type_namespace_names (tp))
def underscore_typename (tp):
  if tp.storage == Decls.ANY:
    return 'Rapicorn__Any'
  return '__'.join (type_namespace_names (tp) + [ tp.name ])
def colon_typename (tp):
  name = '::'.join (type_namespace_names (tp) + [ tp.name ])
  if tp.storage == Decls.INTERFACE:
    name += 'H' # e.g. WidgetHandle
  return name

# exception class:
# const char *exclass = PyExceptionClass_Check (t) ? PyExceptionClass_Name (t) : "<unknown>";
# exclass = strrchr (exclass, '.') ? strrchr (exclass, '.') + 1 : exclass;

class Generator:
  def __init__ (self, idl_file, module_name):
    assert isinstance (module_name, str)
    self.ntab = 26
    self.idl_file = idl_file
    self.module_name = module_name
    self.namespaces = []
    self.strip_path = ""
  def tabwidth (self, n):
    self.ntab = n
  def format_to_tab (self, string, indent = ''):
    if len (string) >= self.ntab:
      return indent + string + ' '
    else:
      f = '%%-%ds' % self.ntab  # '%-20s'
      return indent + f % string
  def zero_value_pyimpl (self, type):
    return { Decls.BOOL      : '0',
             Decls.INT32     : '0',
             Decls.INT64     : '0',
             Decls.FLOAT64   : '0',
             Decls.ENUM      : '0',
             Decls.RECORD    : 'None',
             Decls.SEQUENCE  : '()',
             Decls.STRING    : "''",
             Decls.INTERFACE : "None",
             Decls.ANY       : '()',
           }[type.storage]
  def cxx_type (self, type_node):
    tstorage = type_node.storage
    if tstorage == Decls.VOID:          return 'void'
    if tstorage == Decls.BOOL:          return 'bool'
    if tstorage == Decls.INT32:         return 'int'
    if tstorage == Decls.INT64:         return 'int64_t'
    if tstorage == Decls.FLOAT64:       return 'double'
    if tstorage == Decls.STRING:        return 'String'
    if tstorage == Decls.ANY:           return 'Rapicorn__Any'
    fullnsname = underscore_typename (type_node)
    return fullnsname
  def open_namespace (self, string, joiner, type_node):
    nsn = type_namespace_names (type_node)
    if self.last_namespace != nsn:
      self.last_namespace = nsn
      return string % joiner.join (nsn)
    return ''
  def generate_types_pyxx (self, implementation_types):
    s  = '# === Generated by PyxxStub.py ===            -*-mode:python;-*-\n'
    s += 'from libcpp cimport *\n'
    s += 'from cython.operator cimport dereference as deref\n'
    s += 'from libc.stdint cimport *\n'
    s += 'from cpython.object cimport Py_LT, Py_LE, Py_EQ, Py_NE, Py_GT, Py_GE\n'
    self.tabwidth (16)
    # collect impl types
    types = []
    for tp in implementation_types:
      if tp.isimpl:
        types += [ tp ]
    # C++ Include
    include_header = "ui/clientapi.hh"
    include_namespace = "Rapicorn"
    s += 'cdef extern from "%s" namespace "%s":\n' % (include_header, include_namespace) # FIXME: header and namespace hardcoded
    s += '  pass\n'
    # C++ Builtins # FIXME: move elsewhere
    s += '\n'
    s += '# Builtins\n'
    s += 'cdef extern from * namespace "Rapicorn":\n'
    s += '  cppclass %-40s "%s"\n' % ('Rapicorn__Any', 'Rapicorn::Any')
    s += '  cppclass Rapicorn__Any:\n'
    s += '    pass\n' # FIXME
    s += 'cdef Rapicorn__Any Rapicorn__Any__unwrap (object pyo1):\n'
    s += '  raise NotImplementedError\n' # FIXME
    s += 'cdef object Rapicorn__Any__wrap (const Rapicorn__Any &cxx1):\n'
    s += '  raise NotImplementedError\n' # FIXME
    # C++ Declarations
    s += '\n'
    s += '# C++ declarations\n'
    self.last_namespace = None
    for tp in types:
      if tp.typedef_origin or tp.is_forward:
        continue
      if tp.storage in (Decls.SEQUENCE, Decls.RECORD, Decls.INTERFACE):
        s += self.open_namespace ('cdef extern from * namespace "%s":\n', '::', tp)
        s += '  cppclass %-40s "%s"\n' % (underscore_typename (tp), colon_typename (tp))
    # C++ Enum Values
    s += '\n'
    s += '# C++ Enums\n'
    self.last_namespace = None
    for tp in types:
      if tp.typedef_origin or tp.is_forward:
        continue
      if tp.storage == Decls.ENUM:
        s += self.open_namespace ('cdef extern from * namespace "%s":\n', '::', tp)
        s += '  cdef enum %-50s "%s":\n' % (underscore_typename (tp), colon_typename (tp))
        for opt in tp.options:
          (ident, label, blurb, number) = opt
          s += '    %-60s "%s"\n' % ('%s__%s' % (tp.name, ident), colon_namespace (tp) + '::' + ident)
    # TODO: C++ Callback Types
    # TODO: C++ Marshal Functions
    # C++ classes
    s += '\n'
    s += '# C++ classes\n'
    self.last_namespace = None
    for tp in types:
      if tp.typedef_origin or tp.is_forward:
        continue
      s += self.open_namespace ('cdef extern from * namespace "%s":\n', '::', tp)
      if tp.storage == Decls.SEQUENCE:
        ename, etp = tp.elements
        s += '  cppclass %s (vector[%s]):\n' % (underscore_typename (tp), underscore_typename (etp))
        s += '    pass\n' # FIXME
      elif tp.storage in (Decls.RECORD, Decls.INTERFACE):
        s += '  cppclass %s:\n' % underscore_typename (tp)
        s += '    pass\n' # FIXME
    # Py Enums
    s += '\n'
    s += '# Python Enums\n'
    for tp in [t for t in types if t.storage == Decls.ENUM]:
      s += '\nclass %s (Enum):\n' % tp.name
      for opt in tp.options:
        (ident, label, blurb, number) = opt
        s += '  %-40s = %s\n' % (ident, '%s__%s' % (tp.name, ident))
      for opt in tp.options:
        (ident, label, blurb, number) = opt
        s += '%-42s =  %s.%s\n' % (ident, tp.name, ident)
    # Py Classes
    s += '\n'
    s += '# Python classes\n'
    self.last_namespace = None
    for tp in types:
      if tp.typedef_origin or tp.is_forward:
        continue
      assert type_namespace_names (tp) == [ 'Rapicorn' ] # FIXME: assert unique namespace
      type____name, type_cc_name = underscore_typename (tp), colon_typename (tp)
      if tp.storage == Decls.RECORD:
        s += '\ncdef class %s:\n' % tp.name
        for field in tp.fields:
          ident, type_node = field
          s += '  cdef %s %s\n' % (self.cxx_type (type_node), ident)
        for field in tp.fields:
          ident, type_node = field
          s += '  property %s:\n' % ident
          s += '    def __get__ (self):    return %s\n' % self.py_wrap ('self.%s' % ident, type_node)
          s += '    def __set__ (self, v): self.%s = %s\n' % (ident, self.cxx_unwrap ('v', type_node))
      elif tp.storage == Decls.SEQUENCE:
        ename, etp = tp.elements
        s += '\ncdef class %s (list):\n' % tp.name
        s += '  pass\n'
      elif tp.storage == Decls.INTERFACE:
        s += '\ncdef class %s:\n' % tp.name
        s += '  pass\n' # FIXME
      if tp.storage in (Decls.SEQUENCE, Decls.RECORD, Decls.INTERFACE):
        s += 'cdef %s %s__unwrap (object pyo1) except *:\n' % (type____name, type____name)
        s += reindent ('  ', self.cxx_unwrap_impl ('pyo1', tp))
        s += 'cdef object %s__wrap (const %s &cxx1):\n' % (type____name, type____name)
        s += reindent ('  ', self.py_wrap_impl ('cxx1', tp))
    return s
  def py_wrap (self, ident, tp): # wrap a C++ object to return a PyObject
    if tp.storage in (Decls.ANY, Decls.SEQUENCE, Decls.RECORD, Decls.INTERFACE):
      return underscore_typename (tp) + '__wrap (%s)' % ident
    return ident
  def cxx_unwrap (self, ident, tp): # unwrap a PyObject to yield a C++ object
    if tp.storage in (Decls.ANY, Decls.SEQUENCE, Decls.RECORD, Decls.INTERFACE):
      return underscore_typename (tp) + '__unwrap (%s)' % ident
    return ident
  def cxx_unwrap_impl (self, ident, tp):
    s = ''
    if tp.storage == Decls.SEQUENCE:
      ename, etp = tp.elements
      s += 'cdef %s thisp\n' % self.cxx_type (tp)
      s += 'for element in %s:\n' % ident
      s += '  thisp.push_back (%s);\n' % self.cxx_unwrap ('element', etp)
      s += 'return thisp\n'
    else:
      s += 'raise NotImplementedError\n'
    return s
  def py_wrap_impl (self, ident, tp):
    s = ''
    if tp.storage == Decls.SEQUENCE:
      ename, etp = tp.elements
      s += 'self = %s()\n' % tp.name
      s += 'for idx in range (%s.size()):\n' % ident
      s += '  self.append (%s)\n' % self.py_wrap ('%s[idx]' % ident, etp)
      s += 'return self\n'
    else:
      s += 'raise NotImplementedError\n'
    return s

def generate (namespace_list, **args):
  import sys, tempfile, os
  config = {}
  config.update (args)
  outname = config.get ('output', 'testmodule')
  if outname == '-':
    raise RuntimeError ("-: stdout is not support for generation of multiple files")
  idlfiles = config['files']
  if len (idlfiles) != 1:
    raise RuntimeError ("PyxxStub: exactly one IDL input file is required")
  gg = Generator (idlfiles[0], outname)
  for opt in config['backend-options']:
    if opt.startswith ('strip-path='):
      gg.strip_path += opt[11:]
  fname = outname
  fout = open (fname, 'w')
  textstring = gg.generate_types_pyxx (config['implementation_types'])
  fout.write (textstring)
  fout.close()

# register extension hooks
__Aida__.add_backend (__file__, generate, __doc__)
