/* BEAST - Bedevilled Audio System
 * Copyright (C) 2003 Tim Janik
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
#ifndef __BST_AUX_DIALOGS_H__
#define __BST_AUX_DIALOGS_H__

#include "bstutils.h"

G_BEGIN_DECLS

/* --- list popup dialog --- */
typedef void (*BstListPopupHandler)  (GtkWidget              *dialog,
                                      gchar                 **strings,
                                      gpointer                user_data);
GtkWidget*     bst_list_popup_new    (const gchar            *title,
                                      GtkWidget              *transient_parent,
                                      BstListPopupHandler     handler,
                                      gpointer                data,
                                      GDestroyNotify          destroy);
void           bst_list_popup_add    (GtkWidget              *widget,
                                      const gchar            *string);
typedef void (*BstColorPopupHandler) (GtkWidget              *dialog,
                                      GdkColor               *color,
                                      gpointer                user_data);
GtkWidget*     bst_color_popup_new   (const gchar            *title,
                                      GtkWidget              *transient_parent,
                                      GdkColor                color,
                                      BstColorPopupHandler    handler,
                                      gpointer                data,
                                      GDestroyNotify          destroy);
gboolean       bst_key_combo_valid   (guint                   keyval,
                                      GdkModifierType         modifiers);
gboolean       bst_key_combo_popup   (const gchar            *function,
                                      guint                  *keyval,
                                      GdkModifierType        *modifier);


G_END_DECLS

#endif /* __BST_AUX_DIALOGS_H__ */
