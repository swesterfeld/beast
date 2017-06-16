# This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
"""AidaPyxxStub - Aida Cython Code Generator

More details at http://www.rapicorn.org/
"""
import Decls, re, sys, os

# == Utilities ==
def class_digest (class_info):
  return digest2cbytes (class_info.type_hash())
def digest2cbytes (digest):
  return '0x%02x%02x%02x%02x%02x%02x%02x%02xULL, 0x%02x%02x%02x%02x%02x%02x%02x%02xULL' % digest
def hasancestor (child, parent):
  for p in child.prerequisites:
    if p == parent or hasancestor (p, parent):
      return True
def inherit_reduce (type_list):
  # find the type(s) we *directly* derive from
  reduced = []
  while type_list:
    p = type_list.pop()
    skip = 0
    for c in type_list + reduced:
      if c == p or hasancestor (c, p):
        skip = 1
        break
    if not skip:
      reduced = [ p ] + reduced
  return reduced
def bases (tp):
  ancestors = [pr for pr in tp.prerequisites]
  return inherit_reduce (ancestors)
def type_namespace_names (tp):
  namespaces = tp.list_namespaces() # [ Namespace... ]
  return [d.name for d in namespaces if d.name]
def underscore_namespace (tp):
  return '__'.join (type_namespace_names (tp))
def colon_namespace (tp):
  return '::'.join (type_namespace_names (tp))
def identifier_name (joiner, type_name, member = None):
  parts = type_namespace_names (type_name) + [ type_name.name ]
  if member: parts += [ member ]
  return joiner.join (parts)
def underscore_typename (tp):
  if tp.storage == Decls.ANY:
    return 'Rapicorn__Any'
  return identifier_name ('__', tp)
def colon_typename (tp):
  name = identifier_name ('::', tp)
  if tp.storage == Decls.INTERFACE:
    name += 'H' # e.g. WidgetHandle
  return name
def v8ppclass_type (tp):
  return 'V8ppType_' + identifier_name ('', tp)
def v8ppclass (tp):
  return identifier_name ('', tp) + '_class_'

