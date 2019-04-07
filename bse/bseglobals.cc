// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl.html
#include "bseglobals.hh"
#include "bsemain.hh"
#include "bse/internal.hh"

/* --- functions --- */
void
bse_globals_init (void) { /* FIXME: remove */ }

double
bse_db_to_factor (double dB)
{
  double factor = dB / 20; /* Bell */
  return pow (10, factor);
}

double
bse_db_from_factor (double factor,
		    double min_dB)
{
  if (factor > 0)
    {
      double dB = log10 (factor); /* Bell */
      dB *= 20;
      return dB;
    }
  else
    return min_dB;
}

long
bse_time_range_to_ms (BseTimeRangeType time_range)
{
  assert_return (time_range >= BSE_TIME_RANGE_SHORT, 0);
  assert_return (time_range <= BSE_TIME_RANGE_LONG, 0);

  switch (time_range)
    {
    case BSE_TIME_RANGE_SHORT:		return BSE_TIME_RANGE_SHORT_ms;
    case BSE_TIME_RANGE_MEDIUM:		return BSE_TIME_RANGE_MEDIUM_ms;
    case BSE_TIME_RANGE_LONG:		return BSE_TIME_RANGE_LONG_ms;
    }
  return 0;	/* can't be triggered */
}


/* --- idle handlers --- */
/* important ordering constrains:
 * BSE_PRIORITY_NOW             = -G_MAXINT / 2
 * BSE_PRIORITY_HIGH		= G_PRIORITY_HIGH - 10
 * BSE_PRIORITY_NEXT		= G_PRIORITY_HIGH - 5
 * G_PRIORITY_HIGH		(-100)
 * BSE_PRIORITY_NOTIFY		= G_PRIORITY_DEFAULT - 1
 * G_PRIORITY_DEFAULT		(0)
 * GDK_PRIORITY_EVENTS		= G_PRIORITY_DEFAULT
 * BSE_PRIORITY_PROG_IFACE	= G_PRIORITY_DEFAULT
 * G_PRIORITY_HIGH_IDLE		(100)
 * BSE_PRIORITY_UPDATE		= G_PRIORITY_HIGH_IDLE + 5
 * GTK_PRIORITY_RESIZE		= G_PRIORITY_HIGH_IDLE + 10
 * GDK_PRIORITY_REDRAW		= G_PRIORITY_HIGH_IDLE + 20
 * G_PRIORITY_DEFAULT_IDLE	(200)
 * G_PRIORITY_LOW		(300)
 * BSE_PRIORITY_BACKGROUND	= G_PRIORITY_LOW + 500
 */

namespace Bse {

// Run `function` immediately with the next event loop iteration.
uint
exec_now (const std::function<void()> &function)
{
  using Func = std::function<void()>;
  Func *func = new Func (function);
  void *data = func;
  GSourceFunc wrapper = [] (void *data) -> gboolean {
    Func *func = (Func*) data;
    (*func) ();
    return false;
  };
  GDestroyNotify destroy = [] (void *data) {
    Func *func = (Func*) data;
    delete func;
  };
  GSource *source = g_idle_source_new ();
  g_source_set_priority (source, BSE_PRIORITY_NOW);
  g_source_set_callback (source, wrapper, data, destroy);
  uint id = g_source_attach (source, bse_main_context);
  g_source_unref (source);
  return id;
}

// Run `function` immediately with the next event loop iteration, return `true` to keep alive.
uint
exec_now (const std::function<bool()> &function)
{
  using Func = std::function<bool()>;
  Func *func = new Func (function);
  void *data = func;
  GSourceFunc wrapper = [] (void *data) -> gboolean {
    Func *func = (Func*) data;
    return (*func) ();
  };
  GDestroyNotify destroy = [] (void *data) {
    Func *func = (Func*) data;
    delete func;
  };
  GSource *source = g_idle_source_new ();
  g_source_set_priority (source, BSE_PRIORITY_NOW);
  g_source_set_callback (source, wrapper, data, destroy);
  uint id = g_source_attach (source, bse_main_context);
  g_source_unref (source);
  return id;
}

} // Bse

/**
 * @param function	user function
 * @param data	        user data
 * @return		idle handler id, suitable for bse_idle_remove()
 * Execute @a function (@a data) inside the main BSE thread as soon as possible.
 * Usually this function should not be used but bse_idle_next() should be used instead.
 * Only callbacks that have hard dependencies on immediate asyncronous execution,
 * preceeding even realtime synthesis job handling should be executed this way.
 * This function is MT-safe and may be called from any thread.
 */
