/* GSL Engine - Flow module operation engine
 * Copyright (C) 2001-2003 Tim Janik
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */
#include "gslopmaster.h"

#include "gslcommon.h"
#include "gslopnode.h"
#include "gsloputil.h"
#include "gslopschedule.h"
#include "gslieee754.h"
#include <string.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <errno.h>


#define JOB_DEBUG(...)  sfi_debug ("job", __VA_ARGS__)
#define TJOB_DEBUG(...) sfi_debug ("tjob", __VA_ARGS__)

#define	NODE_FLAG_RECONNECT(node)  G_STMT_START { /*(node)->needs_reset = TRUE*/; } G_STMT_END


/* --- time stamping (debugging) --- */
#define	ToyprofStamp		struct timeval
#define	toyprof_clock_name()	("Glibc gettimeofday(2)")
#define toyprof_stampinit()	/* nothing */
#define	toyprof_stamp(st)	gettimeofday (&(st), 0)
#define	toyprof_stamp_ticks()	(1000000)
static inline guint64
toyprof_elapsed (ToyprofStamp fstamp,
		 ToyprofStamp lstamp)
{
  guint64 first = fstamp.tv_sec * toyprof_stamp_ticks () + fstamp.tv_usec;
  guint64 last  = lstamp.tv_sec * toyprof_stamp_ticks () + lstamp.tv_usec;
  return last - first;
}


/* --- typedefs & structures --- */
typedef struct _Poll Poll;
struct _Poll
{
  Poll	     *next;
  GslPollFunc poll_func;
  gpointer    data;
  guint       n_fds;
  GPollFD    *fds;
  GslFreeFunc free_func;
};
typedef struct _Timer Timer;
struct _Timer
{
  Timer             *next;
  GslEngineTimerFunc timer_func;
  gpointer           data;
  GslFreeFunc        free_func;
};


/* --- prototypes --- */
static void	master_schedule_discard	(void);


/* --- variables --- */
static gboolean	       master_need_reflow = FALSE;
static gboolean	       master_need_process = FALSE;
static EngineNode     *master_consumer_list = NULL;
const gfloat           gsl_engine_master_zero_block[GSL_STREAM_MAX_VALUES] = { 0, }; /* FIXME */
static Timer	      *master_timer_list = NULL;
static Poll	      *master_poll_list = NULL;
static guint           master_n_pollfds = 0;
static guint           master_pollfds_changed = FALSE;
static GPollFD         master_pollfds[GSL_ENGINE_MAX_POLLFDS];
static EngineSchedule *master_schedule = NULL;
static SfiRing        *boundary_node_list = NULL;
static gboolean        master_new_boundary_jobs = FALSE;


/* --- node state functions --- */
static void
add_consumer (EngineNode *node)
{
  g_return_if_fail (ENGINE_NODE_IS_CONSUMER (node) && node->toplevel_next == NULL && node->integrated);
  
  node->toplevel_next = master_consumer_list;
  master_consumer_list = node;
}

static void
remove_consumer (EngineNode *node)
{
  EngineNode *tmp, *last = NULL;
  
  g_return_if_fail (!ENGINE_NODE_IS_CONSUMER (node) || !node->integrated);
  
  for (tmp = master_consumer_list; tmp; last = tmp, tmp = last->toplevel_next)
    if (tmp == node)
      break;
  g_return_if_fail (tmp != NULL);
  if (last)
    last->toplevel_next = node->toplevel_next;
  else
    master_consumer_list = node->toplevel_next;
  node->toplevel_next = NULL;
}

static void
propagate_update_suspend (EngineNode *node)
{
  if (!node->update_suspend)
    {
      guint i, j;
      node->update_suspend = TRUE;
      for (i = 0; i < ENGINE_NODE_N_ISTREAMS (node); i++)
	if (node->inputs[i].src_node)
	  propagate_update_suspend (node->inputs[i].src_node);
      for (j = 0; j < ENGINE_NODE_N_JSTREAMS (node); j++)
	for (i = 0; i < node->module.jstreams[j].jcount; i++)
	  propagate_update_suspend (node->jinputs[j][i].src_node);
    }
}

static void
master_idisconnect_node (EngineNode *node,
			 guint       istream)
{
  EngineNode *src_node = node->inputs[istream].src_node;
  guint ostream = node->inputs[istream].src_stream;
  gboolean was_consumer;
  
  g_assert (ostream < ENGINE_NODE_N_OSTREAMS (src_node) &&
	    src_node->outputs[ostream].n_outputs > 0);	/* these checks better pass */
  
  node->inputs[istream].src_node = NULL;
  node->inputs[istream].src_stream = ~0;
  node->module.istreams[istream].connected = 0;	/* scheduler update */
  was_consumer = ENGINE_NODE_IS_CONSUMER (src_node);
  src_node->outputs[ostream].n_outputs -= 1;
  src_node->module.ostreams[ostream].connected = 0; /* scheduler update */
  src_node->output_nodes = sfi_ring_remove (src_node->output_nodes, node);
  NODE_FLAG_RECONNECT (node);
  NODE_FLAG_RECONNECT (src_node);
  /* update suspension state of input */
  propagate_update_suspend (src_node);
  /* add to consumer list */
  if (!was_consumer && ENGINE_NODE_IS_CONSUMER (src_node))
    add_consumer (src_node);
}

