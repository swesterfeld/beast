/* BEAST - Bedevilled Audio System
 * Copyright (C) 2002 Tim Janik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */
#include "bsttoolbar.h"

#include "bstdefs.h"


/* --- prototypes --- */
static void	bst_toolbar_class_init		(BstToolbarClass	*class);
static void	bst_toolbar_init		(BstToolbar		*self);
static void	bst_toolbar_finalize		(GObject		*object);


/* --- variables --- */
static gpointer parent_class = NULL;


/* --- functions --- */
GType
bst_toolbar_get_type (void)
{
  static GType type = 0;

  if (!type)
    {
      static const GTypeInfo type_info = {
	sizeof (BstToolbarClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) bst_toolbar_class_init,
	NULL,   /* class_finalize */
	NULL,   /* class_data */
	sizeof (BstToolbar),
	0,      /* n_preallocs */
	(GInstanceInitFunc) bst_toolbar_init,
      };

      type = g_type_register_static (GTK_TYPE_FRAME,
				     "BstToolbar",
				     &type_info, 0);
    }

  return type;
}

static void
bst_toolbar_class_init (BstToolbarClass *class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (class);

  parent_class = g_type_class_peek_parent (class);

  gobject_class->finalize = bst_toolbar_finalize;
}

static void
bst_toolbar_init (BstToolbar *self)
{
  g_object_set (self,
		"shadow_type", GTK_SHADOW_OUT,
		NULL);
  self->relief_style = GTK_RELIEF_NONE;
  self->icon_size = BST_SIZE_TOOLBAR;
  self->icon_width = bst_size_width (self->icon_size);
  self->icon_height = bst_size_height (self->icon_size);
  self->size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
  self->box = g_object_new (GTK_TYPE_HBOX,
			    "visible", TRUE,
			    "parent", self,
			    NULL);
  g_object_ref (self->box);
}

