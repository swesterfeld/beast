/* BEAST - Bedevilled Audio System
 * Copyright (C) 1999-2002 Tim Janik and Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */
#include "bstpreferences.h"

#include "bstgconfig.h"
#include <unistd.h>


/* --- prototypes --- */
static void	bst_preferences_class_init	(BstPreferencesClass	*klass);
static void	bst_preferences_init		(BstPreferences		*prefs);
static void	bst_preferences_destroy		(GtkObject		*object);


/* --- static variables --- */
static gpointer             parent_class = NULL;
static BstPreferencesClass *bst_preferences_class = NULL;


/* --- functions --- */
GtkType
bst_preferences_get_type (void)
{
  static GtkType preferences_type = 0;
  
  if (!preferences_type)
    {
      GtkTypeInfo preferences_info =
      {
	"BstPreferences",
	sizeof (BstPreferences),
	sizeof (BstPreferencesClass),
	(GtkClassInitFunc) bst_preferences_class_init,
	(GtkObjectInitFunc) bst_preferences_init,
        /* reserved_1 */ NULL,
	/* reserved_2 */ NULL,
	(GtkClassInitFunc) NULL,
      };
      
      preferences_type = gtk_type_unique (GTK_TYPE_VBOX, &preferences_info);
    }
  
  return preferences_type;
}

static void
bst_preferences_class_init (BstPreferencesClass *class)
{
  GtkObjectClass *object_class;

  object_class = GTK_OBJECT_CLASS (class);

  bst_preferences_class = class;
  parent_class = gtk_type_class (GTK_TYPE_VBOX);

  object_class->destroy = bst_preferences_destroy;
}

static void
bst_preferences_init (BstPreferences *prefs)
{
  prefs->gconf = NULL;
  prefs->bse_param_view = g_object_connect (gtk_widget_new (BST_TYPE_PARAM_VIEW,
							    "visible", TRUE,
							    NULL),
					    "swapped_signal::destroy", g_nullify_pointer, &prefs->bse_param_view,
					    NULL);
  bst_param_view_set_mask (BST_PARAM_VIEW (prefs->bse_param_view), BSE_TYPE_GCONFIG, BSE_TYPE_GCONFIG, NULL, NULL);
  prefs->bst_param_view = g_object_connect (gtk_widget_new (BST_TYPE_PARAM_VIEW, NULL),
					    "swapped_signal::destroy", g_nullify_pointer, &prefs->bst_param_view,
					    NULL);
  bst_param_view_set_mask (BST_PARAM_VIEW (prefs->bst_param_view), BST_TYPE_GCONFIG, 0, NULL, NULL);

  prefs->notebook = g_object_connect (gtk_widget_new (GTK_TYPE_NOTEBOOK,
						      "visible", TRUE,
						      "parent", prefs,
						      "tab_pos", GTK_POS_TOP,
						      "scrollable", FALSE,
						      "can_focus", TRUE,
						      "border_width", 5,
						      NULL),
				      "swapped_signal::destroy", g_nullify_pointer, &prefs->notebook,
				      "signal_after::switch-page", gtk_widget_viewable_changed, NULL,
				      NULL);
  gtk_notebook_append_page (GTK_NOTEBOOK (prefs->notebook), prefs->bst_param_view,
			    gtk_widget_new (GTK_TYPE_LABEL,
					    "visible", TRUE,
					    "label", "BEAST",
					    NULL));
  gtk_notebook_append_page (GTK_NOTEBOOK (prefs->notebook), prefs->bse_param_view,
			    gtk_widget_new (GTK_TYPE_LABEL,
					    "visible", TRUE,
					    "label", "BSE",
					    NULL));
}

static void
bst_preferences_destroy (GtkObject *object)
{
  BstPreferences *prefs = BST_PREFERENCES (object);

  bst_preferences_set_gconfig (prefs, NULL);

  GTK_OBJECT_CLASS (parent_class)->destroy (object);
}

GtkWidget*
bst_preferences_new (BseGConfig *gconf)
{
  GtkWidget *prefs;

  g_return_val_if_fail (BSE_IS_GCONFIG (gconf), NULL);

  prefs = gtk_widget_new (BST_TYPE_PREFERENCES, NULL);
  bst_preferences_set_gconfig (BST_PREFERENCES (prefs), gconf);

  return prefs;
}

static void
preferences_lock_changed (BstPreferences *prefs)
{
  if (prefs->apply)
    gtk_widget_set_sensitive (prefs->apply, prefs->gconf ? bse_gconfig_can_apply (prefs->gconf) : FALSE);
}

