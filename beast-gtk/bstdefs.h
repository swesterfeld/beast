/* BEAST - Bedevilled Audio System
 * Copyright (C) 1998, 1999 Olaf Hoehmann and Tim Janik
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */
#ifndef __BST_DEFS_H__
#define __BST_DEFS_H__

#include        <bse/bse.h>
#include	<gtk/gtk.h>
#include        <gle/gle.h>
#include        <gle/gtkcluehunter.h>
#include        <gle/gtkhwrapbox.h>
#include        <gle/gtkvwrapbox.h>
#include	<gnome.h>
#include	"gnomeforest.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */


typedef enum
{
  BST_OP_NONE,

  /* project operations
   */
  BST_OP_PROJECT_NEW,
  BST_OP_PROJECT_OPEN,
  BST_OP_PROJECT_SAVE,
  BST_OP_PROJECT_SAVE_AS,
  BST_OP_PROJECT_NEW_SONG,
  BST_OP_PROJECT_NEW_SNET,
  BST_OP_PROJECT_CLOSE,
  BST_OP_PROJECT_PLAY,
  BST_OP_PROJECT_STOP,

  /* debugging */
  BST_OP_REBUILD,
  BST_OP_REFRESH,

  /* song operations
   */
  BST_OP_PATTERN_ADD,
  BST_OP_PATTERN_DELETE,
  BST_OP_PATTERN_EDITOR,
  BST_OP_INSTRUMENT_ADD,
  BST_OP_INSTRUMENT_DELETE,

  /* super operations
   */
  BST_OP_UNDO_LAST,
  BST_OP_REDO_LAST,
  BST_OP_PLAY,
  BST_OP_STOP,

  /* application wide
   */
  BST_OP_EXIT,
  BST_OP_HELP_ABOUT,

  BST_OP_LAST
} BstOps;

#define	BST_TAG_DIAMETER	(20)



/* --- pixmap stock --- */
typedef enum
{
  BST_ICON_NOICON,
  BST_ICON_MOUSE_TOOL,
  BST_ICON_LAST
} BstIconId;
BseIcon* bst_icon_from_stock (BstIconId icon_id);


/* --- debug stuff --- */
typedef enum                    /* <skip> */
{ /* keep in sync with bstmain.c */
  BST_DEBUG_KEYTABLE		= (1 << 0),
  BST_DEBUG_MASTER		= (1 << 1),
  BST_DEBUG_SAMPLES		= (1 << 2)
} BstDebugFlags;
extern BstDebugFlags bst_debug_flags;
#ifdef G_ENABLE_DEBUG
#define BST_DEBUG(type, code)   G_STMT_START { \
  if (bst_debug_flags & BST_DEBUG_##type) \
    { code ; } \
} G_STMT_END
#else  /* !G_ENABLE_DEBUG */
#define BST_DEBUG(type, code)   /* do nothing */
#endif /* !G_ENABLE_DEBUG */


/* it's hackish to have these prototypes in here, but we need
 * 'em somewhere, implementations are in bstmain.c
 */
extern void bst_update_can_operate (GtkWidget   *some_widget);
extern void bst_object_set         (gpointer     object,
				    const gchar *first_arg_name,
				    ...); /* hackery rulez! */
#define	BST_OBJECT_ARGS_CHANGED(object)	G_STMT_START { \
    if (!GTK_OBJECT_DESTROYED (object)) \
      gtk_signal_emit_by_name ((GtkObject*) (object), "args-changed"); \
} G_STMT_END


/* --- canvas utils/workarounds --- */
extern GnomeCanvasPoints* gnome_canvas_points_new0 (guint num_points);
extern void gnome_canvas_request_full_update (GnomeCanvas *canvas);
extern guint gnome_canvas_item_get_stacking (GnomeCanvasItem *item);
extern void gnome_canvas_item_keep_between (GnomeCanvasItem *between,
					    GnomeCanvasItem *item1,
					    GnomeCanvasItem *item2);



     
#ifdef __cplusplus
#pragma {
}
#endif /* __cplusplus */

#endif /* __BST_DEFS_H__ */
