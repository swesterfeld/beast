// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl.html
#ifndef __BST_PIANO_ROLL_H__
#define __BST_PIANO_ROLL_H__

#include        "bstutils.hh"

/* --- type macros --- */
#define BST_TYPE_PIANO_ROLL              (bst_piano_roll_get_type ())
#define BST_PIANO_ROLL(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), BST_TYPE_PIANO_ROLL, BstPianoRoll))
#define BST_PIANO_ROLL_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), BST_TYPE_PIANO_ROLL, BstPianoRollClass))
#define BST_IS_PIANO_ROLL(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), BST_TYPE_PIANO_ROLL))
#define BST_IS_PIANO_ROLL_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), BST_TYPE_PIANO_ROLL))
#define BST_PIANO_ROLL_GET_CLASS(object) (G_TYPE_INSTANCE_GET_CLASS ((object), BST_TYPE_PIANO_ROLL, BstPianoRollClass))


/* --- typedefs & enums --- */
typedef struct _BstPianoRoll        BstPianoRoll;
typedef struct _BstPianoRollClass   BstPianoRollClass;
typedef enum    /*< skip >*/
{
  BST_PIANO_ROLL_MARKER_NONE,
  BST_PIANO_ROLL_MARKER_POINTER,
  BST_PIANO_ROLL_MARKER_SELECT
} BstPianoRollMarkerType;


/* --- structures & typedefs --- */
struct BstPianoRollDrag : GxkScrollCanvasDrag {
  uint	        start_tick = 0;
  int           start_note = 0;
  bool		start_valid = false;    // note out of range or non-existant black key
  bool		current_valid = false;  // note out of range or non-existant black key
  uint          current_tick = 0;
  int           current_note = 0;
  /* convenience: */
  BstPianoRoll *proll = NULL;
};
struct _BstPianoRoll
{
  GxkScrollCanvas parent_instance;

  Bse::PartS     part;
  SfiProxy	 song;
  Bse::PartLinkSeq plinks;
  gint		 min_note;
  gint		 max_note;
  guint		 vzoom;

  /* horizontal layout */
  guint		 ppqn;		/* parts per quarter note */
  guint		 qnpt;		/* quarter notes per tact */
  guint		 max_ticks;	/* in ticks */
  gfloat	 hzoom;

  /* last drag state */
  guint          start_tick;
  gint           start_note;
  guint          start_valid : 1;

  guint		 draw_qn_grid : 1;
  guint		 draw_qqn_grid : 1;

  /* slight hack */
  guint          release_closes_toplevel : 1;

  /* selection rectangle */
  int		 selection_tick;
  int		 selection_duration;
  int		 selection_min_note;
  int		 selection_max_note;
};
struct _BstPianoRollClass
{
  GxkScrollCanvasClass parent_class;

  void		(*canvas_drag)			(BstPianoRoll	  *self,
						 BstPianoRollDrag *drag);
  void		(*canvas_clicked)		(BstPianoRoll	  *proll,
						 guint		   button,
						 guint		   tick_position,
						 gint              note,
						 GdkEvent	  *event);
  void		(*piano_drag)			(BstPianoRoll	  *self,
						 BstPianoRollDrag *drag);
  void		(*piano_clicked)		(BstPianoRoll	  *proll,
						 guint		   button,
						 gint              note,
						 GdkEvent	  *event);
};


/* --- prototypes --- */
GType	bst_piano_roll_get_type			(void);
void	bst_piano_roll_set_part                 (BstPianoRoll *self, Bse::PartH part = Bse::PartH());
gfloat	bst_piano_roll_set_hzoom		(BstPianoRoll	*self,
						 gfloat		 hzoom);
gfloat	bst_piano_roll_set_vzoom		(BstPianoRoll	*self,
						 gfloat		 vzoom);
void	bst_piano_roll_set_view_selection	(BstPianoRoll	*self, int tick, int duration, int min_note, int max_note);
gint	bst_piano_roll_get_vpanel_width		(BstPianoRoll	*self);
void    bst_piano_roll_get_paste_pos		(BstPianoRoll	*self,
						 guint          *tick_p,
						 gint		*note_p);
void    bst_piano_roll_set_marker               (BstPianoRoll          *self,
                                                 guint                  mark_index,
                                                 guint                  position,
                                                 BstPianoRollMarkerType mtype);


#endif /* __BST_PIANO_ROLL_H__ */