void
bst_preferences_set_gconfig (BstPreferences *prefs,
			     BseGConfig     *gconf)
{
  g_return_if_fail (BST_IS_PREFERENCES (prefs));
  if (gconf)
    g_return_if_fail (BSE_IS_GCONFIG (gconf));

  if (prefs->gconf)
    {
      bst_param_view_set_object (BST_PARAM_VIEW (prefs->bse_param_view), 0);
      bst_param_view_set_object (BST_PARAM_VIEW (prefs->bst_param_view), 0);
      g_object_disconnect (prefs->gconf,
			   "any_signal", preferences_lock_changed, prefs,
			   NULL);
      bse_object_unref (BSE_OBJECT (prefs->gconf));
      prefs->gconf = NULL;
    }
  prefs->gconf = gconf;
  if (prefs->gconf && prefs->notebook)
    {
      bse_object_ref (BSE_OBJECT (prefs->gconf));
      g_object_connect (prefs->gconf,
			"swapped_signal::lock_changed", preferences_lock_changed, prefs,
			NULL);
      bst_param_view_set_object (BST_PARAM_VIEW (prefs->bse_param_view), BSE_OBJECT_ID (prefs->gconf));
      bst_param_view_set_object (BST_PARAM_VIEW (prefs->bst_param_view), BSE_OBJECT_ID (prefs->gconf));
      if (g_type_next_base (BSE_OBJECT_TYPE (prefs->gconf), BSE_TYPE_GCONFIG))
	{
	  gtk_widget_show (prefs->bst_param_view);
	  gtk_notebook_set_current_page (GTK_NOTEBOOK (prefs->notebook), 0);
	}
      else
	gtk_widget_hide (prefs->bst_param_view);
    }
  preferences_lock_changed (prefs);
}

void
bst_preferences_rebuild (BstPreferences *prefs)
{
  BseObject *object;
  BseObjectClass *class;
  
  g_return_if_fail (BST_IS_PREFERENCES (prefs));
  
  if (!prefs->gconf)
    return;

  object = BSE_OBJECT (prefs->gconf);
  class = BSE_OBJECT_GET_CLASS (object);
  
  bst_preferences_revert (prefs);
}

void
bst_preferences_apply (BstPreferences *prefs)
{
  g_return_if_fail (BST_IS_PREFERENCES (prefs));

  bse_gconfig_apply (prefs->gconf);
}

void
bst_preferences_save (BstPreferences *prefs)
{
  BseErrorType error;
  gchar *file_name;

  g_return_if_fail (BST_IS_PREFERENCES (prefs));

  file_name = BST_STRDUP_RC_FILE ();
  error = bst_rc_dump (file_name, prefs->gconf);
  if (error)
    g_warning ("error saving rc-file \"%s\": %s", file_name, bse_error_blurb (error));
  g_free (file_name);
}

void
bst_preferences_revert (BstPreferences *prefs)
{
  g_return_if_fail (BST_IS_PREFERENCES (prefs));

  bse_gconfig_revert (prefs->gconf);
}

void
bst_preferences_default_revert (BstPreferences *prefs)
{
  g_return_if_fail (BST_IS_PREFERENCES (prefs));

  bse_gconfig_default_revert (prefs->gconf);
}


/* --- rc file --- */
#include <fcntl.h>
#include <errno.h>
#include "../PKG_config.h"
static void
ifactory_print_func (gpointer     user_data,
		     const gchar *str)
{
  BseStorage *storage = user_data;

  bse_storage_break (storage);
  // bse_storage_indent (storage);
  bse_storage_puts (storage, str);
  if (str[0] == ';')
    bse_storage_needs_break (storage);
}

static void
accel_map_print (gpointer        data,
		 const gchar    *accel_path,
		 guint           accel_key,
		 guint           accel_mods,
		 gboolean        changed)
{
  GString *gstring = g_string_new (changed ? NULL : "; ");
  BseStorage *storage = data;
  gchar *tmp, *name;

  g_string_append (gstring, "(gtk_accel_path \"");

  tmp = g_strescape (accel_path, NULL);
  g_string_append (gstring, tmp);
  g_free (tmp);

  g_string_append (gstring, "\" \"");

  name = gtk_accelerator_name (accel_key, accel_mods);
  tmp = g_strescape (name, NULL);
  g_free (name);
  g_string_append (gstring, tmp);
  g_free (tmp);

  g_string_append (gstring, "\")");

  bse_storage_puts (storage, gstring->str);
  bse_storage_break (storage);

  g_string_free (gstring, TRUE);
}