static void
bst_toolbar_finalize (GObject *object)
{
  BstToolbar *self = BST_TOOLBAR (object);

  g_object_unref (self->size_group);
  self->size_group = NULL;
  g_object_unref (self->box);
  self->box = NULL;

  /* chain parent class' handler */
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

BstToolbar*
bst_toolbar_new (gpointer  nullify_pointer)
{
  GtkWidget *toolbar;

  toolbar = g_object_new (BST_TYPE_TOOLBAR,
			  "visible", TRUE,
			  NULL);
  if (nullify_pointer)
    g_object_connect (toolbar,
		      "swapped_signal::destroy", g_nullify_pointer, nullify_pointer,
		      NULL);

  return BST_TOOLBAR (toolbar);
}

static void
update_child (BstToolbar *self,
	      GtkWidget  *child)
{
  gpointer relief_data = g_object_get_data (G_OBJECT (child), "bst-toolbar-relief");
  gpointer size_data = g_object_get_data (G_OBJECT (child), "bst-toolbar-size");
  if (relief_data)
    g_object_set (relief_data,
		  "relief", self->relief_style,
		  NULL);
  if (size_data)
    g_object_set (size_data,
		  "width_request", self->icon_width,
		  "height_request", self->icon_height,
		  NULL);
}

static gboolean
button_event_filter (GtkWidget *widget,
		     GdkEvent  *event)
{
  switch (event->type)
    {
    case GDK_BUTTON_PRESS:
    case GDK_2BUTTON_PRESS:
    case GDK_3BUTTON_PRESS:
    case GDK_BUTTON_RELEASE:
    case GDK_MOTION_NOTIFY:
    case GDK_KEY_PRESS:
    case GDK_KEY_RELEASE:
    case GDK_ENTER_NOTIFY:
    case GDK_LEAVE_NOTIFY:
      return TRUE;
    default:
      return FALSE;
    }
}

static void
button_event_window_lower (GtkButton *button)
{
  gdk_window_lower (button->event_window);
}

GtkWidget*
bst_toolbar_append (BstToolbar     *self,
		    BstToolbarChild child_type,
		    const gchar    *name,
		    const gchar    *tooltip,
		    GtkWidget      *icon)
{
  GtkWidget *child = NULL;
  gpointer relief_data = NULL, size_data = NULL;
  guint padding = 0;

  g_return_val_if_fail (BST_IS_TOOLBAR (self), NULL);

  switch (child_type)
    {
      GtkWidget *any, *vbox;
      GType widget_type;
      gboolean extra, filtered, trunc;
    case BST_TOOLBAR_SPACE:
      g_return_val_if_fail (name == NULL && tooltip == NULL && icon == NULL, NULL);
      child = g_object_new (GTK_TYPE_ALIGNMENT,
			    "visible", TRUE,
			    "width_request", 10,
			    NULL);
      break;
    case BST_TOOLBAR_SEPARATOR:
      g_return_val_if_fail (name == NULL && tooltip == NULL && icon == NULL, NULL);
      child = g_object_new (GTK_TYPE_ALIGNMENT,
			    "visible", TRUE,
			    "yalign", 0.5,
			    "yscale", 0.4,
			    NULL);
      any = g_object_new (GTK_TYPE_VSEPARATOR,
			  "visible", TRUE,
			  "parent", child,
			  NULL);
      padding = 2;
      break;
    case BST_TOOLBAR_BUTTON:
    case BST_TOOLBAR_TRUNC_BUTTON:
    case BST_TOOLBAR_EXTRA_BUTTON:
    case BST_TOOLBAR_WIDGET:
    case BST_TOOLBAR_TRUNC_WIDGET:
    case BST_TOOLBAR_EXTRA_WIDGET:
    case BST_TOOLBAR_TOGGLE:
    case BST_TOOLBAR_TRUNC_TOGGLE:
    case BST_TOOLBAR_EXTRA_TOGGLE:
      if (child_type == BST_TOOLBAR_TOGGLE ||
	  child_type == BST_TOOLBAR_TRUNC_TOGGLE ||
	  child_type == BST_TOOLBAR_EXTRA_TOGGLE)
	widget_type = GTK_TYPE_TOGGLE_BUTTON;
      else
	widget_type = GTK_TYPE_BUTTON;
      filtered = (child_type == BST_TOOLBAR_WIDGET ||
		  child_type == BST_TOOLBAR_TRUNC_WIDGET ||
		  child_type == BST_TOOLBAR_EXTRA_WIDGET);
      trunc = (child_type == BST_TOOLBAR_TRUNC_BUTTON ||
	       child_type == BST_TOOLBAR_TRUNC_TOGGLE ||
	       child_type == BST_TOOLBAR_TRUNC_WIDGET);
      extra = (child_type == BST_TOOLBAR_EXTRA_BUTTON ||
	       child_type == BST_TOOLBAR_EXTRA_TOGGLE ||
	       child_type == BST_TOOLBAR_EXTRA_WIDGET);
      child = g_object_new (widget_type,
			    "visible", TRUE,
			    "can_focus", FALSE,
			    NULL);
      if (filtered)
	{
	  g_object_connect (child,
			    "signal::event", button_event_filter, NULL,
			    "signal_after::map", button_event_window_lower, NULL,
			    NULL);
	  g_object_set (child,
			"relief", GTK_RELIEF_NONE,
			NULL);
	}
      else
	relief_data = child;
      vbox = g_object_new (GTK_TYPE_VBOX,
			   "visible", TRUE,
			   "parent", child,
			   NULL);
      any = g_object_new (GTK_TYPE_ALIGNMENT,
			  "visible", TRUE,
			  "xalign", 0.5,
			  "yalign", 0.5,
			  "yscale", 1.0,
			  "xscale", 1.0,
			  "child", icon,
			  NULL);
      if (filtered && icon)
	gtk_tooltips_set_tip (BST_TOOLTIPS, icon, tooltip, NULL);
      gtk_box_pack_start (GTK_BOX (vbox), any, TRUE, TRUE, 0);
      size_data = filtered ? NULL : icon;
      if (name)
	{
	  GtkWidget *label = g_object_new (GTK_TYPE_LABEL,
					   "visible", TRUE,
					   "label", name,
					   trunc ? "xalign" : NULL, 0.0,
					   NULL);
	  g_object_set_data (G_OBJECT (child), "bst-toolbar-label", label);
	  if (trunc)
	    gtk_box_pack_end (GTK_BOX (vbox),
			      g_object_new (GTK_TYPE_ALIGNMENT,
					    "visible", TRUE,
					    "child", label,
					    "xalign", 0.5,
					    "yalign", 1.0,
					    "xscale", 0.0,
					    "yscale", 0.0,
					    "width_request", 1,
					    NULL),
			      FALSE, FALSE, 0);
	  else
	    gtk_box_pack_end (GTK_BOX (vbox), label, FALSE, FALSE, 0);
	}
      if (!extra)
	gtk_size_group_add_widget (self->size_group, child);
      break;
    }
  g_object_set_data (G_OBJECT (child), "bst-toolbar-size", size_data);
  g_object_set_data (G_OBJECT (child), "bst-toolbar-relief", relief_data);
  update_child (self, child);
  gtk_box_pack_start (GTK_BOX (self->box), child, FALSE, FALSE, padding);
  gtk_tooltips_set_tip (BST_TOOLTIPS, child, tooltip, NULL);

  return child;
}

typedef struct {
  GtkWidget           *alignment;
  GtkWidget           *menu;
  BstToolbarChoiceFunc choice_func;
  gpointer             data;
  GDestroyNotify       data_free;
} BstToolbarChoiceData;

static void
free_choice_data (gpointer data)
{
  BstToolbarChoiceData *cdata = data;

  if (cdata->data_free)
    cdata->data_free (cdata->data);
}

static void
menu_position_func (GtkMenu  *menu,
		    gint     *x_p,
		    gint     *y_p,
		    gboolean *push_in_p,
		    gpointer  user_data)
{
  GtkWidget *widget = user_data;
  gint x, y;

  gdk_window_get_origin (widget->window, &x, &y);
  if (GTK_WIDGET_NO_WINDOW (widget))
    {
      *x_p = x + widget->allocation.x;
      *y_p = y + widget->allocation.y;
    }
  else
    {
      *x_p = x;
      *y_p = y;
    }
  *push_in_p = FALSE;
}

static void
button_menu_popup (GtkWidget *button)
{
  BstToolbarChoiceData *cdata = g_object_get_data (G_OBJECT (button), "bst-toolbar-choice-data");
  GdkEvent *event = gtk_get_current_event ();

  if (event)
    {
      GtkWidget *icon = GTK_BIN (cdata->alignment)->child;
      GtkWidget *parent = icon ? g_object_get_data (G_OBJECT (icon), "bst-toolbar-parent") : NULL;

      /* fix alignment size across removing child */
      g_object_set (cdata->alignment,
		    "width_request", cdata->alignment->requisition.width,
		    "height_request", cdata->alignment->requisition.height,
		    NULL);
      if (parent)
	{
	  g_object_ref (icon);
	  gtk_container_remove (GTK_CONTAINER (cdata->alignment), icon);
	  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (parent), icon);
	  g_object_unref (icon);
	  /* remember last setting */
	  g_object_set_data (G_OBJECT (cdata->alignment), "bst-toolbar-last-icon", icon);
	}
      gtk_menu_popup (GTK_MENU (cdata->menu), NULL, NULL, menu_position_func, button, 0, gdk_event_get_time (event));
    }
}

