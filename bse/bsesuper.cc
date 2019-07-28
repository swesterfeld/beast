// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl.html
#include "bsesuper.hh"
#include "bseproject.hh"
#include "bse/bsemain.hh"
#include "bse/internal.hh"
#include <string.h>

enum
{
  PARAM_0,
  PARAM_COPYRIGHT,
  PARAM_CREATION_TIME,
  PARAM_MOD_TIME
};


/* --- variables --- */
static GTypeClass	*parent_class = NULL;
static GQuark		 quark_author = 0;
static GQuark		 quark_license = 0;
static GSList		*bse_super_objects = NULL;


/* --- functions --- */
static void
bse_super_init (BseSuper *super,
		gpointer  rclass)
{
  super->creation_time = sfi_time_from_utc (sfi_time_system ());
  super->mod_time = super->creation_time;
  super->context_handle = ~0;

  bse_super_objects = g_slist_prepend (bse_super_objects, super);

  // default to 'Unnamed-###' unames
  bse_item_set (super, "uname", _("Unnamed"), NULL);
}

static void
bse_super_finalize (GObject *object)
{
  BseSuper *super = BSE_SUPER (object);

  bse_super_objects = g_slist_remove (bse_super_objects, super);

  /* chain parent class' handler */
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
bse_super_set_property (GObject      *object,
			guint         param_id,
			const GValue *value,
			GParamSpec   *pspec)
{
  BseSuper *super = BSE_SUPER (object);
  switch (param_id)
    {
    case PARAM_COPYRIGHT:
      if (g_object_get_qdata ((GObject*) super, quark_license) == NULL)
        g_object_set_qdata_full ((GObject*) super, quark_license,
                                 g_strdup (g_value_get_string (value)),
                                 g_free);
      g_object_notify ((GObject*) super, "license");
      break;
    case PARAM_MOD_TIME:
      super->mod_time = MAX (super->creation_time, sfi_value_get_time (value));
      break;
    case PARAM_CREATION_TIME:
      super->creation_time = sfi_value_get_time (value);
      /* we have to ensure that mod_time is always >= creation_time */
      if (super->creation_time > super->mod_time)
	{
	  super->mod_time = super->creation_time;
	  g_object_notify ((GObject*) super, "modification-time");
	}
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (super, param_id, pspec);
      break;
    }
}

static void
bse_super_get_property (GObject     *object,
			guint        param_id,
			GValue      *value,
			GParamSpec  *pspec)
{
  BseSuper *super = BSE_SUPER (object);
  switch (param_id)
    {
    case PARAM_MOD_TIME:
      sfi_value_set_time (value, super->mod_time);
      break;
    case PARAM_CREATION_TIME:
      sfi_value_set_time (value, super->creation_time);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (super, param_id, pspec);
      break;
    }
}

static void
super_modified (BseSuper *super,
                SfiTime	 stamp)
{
  super->mod_time = MAX (super->mod_time, stamp);
}

static void
super_compat_setup (BseItem               *item,
                    guint                  vmajor,
                    guint                  vminor,
                    guint                  vmicro)
{
  if (BSE_VERSION_CMP (vmajor, vminor, vmicro, 0, 7, 0) < 0)
    bse_item_set (item,
                  "author", "",
                  "license", "",
                  NULL);
}

static void
super_compat_finish (BseSuper       *super,
                     guint           vmajor,
                     guint           vminor,
                     guint           vmicro)
{
}

static void
bse_super_class_init (BseSuperClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  BseObjectClass *object_class = BSE_OBJECT_CLASS (klass);
  BseItemClass *item_class = BSE_ITEM_CLASS (klass);
  // BseSourceClass *source_class = BSE_SOURCE_CLASS (klass);

  parent_class = (GTypeClass*) g_type_class_peek_parent (klass);
  quark_author = g_quark_from_static_string ("author");
  quark_license = g_quark_from_static_string ("license");

  gobject_class->set_property = bse_super_set_property;
  gobject_class->get_property = bse_super_get_property;
  gobject_class->finalize = bse_super_finalize;

  item_class->compat_setup = super_compat_setup;

  klass->modified = super_modified;
  klass->compat_finish = super_compat_finish;

  bse_object_class_add_param (object_class, NULL,
			      PARAM_COPYRIGHT,
			      sfi_pspec_string ("copyright", NULL, NULL, NULL, "w")); // COMPAT-FIXME: remove around 0.7.0
  bse_object_class_add_param (object_class, "Time Stamps",
			      PARAM_CREATION_TIME,
			      sfi_pspec_time ("creation_time", _("Creation Time"), NULL,
					      SFI_PARAM_STANDARD_RDONLY));
  bse_object_class_add_param (object_class, "Time Stamps",
			      PARAM_MOD_TIME,
			      sfi_pspec_time ("modification_time", _("Last modification time"), NULL,
					      SFI_PARAM_STANDARD_RDONLY));
}

BSE_BUILTIN_TYPE (BseSuper)
{
  static const GTypeInfo super_info = {
    sizeof (BseSuperClass),

    (GBaseInitFunc) NULL,
    (GBaseFinalizeFunc) NULL,
    (GClassInitFunc) bse_super_class_init,
    (GClassFinalizeFunc) NULL,
    NULL /* class_data */,

    sizeof (BseSuper),
    0 /* n_preallocs */,
    (GInstanceInitFunc) bse_super_init,
  };

  return bse_type_register_abstract (BSE_TYPE_CONTAINER,
                                     "BseSuper",
                                     "Base type for item managers",
                                     __FILE__, __LINE__,
                                     &super_info);
}

namespace Bse {

SuperImpl::SuperImpl (BseObject *bobj) :
  ContainerImpl (bobj)
{
  // default-fill fields
  if (!Bse::global_config->author_default.empty())
    set_qauthor (Bse::global_config->author_default);
  if (!Bse::global_config->license_default.empty())
    set_qlicense (Bse::global_config->license_default);
}

SuperImpl::~SuperImpl ()
{}

void
SuperImpl::set_qauthor (const String& author)
{
  g_object_set_qdata_full ((GObject *) as<BseSuper*>(),
                           quark_author,
                           g_strdup (author.c_str()),
                           g_free);
}

void
SuperImpl::set_qlicense (const String& license)
{
  g_object_set_qdata_full ((GObject *) as<BseSuper*>(),
                           quark_license,
                           g_strdup (license.c_str()),
                           g_free);
}

String
SuperImpl::author() const
{
  BseSuper *self = const_cast<SuperImpl*> (this)->as<BseSuper *>();

  auto str = (const char *) g_object_get_qdata (static_cast<GObject*> (self), quark_author);
  return str ? str : ""; // avoid assigning NULL to std::string
}

void
SuperImpl::author (const String& new_author)
{
  String value = author();
  if (APPLY_IDL_PROPERTY (value, new_author))
    set_qauthor (value);
}

String
SuperImpl::license() const
{
  BseSuper *self = const_cast<SuperImpl*> (this)->as<BseSuper *>();

  auto str = (const char *) g_object_get_qdata (static_cast<GObject*> (self), quark_license);
  return str ? str : ""; // avoid assigning NULL to std::string
}

void
SuperImpl::license (const String& new_license)
{
  String value = license();
  if (APPLY_IDL_PROPERTY (value, new_license))
    set_qlicense (value);
}
// BseSuper *self = as<BseSuper*>();
// SuperIfaceP sp = super->as<SuperIfaceP>();

} // Bse
