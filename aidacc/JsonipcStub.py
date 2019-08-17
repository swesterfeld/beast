# This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
"""AidaJsonipcStub - Aida Code Generator for Jsonipc

More details at https://beast.testbit.org/
"""
import Decls, re, sys, os

# == Utilities ==
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
def identifier_name (joiner, type_name, member = None):
  parts = type_namespace_names (type_name) + [ type_name.name ]
  if member: parts += [ member ]
  return joiner.join (parts)
def ident (name):
  return re.sub (r'[^a-z0-9_A-Z]+', '_', name)
def enumstring (name):
  return re.sub (r'[^a-z0-9A-Z]+', '-', name.lower())
def get_cxxclass (tp):
  return identifier_name ('::', tp)
def get_cxxiface (tp):
  return get_cxxclass (tp) + 'Iface'
def get_cxximpl (tp):
  return get_cxxclass (tp) + 'Impl'
def get_jsclass (tp):
  return tp.name

class Generator:
  def __init__ (self, idl_file, module_name):
    assert isinstance (module_name, str)
    self.idl_file = idl_file
    self.module_name = module_name
    self.strip_path = ""
    self.idcounter = 1001
  def generate_types_jsonipc (self, implementation_types):
    s  = '// === Generated by JsonipcStub.py ===\n'
    # includes
    s += '#include "jsonipc/jsonipc.hh"\n'
    s += '\n'
    # collect impl types
    namespaces = []
    base_objects = []
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
      raise NotImplementedError ('Jsonipc code generation requires exactly 1 namespace (%d given)' % max_namespaces)
    self.namespace = namespaces[0][0].name
    del namespaces, max_namespaces
    # Collect Jsonipc::Class types
    jip_enum_types, jip_class_types = [], []
    for tp in types:
      if tp.is_forward:
        continue
      if tp.storage in (Decls.SEQUENCE, Decls.RECORD, Decls.INTERFACE):
        jip_class_types += [ tp ]
      elif tp.storage == Decls.ENUM:
        jip_enum_types += [ tp ]
    # Main binding registration
    s += 'static void\n'
    s += '%s_jsonipc_stub ()\n{\n' % self.namespace
    for tp in jip_enum_types:
      # tp.storage == Decls.ENUM:
      cxxclass, jsclass = get_cxxclass (tp), get_jsclass (tp)
      s += '  Jsonipc::Enum<%s> jsonipc__%s ("%s");\n' % (cxxclass, ident (cxxclass), jsclass)
      s += '  jsonipc__%s\n' % ident (cxxclass)
      for opt in tp.options:
        (evident, label, blurb, number) = opt
        # s += '  %s.set_const ("%s", %s);\n' % (v8ppenum (tp), ident, identifier_name ('::', tp, ident))
        s += '    .set (%s, "%s") // %s\n' % (identifier_name ('::', tp, evident), evident, enumstring (get_cxxclass (tp) + '-' + evident))
      s += '  ;\n'
    if jip_class_types:
      s += '  Jsonipc::Class<Aida::ImplicitBase> jsonipc__Aida_ImplicitBase ("AidaImplicitBase");\n'
    for tp in jip_class_types:
      b = ''
      # Records
      if tp.storage == Decls.RECORD:
        cxxclass, jsclass = get_cxxclass (tp), get_jsclass (tp)
        s += '  Jsonipc::Serializable<%s> jsonipc__%s ("%s");\n' % (cxxclass, ident (cxxclass), jsclass)
        # fields
        for fname, ftp in tp.fields:
          b += '    .set ("%s", &%s::%s)\n' % (fname, cxxclass, fname)
        # output non-empty binding
        if b:
          s += '  jsonipc__%s\n' % ident (cxxclass) + b + '  ;\n'
      # Interfaces
      if tp.storage == Decls.INTERFACE:
        cxxclass, jsclass = get_cxxiface (tp), get_jsclass (tp)
        s += '  Jsonipc::Class<%s> jsonipc__%s ("%sIface");\n' % (cxxclass, ident (cxxclass), jsclass)
        # inherit
        for tb in bases (tp):
          b += '    .inherit<%s> ("%sIface")\n' % (get_cxxiface (tb), get_jsclass (tb))
        if not tp.prerequisites: # no bases
          b += '    .inherit<Aida::ImplicitBase> ("AidaImplicitBase")\n'
        # properties
        for fname, ftp in tp.fields:
          b += '    .set ("%s", ' % fname
          b += '&%s::%s, &%s::%s)\n' % (cxxclass, fname, cxxclass, fname)
        # methods
        for mtp in tp.methods:
          mname = mtp.name
          b += '    .set ("%s", &%s::%s)\n' % (mname, cxxclass, mname)
        # output non-empty binding
        if b:
          s += '  jsonipc__%s\n' % ident (cxxclass) + b + '  ;\n'
    # Registration of conventional Impl types
    for tp in jip_class_types:
      if tp.storage == Decls.INTERFACE:
        cxxiface, cxximpl, jsclass = get_cxxiface (tp), get_cxximpl (tp), get_jsclass (tp)
        jsclass_ = 'Object_' if jsclass == 'Object' else jsclass # work around Javascript nameclash
        s += '  Jsonipc::Class<%s> jsonipc__%s ("%s");\n' % (cxximpl, ident (cxximpl), jsclass_)
        s += '  jsonipc__%s\n' % ident (cxximpl)
        s += '    .inherit<%s> ("%sIface")\n' % (cxxiface, jsclass)
        s += '  ;\n'
    s += '}\n'
    return s

def generate (namespace_list, **args):
  import sys, tempfile, os
  config = {}
  config.update (args)
  if '--print-include-path' in config.get ('backend-options', []):
    includestem = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
    includepath = os.path.join (includestem, 'jsonipc')
    print includepath
  else:
    outname = config.get ('output', 'testmodule')
    if outname == '-':
      raise RuntimeError ("-: stdout is not support for generation of multiple files")
    idlfiles = config['files']
    if len (idlfiles) != 1:
      raise RuntimeError ("JsonipcStub: exactly one IDL input file is required")
    gg = Generator (idlfiles[0], outname)
    for opt in config['backend-options']:
      if opt.startswith ('strip-path='):
        gg.strip_path += opt[11:]
    fname = outname
    fout = open (fname, 'w')
    textstring = gg.generate_types_jsonipc (config['implementation_types'])
    fout.write (textstring)
    fout.close()

# register extension hooks
__Aida__.add_backend (__file__, generate, __doc__)