static void
button_menu_popdown (GtkWidget *button)
{
  BstToolbarChoiceData *cdata = g_object_get_data (G_OBJECT (button), "bst-toolbar-choice-data");

  if (!GTK_BIN (cdata->alignment)->child)
    {
      /* probably aborted, restore last setting */
      GtkWidget *icon = g_object_get_data (G_OBJECT (cdata->alignment), "bst-toolbar-last-icon");
      if (icon)
	{
	  g_object_ref (icon);
	  if (icon->parent)
	    gtk_container_remove (GTK_CONTAINER (icon->parent), icon);
	  gtk_container_add (GTK_CONTAINER (cdata->alignment), icon);
	  g_object_unref (icon);
	}
    }
  g_object_set_data (G_OBJECT (cdata->alignment), "bst-toolbar-last-icon", NULL);
  /* restore alignment size */
  g_object_set (cdata->alignment,
		"width_request", -1,
		"height_request", -1,
		NULL);
}

GtkWidget*
bst_toolbar_append_choice (BstToolbar          *self,
			   BstToolbarChild      child_type,
			   BstToolbarChoiceFunc choice_func,
			   gpointer             data,
			   GDestroyNotify       data_free)
{
  BstToolbarChoiceData *cdata;
  GtkWidget *button;

  g_return_val_if_fail (BST_IS_TOOLBAR (self), NULL);
  g_return_val_if_fail (choice_func != NULL, NULL);
  g_return_val_if_fail (child_type >= BST_TOOLBAR_BUTTON && child_type <= BST_TOOLBAR_EXTRA_BUTTON, NULL);

  cdata = g_new0 (BstToolbarChoiceData, 1);
  cdata->choice_func = choice_func;
  cdata->data = data;
  cdata->data_free = data_free;
  cdata->alignment = g_object_new (GTK_TYPE_ALIGNMENT,
				   "visible", TRUE,
				   NULL);
  button = bst_toolbar_append (self, child_type,
			       "drop-down", NULL, cdata->alignment);
  cdata->menu = g_object_new (GTK_TYPE_MENU, NULL);
  g_object_connect (cdata->menu,
		    "swapped_object_signal::selection-done", button_menu_popdown, button,
		    NULL);
  g_object_set_data_full (G_OBJECT (button), "bst-toolbar-choice-data", cdata, free_choice_data);
  g_object_connect (button,
		    "signal::clicked", button_menu_popup, NULL,
		    NULL);

  return button;
}

