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
#ifndef __BST_WAVE_VIEW_H__
#define __BST_WAVE_VIEW_H__

#include	"bstitemview.h"

G_BEGIN_DECLS


/* --- Gtk+ type macros --- */
#define	BST_TYPE_WAVE_VIEW	      (bst_wave_view_get_type ())
#define	BST_WAVE_VIEW(object)	      (GTK_CHECK_CAST ((object), BST_TYPE_WAVE_VIEW, BstWaveView))
#define	BST_WAVE_VIEW_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), BST_TYPE_WAVE_VIEW, BstWaveViewClass))
#define	BST_IS_WAVE_VIEW(object)      (GTK_CHECK_TYPE ((object), BST_TYPE_WAVE_VIEW))
#define	BST_IS_WAVE_VIEW_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), BST_TYPE_WAVE_VIEW))
#define BST_WAVE_VIEW_GET_CLASS(obj)  (GTK_CHECK_GET_CLASS ((obj), BST_TYPE_WAVE_VIEW, BstWaveViewClass))


/* --- structures & typedefs --- */
typedef	struct	_BstWaveView		BstWaveView;
typedef	struct	_BstWaveViewClass	BstWaveViewClass;
struct _BstWaveView
{
  BstItemView	 parent_object;
  guint          editable : 1;
};
struct _BstWaveViewClass
{
  BstItemViewClass parent_class;
};


/* --- actions --- */
enum {
  BST_ACTION_LOAD_WAVE  = BST_ACTION_WAVE_FIRST,
  BST_ACTION_LOAD_WAVE_LIB,
  BST_ACTION_DELETE_WAVE,
  BST_ACTION_EDIT_WAVE,
  BST_ACTION_WAVE_LAST
};


/* --- prototypes --- */
GType		bst_wave_view_get_type          (void);
GtkWidget*	bst_wave_view_new               (SfiProxy     wrepo);
void            bst_wave_view_set_editable      (BstWaveView *self,
                                                 gboolean     enabled);

G_END_DECLS

#endif /* __BST_WAVE_VIEW_H__ */
