/* BEAST - Bedevilled Audio System
 * Copyright (C) 1998-2003 Tim Janik
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
#include "bstitemview.h"
#include "bstparamview.h"
#include "bsttreestores.h"
#include "bstactivatable.h"


/* --- prototypes --- */
static void	bst_item_view_class_init	(BstItemViewClass	*klass);
static void     bst_item_view_init_activatable  (BstActivatableIface    *iface,
                                                 gpointer                iface_data);
static void	bst_item_view_init		(BstItemView		*self,
						 BstItemViewClass	*real_class);
static void	bst_item_view_destroy		(GtkObject		*object);
static void	bst_item_view_finalize		(GObject		*object);
static void	bst_item_view_update_activatable(BstActivatable         *activatable);
static void	bst_item_view_create_tree	(BstItemView		*self);
static void	item_view_listen_on		(BstItemView		*self,
						 SfiProxy		 item);
static void	item_view_unlisten_on		(BstItemView		*self,
						 SfiProxy		 item);
static void	item_view_set_container		(BstItemView		*self,
						 SfiProxy		 new_container);


/* --- static variables --- */
static gpointer		 parent_class = NULL;


/* --- functions --- */
GType
bst_item_view_get_type (void)
{
  static GType type = 0;
  if (!type)
    {
      static const GTypeInfo type_info = {
        sizeof (BstItemViewClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc) bst_item_view_class_init,
	NULL,   /* class_finalize */
	NULL,   /* class_data */
	sizeof (BstItemView),
	0,      /* n_preallocs */
	(GInstanceInitFunc) bst_item_view_init,
      };
      static const GInterfaceInfo activatable_info = {
        (GInterfaceInitFunc) bst_item_view_init_activatable,    /* interface_init */
        NULL,                                                   /* interface_finalize */
        NULL                                                    /* interface_data */
      };
      type = g_type_register_static (GTK_TYPE_ALIGNMENT, "BstItemView", &type_info, 0);
      g_type_add_interface_static (type, BST_TYPE_ACTIVATABLE, &activatable_info);
    }
  return type;
}

static void
bst_item_view_class_init (BstItemViewClass *class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (class);
  GtkObjectClass *object_class = GTK_OBJECT_CLASS (class);
  
  parent_class = g_type_class_peek_parent (class);
  
  gobject_class->finalize = bst_item_view_finalize;

  object_class->destroy = bst_item_view_destroy;
  
  class->item_type = NULL;
  class->n_ops = 0;
  class->ops = NULL;
  class->show_properties = TRUE;
  
  class->create_tree = bst_item_view_create_tree;
  class->listen_on = item_view_listen_on;
  class->unlisten_on = item_view_unlisten_on;

  class->set_container = item_view_set_container;
}

static void
bst_item_view_init_activatable (BstActivatableIface *iface,
                                gpointer             iface_data)
{
  iface->update = bst_item_view_update_activatable;
}

static void
button_action (GtkWidget *widget,
	       gpointer	  op)
{
  while (!BST_IS_ITEM_VIEW (widget))
    widget = widget->parent;
  bst_activatable_activate (BST_ACTIVATABLE (widget), GPOINTER_TO_UINT (op));
}