static void
master_jdisconnect_node (EngineNode *node,
			 guint       jstream,
			 guint       con)
{
  EngineNode *src_node = node->jinputs[jstream][con].src_node;
  guint i, ostream = node->jinputs[jstream][con].src_stream;
  gboolean was_consumer;
  
  g_assert (ostream < ENGINE_NODE_N_OSTREAMS (src_node) &&
	    node->module.jstreams[jstream].jcount > 0 &&
	    src_node->outputs[ostream].n_outputs > 0);	/* these checks better pass */
  
  i = --node->module.jstreams[jstream].jcount;
  node->jinputs[jstream][con] = node->jinputs[jstream][i];
  node->module.jstreams[jstream].values[i] = NULL; /* float**values 0-termination */
  was_consumer = ENGINE_NODE_IS_CONSUMER (src_node);
  src_node->outputs[ostream].n_outputs -= 1;
  src_node->module.ostreams[ostream].connected = 0; /* scheduler update */
  src_node->output_nodes = sfi_ring_remove (src_node->output_nodes, node);
  NODE_FLAG_RECONNECT (node);
  NODE_FLAG_RECONNECT (src_node);
  /* update suspension state of input */
  propagate_update_suspend (src_node);
  /* add to consumer list */
  if (!was_consumer && ENGINE_NODE_IS_CONSUMER (src_node))
    add_consumer (src_node);
}

static void
master_disconnect_node_outputs (EngineNode *src_node,
				EngineNode *dest_node)
{
  gint i, j;
  
  for (i = 0; i < ENGINE_NODE_N_ISTREAMS (dest_node); i++)
    if (dest_node->inputs[i].src_node == src_node)
      master_idisconnect_node (dest_node, i);
  for (j = 0; j < ENGINE_NODE_N_JSTREAMS (dest_node); j++)
    for (i = 0; i < dest_node->module.jstreams[j].jcount; i++)
      if (dest_node->jinputs[j][i].src_node == src_node)
	master_jdisconnect_node (dest_node, j, i--);
}


/* --- timed job handling --- */
static inline void
insert_trash_job (EngineNode      *node,
                  EngineReplyJob  *rjob)
{
  rjob->next = node->rjob_first;
  node->rjob_first = rjob;
  if (!node->rjob_last)
    node->rjob_last = node->rjob_first;
}

static inline EngineTimedJob*
node_pop_flow_job (EngineNode  *node,
                   guint64      tick_stamp)
{
  EngineTimedJob *tjob = node->flow_jobs;
  if_reject (tjob != NULL)
    {
      if (tjob->tick_stamp <= tick_stamp)
        {
          node->flow_jobs = tjob->next;
          insert_trash_job (node, (EngineReplyJob*) tjob);
        }
      else
        tjob = NULL;
    }
  return tjob;
}

static inline EngineTimedJob*
node_pop_boundary_job (EngineNode  *node,
                       guint64      tick_stamp,
                       SfiRing     *blist_node)
{
  EngineTimedJob *tjob = node->boundary_jobs;
  if (tjob != NULL)
    {
      if (tjob->tick_stamp <= tick_stamp)
        {
          node->boundary_jobs = tjob->next;
          insert_trash_job (node, (EngineReplyJob*) tjob);
          if (!node->boundary_jobs)
            boundary_node_list = sfi_ring_remove_node (boundary_node_list, blist_node);
        }
      else
        tjob = NULL;
    }
  return tjob;
}

static inline EngineTimedJob*
insert_timed_job (EngineTimedJob *head,
                  EngineTimedJob *tjob)
{
  EngineTimedJob *last = NULL, *tmp = head;
  /* find next position */
  while (tmp && tmp->tick_stamp <= tjob->tick_stamp)
    {
      last = tmp;
      tmp = last->next;
    }
  /* insert before */
  tjob->next = tmp;
  if (last)
    last->next = tjob;
  else
    head = tjob;
  return head;
}

static void
node_insert_flow_job (EngineNode      *node,
                      EngineTimedJob  *tjob)
{
  node->flow_jobs = insert_timed_job (node->flow_jobs, tjob);
}

static void
node_insert_boundary_job (EngineNode      *node,
                          EngineTimedJob  *tjob)
{
  if (!node->boundary_jobs)
    boundary_node_list = sfi_ring_append (boundary_node_list, node);
  node->boundary_jobs = insert_timed_job (node->boundary_jobs, tjob);
}

static inline guint64
node_peek_flow_job_stamp (EngineNode *node)
{
  EngineTimedJob *tjob = node->flow_jobs;
  if_reject (tjob != NULL)
    return tjob->tick_stamp;
  return GSL_MAX_TICK_STAMP;
}

static inline guint64
node_peek_boundary_job_stamp (EngineNode *node)
{
  EngineTimedJob *tjob = node->boundary_jobs;
  if_reject (tjob != NULL)
    return tjob->tick_stamp;
  return GSL_MAX_TICK_STAMP;
}