static void
menu_item_select (GtkWidget *item,
		  GtkWidget *button)
{
  BstToolbarChoiceData *cdata = g_object_get_data (G_OBJECT (button), "bst-toolbar-choice-data");
  GtkWidget *label = g_object_get_data (G_OBJECT (button), "bst-toolbar-label");
  GtkWidget *icon = gtk_image_menu_item_get_image (GTK_IMAGE_MENU_ITEM (item));
  gpointer choice = g_object_get_data (G_OBJECT (item), "bst-toolbar-choice");

  if (cdata->alignment && icon)
    {
      if (GTK_BIN (cdata->alignment)->child)
	{
	  GtkWidget *old_icon = GTK_BIN (cdata->alignment)->child;
	  GtkWidget *old_parent = g_object_get_data (G_OBJECT (old_icon), "bst-toolbar-parent");
	  /* remove old child */
	  g_object_ref (old_icon);
	  gtk_container_remove (GTK_CONTAINER (old_icon->parent), old_icon);
	  if (old_parent)
	    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (old_parent), old_icon);
	  g_object_unref (old_icon);
	}
      g_object_ref (icon);
      gtk_container_remove (GTK_CONTAINER (icon->parent), icon);
      gtk_container_add (GTK_CONTAINER (cdata->alignment), icon);
      g_object_unref (icon);
    }
  if (label)
    gtk_label_set (GTK_LABEL (label), g_object_get_data (G_OBJECT (item), "bst-toolbar-name"));
  gtk_tooltips_set_tip (BST_TOOLTIPS, button, g_object_get_data (G_OBJECT (item), "bst-toolbar-tooltip"), NULL);

  if (cdata->alignment)
    g_object_set (cdata->alignment,
		  "width_request", -1,
		  "height_request", -1,
		  NULL);
  cdata->choice_func (cdata->data, GPOINTER_TO_UINT (choice));
}

static GtkWidget*
toolbar_choice_add (GtkWidget            *widget,
		    BstToolbarChoiceData *cdata,
		    const gchar          *name,
		    const gchar          *tooltip,
		    GtkWidget            *icon)
{
  GtkWidget *item;

  item = gtk_image_menu_item_new_with_label (name);
  if (icon)
    g_object_set_data (G_OBJECT (icon), "bst-toolbar-parent", item);
  g_object_set (item,
		"visible", TRUE,
		"image", icon,
		"parent", cdata->menu,
		NULL);
  g_object_connect (item,
		    "object_signal::activate", menu_item_select, widget,
		    NULL);
  g_object_set_data_full (G_OBJECT (item), "bst-toolbar-tooltip", g_strdup (tooltip), g_free);
  g_object_set_data_full (G_OBJECT (item), "bst-toolbar-name", g_strdup (name), g_free);
  return item;
}

void
bst_toolbar_choice_add (GtkWidget   *widget,
			const gchar *name,
			const gchar *tooltip,
			GtkWidget   *icon,
			guint        choice)
{
  BstToolbarChoiceData *choice_data;
  GtkWidget *item;
  gboolean need_selection;

  g_return_if_fail (GTK_IS_BUTTON (widget));
  choice_data = g_object_get_data (G_OBJECT (widget), "bst-toolbar-choice-data");
  g_return_if_fail (choice_data != NULL);

  need_selection = GTK_MENU_SHELL (choice_data->menu)->children == NULL;
  item = toolbar_choice_add (widget, choice_data, name, tooltip, icon);
  g_object_set_data (G_OBJECT (item), "bst-toolbar-choice", GUINT_TO_POINTER (choice));
  if (need_selection)
    menu_item_select (item, widget);
}

void
bst_toolbar_choice_set (GtkWidget   *widget,
			const gchar *name,
			const gchar *tooltip,
			GtkWidget   *icon,
			guint        choice)
{
  BstToolbarChoiceData *choice_data;
  GtkWidget *item;

  g_return_if_fail (GTK_IS_BUTTON (widget));
  choice_data = g_object_get_data (G_OBJECT (widget), "bst-toolbar-choice-data");
  g_return_if_fail (choice_data != NULL);

  item = toolbar_choice_add (widget, choice_data, name, tooltip, icon);
  g_object_set_data (G_OBJECT (item), "bst-toolbar-choice", GUINT_TO_POINTER (choice));
  menu_item_select (item, widget);
}