static void
bst_item_view_init (BstItemView      *self,
		    BstItemViewClass *ITEM_VIEW_CLASS)
{
  GtkWidget *tool_box;
  gboolean vpack = ITEM_VIEW_CLASS->horizontal_ops;
  guint i;

  /* action buttons */
  self->op_widgets = g_new0 (GtkWidget*, ITEM_VIEW_CLASS->n_ops);
  self->tools = g_object_new (GTK_TYPE_ALIGNMENT, /* don't want tool buttons to resize */
			      "visible", TRUE,
			      "xscale", 0.0,
			      "yscale", 0.0,
			      "xalign", vpack ? 0.0 : 0.5,
			      "yalign", vpack ? 0.5 : 0.0,
			      NULL);
  tool_box = g_object_new (vpack ? GTK_TYPE_HBOX : GTK_TYPE_VBOX,
			   "homogeneous", TRUE,
			   "spacing", 3,
			   "parent", self->tools,
			   NULL);
  for (i = 0; i < ITEM_VIEW_CLASS->n_ops; i++)
    {
      BstItemViewOp *bop = ITEM_VIEW_CLASS->ops + i;
      GtkWidget *label = g_object_new (GTK_TYPE_LABEL,
				       "label", bop->op_name,
				       NULL);
      self->op_widgets[i] = g_object_new (GTK_TYPE_BUTTON,
					  "can_focus", FALSE,
					  "parent", tool_box,
					  "sensitive", FALSE,
					  NULL);
      g_object_connect (self->op_widgets[i],
			"signal::clicked", button_action, GUINT_TO_POINTER (bop->op),
			"signal::destroy", gtk_widget_destroyed, &self->op_widgets[i],
			NULL);
      if (!bop->stock_icon)
	gtk_container_add (GTK_CONTAINER (self->op_widgets[i]), label);
      else
	g_object_new (GTK_TYPE_VBOX,
		      "homogeneous", FALSE,
		      "spacing", 0,
		      "child", gxk_stock_image (bop->stock_icon, BST_SIZE_BIG_BUTTON),
		      "child", label,
		      "parent", self->op_widgets[i],
		      NULL);
      if (bop->tooltip)
	gtk_tooltips_set_tip (GXK_TOOLTIPS, self->op_widgets[i], bop->tooltip, NULL);
    }
  gtk_widget_show_all (self->tools);
  
  /* pack list view + button box */
  self->paned = g_object_new (GTK_TYPE_VPANED,
			      "parent", self,
			      NULL);
  gxk_nullify_on_destroy (self->paned, &self->paned);
  
  /* property view */
  if (ITEM_VIEW_CLASS->show_properties)
    {
      self->pview = bst_param_view_new (0);
      bst_param_view_set_mask (BST_PARAM_VIEW (self->pview), "BseItem", 0, NULL, NULL);
      gxk_nullify_on_destroy (self->pview, &self->pview);
      gtk_paned_pack2 (self->paned, self->pview, TRUE, TRUE);
    }
  
  /* show the sutter */
  gtk_widget_show_all (GTK_WIDGET (self));
  
  self->container = 0;
}

static void
bst_item_view_destroy (GtkObject *object)
{
  BstItemView *self = BST_ITEM_VIEW (object);

  bst_item_view_set_container (self, 0);
  
  bst_activatable_update_enqueue (BST_ACTIVATABLE (object));

  GTK_OBJECT_CLASS (parent_class)->destroy (object);

  bst_activatable_update_dequeue (BST_ACTIVATABLE (object));
}