BseErrorType
bst_rc_dump (const gchar *file_name,
	     BseGConfig  *gconf)
{
  BseStorage *storage;
  gint fd;

  g_return_val_if_fail (file_name != NULL, BSE_ERROR_INTERNAL);
  g_return_val_if_fail (BSE_IS_GCONFIG (gconf), BSE_ERROR_INTERNAL);

  fd = open (file_name,
	     O_WRONLY | O_CREAT | O_TRUNC, /* O_EXCL, */
	     0666);

  if (fd < 0)
    return (errno == EEXIST ? BSE_ERROR_FILE_EXISTS : BSE_ERROR_FILE_IO);

  storage = bse_storage_new ();
  bse_storage_prepare_write (storage, TRUE);

  /* put blurb
   */
  bse_storage_puts (storage, "; rc-file for BEAST v" BST_VERSION);

  bse_storage_break (storage);
  
  /* store BseGlobals
   */
  bse_storage_break (storage);
  bse_storage_puts (storage, "; BseGlobals dump");
  bse_storage_break (storage);
  bse_storage_puts (storage, "(preferences");
  bse_storage_push_level (storage);
  bse_object_store (BSE_OBJECT (gconf), storage);
  bse_storage_pop_level (storage);

  bse_storage_break (storage);

  /* store item factory paths
   */
  bse_storage_break (storage);
  bse_storage_puts (storage, "; GtkItemFactory path dump");
  bse_storage_break (storage);
  bse_storage_puts (storage, "(menu-accelerators");
  bse_storage_push_level (storage);
  bse_storage_break (storage);
  gtk_accel_map_foreach (storage, accel_map_print);
  bse_storage_pop_level (storage);
  bse_storage_handle_break (storage);
  bse_storage_putc (storage, ')');
  
  /* flush stuff to rc file
   */
  bse_storage_flush_fd (storage, fd);
  bse_storage_destroy (storage);

  return close (fd) < 0 ? BSE_ERROR_FILE_IO : BSE_ERROR_NONE;
}

BseErrorType
bst_rc_parse (const gchar *file_name,
	      BseGConfig  *gconf)
{
  BseStorage *storage;
  GScanner *scanner;
  GTokenType expected_token = G_TOKEN_NONE;
  BseErrorType error;

  g_return_val_if_fail (file_name != NULL, BSE_ERROR_INTERNAL);
  g_return_val_if_fail (BSE_IS_GCONFIG (gconf), BSE_ERROR_INTERNAL);
  
  storage = bse_storage_new ();
  error = bse_storage_input_file (storage, file_name);
  if (error)
    {
      bse_storage_destroy (storage);
      return error;
    }
  scanner = storage->scanner;

  while (!bse_storage_input_eof (storage) && expected_token == G_TOKEN_NONE)
    {
      g_scanner_get_next_token (scanner);

      if (scanner->token == G_TOKEN_EOF)
	break;
      else if (scanner->token == '(' && g_scanner_peek_next_token (scanner) == G_TOKEN_IDENTIFIER)
	{
	  g_scanner_get_next_token (scanner);
	  if (bse_string_equals ("preferences", scanner->value.v_identifier))
	    expected_token = bse_object_restore (BSE_OBJECT (gconf), storage);
	  else if (bse_string_equals ("menu-accelerators", scanner->value.v_identifier))
	    {
	      gtk_accel_map_load_scanner (scanner);

	      if (g_scanner_get_next_token (scanner) != ')')
		expected_token = ')';
	    }
	  else
	    expected_token = G_TOKEN_IDENTIFIER;
	}
      else
	expected_token = G_TOKEN_EOF;
    }

  if (expected_token != G_TOKEN_NONE)
    bse_storage_unexp_token (storage, expected_token);

  error = scanner->parse_errors >= scanner->max_parse_errors ? BSE_ERROR_PARSE_ERROR : BSE_ERROR_NONE;

  bse_storage_destroy (storage);

  return error;
}

void
bst_preferences_create_buttons (BstPreferences *prefs,
				BstDialog      *dialog)
{
  GtkWidget *widget;

  g_return_if_fail (BST_IS_PREFERENCES (prefs));
  g_return_if_fail (BST_IS_DIALOG (dialog));
  g_return_if_fail (prefs->apply == NULL);

  /* Apply
   */
  prefs->apply = g_object_connect (bst_dialog_default_action (dialog, BST_STOCK_APPLY, NULL, NULL),
				   "swapped_signal::clicked", bst_preferences_apply, prefs,
				   "swapped_signal::clicked", bst_preferences_save, prefs,
				   "swapped_signal::destroy", g_nullify_pointer, &prefs->apply,
				   NULL);
  gtk_tooltips_set_tip (BST_TOOLTIPS, prefs->apply,
			"Apply and save the preference values. Some values may only take effect after "
			"restart. The preference values are locked against modifcation during "
			"playback.",
			NULL);

  /* Revert
   */
  widget = bst_dialog_action_swapped (dialog, BST_STOCK_REVERT, bst_preferences_revert, prefs);
  gtk_tooltips_set_tip (BST_TOOLTIPS, widget,
			"Revert the preference values to the current internal values.",
			NULL);

  /* Default Revert
   */
  widget = bst_dialog_action_swapped (dialog, BST_STOCK_DEFAULT_REVERT, bst_preferences_default_revert, prefs);
  gtk_tooltips_set_tip (BST_TOOLTIPS, widget,
			"Revert to hardcoded default values (factory settings).",
			NULL);

  /* Close
   */
  widget = bst_dialog_action (dialog, BST_STOCK_CLOSE, gtk_toplevel_delete, NULL);
}