uint
bse_idle_now (GSourceFunc function,
	      void       *data)
{
  GSource *source = g_idle_source_new ();
  uint id;
  g_source_set_priority (source, BSE_PRIORITY_NOW);
  g_source_set_callback (source, function, data, NULL);
  id = g_source_attach (source, bse_main_context);
  g_source_unref (source);
  return id;
}

/**
 * @param function	user function
 * @param data	        user data
 * @return		idle handler id, suitable for bse_idle_remove()
 * Execute @a function (@a data) inside the main BSE thread as soon as resonably possible.
 * This function is intended to be used by code which needs to execute some portions
 * asyncronously as soon as the BSE core isn't occupied by realtime job handling.
 * This function is MT-safe and may be called from any thread.
 */
uint
bse_idle_next (GSourceFunc function,
	       void       *data)
{
  GSource *source = g_idle_source_new ();
  uint id;
  g_source_set_priority (source, BSE_PRIORITY_NEXT);
  g_source_set_callback (source, function, data, NULL);
  id = g_source_attach (source, bse_main_context);
  g_source_unref (source);
  return id;
}

/**
 * @param function	user function
 * @param data	user data
 * @return		idle handler id, suitable for bse_idle_remove()
 * Queue @a function (@a data) for execution inside the main BSE thread,
 * similar to bse_idle_now(), albeit with a lower priority.
 * This function is intended to be used by code which emits
 * asyncronous notifications.
 * This function is MT-safe and may be called from any thread.
 */
uint
bse_idle_notify (GSourceFunc function,
		 void       *data)
{
  GSource *source = g_idle_source_new ();
  uint id;
  g_source_set_priority (source, BSE_PRIORITY_NOTIFY);
  g_source_set_callback (source, function, data, NULL);
  id = g_source_attach (source, bse_main_context);
  g_source_unref (source);
  return id;
}

uint
bse_idle_normal (GSourceFunc function,
		 void       *data)
{
  GSource *source = g_idle_source_new ();
  uint id;
  g_source_set_priority (source, BSE_PRIORITY_NORMAL);
  g_source_set_callback (source, function, data, NULL);
  id = g_source_attach (source, bse_main_context);
  g_source_unref (source);
  return id;
}

uint
bse_idle_update (GSourceFunc function,
		 void       *data)
{
  GSource *source = g_idle_source_new ();
  uint id;
  g_source_set_priority (source, BSE_PRIORITY_UPDATE);
  g_source_set_callback (source, function, data, NULL);
  id = g_source_attach (source, bse_main_context);
  g_source_unref (source);
  return id;
}

uint
bse_idle_background (GSourceFunc function,
		     void       *data)
{
  GSource *source = g_idle_source_new ();
  uint id;
  g_source_set_priority (source, BSE_PRIORITY_BACKGROUND);
  g_source_set_callback (source, function, data, NULL);
  id = g_source_attach (source, bse_main_context);
  g_source_unref (source);
  return id;
}

/**
 * @param usec_delay	microsecond delay
 * @param function	user function
 * @param data	user data
 * @return		idle handler id, suitable for bse_idle_remove()
 * Execute @a function (@a data) with the main BSE thread, similar to
 * bse_idle_now(), after a delay period of @a usec_delay has passed.
 * This function is MT-safe and may be called from any thread.
 */
uint
bse_idle_timed (guint64     usec_delay,
		GSourceFunc function,
		void       *data)
{
  GSource *source = g_timeout_source_new (CLAMP (usec_delay / 1000, 0, G_MAXUINT));
  uint id;
  g_source_set_priority (source, BSE_PRIORITY_NEXT);
  g_source_set_callback (source, function, data, NULL);
  id = g_source_attach (source, bse_main_context);
  g_source_unref (source);
  return id;
}

/**
 * @param id	idle handler id
 * Remove or unqueue an idle handler queued by bse_idle_now()
 * or one of its variants.
 * This function is MT-safe and may be called from any thread.
 */
gboolean
bse_idle_remove (uint id)
{
  GSource *source;

  assert_return (id > 0, FALSE);

  source = g_main_context_find_source_by_id (bse_main_context, id);
  if (source)
    g_source_destroy (source);
  return source != NULL;
}