static void
bst_item_view_finalize (GObject *object)
{
  BstItemView *self = BST_ITEM_VIEW (object);

  bst_item_view_set_container (self, 0);

  g_free (self->op_widgets);
  if (self->wlist)
    g_object_unref (self->wlist);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

void
bst_item_view_name_edited (BstItemView *self,
			   const gchar *strpath,
			   const gchar *text)
{
  g_return_if_fail (BST_IS_ITEM_VIEW (self));

  if (strpath)
    {
      gint row = gxk_tree_spath_index0 (strpath);
      SfiProxy item = bst_item_view_get_proxy (self, row);
      if (item)
	bse_item_set_name (item, text);
    }
}

void
bst_item_view_blurb_edited (BstItemView *self,
			    const gchar *strpath,
			    const gchar *text)
{
  g_return_if_fail (BST_IS_ITEM_VIEW (self));

  if (strpath)
    {
      gint row = gxk_tree_spath_index0 (strpath);
      SfiProxy item = bst_item_view_get_proxy (self, row);
      if (item)
	bse_proxy_set (item, "blurb", text, NULL);
    }
}

static void
item_view_listener (GtkTreeModel *model,
		    SfiProxy      item,
		    gboolean      added)
{
  BstItemView *self = g_object_get_data (model, "item-view");
  if (added)
    BST_ITEM_VIEW_GET_CLASS (self)->listen_on (self, item);
  else
    BST_ITEM_VIEW_GET_CLASS (self)->unlisten_on (self, item);
  bst_widget_update_activatable (self);
}

GtkTreeModel*
bst_item_view_adapt_list_wrapper (BstItemView    *self,
				  GxkListWrapper *lwrapper)
{
  g_return_val_if_fail (BST_IS_ITEM_VIEW (self), NULL);
  g_return_val_if_fail (GXK_IS_LIST_WRAPPER (lwrapper), NULL);
  g_return_val_if_fail (self->wlist == NULL, NULL);

  g_object_set_data (lwrapper, "item-view", self);
  bst_child_list_wrapper_set_listener (lwrapper, item_view_listener);
  self->wlist = g_object_ref (lwrapper);
  return gtk_tree_model_sort_new_with_model (GTK_TREE_MODEL (self->wlist));
}

static void
bst_item_view_create_tree (BstItemView *self)
{
  GtkWidget *scwin;
  GtkTreeSelection *tsel;
  GtkTreeModel *smodel;
  GxkListWrapper *lwrapper;

  /* item list model */
  lwrapper = bst_child_list_wrapper_store_new ();
  smodel = bst_item_view_adapt_list_wrapper (self, lwrapper);
  g_object_unref (lwrapper);

  /* item list view */
  scwin = g_object_new (GTK_TYPE_SCROLLED_WINDOW,
			"hscrollbar_policy", GTK_POLICY_AUTOMATIC,
			"vscrollbar_policy", GTK_POLICY_ALWAYS,
			"border_width", 0,
			"shadow_type", GTK_SHADOW_IN,
			NULL);
  self->tree = g_object_new (GTK_TYPE_TREE_VIEW,
			     "can_focus", TRUE,
			     "model", smodel,
			     "border_width", 5,
			     "parent", scwin,
			     "height_request", BST_ITEM_VIEW_TREE_HEIGHT,
			     NULL);
  gxk_nullify_on_destroy (self->tree, &self->tree);
  tsel = gtk_tree_view_get_selection (self->tree);
  gtk_tree_selection_set_mode (tsel, GTK_SELECTION_BROWSE);
  gxk_tree_selection_force_browse (tsel, smodel);
  g_object_unref (smodel);
  
  /* add list view columns */
  gxk_tree_view_add_text_column (self->tree, BST_PROXY_STORE_SEQID, "S",
				 0.0, "ID", NULL,
				 NULL, NULL, 0);
  gxk_tree_view_add_text_column (self->tree, BST_PROXY_STORE_NAME, "S",
				 0.0, "Name", NULL,
				 bst_item_view_name_edited, self, G_CONNECT_SWAPPED);
  gxk_tree_view_add_text_column (self->tree, BST_PROXY_STORE_BLURB, "",
				 0.0, "Comment", NULL,
				 bst_item_view_blurb_edited, self, G_CONNECT_SWAPPED);
  if (BST_DBG_EXT)
    gxk_tree_view_add_text_column (self->tree, BST_PROXY_STORE_TYPE, "",
				   0.0, "Type", NULL,
				   NULL, NULL, 0);

  /* make widgets visible */
  gtk_widget_show_all (scwin);
  bst_widget_update_activatable (self);
}

static void
pview_selection_changed (BstItemView *self)
{
  if (self->pview)
    bst_param_view_set_item (BST_PARAM_VIEW (self->pview),
			     bst_item_view_get_current (self));
  bst_widget_update_activatable (self);
}

void
bst_item_view_complete_tree (BstItemView *self)
{
  if (!self->wlist && !self->tree)
    {
      BST_ITEM_VIEW_GET_CLASS (self)->create_tree (self);
      if (self->tree)
	{
	  GtkWidget *widget = GTK_WIDGET (self->tree);
	  while (widget->parent)
	    widget = widget->parent;

	  /* update property editor */
	  g_object_connect (gtk_tree_view_get_selection (self->tree),
			    "swapped_object_signal::changed", pview_selection_changed, self,
			    NULL);

	  /* pack list view + button box */
	  if (!self->tools->parent)
	    {
	      gboolean vpack = BST_ITEM_VIEW_GET_CLASS (self)->horizontal_ops;
	      GtkWidget *lbox = g_object_new (vpack ? GTK_TYPE_VBOX : GTK_TYPE_HBOX,
					      "visible", TRUE,
					      "border_width", 3,
					      "spacing", 3,
					      NULL);
	      if (!vpack)
		gtk_box_pack_start (GTK_BOX (lbox), widget, TRUE, TRUE, 0);
	      gtk_box_pack_start (GTK_BOX (lbox), self->tools, FALSE, FALSE, 0);
	      if (vpack)
		gtk_box_pack_start (GTK_BOX (lbox), widget, TRUE, TRUE, 0);
	      gtk_paned_pack1 (self->paned, lbox, FALSE, FALSE);
	    }
	  else
	    gtk_paned_pack1 (self->paned, widget, FALSE, FALSE);

	  /* adapt param view */
	  pview_selection_changed (self);
	}
    }
}

static void
item_view_listen_on (BstItemView *self,
		     SfiProxy     item)
{
  bse_proxy_connect (item, "swapped_signal::property-notify", bst_widget_update_activatable, self, NULL);
  bse_proxy_connect (item, "swapped_signal::property-notify", gtk_true, self, NULL);
  if (self->auto_select == item)
    bst_item_view_select (self, item);
  self->auto_select = 0;
}

static void
item_view_unlisten_on (BstItemView *self,
		       SfiProxy     item)
{
  bse_proxy_disconnect (item, "any_signal", bst_widget_update_activatable, self, NULL);
  bse_proxy_disconnect (item, "any_signal", gtk_true, self, NULL);
}

static void
bst_item_view_release_container (BstItemView  *item_view)
{
  bst_item_view_set_container (item_view, 0);
}

static void
item_view_set_container (BstItemView *self,
			 SfiProxy     new_container)
{
  if (self->container)
    {
      bse_proxy_disconnect (self->container,
			    "any_signal", bst_item_view_release_container, self,
			    NULL);
      if (self->wlist)
	bst_child_list_wrapper_setup (self->wlist, 0, NULL);
      if (self->pview)
	bst_param_view_set_item (BST_PARAM_VIEW (self->pview), 0);
    }
  self->container = new_container;
  if (self->container)
    {
      bse_proxy_connect (self->container,
			 "swapped_signal::release", bst_item_view_release_container, self,
			 NULL);
      bst_item_view_complete_tree (self);
      if (self->wlist)
	bst_child_list_wrapper_setup (self->wlist, self->container, BST_ITEM_VIEW_GET_CLASS (self)->item_type);
    }
}

void
bst_item_view_set_container (BstItemView *self,
			     SfiProxy     new_container)
{
  g_return_if_fail (BST_IS_ITEM_VIEW (self));
  if (new_container)
    g_return_if_fail (BSE_IS_CONTAINER (new_container));

  BST_ITEM_VIEW_GET_CLASS (self)->set_container (self, new_container);

  bst_widget_update_activatable (self);
}

void
bst_item_view_select (BstItemView *self,
		      SfiProxy	   item)
{
  g_return_if_fail (BST_IS_ITEM_VIEW (self));
  g_return_if_fail (BSE_IS_ITEM (item));

  if (self->tree && bse_item_get_parent (item) == self->container)
    {
      GtkTreeIter witer;
      if (bst_child_list_wrapper_get_iter (self->wlist, &witer, item))
	{
	  GtkTreeModel *smodel = gtk_tree_view_get_model (self->tree);
	  GtkTreeIter siter;
	  if (GTK_IS_TREE_MODEL_SORT (smodel))
	    gtk_tree_model_sort_convert_child_iter_to_iter (GTK_TREE_MODEL_SORT (smodel), &siter, &witer);
	  else
	    siter = witer;
	  gtk_tree_selection_select_iter (gtk_tree_view_get_selection (self->tree), &siter);
	}
      else /* probably not added yet */
	self->auto_select = item;
    }
}

gint
bst_item_view_get_proxy_row (BstItemView *self,
                             SfiProxy	  item)
{
  g_return_val_if_fail (BST_IS_ITEM_VIEW (self), -1);
  g_return_val_if_fail (BSE_IS_ITEM (item), -1);

  if (self->tree && bse_item_get_parent (item) == self->container)
    {
      GtkTreeIter witer;
      if (bst_child_list_wrapper_get_iter (self->wlist, &witer, item))
	{
	  GtkTreeModel *smodel = gtk_tree_view_get_model (self->tree);
	  GtkTreePath *path;
          GtkTreeIter siter;
          gint row = -1;
	  if (GTK_IS_TREE_MODEL_SORT (smodel))
	    gtk_tree_model_sort_convert_child_iter_to_iter (GTK_TREE_MODEL_SORT (smodel), &siter, &witer);
	  else
	    siter = witer;
          path = gtk_tree_model_get_path (smodel, &siter);
          if (gtk_tree_path_get_depth (path) > 0)
            row = gtk_tree_path_get_indices (path)[0];
          gtk_tree_path_free (path);
          return row;
	}
    }
  return -1;
}

SfiProxy
bst_item_view_get_proxy (BstItemView *self,
			 gint         row)
{
  SfiProxy item = 0;

  g_return_val_if_fail (BST_IS_ITEM_VIEW (self), 0);

  if (self->tree && row >= 0)
    {
      GtkTreeIter siter, witer;
      GtkTreeModel *smodel = gtk_tree_view_get_model (self->tree);
      GtkTreePath *path = gtk_tree_path_new_from_indices (row, -1);
      gtk_tree_model_get_iter (smodel, &siter, path);
      gtk_tree_path_free (path);
      if (GTK_IS_TREE_MODEL_SORT (smodel))
	gtk_tree_model_sort_convert_iter_to_child_iter (GTK_TREE_MODEL_SORT (smodel), &witer, &siter);
      else
	witer = siter;
      item = bst_child_list_wrapper_get_from_iter (self->wlist, &witer);
    }
  if (item)
    g_return_val_if_fail (BSE_IS_ITEM (item), 0);
  return item;
}

SfiProxy
bst_item_view_get_current (BstItemView *self)
{
  SfiProxy item = 0;
  GtkTreeIter siter;
  GtkTreeModel *smodel;

  g_return_val_if_fail (BST_IS_ITEM_VIEW (self), 0);

  if (self->tree && gtk_tree_selection_get_selected (gtk_tree_view_get_selection (self->tree),
						     &smodel, &siter))
    {
      GtkTreeIter witer;
      if (GTK_IS_TREE_MODEL_SORT (smodel))
	gtk_tree_model_sort_convert_iter_to_child_iter (GTK_TREE_MODEL_SORT (smodel), &witer, &siter);
      else
	witer = siter;
      item = bst_child_list_wrapper_get_from_iter (self->wlist, &witer);
    }
  if (item)
    g_return_val_if_fail (BSE_IS_ITEM (item), 0);
  return item;
}

void
bst_item_view_enable_param_view (BstItemView *self,
                                 gboolean     enabled)
{
  g_return_if_fail (BST_IS_ITEM_VIEW (self));

  if (self->pview)
    {
      if (enabled)
        gtk_widget_show (self->pview);
      else
        gtk_widget_hide (self->pview);
    }
  bst_widget_update_activatable (self);
}

static void
bst_item_view_update_activatable (BstActivatable *activatable)
{
  BstItemView *self = BST_ITEM_VIEW (activatable);
  BstItemViewClass *ivclass = BST_ITEM_VIEW_GET_CLASS (self);
  gulong i;

  /* update action buttons */
  for (i = 0; i < ivclass->n_ops; i++)
    {
      BstItemViewOp *bop = ivclass->ops + i;
      if (self->op_widgets[i])
        gtk_widget_set_sensitive (self->op_widgets[i], bst_activatable_can_activate (activatable, bop->op));
    }
}
