/* BEAST - Bedevilled Audio System
 * Copyright (C) 2004 Tim Janik
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
#ifndef __BST_GROW_BAR_H__
#define __BST_GROW_BAR_H__

#include <gtk/gtkalignment.h>
#include <gtk/gtkrange.h>

G_BEGIN_DECLS

/* --- type macros --- */
#define BST_TYPE_GROW_BAR              (bst_grow_bar_get_type ())
#define BST_GROW_BAR(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), BST_TYPE_GROW_BAR, BstGrowBar))
#define BST_GROW_BAR_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), BST_TYPE_GROW_BAR, BstGrowBarClass))
#define BST_IS_GROW_BAR(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), BST_TYPE_GROW_BAR))
#define BST_IS_GROW_BAR_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), BST_TYPE_GROW_BAR))
#define BST_GROW_BAR_GET_CLASS(object) (G_TYPE_INSTANCE_GET_CLASS ((object), BST_TYPE_GROW_BAR, BstGrowBarClass))

/* --- type macros --- */
#define BST_TYPE_HGROW_BAR              (bst_hgrow_bar_get_type ())
#define BST_HGROW_BAR(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), BST_TYPE_HGROW_BAR, BstHGrowBar))
#define BST_HGROW_BAR_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), BST_TYPE_HGROW_BAR, BstHGrowBarClass))
#define BST_IS_HGROW_BAR(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), BST_TYPE_HGROW_BAR))
#define BST_IS_HGROW_BAR_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), BST_TYPE_HGROW_BAR))
#define BST_HGROW_BAR_GET_CLASS(object) (G_TYPE_INSTANCE_GET_CLASS ((object), BST_TYPE_HGROW_BAR, BstHGrowBarClass))

/* --- type macros --- */
#define BST_TYPE_VGROW_BAR              (bst_vgrow_bar_get_type ())
#define BST_VGROW_BAR(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), BST_TYPE_VGROW_BAR, BstVGrowBar))
#define BST_VGROW_BAR_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), BST_TYPE_VGROW_BAR, BstVGrowBarClass))
#define BST_IS_VGROW_BAR(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), BST_TYPE_VGROW_BAR))
#define BST_IS_VGROW_BAR_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), BST_TYPE_VGROW_BAR))
#define BST_VGROW_BAR_GET_CLASS(object) (G_TYPE_INSTANCE_GET_CLASS ((object), BST_TYPE_VGROW_BAR, BstVGrowBarClass))


/* --- structures & typedefs --- */
typedef struct {
  GtkAlignment parent_instance;
  double       max_upper;
  GtkRange    *range;
} BstGrowBar;
typedef struct {
  GtkAlignmentClass parent_class;
  gboolean          is_horizontal;
} BstGrowBarClass;
typedef BstGrowBar        BstHGrowBar;
typedef BstGrowBar        BstVGrowBar;
typedef BstGrowBarClass   BstHGrowBarClass;
typedef BstGrowBarClass   BstVGrowBarClass;


/* --- prototypes --- */
GType		bst_hgrow_bar_get_type	        (void);
GType		bst_vgrow_bar_get_type	        (void);
GType		bst_grow_bar_get_type	        (void);
void		bst_grow_bar_set_max_upper      (BstGrowBar	*self,
                                                 gdouble         maxupper);
void            bst_grow_bar_set_adjustment     (BstGrowBar     *self,
                                                 GtkAdjustment  *adj);
GtkAdjustment*  bst_grow_bar_get_adjustment     (BstGrowBar     *self);

G_END_DECLS

#endif /* __BST_GROW_BAR_H__ */
