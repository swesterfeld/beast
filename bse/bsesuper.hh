// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl.html
#ifndef __BSE_SUPER_H__
#define __BSE_SUPER_H__

#include        <bse/bsecontainer.hh>

/* --- object type macros --- */
#define	BSE_TYPE_SUPER		    (BSE_TYPE_ID (BseSuper))
#define BSE_SUPER(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), BSE_TYPE_SUPER, BseSuper))
#define BSE_SUPER_CLASS(class)      (G_TYPE_CHECK_CLASS_CAST ((class), BSE_TYPE_SUPER, BseSuperClass))
#define BSE_IS_SUPER(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), BSE_TYPE_SUPER))
#define BSE_IS_SUPER_CLASS(class)   (G_TYPE_CHECK_CLASS_TYPE ((class), BSE_TYPE_SUPER))
#define BSE_SUPER_GET_CLASS(object) (G_TYPE_INSTANCE_GET_CLASS ((object), BSE_TYPE_SUPER, BseSuperClass))
/* --- BseSuper member macros --- */
#define BSE_SUPER_NEEDS_CONTEXT(object)		  ((BSE_OBJECT_FLAGS (object) & BSE_SUPER_FLAG_NEEDS_CONTEXT) != 0)

typedef enum                            /*< skip >*/
{
  BSE_SUPER_FLAG_NEEDS_CONTEXT		 = 1 << (BSE_CONTAINER_FLAGS_USHIFT + 0),
} BseSuperFlags;
#define BSE_SUPER_FLAGS_USHIFT	       (BSE_CONTAINER_FLAGS_USHIFT + 1)

struct BseSuper : BseContainer {
  SfiTime	 creation_time;
  SfiTime	 mod_time;
  /* for BseProject */
  guint          context_handle;
  void           set_flag    (BseSuperFlags f)   { change_flags (uint16_t (f), true); }
  void           unset_flag  (BseSuperFlags f)   { change_flags (uint16_t (f), false); }
  using BseContainer::set_flag;
  using BseContainer::unset_flag;
};
struct BseSuperClass : BseContainerClass {
  void		(*modified)		(BseSuper	*super,
					 SfiTime	 stamp);
  void          (*compat_finish)        (BseSuper       *super,
                                         guint           vmajor,
                                         guint           vminor,
                                         guint           vmicro);
};

namespace Bse {

class SuperImpl : public ContainerImpl, public virtual SuperIface {
protected:
  virtual           ~SuperImpl         ();
  void               set_qauthor       (const String& val);
  void               set_qlicense      (const String& val);
public:
  explicit           SuperImpl         (BseObject*);

  virtual String     author            () const override;
  virtual void       author            (const String& val) override;
  virtual String     license           () const override;
  virtual void       license           (const String& val) override;
};

} // Bse

#endif /* __BSE_SUPER_H__ */