/* --- job processing --- */
static void
master_process_job (GslJob *job)
{
  switch (job->job_id)
    {
      EngineNode *node, *src_node;
      Poll *poll, *poll_last;
      Timer *timer;
      guint64 stamp;
      guint istream, jstream, ostream, con;
      EngineTimedJob *tjob;
      EngineReplyJob *rjob;
      gboolean was_consumer;
    case ENGINE_JOB_SYNC:
      JOB_DEBUG ("sync");
      master_need_reflow |= TRUE;
      master_schedule_discard();
      GSL_SPIN_LOCK (job->data.sync.lock_mutex);
      *job->data.sync.lock_p = TRUE;
      sfi_cond_signal (job->data.sync.lock_cond);
      while (*job->data.sync.lock_p)
        sfi_cond_wait (job->data.sync.lock_cond, job->data.sync.lock_mutex);
      GSL_SPIN_UNLOCK (job->data.sync.lock_mutex);
      break;
    case ENGINE_JOB_INTEGRATE:
      node = job->data.node;
      JOB_DEBUG ("integrate(%p)", node);
      g_return_if_fail (node->integrated == FALSE);
      g_return_if_fail (node->sched_tag == FALSE);
      job->data.node = NULL;  /* ownership taken over */
      _engine_mnl_integrate (node);
      if (ENGINE_NODE_IS_CONSUMER (node))
	add_consumer (node);
      node->counter = GSL_TICK_STAMP;
      NODE_FLAG_RECONNECT (node);
      node->local_active = 0;   /* by default not suspended */
      node->update_suspend = TRUE;
      node->needs_reset = TRUE;
      master_need_reflow |= TRUE;
      break;
    case ENGINE_JOB_KILL_INPUTS:
      node = job->data.node;
      JOB_DEBUG ("kill_inputs(%p)", node);
      g_return_if_fail (node->integrated == TRUE);
      for (istream = 0; istream < ENGINE_NODE_N_ISTREAMS (node); istream++)
	if (node->inputs[istream].src_node)
	  master_idisconnect_node (node, istream);
      for (jstream = 0; jstream < ENGINE_NODE_N_JSTREAMS (node); jstream++)
	while (node->module.jstreams[jstream].jcount)
	  master_jdisconnect_node (node, jstream, node->module.jstreams[jstream].jcount - 1);
      master_need_reflow |= TRUE;
      break;
    case ENGINE_JOB_KILL_OUTPUTS:
      node = job->data.node;
      JOB_DEBUG ("kill_outputs(%p)", node);
      g_return_if_fail (node->integrated == TRUE);
      while (node->output_nodes)
	master_disconnect_node_outputs (node, node->output_nodes->data);
      master_need_reflow |= TRUE;
      break;
    case ENGINE_JOB_DISCARD:
      node = job->data.node;
      JOB_DEBUG ("discard(%p, %p)", node, node->module.klass);
      g_return_if_fail (node->integrated == TRUE);
      /* kill inputs */
      for (istream = 0; istream < ENGINE_NODE_N_ISTREAMS (node); istream++)
	if (node->inputs[istream].src_node)
	  master_idisconnect_node (node, istream);
      for (jstream = 0; jstream < ENGINE_NODE_N_JSTREAMS (node); jstream++)
	while (node->module.jstreams[jstream].jcount)
	  master_jdisconnect_node (node, jstream, node->module.jstreams[jstream].jcount - 1);
      /* kill outputs */
      while (node->output_nodes)
	master_disconnect_node_outputs (node, node->output_nodes->data);
      /* remove from consumer list */
      if (ENGINE_NODE_IS_CONSUMER (node))
	{
	  _engine_mnl_remove (node);
	  remove_consumer (node);
	}
      else
	_engine_mnl_remove (node);
      node->counter = GSL_MAX_TICK_STAMP;
      master_need_reflow |= TRUE;
      master_schedule_discard ();	/* discard schedule so node may be freed */
      /* nuke pending timed jobs */
      do
        tjob = node_pop_flow_job (node, GSL_MAX_TICK_STAMP);
      while (tjob);
      if (node->boundary_jobs)
        do
          tjob = node_pop_boundary_job (node, GSL_MAX_TICK_STAMP, sfi_ring_find (boundary_node_list, node));
        while (tjob);
      _engine_node_collect_jobs (node);
      break;
    case ENGINE_JOB_SET_CONSUMER:
    case ENGINE_JOB_UNSET_CONSUMER:
      node = job->data.node;
      JOB_DEBUG ("toggle_consumer(%p)", node);
      g_return_if_fail (node->integrated == TRUE);
      was_consumer = ENGINE_NODE_IS_CONSUMER (node);
      node->is_consumer = job->job_id == ENGINE_JOB_SET_CONSUMER;
      if (was_consumer != ENGINE_NODE_IS_CONSUMER (node))
	{
	  if (ENGINE_NODE_IS_CONSUMER (node))
	    add_consumer (node);
	  else
	    remove_consumer (node);
	  master_need_reflow |= TRUE;
	}
      break;
    case ENGINE_JOB_SUSPEND:
      node = job->data.tick.node;
      stamp = job->data.tick.stamp;
      JOB_DEBUG ("suspend(%p,%llu)", node, stamp);
      g_return_if_fail (node->integrated == TRUE);
      if (node->local_active < stamp)
	{
	  propagate_update_suspend (node);
	  node->local_active = stamp;
	  node->needs_reset = TRUE;
	  master_need_reflow |= TRUE;
	}
      break;
    case ENGINE_JOB_RESUME:
      node = job->data.tick.node;
      stamp = job->data.tick.stamp;
      JOB_DEBUG ("resume(%p,%llu)", node, stamp);
      g_return_if_fail (node->integrated == TRUE);
      if (node->local_active > stamp)
	{
	  propagate_update_suspend (node);
	  node->local_active = stamp;
	  node->needs_reset = TRUE;
	  master_need_reflow |= TRUE;
	}
      break;
    case ENGINE_JOB_ICONNECT:
      node = job->data.connection.dest_node;
      src_node = job->data.connection.src_node;
      istream = job->data.connection.dest_ijstream;
      ostream = job->data.connection.src_ostream;
      JOB_DEBUG ("connect(%p,%u,%p,%u)", node, istream, src_node, ostream);
      g_return_if_fail (node->integrated == TRUE);
      g_return_if_fail (src_node->integrated == TRUE);
      g_return_if_fail (node->inputs[istream].src_node == NULL);
      node->inputs[istream].src_node = src_node;
      node->inputs[istream].src_stream = ostream;
      node->module.istreams[istream].connected = 0;	/* scheduler update */
      /* remove from consumer list */
      was_consumer = ENGINE_NODE_IS_CONSUMER (src_node);
      src_node->outputs[ostream].n_outputs += 1;
      src_node->module.ostreams[ostream].connected = 0; /* scheduler update */
      src_node->output_nodes = sfi_ring_append (src_node->output_nodes, node);
      NODE_FLAG_RECONNECT (node);
      NODE_FLAG_RECONNECT (src_node);
      /* update suspension state of input */
      propagate_update_suspend (src_node);
      if (was_consumer && !ENGINE_NODE_IS_CONSUMER (src_node))
	remove_consumer (src_node);
      master_need_reflow |= TRUE;
      break;
    case ENGINE_JOB_JCONNECT:
      node = job->data.connection.dest_node;
      src_node = job->data.connection.src_node;
      jstream = job->data.connection.dest_ijstream;
      ostream = job->data.connection.src_ostream;
      JOB_DEBUG ("jconnect(%p,%u,%p,%u)", node, jstream, src_node, ostream);
      g_return_if_fail (node->integrated == TRUE);
      g_return_if_fail (src_node->integrated == TRUE);
      con = node->module.jstreams[jstream].jcount++;
      node->jinputs[jstream] = g_renew (EngineJInput, node->jinputs[jstream], node->module.jstreams[jstream].jcount);
      node->module.jstreams[jstream].values = g_renew (const gfloat*, node->module.jstreams[jstream].values, node->module.jstreams[jstream].jcount + 1);
      node->module.jstreams[jstream].values[node->module.jstreams[jstream].jcount] = NULL; /* float**values 0-termination */
      node->jinputs[jstream][con].src_node = src_node;
      node->jinputs[jstream][con].src_stream = ostream;
      /* remove from consumer list */
      was_consumer = ENGINE_NODE_IS_CONSUMER (src_node);
      src_node->outputs[ostream].n_outputs += 1;
      src_node->module.ostreams[ostream].connected = 0; /* scheduler update */
      src_node->output_nodes = sfi_ring_append (src_node->output_nodes, node);
      NODE_FLAG_RECONNECT (node);
      NODE_FLAG_RECONNECT (src_node);
      /* update suspension state of input */
      propagate_update_suspend (src_node);
      if (was_consumer && !ENGINE_NODE_IS_CONSUMER (src_node))
	remove_consumer (src_node);
      master_need_reflow |= TRUE;
      break;
    case ENGINE_JOB_IDISCONNECT:
      node = job->data.connection.dest_node;
      JOB_DEBUG ("idisconnect(%p,%u)", node, job->data.connection.dest_ijstream);
      g_return_if_fail (node->integrated == TRUE);
      g_return_if_fail (node->inputs[job->data.connection.dest_ijstream].src_node != NULL);
      master_idisconnect_node (node, job->data.connection.dest_ijstream);
      master_need_reflow |= TRUE;
      break;
    case ENGINE_JOB_JDISCONNECT:
      node = job->data.connection.dest_node;
      jstream = job->data.connection.dest_ijstream;
      src_node = job->data.connection.src_node;
      ostream = job->data.connection.src_ostream;
      JOB_DEBUG ("jdisconnect(%p,%u,%p,%u)", node, jstream, src_node, ostream);
      g_return_if_fail (node->integrated == TRUE);
      g_return_if_fail (node->module.jstreams[jstream].jcount > 0);
      for (con = 0; con < node->module.jstreams[jstream].jcount; con++)
	if (node->jinputs[jstream][con].src_node == src_node &&
	    node->jinputs[jstream][con].src_stream == ostream)
	  break;
      if (con < node->module.jstreams[jstream].jcount)
	{
	  master_jdisconnect_node (node, jstream, con);
	  master_need_reflow |= TRUE;
	}
      else
	g_warning ("jdisconnect(dest:%p,%u,src:%p,%u): no such connection", node, jstream, src_node, ostream);
      break;
    case ENGINE_JOB_FORCE_RESET:
      node = job->data.node;
      JOB_DEBUG ("reset(%p)", node);
      g_return_if_fail (node->integrated == TRUE);
      node->counter = GSL_TICK_STAMP;
      node->needs_reset = TRUE;
      break;
    case ENGINE_JOB_ACCESS:
      node = job->data.access.node;
      JOB_DEBUG ("access node(%p): %p(%p)", node, job->data.access.access_func, job->data.access.data);
      g_return_if_fail (node->integrated == TRUE);
      node->counter = GSL_TICK_STAMP;
      job->data.access.access_func (&node->module, job->data.access.data);
      break;
    case ENGINE_JOB_REQUEST_REPLY:
      node = job->data.timed_job.node;
      rjob = job->data.reply_job.rjob;
      JOB_DEBUG ("add reply_request(%p,%p)", node, rjob);
      g_return_if_fail (node->integrated == TRUE);
      job->data.reply_job.rjob = NULL;  /* ownership taken over */
      rjob->next = node->reply_jobs;
      node->reply_jobs = rjob;
      break;
    case ENGINE_JOB_FLOW_JOB:
      node = job->data.timed_job.node;
      tjob = job->data.timed_job.tjob;
      JOB_DEBUG ("add flow_job(%p,%p)", node, tjob);
      g_return_if_fail (node->integrated == TRUE);
      job->data.timed_job.tjob = NULL;	/* ownership taken over */
      node_insert_flow_job (node, tjob);
      _engine_mnl_node_changed (node);
      break;
    case ENGINE_JOB_BOUNDARY_JOB:
      node = job->data.timed_job.node;
      tjob = job->data.timed_job.tjob;
      JOB_DEBUG ("add boundary_job(%p,%p)", node, tjob);
      g_return_if_fail (node->integrated == TRUE);
      job->data.timed_job.tjob = NULL;	/* ownership taken over */
      node_insert_boundary_job (node, tjob);
      master_new_boundary_jobs = TRUE;
      break;
    case ENGINE_JOB_DEBUG:
      JOB_DEBUG ("debug");
      g_printerr ("JOB-DEBUG: %s\n", job->data.debug);
      break;
    case ENGINE_JOB_ADD_POLL:
      JOB_DEBUG ("add poll %p(%p,%u)", job->data.poll.poll_func, job->data.poll.data, job->data.poll.n_fds);
      if (job->data.poll.n_fds + master_n_pollfds > GSL_ENGINE_MAX_POLLFDS)
	g_error ("adding poll job exceeds maximum number of poll-fds (%u > %u)",
		 job->data.poll.n_fds + master_n_pollfds, GSL_ENGINE_MAX_POLLFDS);
      poll = sfi_new_struct0 (Poll, 1);
      poll->poll_func = job->data.poll.poll_func;
      poll->data = job->data.poll.data;
      poll->free_func = job->data.poll.free_func;
      job->data.poll.free_func = NULL;		/* don't free data this round */
      poll->n_fds = job->data.poll.n_fds;
      poll->fds = poll->n_fds ? master_pollfds + master_n_pollfds : master_pollfds;
      master_n_pollfds += poll->n_fds;
      if (poll->n_fds)
	master_pollfds_changed = TRUE;
      memcpy (poll->fds, job->data.poll.fds, sizeof (poll->fds[0]) * poll->n_fds);
      poll->next = master_poll_list;
      master_poll_list = poll;
      break;
    case ENGINE_JOB_REMOVE_POLL:
      JOB_DEBUG ("remove poll %p(%p)", job->data.poll.poll_func, job->data.poll.data);
      for (poll = master_poll_list, poll_last = NULL; poll; poll_last = poll, poll = poll_last->next)
	if (poll->poll_func == job->data.poll.poll_func && poll->data == job->data.poll.data)
	  {
	    if (poll_last)
	      poll_last->next = poll->next;
	    else
	      master_poll_list = poll->next;
	    break;
	  }
      if (poll)
	{
	  job->data.poll.free_func = poll->free_func;	/* free data with job */
	  poll_last = poll;
	  if (poll_last->n_fds)
	    {
	      for (poll = master_poll_list; poll; poll = poll->next)
		if (poll->fds > poll_last->fds)
		  poll->fds -= poll_last->n_fds;
	      g_memmove (poll_last->fds, poll_last->fds + poll_last->n_fds,
			 ((guint8*) (master_pollfds + master_n_pollfds)) -
			 ((guint8*) (poll_last->fds + poll_last->n_fds)));
	      master_n_pollfds -= poll_last->n_fds;
	      master_pollfds_changed = TRUE;
	    }
	  sfi_delete_struct (Poll, poll_last);
	}
      else
	g_warning (G_STRLOC ": failed to remove unknown poll function %p(%p)",
		   job->data.poll.poll_func, job->data.poll.data);
      break;
    case ENGINE_JOB_ADD_TIMER:
      JOB_DEBUG ("add timer %p(%p)", job->data.timer.timer_func, job->data.timer.data);
      timer = sfi_new_struct0 (Timer, 1);
      timer->timer_func = job->data.timer.timer_func;
      timer->data = job->data.timer.data;
      timer->free_func = job->data.timer.free_func;
      job->data.timer.free_func = NULL;		/* don't free data this round */
      timer->next = master_timer_list;
      master_timer_list = timer;
      break;
    default:
      g_assert_not_reached ();
    }
  JOB_DEBUG ("done");
}

