/* BSE - Bedevilled Sound Engine                        -*-mode: c++;-*-
 * Copyright (C) 2003 Tim Janik
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */
#ifndef __BSE_CXX_PLUGIN_H__
#define __BSE_CXX_PLUGIN_H__

/* default plugin name specification (if omitted in plugin) */
#ifndef BSE_PLUGIN_NAME
#  ifdef BSE_PLUGIN_FALLBACK
#    define BSE_PLUGIN_NAME BSE_PLUGIN_FALLBACK
#  else /* !BSE_PLUGIN_NAME && !BSE_PLUGIN_FALLBACK */
#    define BSE_PLUGIN_NAME __FILE__
#  endif /* !BSE_PLUGIN_NAME && !BSE_PLUGIN_FALLBACK */
#endif /* !BSE_PLUGIN_NAME */

#include <bse/bsecxxmodule.h>
#include <bse/bseexports.h>
#include <bse/bseparam.h>

namespace Bse {

/* -- export identity --- */
/* provide plugin export identity, preceeding all type exports */
#ifndef BSE_COMPILATION
#define BSE_CXX_DEFINE_EXPORTS()                                                \
  static ::BseExportNode __export_chain_head = { NULL, BSE_EXPORT_NODE_LINK, }; \
  static ::BseExportIdentity __export_identity =                                \
                    BSE_EXPORT_IDENTITY (BSE_PLUGIN_NAME, __export_chain_head); \
  extern "C" {                                                                  \
    extern ::BseExportIdentity *const BSE_EXPORT_IDENTITY_SYMBOL;               \
    ::BseExportIdentity *const BSE_EXPORT_IDENTITY_SYMBOL = &__export_identity; \
  }
#define BSE_CXX_EXPORT_IDENTITY    __export_identity
#else   /* BSE internal "Plugins" */
#define BSE_CXX_DEFINE_EXPORTS()
#define BSE_CXX_EXPORT_IDENTITY    bse_builtin_export_identity
extern "C" {
extern ::BseExportIdentity bse_builtin_export_identity; /* sync with bseplugin.h */
};
#endif


/* --- enum registration --- */
/* enum registration is based on a static ExportTypeKeeper
 * object, which provides the enum's get_type() implementation and
 * auto-registers the enum's export node with the plugin's
 * export_identity.
 */
#define BSE_CXX_DECLARED_ENUM_TYPE(EnumType)                            \
  (bse_type_keeper__3##EnumType.get_type ())
#define BSE_CXX_DECLARE_ENUM(EnumType,EnumName,N,ICode)                 \
  template<class E> static BseExportNode* bse_export_node ();           \
  template<> static BseExportNode*                                      \
  bse_export_node<EnumType> ()                                          \
  {                                                                     \
    static BseExportNodeEnum enode = {                                  \
      { NULL, BSE_EXPORT_NODE_ENUM, EnumName, },                        \
    };                                                                  \
    static GEnumValue values[N + 1];                                    \
    if (!enode.values) {                                                \
      GEnumValue *v = values;                                           \
      ICode; /* initializes values via *v++ = ...; */                   \
      g_assert (v == values + N);                                       \
      *v++ = ::Bse::EnumValue (0, 0, 0); /* NULL termination */         \
      enode.values = values;                                            \
    }                                                                   \
    return &enode.node;                                                 \
  }                                                                     \
  extern ::Bse::ExportTypeKeeper bse_type_keeper__3##EnumType;
#define BSE_CXX_REGISTER_ENUM(EnumType)                                 \
  ::Bse::ExportTypeKeeper                                               \
         bse_type_keeper__3##EnumType (bse_export_node<EnumType>,       \
                                       &BSE_CXX_EXPORT_IDENTITY);
/* convenience creator to allow easy assignments of GEnumValue structs */
inline const GEnumValue
EnumValue (int         int_value,
           const char *value_name,
           const char *value_nick)
{
  GEnumValue value;
  value.value = int_value;
  value.value_name = const_cast<char*> (value_name);
  value.value_nick = const_cast<char*> (value_nick);
  return value;
}


/* --- procedure registration --- */
/* procedure registration works similar to enum registration. */
#define BSE_CXX_DECLARED_PROC_TYPE(ProcType)                            \
  (bse_type_keeper__9##ProcType.get_type ())
#define BSE_CXX_DECLARE_PROC(ProcType)                                  \
  extern ::Bse::ExportTypeKeeper bse_type_keeper__9##ProcType;
#define BSE_CXX_REGISTER_PROC(ProcType)                                         \
  template<class C> static ::BseExportNode* bse_export_node ();                 \
  template<> static ::BseExportNode*                                            \
  bse_export_node<Procedure_##ProcType> ()                                      \
  {                                                                             \
    static ::BseExportNodeProc pnode = {                                        \
      { NULL, ::BSE_EXPORT_NODE_PROC, NULL, },                                  \
      0, Procedure_##ProcType::init, Procedure_##ProcType::marshal,             \
    };                                                                          \
    if (!pnode.node.name) {                                                     \
      pnode.node.name = Procedure_##ProcType::type_name();                      \
      pnode.node.category = Procedure_##ProcType::category();                   \
      pnode.node.authors = Procedure_##ProcType::authors();                     \
      pnode.node.license = Procedure_##ProcType::license();                     \
      pnode.node.pixstream = Procedure_##ProcType::pixstream();                 \
      pnode.node.blurb = Procedure_##ProcType::blurb();                         \
    }                                                                           \
    return &pnode.node;                                                         \
  }                                                                             \
  ::Bse::ExportTypeKeeper                                                       \
         bse_type_keeper__9##ProcType (bse_export_node<Procedure_##ProcType>,   \
                                   &BSE_CXX_EXPORT_IDENTITY);


/* --- class registration --- */
/* class registration works similar to enum registration.
 * in addition, we need to define a couple trampoline functions to make
 * C++ methods callable, and for effects, we're providing some basic
 * method implementations to interface with the synmthesis Module.
 */
#define BSE_CXX_DECLARED_CLASS_TYPE(ClassType)                          \
  (bse_type_keeper__0##ClassType.get_type ())
#define BSE_CXX_DECLARE_CLASS(ClassType)                                \
  extern ::Bse::ExportTypeKeeper bse_type_keeper__0##ClassType;
#define BSE_CXX_REGISTER_EFFECT(Effect)                                         \
  BSE_CXX_DEFINE_SET_PROPERTY (Effect ## Base);                                 \
  BSE_CXX_DEFINE_GET_PROPERTY (Effect ## Base);                                 \
  BSE_CXX_DEFINE_CLASS_INIT (Effect,                                            \
                             BSE_CXX_SYM (Effect ## Base, set_property),        \
                             BSE_CXX_SYM (Effect ## Base, get_property));       \
  BSE_CXX_DEFINE_INSTANCE_INIT (Effect);                                        \
  BSE_CXX_DEFINE_STATIC_DATA (Effect##Base);                                    \
  template<class C> static ::BseExportNode* bse_export_node ();                 \
  template<> static ::BseExportNode*                                            \
  bse_export_node<Effect> ()                                                    \
  {                                                                             \
    static ::BseExportNodeClass cnode = {                                       \
      { NULL, ::BSE_EXPORT_NODE_CLASS, NULL, },                                 \
      "BseEffect", BSE_CXX_COMMON_CLASS_SIZE,                                   \
      (GClassInitFunc) BSE_CXX_SYM (Effect, class_init), NULL,                  \
      BSE_CXX_INSTANCE_OFFSET + sizeof (Effect),                                \
      BSE_CXX_SYM (Effect, instance_init),                                      \
    };                                                                          \
    if (!cnode.node.name) {                                                     \
      cnode.node.name = Effect::type_name();                                    \
      cnode.node.category = Effect::category();                                 \
      cnode.node.authors = Effect::authors();                                   \
      cnode.node.license = Effect::license();                                   \
      cnode.node.pixstream = Effect::pixstream();                               \
      cnode.node.blurb = Effect::blurb();                                       \
    }                                                                           \
    return &cnode.node;                                                         \
  }                                                                             \
  ::Bse::ExportTypeKeeper                                                       \
         bse_type_keeper__0##Effect (bse_export_node<Effect>,                   \
                                     &BSE_CXX_EXPORT_IDENTITY);
/* effect method: create_module(); */
#define BSE_CXX_DEFINE_CREATE_MODULE(ObjectType,ModuleType,ParamType)           \
  Bse::SynthesisModule*                                                         \
  ObjectType::create_module (unsigned int context_handle,                       \
                             GslTrans    *trans)                                \
  { /* create a synthesis module */                                             \
    return new ModuleType();                                                    \
  }
/* effect method: module_configurator(); */
#define BSE_CXX_DEFINE_MODULE_CONFIGURATOR(ObjectType,ModuleType,ParamType)     \
Bse::SynthesisModule::Accessor*                                                 \
ObjectType::module_configurator()                                               \
{                                                                               \
  return SynthesisModule::accessor (&ModuleType::config, ParamType (this));     \
}
/* convenience macro to define BseEffect module methods */
#define BSE_EFFECT_INTEGRATE_MODULE(ObjectType,ModuleType,ParamType)            \
  BSE_CXX_DEFINE_CREATE_MODULE (ObjectType,ModuleType,ParamType);               \
  BSE_CXX_DEFINE_MODULE_CONFIGURATOR (ObjectType,ModuleType,ParamType);
/* implement static_data portions used by auto-generated classes */
#define BSE_CXX_DEFINE_STATIC_DATA(ObjectType)                                  \
  ObjectType::StaticData ObjectType::static_data;


/* --- type keeper for export nodes --- */
class ExportTypeKeeper
{
  BseExportNode    *enode;
  explicit          ExportTypeKeeper (const ExportTypeKeeper&);
  ExportTypeKeeper& operator=        (const ExportTypeKeeper&);
public:
  explicit          ExportTypeKeeper (::BseExportNode*   (*export_node) (),
                                      ::BseExportIdentity *export_identity)
  {
    enode = export_node ();
    enode->next = export_identity->export_chain;
    export_identity->export_chain = enode;
  }
  const GType get_type()
  {
    return enode->type;
  }
};

} // Bse

#endif /* __BSE_CXX_PLUGIN_H__ */
