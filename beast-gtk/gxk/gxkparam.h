/* BEAST - Bedevilled Audio System
 * Copyright (C) 1998-2002 Tim Janik and Red Hat, Inc.
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
#ifndef __BST_PARAM_H__
#define __BST_PARAM_H__

#include	"bstdefs.h"
#include	<gtk/gtk.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* --- structure --- */
typedef struct _BstParam BstParam;
struct _BstParam
{
  GValue      value;
  GParamSpec *pspec;
  gpointer    owner;
  gpointer    group;
  guint	      locked : 24;
  guint	      editable : 1;
  guint	      is_object : 1;
  guint	      is_procedure : 1;
};


/* --- prototypes --- */
/* the parent_box is assumed to already have all parents set, up to the
 * toplevel GtkWindow.
 */
BstParam*	bst_param_create		(gpointer	 owner,
						 GType		 owner_type,
						 GParamSpec	*pspec,
						 const gchar    *param_group,
						 GtkWidget	*parent,
						 GtkTooltips	*tooltips);
/* bst_param_get   - set the widget's value from the object
 * bst_param_set   - set the object's value from the widget (this implies
 *		     a succeding call to bst_param_get).
 * bst_param_reset - reset to 0
 */
void		bst_param_get			(BstParam	*bparam);
void		bst_param_set			(BstParam	*bparam);
void		bst_param_reset			(BstParam	*bparam);
void		bst_param_set_default		(BstParam	*bparam);
gboolean	bst_param_set_value		(BstParam	*bparam,
						 const GValue	*value);
void		bst_param_set_object		(BstParam	*bparam,
						 BseObject	*object);
void		bst_param_set_procedure		(BstParam	*bparam,
						 BseProcedureClass *proc);
void		bst_param_set_editable		(BstParam	*bparam,
						 gboolean	 editable);
void		bst_param_destroy		(BstParam	*bparam);





#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __BST_PARAM_H__ */