static void
master_poll_check (glong   *timeout_p,
		   gboolean check_with_revents)
{
  gboolean need_processing = FALSE;
  Poll *poll;
  
  if (master_need_process || *timeout_p == 0)
    {
      master_need_process = TRUE;
      return;
    }
  for (poll = master_poll_list; poll; poll = poll->next)
    {
      glong timeout = -1;
      
      if (poll->poll_func (poll->data, gsl_engine_block_size (), &timeout,
			   poll->n_fds, poll->n_fds ? poll->fds : NULL, check_with_revents)
	  || timeout == 0)
	{
	  need_processing |= TRUE;
	  *timeout_p = 0;
	  break;
	}
      else if (timeout > 0)
	*timeout_p = *timeout_p < 0 ? timeout : MIN (*timeout_p, timeout);
    }
  master_need_process = need_processing;
}

static void
master_tick_stamp_inc (void)
{
  Timer *timer, *last = NULL;
  guint64 new_stamp;
  _gsl_tick_stamp_inc ();
  new_stamp = GSL_TICK_STAMP;
  timer = master_timer_list;
  while (timer)
    {
      Timer *next = timer->next;
      if (!timer->timer_func (timer->data, new_stamp))
	{
	  GslTrans *trans = gsl_trans_open ();
	  if (last)
	    last->next = next;
	  else
	    master_timer_list = next;
	  /* free timer data in user thread */
	  gsl_trans_add (trans, gsl_job_add_timer (timer->timer_func, timer->data, timer->free_func));
	  gsl_trans_dismiss (trans);
	  sfi_delete_struct (Timer, timer);
	}
      else
	last = timer;
      timer = next;
    }
}