class Generator:
  def __init__ (self, idl_file, module_name):
    assert isinstance (module_name, str)
    self.ntab = 26
    self.idl_file = idl_file
    self.module_name = module_name
    self.strip_path = ""
    self.idcounter = 1001
    self.marshallers = {}
  def generate_types_v8 (self, implementation_types):
    s  = '// === Generated by V8Stub.py ===            -*-mode:javascript;-*-\n'
    # includes
    s += '#include "v8pp/context.hpp"\n'
    s += '#include "v8pp/module.hpp"\n'
    s += '#include "v8pp/class.hpp"\n'
    # collect impl types
    namespaces = []
    types = []
    for tp in implementation_types:
      if tp.isimpl:
        types += [ tp ]
        namespaces.append (tp.list_namespaces())
    if not types:
      return s
    # check unique namespace
    while len (namespaces) >= 2 and namespaces[0] == namespaces[1]:
      namespaces = namespaces[1:]
    namespaces = [[n for n in names if n.name] for names in namespaces] # strip (initial) empty names
    max_namespaces = max (len (namespaces), len (namespaces[0]))
    if max_namespaces != 1:
      raise NotImplementedError ('V8 code generation requires exactly 1 namespace (%d given)' % max_namespaces)
    self.namespace = namespaces[0][0].name
    del namespaces, max_namespaces
    # Collect v8pp::class_ types
    v8pp_class_types = []
    for tp in types:
      if tp.is_forward:
        continue
      if tp.storage in (Decls.SEQUENCE, Decls.RECORD, Decls.INTERFACE):
        v8pp_class_types += [ tp ]
    # C++ class type aliases for v8pp::class_
    s += '\n// v8pp::class_ aliases\n'
    s += 'typedef %-40s V8ppType_AidaRemoteHandle;\n' % 'v8pp::class_<Aida::RemoteHandle>'
    for tp in v8pp_class_types:
      s += 'typedef %-40s %s;\n' % ('v8pp::class_<%s>' % colon_typename (tp), v8ppclass_type (tp))
    # C++ class specialisations for v8pp::convert
    s += '\n// v8pp::convert<> specialisations\n'
    s += 'namespace v8pp {\n'
    s += 'template<> struct convert%-40s : convert_AidaRemoteHandle<Aida::RemoteHandle> {};\n' % '<Aida::RemoteHandle>'
    s += 'template<> struct convert%-40s : convert_AidaRemoteHandle<Aida::RemoteHandle*> {};\n' % '<Aida::RemoteHandle*>'
    for tp in v8pp_class_types:
      cn = colon_typename (tp)
      if tp.storage == Decls.INTERFACE:
        s += 'template<> struct convert%-40s : convert_AidaRemoteHandle<%s>  {};\n' % ('<%s>' % cn, cn)
        s += 'template<> struct convert%-40s : convert_AidaRemoteHandle<%s*> {};\n' % ('<%s*>' % cn, cn)
        s += 'template<> struct convert%-40s : convert_AidaRemoteHandle<%s>  {};\n' % ('<Aida::RemoteMember<%s>>' % cn, cn)
        s += 'template<> struct convert%-40s : convert_AidaRemoteHandle<%s*> {};\n' % ('<Aida::RemoteMember<%s>*>' % cn, cn)
      elif tp.storage == Decls.SEQUENCE:
        s += 'template<> struct convert%-40s : convert_AidaSequence<%s> {};\n' % ('<%s>' % cn, cn)
    s += '} // v8pp\n'
    # V8stub - main binding stub
    s += '\n// Main binding stub\n'
    s += 'struct V8stub final {\n'
    s += '  v8::Isolate                             *const isolate_;\n'
    s += '  %-40s %s;\n' % ('V8ppType_AidaRemoteHandle', 'AidaRemoteHandle_class_')
    for tp in v8pp_class_types:
      s += '  %-40s %s;\n' % (v8ppclass_type (tp), v8ppclass (tp))
    s += '  v8pp::module                             module_;\n'
    s += 'public:\n'
    s += '  V8stub (v8::Isolate *const isolate);\n'
    s += '};\n'
    # V8stub ctor - begin
    s += '\nV8stub::V8stub (v8::Isolate *const isolate) :\n'
    s += '  isolate_ (isolate),\n'
    s += '  AidaRemoteHandle_class_ (isolate),\n'
    for tp in v8pp_class_types:
      s += '  %s (isolate),\n' % v8ppclass (tp)
    s += '  module_ (isolate)\n'
    s += '{\n'
    # Wrapper registration
    for tp in v8pp_class_types:
      cn = colon_typename (tp)
      if tp.storage == Decls.INTERFACE:
        s += '  aida_remote_handle_wrapper_map (Aida::TypeHash '
        s += '(%s), ' % class_digest (tp)
        s += 'aida_remote_handle_wrapper_impl<%s>);\n' % cn
    # Class bindings
    for tp in v8pp_class_types:
      cn = colon_typename (tp)
      b = ''
      # Class inheritance
      if tp.storage == Decls.INTERFACE:
        for tb in bases (tp):
          b += '    .inherit<%s>()\n' % colon_typename (tb)
        if len (bases (tp)) == 0:
          b += '    .inherit<%s>()\n' % 'Aida::RemoteHandle'
      # Class ctor
      if tp.storage == Decls.SEQUENCE or tp.storage == Decls.RECORD:
        b += '    .ctor()\n'
      # Class properties
      if tp.storage != Decls.SEQUENCE:
        for fname, ftp in tp.fields:
          if tp.storage == Decls.INTERFACE:
            b += '    .set ("%s", ' % fname
            b += 'v8pp::property (&%s::%s, &%s::%s))\n' % (cn, fname, cn, fname)
          elif tp.storage == Decls.RECORD:
            b += '    .set ("%s", &%s::%s)\n' % (fname, cn, fname)
      # Class methods
      if tp.storage == Decls.INTERFACE:
        for mtp in tp.methods:
          rtp, mname = mtp.rtype, mtp.name
          b += '    .set ("%s", &%s::%s)\n' % (mname, cn, mname)
      # output only non-empty bindings
      if b:
        s += '  %s\n' % v8ppclass (tp)
        s += b
        s += '  ;\n'
    # Class registration
    for tp in v8pp_class_types:
      s += '  module_.set ("%s", %s);\n' % (tp.name, v8ppclass (tp))
    # V8stub ctor - end
    s += '}\n'
    return s

def generate (namespace_list, **args):
  import sys, tempfile, os
  config = {}
  config.update (args)
  if '--print-include-path' in config.get ('backend-options', []):
    includestem = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
    includepath = os.path.join (includestem, 'v8')
    print includepath
  else:
    outname = config.get ('output', 'testmodule')
    if outname == '-':
      raise RuntimeError ("-: stdout is not support for generation of multiple files")
    idlfiles = config['files']
    if len (idlfiles) != 1:
      raise RuntimeError ("V8Stub: exactly one IDL input file is required")
    gg = Generator (idlfiles[0], outname)
    for opt in config['backend-options']:
      if opt.startswith ('strip-path='):
        gg.strip_path += opt[11:]
    fname = outname
    fout = open (fname, 'w')
    textstring = gg.generate_types_v8 (config['implementation_types'])
    fout.write (textstring)
    fout.close()

# register extension hooks
__Aida__.add_backend (__file__, generate, __doc__)