static inline guint64
master_update_node_state (EngineNode *node,
                          guint64     max_tick)
{
  /* the node is not necessarily scheduled */
  EngineTimedJob *tjob;
  /* if a reset is pending, it needs to be handled *before*
   * flow jobs change state.
   */
  if_reject (node->needs_reset && !ENGINE_NODE_IS_SUSPENDED (node, node->counter))
    {
      /* for suspended nodes, reset() occours later */
      if (node->module.klass->reset)
        node->module.klass->reset (&node->module);
      node->needs_reset = FALSE;
    }
  tjob = node_pop_flow_job (node, max_tick);
  if_reject (tjob != NULL)
    do
      {
	TJOB_DEBUG ("flow-access for (%p:s=%u) at:%lld current:%lld\n",
		    node, node->sched_tag, tjob->tick_stamp, node->counter);
        tjob->access_func (&node->module, tjob->data);
        tjob = node_pop_flow_job (node, max_tick);
      }
    while (tjob);
  return node_peek_flow_job_stamp (node);
}

gpointer
gsl_module_peek_reply (GslModule *module)
{
  EngineNode *node = ENGINE_NODE (module);
  g_return_val_if_fail (ENGINE_NODE_IS_SCHEDULED (node), NULL);
  return node->reply_jobs ? node->reply_jobs->data : NULL;
}

gpointer
gsl_module_process_reply (GslModule *module)
{
  EngineNode *node = ENGINE_NODE (module);
  g_return_val_if_fail (ENGINE_NODE_IS_SCHEDULED (node), NULL);
  if (G_UNLIKELY (node->reply_jobs))
    {
      EngineReplyJob *rjob = node->reply_jobs;
      node->reply_jobs = rjob->next;
      insert_trash_job (node, rjob);
      return rjob->data;
    }
  return NULL;
}

static void
master_process_locked_node (EngineNode *node,
			    guint       n_values)
{
  guint64 next_counter, new_counter, final_counter = GSL_TICK_STAMP + n_values;
  guint i, j, diff;
  
  g_return_if_fail (node->integrated && node->sched_tag);
  
  while (node->counter < final_counter)
    {
      /* call reset() and exec flow jobs */
      next_counter = master_update_node_state (node, node->counter);
      /* figure n_values to process */
      new_counter = MIN (next_counter, final_counter);
      if (node->next_active > node->counter)
        new_counter = MIN (node->next_active, new_counter);
      diff = node->counter - GSL_TICK_STAMP;
      /* ensure all istream inputs have n_values available */
      for (i = 0; i < ENGINE_NODE_N_ISTREAMS (node); i++)
	{
	  EngineNode *inode = node->inputs[i].real_node;
	  
	  if (inode)
	    {
	      ENGINE_NODE_LOCK (inode);
	      if (inode->counter < final_counter)
		master_process_locked_node (inode, final_counter - node->counter);
	      node->module.istreams[i].values = inode->outputs[node->inputs[i].real_stream].buffer;
	      node->module.istreams[i].values += diff;
	      ENGINE_NODE_UNLOCK (inode);
	    }
	  else
	    node->module.istreams[i].values = gsl_engine_master_zero_block;
	}
      /* ensure all jstream inputs have n_values available */
      for (j = 0; j < ENGINE_NODE_N_JSTREAMS (node); j++)
	for (i = 0; i < node->module.jstreams[j].n_connections; i++) /* assumes scheduled node */
	  {
	    EngineNode *inode = node->jinputs[j][i].real_node;
	    
	    ENGINE_NODE_LOCK (inode);
	    if (inode->counter < final_counter)
	      master_process_locked_node (inode, final_counter - node->counter);
	    node->module.jstreams[j].values[i] = inode->outputs[node->jinputs[j][i].real_stream].buffer;
	    node->module.jstreams[j].values[i] += diff;
	    ENGINE_NODE_UNLOCK (inode);
	  }
      /* update obuffer pointer */
      for (i = 0; i < ENGINE_NODE_N_OSTREAMS (node); i++)
	node->module.ostreams[i].values = node->outputs[i].buffer + diff;
      /* process() node */
      if_reject (ENGINE_NODE_IS_SUSPENDED (node, node->counter))
	{
	  /* suspended node processing behaviour */
	  for (i = 0; i < ENGINE_NODE_N_OSTREAMS (node); i++)
	    if (node->module.ostreams[i].connected)
	      node->module.ostreams[i].values = (gfloat*) gsl_engine_master_zero_block;
          node->needs_reset = TRUE;
	}
      else
        node->module.klass->process (&node->module, new_counter - node->counter);
      /* catch obuffer pointer changes */
      for (i = 0; i < ENGINE_NODE_N_OSTREAMS (node); i++)
	{
	  /* FIXME: this takes the worst possible performance hit to support virtualization */
	  if (node->module.ostreams[i].values != node->outputs[i].buffer + diff)
	    memcpy (node->outputs[i].buffer + diff, node->module.ostreams[i].values,
		    (new_counter - node->counter) * sizeof (gfloat));
	}
      /* update node counter */
      node->counter = new_counter;
    }
}

static GslLong gsl_profile_modules = 0;	/* set to 1 in gdb to get profile output */

static void
master_process_flow (void)
{
  guint64 final_counter = GSL_TICK_STAMP + gsl_engine_block_size ();
  GslLong profile_maxtime = 0;
  GslLong profile_modules = gsl_profile_modules;
  EngineNode *profile_node = NULL;
  
  g_return_if_fail (master_need_process == TRUE);
  
  g_assert (gsl_fpu_okround () == TRUE);
  
  if (master_schedule)
    {
      EngineNode *node;
      
      _engine_schedule_restart (master_schedule);
      _engine_set_schedule (master_schedule);
      
      node = _engine_pop_unprocessed_node ();
      while (node)
	{
	  ToyprofStamp profile_stamp1, profile_stamp2;
	  
	  if_reject (profile_modules)
	    toyprof_stamp (profile_stamp1);
	  
	  master_process_locked_node (node, gsl_engine_block_size ());
	  
	  if_reject (profile_modules)
	    {
	      GslLong duration;
	      
	      toyprof_stamp (profile_stamp2);
	      duration = toyprof_elapsed (profile_stamp1, profile_stamp2);
	      if (duration > profile_maxtime)
		{
		  profile_maxtime = duration;
		  profile_node = node;
		}
	    }
	  
	  _engine_push_processed_node (node);
	  node = _engine_pop_unprocessed_node ();
	}
      
      if_reject (profile_modules)
	{
	  if (profile_node)
	    {
	      if (profile_maxtime > profile_modules)
		g_print ("Excess Node: %p  Duration: %lu usecs     ((void(*)())%p)         \n",
			 profile_node, profile_maxtime, profile_node->module.klass->process);
	      else
		g_print ("Slowest Node: %p  Duration: %lu usecs     ((void(*)())%p)         \r",
			 profile_node, profile_maxtime, profile_node->module.klass->process);
	    }
	}

      /* walk unscheduled nodes with flow jobs */
      node = _engine_mnl_head ();
      while (node && GSL_MNL_UNSCHEDULED_FLOW_NODE (node))
	{
	  EngineNode *tmp = node->mnl_next;
          node->counter = final_counter;
          master_update_node_state (node, node->counter - 1);
	  _engine_mnl_node_changed (node);      /* collects trash jobs */
	  node = tmp;
	}

      /* nothing new to process, wait for slaves */
      _engine_wait_on_unprocessed ();
      
      _engine_unset_schedule (master_schedule);
      master_tick_stamp_inc ();
      _engine_recycle_const_values (FALSE);
    }
  master_need_process = FALSE;
}

static void
master_reschedule_flow (void)
{
  EngineNode *node;
  
  g_return_if_fail (master_need_reflow == TRUE);
  
  if (!master_schedule)
    master_schedule = _engine_schedule_new ();
  else
    {
      _engine_schedule_unsecure (master_schedule);
      _engine_schedule_clear (master_schedule);
    }
  for (node = master_consumer_list; node; node = node->toplevel_next)
    _engine_schedule_consumer_node (master_schedule, node);
  _engine_schedule_secure (master_schedule);
  master_need_reflow = FALSE;
}

static void
master_schedule_discard (void)
{
  g_return_if_fail (master_need_reflow == TRUE);
  
  if (master_schedule)
    {
      _engine_schedule_unsecure (master_schedule);
      _engine_schedule_destroy (master_schedule);
      master_schedule = NULL;
    }
}


/* --- MasterThread main loop --- */
gboolean
_engine_master_prepare (GslEngineLoop *loop)
{
  gboolean need_dispatch;
  guint i;
  
  g_return_val_if_fail (loop != NULL, FALSE);
  
  /* setup and clear pollfds here already, so master_poll_check() gets no junk (and IRIX can't handle non-0 revents) */
  loop->fds_changed = master_pollfds_changed;
  master_pollfds_changed = FALSE;
  loop->n_fds = master_n_pollfds;
  loop->fds = master_pollfds;
  for (i = 0; i < loop->n_fds; i++)
    loop->fds[i].revents = 0;
  loop->revents_filled = FALSE;
  
  loop->timeout = -1;
  /* cached checks first */
  need_dispatch = master_need_reflow || master_need_process;
  /* lengthy query */
  if (!need_dispatch)
    need_dispatch = _engine_job_pending ();
  /* invoke custom poll checks */
  if (!need_dispatch)
    {
      master_poll_check (&loop->timeout, FALSE);
      need_dispatch = master_need_process;
    }
  if (need_dispatch)
    loop->timeout = 0;
  
  return need_dispatch;
}

gboolean
_engine_master_check (const GslEngineLoop *loop)
{
  gboolean need_dispatch;
  
  g_return_val_if_fail (loop != NULL, FALSE);
  g_return_val_if_fail (loop->n_fds == master_n_pollfds, FALSE);
  g_return_val_if_fail (loop->fds == master_pollfds, FALSE);
  if (loop->n_fds)
    g_return_val_if_fail (loop->revents_filled == TRUE, FALSE);
  
  /* cached checks first */
  need_dispatch = master_need_reflow || master_need_process;
  /* lengthy query */
  if (!need_dispatch)
    need_dispatch = _engine_job_pending ();
  /* invoke custom poll checks */
  if (!need_dispatch)
    {
      glong dummy = -1;
      
      master_poll_check (&dummy, TRUE);
      need_dispatch = master_need_process;
    }
  
  return need_dispatch;
}

void
_engine_master_dispatch_jobs (void)
{
  guint64 CURRENT_STAMP = GSL_TICK_STAMP;
  guint64 last_block_tick = CURRENT_STAMP + gsl_engine_block_size() - 1;
  GslJob *job = _engine_pop_job ();
  /* here, we have to process _all_ pending jobs in a row. a popped job
   * stays valid until the next call to _engine_pop_job().
   */
  while (job)
    {
      master_process_job (job);
      job = _engine_pop_job ();
    }
  /* process boundary jobs and possibly newly queued jobs after that. */
  if_reject (boundary_node_list != NULL)
    do
      {
        SfiRing *ring = boundary_node_list;
        master_new_boundary_jobs = FALSE;       /* to catch new boundary jobs */
        while (ring)
          {
            SfiRing *current = ring;
            EngineNode *node = ring->data;
            EngineTimedJob *tjob;
            ring = sfi_ring_walk (ring, boundary_node_list);
            tjob = node_pop_boundary_job (node, last_block_tick, current);
            if (tjob)
              node->counter = CURRENT_STAMP;
            while (tjob)
              {
                TJOB_DEBUG ("boundary-access for (%p:s=%u) at:%lld current:%lld\n",
                            node, node->sched_tag, tjob->tick_stamp, node->counter);
                tjob->access_func (&node->module, tjob->data);
                tjob = node_pop_boundary_job (node, last_block_tick, current);
              }
          }
        /* process newly queued jobs if any */
        job = _engine_pop_job ();
        while (job)
          {
            master_process_job (job);
            job = _engine_pop_job ();
          }
        /* need to repeat if master_process_job() just queued a new boundary job */
      }
    while (master_new_boundary_jobs);   /* new boundary jobs arrived */
}

void
_engine_master_dispatch (void)
{
  /* processing has prime priority, but we can't process the
   * network, until all jobs have been handled and if necessary
   * rescheduled the network.
   * that's why we have to handle everything at once and can't
   * preliminarily return after just handling jobs or rescheduling.
   */
  _engine_master_dispatch_jobs ();
  if (master_need_reflow)
    master_reschedule_flow ();
  if (master_need_process)
    master_process_flow ();
}

void
_engine_master_thread (EngineMasterData *mdata)
{
  gboolean run = TRUE;

  /* assert sane configuration checks, since we're simply casting structures */
  g_assert (sizeof (struct pollfd) == sizeof (GPollFD) &&
	    G_STRUCT_OFFSET (GPollFD, fd) == G_STRUCT_OFFSET (struct pollfd, fd) &&
	    G_STRUCT_OFFSET (GPollFD, events) == G_STRUCT_OFFSET (struct pollfd, events) &&
	    G_STRUCT_OFFSET (GPollFD, revents) == G_STRUCT_OFFSET (struct pollfd, revents));
  
  /* add the thread wakeup pipe to master pollfds,
   * so we get woken  up in time.
   */
  master_pollfds[0].fd = mdata->wakeup_pipe[0];
  master_pollfds[0].events = G_IO_IN;
  master_n_pollfds = 1;
  master_pollfds_changed = TRUE;

  toyprof_stampinit ();

  while (run)
    {
      GslEngineLoop loop;
      gboolean need_dispatch;
      
      need_dispatch = _engine_master_prepare (&loop);
      
      if (!need_dispatch)
	{
	  gint err;
	  
	  err = poll ((struct pollfd*) loop.fds, loop.n_fds, loop.timeout);
	  
	  if (err >= 0)
	    loop.revents_filled = TRUE;
	  else if (errno != EINTR)
	    g_printerr ("%s: poll() error: %s\n", G_STRFUNC, g_strerror (errno));
	  
	  if (loop.revents_filled)
	    need_dispatch = _engine_master_check (&loop);
	}
      
      if (need_dispatch)
	_engine_master_dispatch ();
      
      /* clear wakeup pipe */
      {
	guint8 data[64];
	gint l;
	do
	  l = read (mdata->wakeup_pipe[0], data, sizeof (data));
	while ((l < 0 && errno == EINTR) || l == sizeof (data));
      }

      /* wakeup user thread if necessary */
      if (gsl_engine_has_garbage ())
	sfi_thread_wakeup (mdata->user_thread);
    }
}

/* vim:set ts=8 sts=2 sw=2: */
