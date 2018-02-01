// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl.html
#include <bse/bsecxxplugin.hh>
#include "bsepcmdevice-jack.hh"
#include <bse/bseblockutils.hh>
#include <bse/gsldatautils.hh>
#include <jack/jack.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <set>
#include <string>
#include <map>
#include <vector>
#include <chrono>

using std::vector;
using std::set;
using std::map;
using std::string;
using std::min;
using std::max;
using std::copy;
using Bse::Block;

namespace {

/**
 * This function uses std::copy to copy the n_values of data from ivalues
 * to ovalues. If a specialized version is available in bseblockutils,
 * then this - usually faster - version will be used.
 *
 * The data in ivalues and ovalues may not overlap.
 */
template<class Data> void
fast_copy (guint       n_values,
           Data       *ovalues,
	   const Data *ivalues)
{
  copy (ivalues, ivalues + n_values, ovalues);
}

template<> void
fast_copy (guint        n_values,
           float       *ovalues,
           const float *ivalues)
{
  Block::copy (n_values, ovalues, ivalues);
}

template<> void
fast_copy (guint	  n_values,
           guint32       *ovalues,
           const guint32 *ivalues)
{
  Block::copy (n_values, ovalues, ivalues);
}

/**
 * The FrameRingBuffer class implements a ringbuffer for the communication
 * between two threads. One thread - the producer thread - may only write
 * data to the ringbuffer. The other thread - the consumer thread - may
 * only read data from the ringbuffer.
 *
 * Given that these two threads only use the appropriate functions, no
 * other synchronization is required to ensure that the data gets safely
 * from the producer thread to the consumer thread. However, all operations
 * that are provided by the ringbuffer are non-blocking, so that you may
 * need a condition or other synchronization primitive if you want the
 * producer and/or consumer to block if the ringbuffer is full/empty.
 *
 * Implementation: the synchronization between the two threads is only
 * implemented by two index variables (read_frame_pos and write_frame_pos)
 * for which atomic integer reads and writes are required. Since the
 * producer thread only modifies the write_frame_pos and the consumer thread
 * only modifies the read_frame_pos, no compare-and-swap or similar
 * operations are needed to avoid concurrent writes.
 */
template<class T>
class FrameRingBuffer {
  //BIRNET_PRIVATE_COPY (FrameRingBuffer);
private:
  vector< vector<T> > m_channel_buffer;
  std::atomic<int>    m_atomic_read_frame_pos;
  std::atomic<int>    m_atomic_write_frame_pos;
  std::atomic<int>    m_atomic_dummy;
  guint		      m_channel_buffer_size;	  // = n_frames + 1; the extra frame allows us to
                                                  // see the difference between an empty/full ringbuffer
  guint		      m_n_channels;

  void
  write_memory_barrier()
  {

    /*
     * writing this dummy integer should ensure that all prior writes
     * are committed to memory
     */
    m_atomic_dummy = 0x12345678;
  }
public:
  FrameRingBuffer (guint n_frames = 0,
		   guint n_channels = 1)
  {
    resize (n_frames, n_channels);
  }
  /**
   * @returns the number of frames that are available for reading
   *
   * Check available read space in the ringbuffer.
   * This function may only be called from the consumer thread.
   */
  guint
  get_readable_frames()
  {
    int wpos = m_atomic_write_frame_pos;
    int rpos = m_atomic_read_frame_pos;

    if (wpos < rpos)		    /* wpos == rpos -> empty ringbuffer */
      wpos += m_channel_buffer_size;

    return wpos - rpos;
  }
  /**
   * @returns the number of successfully read frames
   *
   * Read data from the ringbuffer; if there is not enough data
   * in the ringbuffer, the function will return the number of frames
   * that could be read without blocking.
   *
   * This function should be called from the consumer thread.
   */
  guint
  read (guint   n_frames,
        T     **frames)
  {
    int rpos = m_atomic_read_frame_pos;
    guint can_read = min (get_readable_frames(), n_frames);

    guint read1 = min (can_read, m_channel_buffer_size - rpos);
    guint read2 = can_read - read1;

    for (guint ch = 0; ch < m_n_channels; ch++)
      {
	fast_copy (read1, frames[ch], &m_channel_buffer[ch][rpos]);
	fast_copy (read2, frames[ch] + read1, &m_channel_buffer[ch][0]);
      }

    m_atomic_read_frame_pos = (rpos + can_read) % m_channel_buffer_size;
    return can_read;
  }
  /**
   * @returns the number of frames that can be written
   *
   * Check available write space in the ringbuffer.
   * This function should be called from the producer thread.
   */
  guint
  get_writable_frames()
  {
    int wpos = m_atomic_write_frame_pos;
    int rpos = m_atomic_read_frame_pos;

    if (rpos <= wpos)		    /* wpos == rpos -> empty ringbuffer */
      rpos += m_channel_buffer_size;

    // the extra frame allows us to see the difference between an empty/full ringbuffer
    return rpos - wpos - 1;
  }
  /**
   * @returns the number of successfully written frames
   *
   * Write data to the ringbuffer; if there is not enough free space
   * in the ringbuffer, the function will return the amount of frames
   * consumed by a partial write (without blocking).
   *
   * This function may only be called from the producer thread.
   */
  guint
  write (guint     n_frames,
         const T **frames)
  {
    int wpos = m_atomic_write_frame_pos;
    guint can_write = min (get_writable_frames(), n_frames);

    guint write1 = min (can_write, m_channel_buffer_size - wpos);
    guint write2 = can_write - write1;

    for (guint ch = 0; ch < m_n_channels; ch++)
      {
	fast_copy (write1, &m_channel_buffer[ch][wpos], frames[ch]);
	fast_copy (write2, &m_channel_buffer[ch][0], frames[ch] + write1);
      }
 
    // It is important that the data from the previous writes get committed
    // to memory *before* the index variable is updated. Otherwise, the
    // consumer thread could be reading invalid data, if the index variable
    // got written before the rest of the data (when unordered writes are
    // performed).
    write_memory_barrier();

    m_atomic_write_frame_pos = (wpos + can_write) % m_channel_buffer_size;
    return can_write;
  }
  /**
   * @returns the maximum number of frames that the ringbuffer can contain
   *
   * This function can be called from any thread.
   */
  guint
  get_total_n_frames() const
  {
    // the extra frame allows us to see the difference between an empty/full ringbuffer
    return m_channel_buffer_size - 1;
  }
  /**
   * @returns the number of elements that are part of one frame
   *
   * This function can be called from any thread.
   */
  guint
  get_n_channels() const
  {
    return m_n_channels;
  }
  /**
   * Clear the ringbuffer.
   *
   * This function may not be used while either the producer thread or
   * the consumer thread are modifying the ringbuffer.
   */
  void
  clear()
  {
    m_atomic_read_frame_pos = 0;
    m_atomic_write_frame_pos = 0;
  }
  /**
   * Resize and clear the ringbuffer.
   *
   * This function may not be used while either the producer thread or
   * the consumer thread are modifying the ringbuffer.
   */
  void
  resize (guint n_frames,
          guint n_channels = 1)
  {
    m_n_channels = n_channels;
    m_channel_buffer.resize (n_channels);

    // the extra frame allows us to see the difference between an empty/full ringbuffer
    m_channel_buffer_size = n_frames + 1;
    for (guint ch = 0; ch < m_n_channels; ch++)
      m_channel_buffer[ch].resize (m_channel_buffer_size);

    clear();
  }
};


#define PDEBUG(...)     Bse::debug ("pcm-jack", __VA_ARGS__)

/* --- JACK PCM handle --- */
enum
{
  CALLBACK_STATE_INACTIVE = 0,
  CALLBACK_STATE_ACTIVE = 1,
};

struct JackPcmHandle
{
  BsePcmHandle  handle;

  vector<jack_port_t *>	  input_ports;
  vector<jack_port_t *>	  output_ports;

  guint			  buffer_frames;	/* input/output ringbuffer size in frames */

  std::condition_variable cond;
  std::mutex              cond_mutex;
  jack_client_t		 *jack_client;
  FrameRingBuffer<float>  input_ringbuffer;
  FrameRingBuffer<float>  output_ringbuffer;

  std::atomic<int>        atomic_callback_state;
  std::atomic<int>        atomic_ixruns;
  std::atomic<int>        atomic_oxruns;
  std::atomic<int>        atomic_pixruns;
  std::atomic<int>        atomic_poxruns;

  bool			  is_down;

  JackPcmHandle (jack_client_t *jack_client) :
    jack_client (jack_client),
    atomic_callback_state (0),
    atomic_ixruns (0),
    atomic_oxruns (0),
    atomic_pixruns (0),
    atomic_poxruns (0),
    is_down (false)
  {
    memset (&handle, 0, sizeof (handle));
  }
};

}

/* --- prototypes --- */
static void             bse_pcm_device_jack_class_init  (BsePcmDeviceJACKClass  *klass);
static void             bse_pcm_device_jack_init        (BsePcmDeviceJACK       *self);
static gsize            jack_device_read                (BsePcmHandle           *handle,
                                                         gfloat                 *values);
static void             jack_device_write               (BsePcmHandle           *handle,
                                                         const gfloat           *values);
static gboolean         jack_device_check_io            (BsePcmHandle           *handle,
                                                         glong                  *tiumeoutp);
static guint            jack_device_latency             (BsePcmHandle           *handle);
static void             jack_device_retrigger           (JackPcmHandle          *jack);

/* --- define object type and export to BSE --- */

BSE_RESIDENT_TYPE_DEF (BsePcmDeviceJACK, bse_pcm_device_jack, BSE_TYPE_PCM_DEVICE, NULL,
                       "PCM driver implementation for JACK Audio Connection Kit "
                                  "(http://jackaudio.org)", NULL);

/* --- variables --- */
static gpointer parent_class = NULL;

/* --- functions --- */
static void
bse_pcm_device_jack_init (BsePcmDeviceJACK *self)
{
  /* FIXME: move signal handling somewhere else */
  sigset_t signal_mask;
  sigemptyset (&signal_mask);
  sigaddset (&signal_mask, SIGPIPE);

  int rc = pthread_sigmask (SIG_BLOCK, &signal_mask, NULL);
  if (rc != 0)
    g_printerr ("jack driver: pthread_sigmask for SIGPIPE failed: %s\n", strerror (errno));

  jack_status_t status;

  self->jack_client = jack_client_open ("beast", JackNoStartServer, &status); /* FIXME: translations(?) */
  PDEBUG ("attaching to JACK server returned status: %d\n", status);
  /* FIXME: check status for proper error reporting
   * FIXME: what about server name? (necessary if more than one jackd instance is running)
   * FIXME: get rid of device unloading, so that the jackd connection can be kept initialized
   *
   * maybe move this to a different function
   */
}

namespace {
struct DeviceDetails {
  guint ports, input_ports, output_ports, physical_ports, terminal_ports;
  bool default_device;
  vector<string> input_port_names;
  vector<string> output_port_names;
  DeviceDetails() :
    ports (0),
    input_ports (0),
    output_ports (0),
    physical_ports (0),
    terminal_ports (0),
    default_device (false)
  {
  }
};
}

static map<string, DeviceDetails>
query_jack_devices (BsePcmDeviceJACK *self)
{
  map<string, DeviceDetails> devices;

  /* FIXME: we need to make a difference here between
   *  - jackd is running, but no output ports can be found
   *  - jackd is not running
   */
  const char **jack_ports = self->jack_client ? jack_get_ports (self->jack_client, NULL, NULL, 0) : NULL;
  if (jack_ports)
    {
      bool have_default_device = false;

      for (guint i = 0; jack_ports[i]; i++)
	{
	  const char *end = strchr (jack_ports[i], ':');
	  if (end)
	    {
	      string device_name (jack_ports[i], end);

	      jack_port_t *jack_port = jack_port_by_name (self->jack_client, jack_ports[i]);
	      if (jack_port)
		{
                  const char *port_type = jack_port_type (jack_port);
                  if (strcmp (port_type, JACK_DEFAULT_AUDIO_TYPE) == 0)
                    {
	              DeviceDetails &details = devices[device_name];
	              details.ports++;

                      int flags = jack_port_flags (jack_port);
                      if (flags & JackPortIsInput)
                        {
                          details.input_ports++;
                          details.input_port_names.push_back (jack_ports[i]);
                        }
                      if (flags & JackPortIsOutput)
                        {
                          details.output_ports++;
                          details.output_port_names.push_back (jack_ports[i]);
                        }
                      if (flags & JackPortIsPhysical)
                        {
                          details.physical_ports++;

                          if (!have_default_device)
                            {
                              /* the first device that has physical ports is the default device */
                              details.default_device = true;
                              have_default_device = true;
                            }
                        }
                      if (flags & JackPortIsTerminal)
                        details.terminal_ports++;
                    }
		}
	    }
	}
      free (jack_ports);
    }

  return devices;
}

static SfiRing*
bse_pcm_device_jack_list_devices (BseDevice *device)
{
  BsePcmDeviceJACK *self = BSE_PCM_DEVICE_JACK (device);
  map<string, DeviceDetails> devices = query_jack_devices (self);

  SfiRing *ring = NULL;
  for (map<string, DeviceDetails>::iterator di = devices.begin(); di != devices.end(); di++)
    {
      const string &device_name = di->first;
      DeviceDetails &details = di->second;
      BseDeviceEntry *entry;
      entry = bse_device_group_entry_new (device, g_strdup (device_name.c_str()),
                                                  g_strdup ("JACK Devices"),
                                                  g_strdup_printf ("%s (%d*input %d*output)%s",
								   device_name.c_str(),
								   details.input_ports,
								   details.output_ports,
								   details.physical_ports == details.ports ?
								     " [ hardware ]" : ""));

      /* ensure that the default device always is the first item listed */
      if (details.default_device)
	ring = sfi_ring_prepend (ring, entry);
      else
	ring = sfi_ring_append (ring, entry);
    }

  if (!ring)
    ring = sfi_ring_append (ring, bse_device_error_new (device, g_strdup_printf ("No devices found")));
  return ring;
}

static void
jack_shutdown_callback (void *jack_handle_ptr)
{
  JackPcmHandle *jack = static_cast <JackPcmHandle *> (jack_handle_ptr);

  std::lock_guard<std::mutex> cond_locker (jack->cond_mutex);
  jack->is_down = true;
  jack->cond.notify_one();
}

static int
jack_process_callback (jack_nframes_t n_frames,
                       void          *jack_handle_ptr)
{
  typedef jack_default_audio_sample_t JackSample;

  JackPcmHandle *jack = static_cast <JackPcmHandle *> (jack_handle_ptr);

  int callback_state = jack->atomic_callback_state;

  /* still waiting for initialization to complete */
  if (callback_state == CALLBACK_STATE_ACTIVE)
    {
      bool have_cond_lock = jack->cond_mutex.try_lock();

      guint n_channels = jack->handle.n_channels;

      if (jack->handle.readable)
	{
	  /* interleave input data for processing in the engine thread */
	  g_assert (jack->input_ports.size() == n_channels);

	  const JackSample *values[n_channels];
	  for (guint ch = 0; ch < n_channels; ch++)
	    values[ch] = (JackSample *) jack_port_get_buffer (jack->input_ports[ch], n_frames);

	  guint frames_written = jack->input_ringbuffer.write (n_frames, values);
	  if (frames_written != n_frames)
	    jack->atomic_ixruns++;      /* input underrun detected */
	}
      if (jack->handle.writable)
	{
	  g_assert (jack->output_ports.size() == n_channels);

	  JackSample *values[n_channels];
	  for (guint ch = 0; ch < n_channels; ch++)
	    values[ch] = (JackSample *) jack_port_get_buffer (jack->output_ports[ch], n_frames);

	  guint read_frames = jack->output_ringbuffer.read (n_frames, values);
	  if (read_frames != n_frames)
	    {
	      jack->atomic_oxruns++;     /* output underrun detected */

	      for (guint ch = 0; ch < n_channels; ch++)
		Block::fill ((n_frames - read_frames), &values[ch][read_frames], 0.0);
	    }
	}

      /*
       * as we can't (always) lock our data structures from the jack realtime
       * thread, using a condition introduces the possibility for a race:
       *
       * normal operation:
       *
       * [bse thread]      writes some data to output_ringbuffer
       * [bse thread]      checks remaining space, there is no room left
       * [bse thread]      sleeps on the condition
       * [jack thread]     reads some data from output_ringbuffer
       * [jack thread]     signals the condition
       * [bse thread]      continues to write some data to output_ringbuffer
       *
       * race condition:
       *
       * [bse thread]      writes some data to output_ringbuffer
       * [bse thread]      checks remaining space, there is no room left
       * [jack thread]     reads some data from output_ringbuffer
       * [jack thread]     signals the condition
       * [bse thread]      sleeps on the condition
       * 
       * since the jack callback gets called periodically, the bse thread will
       * never starve, though, in the worst case it will just miss a frame
       *
       * so we absolutely exclude the possibility for priority inversion (by
       * using trylock instead of lock), by introducing extra latency in
       * some extremely rare cases
       */
      if (have_cond_lock)
	jack->cond_mutex.unlock();
    }
  else
    {
      for (guint ch = 0; ch < jack->input_ports.size(); ch++)
	{
	  JackSample *values = (JackSample *) jack_port_get_buffer (jack->input_ports[ch], n_frames);
	  Block::fill (n_frames, values, 0.0);
	}
      for (guint ch = 0; ch < jack->output_ports.size(); ch++)
	{
	  JackSample *values = (JackSample *) jack_port_get_buffer (jack->output_ports[ch], n_frames);
	  Block::fill (n_frames, values, 0.0);
	}
    }
  jack->cond.notify_one();
  return 0;
}

static void
terminate_and_free_jack (JackPcmHandle *jack)
{
  assert_return (jack->jack_client != NULL);

  jack_deactivate (jack->jack_client);

  delete jack;
}

static Bse::Error
bse_pcm_device_jack_open (BseDevice     *device,
                          gboolean       require_readable,
                          gboolean       require_writable,
                          guint          n_args,
                          const gchar  **args)
{
  BsePcmDeviceJACK *self = BSE_PCM_DEVICE_JACK (device);
  if (!self->jack_client)
    return Bse::Error::FILE_OPEN_FAILED;

  JackPcmHandle *jack = new JackPcmHandle (self->jack_client);
  BsePcmHandle *handle = &jack->handle;

  handle->readable = require_readable;
  handle->writable = require_writable;
  handle->n_channels = BSE_PCM_DEVICE (device)->req_n_channels;

  /* try setup */
  Bse::Error error = Bse::Error::NONE;

  const char *dname = (n_args == 1) ? args[0] : "alsa_pcm";
  handle->mix_freq = jack_get_sample_rate (self->jack_client);

  jack->atomic_callback_state = CALLBACK_STATE_INACTIVE;

  for (guint i = 0; i < handle->n_channels; i++)
    {
      const int port_name_size = jack_port_name_size();
      char port_name[port_name_size];
      jack_port_t *port;

      snprintf (port_name, port_name_size, "in_%u", i);
      port = jack_port_register (self->jack_client, port_name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
      if (port)
	jack->input_ports.push_back (port);
      else
	error = Bse::Error::FILE_OPEN_FAILED;

      snprintf (port_name, port_name_size, "out_%u", i);
      port = jack_port_register (self->jack_client, port_name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
      if (port)
	jack->output_ports.push_back (port);
      else
	error = Bse::Error::FILE_OPEN_FAILED;
    }

  /* activate */
  
  if (error == 0)
    {
      jack_set_process_callback (self->jack_client, jack_process_callback, jack);
      jack_on_shutdown (self->jack_client, jack_shutdown_callback, jack);

      if (jack_activate (self->jack_client) != 0)
	error = Bse::Error::FILE_OPEN_FAILED;
    }

  /* If an error occurred so far, then the process() callback will not be
   * called and the condition will never be signalled, so we set the is_down
   * flag, which will ensure that we won't wait forever on the condition.
   */
  if (error != 0)
    jack->is_down = true;

  /* connect ports */

  if (error == 0)
    {
      map<string, DeviceDetails> devices = query_jack_devices (self);
      map<string, DeviceDetails>::const_iterator di;

      di = devices.find (dname);
      if (di != devices.end())
	{
	  const DeviceDetails &details = di->second;

	  for (guint ch = 0; ch < handle->n_channels; ch++)
	    {
	      if (details.output_ports > ch)
		jack_connect (self->jack_client, details.output_port_names[ch].c_str(),
		                                 jack_port_name (jack->input_ports[ch]));
	      if (details.input_ports > ch)
		jack_connect (self->jack_client, jack_port_name (jack->output_ports[ch]),
		                                 details.input_port_names[ch].c_str());
	    }
	}
    }

  /* setup buffer size */

  if (error == 0)
    {
      // keep at least two jack callback sizes for dropout free audio
      guint min_buffer_frames = jack_get_buffer_size (self->jack_client) * 2;

      // keep an extra engine buffer size (this compensates also for cases where the
      // engine buffer size is not a 2^N value, which would otherwise cause the
      // buffer never to be fully filled with 2 periods of data)
      min_buffer_frames += BSE_PCM_DEVICE (device)->req_block_length;

      // honor the user defined latency specification
      //
      // FIXME: should we try to tweak local buffering so that it corresponds
      // to the user defined latency, or global buffering (including the jack
      // buffer)? we do the former, since it is easier
      guint user_buffer_frames = BSE_PCM_DEVICE (device)->req_latency_ms * handle->mix_freq / 1000;
      guint buffer_frames = max (min_buffer_frames, user_buffer_frames);

      jack->input_ringbuffer.resize (buffer_frames, handle->n_channels);
      jack->output_ringbuffer.resize (buffer_frames, handle->n_channels);
      jack->buffer_frames  = jack->output_ringbuffer.get_writable_frames();

      // the ringbuffer should be exactly as big as requested
      g_assert (jack->buffer_frames == buffer_frames);
      PDEBUG ("ringbuffer size = %.3fms", jack->buffer_frames / double (handle->mix_freq) * 1000);
    }

  /* setup PCM handle or shutdown */
  if (error == 0)
    {
      bse_device_set_opened (device, dname, handle->readable, handle->writable);
      if (handle->readable)
        handle->read = jack_device_read;
      if (handle->writable)
        handle->write = jack_device_write;
      handle->check_io = jack_device_check_io;
      handle->latency = jack_device_latency;
      BSE_PCM_DEVICE (device)->handle = handle;

      /* will prevent dropouts at initialization, when no data is there at all */
      jack_device_retrigger (jack);
      jack_device_latency (handle);   // debugging only: print latency values
    }
  else
    {
      terminate_and_free_jack (jack);
    }
  PDEBUG ("JACK: opening PCM \"%s\" readupble=%d writable=%d: %s", dname, require_readable, require_writable, bse_error_blurb (error));

  return error;
}

static void
bse_pcm_device_jack_close (BseDevice *device)
{
  JackPcmHandle *jack = (JackPcmHandle*) BSE_PCM_DEVICE (device)->handle;
  BSE_PCM_DEVICE (device)->handle = NULL;

  terminate_and_free_jack (jack);
}

static void
bse_pcm_device_jack_finalize (GObject *object)
{
  BsePcmDeviceJACK *self = BSE_PCM_DEVICE_JACK (object);
  if (self->jack_client)
    {
      jack_client_close (self->jack_client);
      self->jack_client = NULL;
    }

  /* chain parent class' handler */
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
jack_device_retrigger (JackPcmHandle *jack)
{
  assert_return (jack->jack_client != NULL);

  /*
   * we need to reset the active flag to false here, as we modify the
   * buffers in a non threadsafe way; this is why we also wait for
   * the condition, to ensure that the jack callback really isn't
   * active any more
   */
  jack->atomic_callback_state = CALLBACK_STATE_INACTIVE;

  /* usually should not timeout, but be notified by the jack callback */
  {
    std::unique_lock<std::mutex> cond_locker (jack->cond_mutex);

    if (!jack->is_down)
      jack->cond.wait_for (cond_locker, std::chrono::milliseconds (100));
  }

  /* jack_ringbuffer_reset is not threadsafe! */
  jack->input_ringbuffer.clear();
  jack->output_ringbuffer.clear();

  /* initialize output ringbuffer with silence */
  vector<float>	  silence (jack->output_ringbuffer.get_total_n_frames());
  vector<const float *> silence_buffers (jack->output_ringbuffer.get_n_channels());

  fill (silence_buffers.begin(), silence_buffers.end(), &silence[0]);

  guint frames_written = jack->output_ringbuffer.write (jack->buffer_frames, &silence_buffers[0]);
  g_assert (frames_written == jack->buffer_frames);
  PDEBUG ("jack_device_retrigger: %d frames written", frames_written);
}

static gboolean
jack_device_check_io (BsePcmHandle *handle,
                      glong        *timeoutp)
{
  JackPcmHandle *jack = (JackPcmHandle*) handle;
  g_return_val_if_fail (jack->jack_client != NULL, false);

/*
  int ixruns = Atomic::int_get (&jack->ixruns);
  int oxruns = Atomic::int_get (&jack->oxruns);

  if (jack->poxruns != oxruns || jack->pixruns != ixruns)
    {
      printf ("beast caused jack input xruns %d, output xruns %d\n", ixruns, oxruns);
      jack->poxruns = oxruns;
      jack->pixruns = ixruns;

      jack_device_retrigger (jack);
    }
*/
  
  /* (FIXME?)
   *
   * we can not guarantee that beast will read the data from the jack ringbuffer,
   * since this depends on the presence of a module that actually reads data in
   * the synthesis graph : so we simply ignore the ixruns variable; I am not sure
   * if thats the right thing to do, though
   */
  int oxruns = jack->atomic_oxruns;

  if (jack->atomic_poxruns != oxruns)
    {
      g_printerr ("%d beast jack driver xruns\n", oxruns);
      jack->atomic_poxruns = oxruns;

      jack_device_retrigger (jack);
    }

  jack->atomic_callback_state = CALLBACK_STATE_ACTIVE;

  guint n_frames_avail = min (jack->output_ringbuffer.get_writable_frames(), jack->input_ringbuffer.get_readable_frames());

  /* check whether data can be processed */
  if (n_frames_avail >= handle->block_length)
    return TRUE;        /* need processing */

  /* calculate timeout until processing is possible or needed */
  guint diff_frames = handle->block_length - n_frames_avail;
  *timeoutp = diff_frames * 1000 / handle->mix_freq;
  return FALSE;
}

static guint
jack_device_latency (BsePcmHandle *handle)
{
  JackPcmHandle *jack = (JackPcmHandle*) handle;
  jack_nframes_t rlatency = 0, wlatency = 0;

  g_return_val_if_fail (jack->jack_client != NULL, 0);

  /* FIXME: the API of this function is broken, because you can't use its result
   * to sync for instance the play position pointer with the screen
   *
   * why? because it doesn't return you rlatency and wlatency seperately, but
   * only the sum of both 
   *
   * when using jack, there is no guarantee that rlatency and wlatency are equal
   */
  for (guint i = 0; i < jack->input_ports.size(); i++)
    {
      jack_nframes_t latency = jack_port_get_total_latency (jack->jack_client, jack->input_ports[i]);
      rlatency = max (rlatency, latency);
    }

  for (guint i = 0; i < jack->output_ports.size(); i++)
    {
      jack_nframes_t latency = jack_port_get_total_latency (jack->jack_client, jack->output_ports[i]);
      wlatency = max (wlatency, latency);
    }
  
  guint total_latency = 2 * jack->buffer_frames + rlatency + wlatency;
  PDEBUG ("rlatency=%.3f ms wlatency=%.3f ms ringbuffer=%.3f ms total_latency=%.3f ms",
          rlatency / double (handle->mix_freq) * 1000,
          wlatency / double (handle->mix_freq) * 1000,
          jack->buffer_frames / double (handle->mix_freq) * 1000,
          total_latency / double (handle->mix_freq) * 1000);
  return total_latency;
}

static gsize
jack_device_read (BsePcmHandle *handle,
                  gfloat       *values)
{
  JackPcmHandle *jack = (JackPcmHandle*) handle;
  g_return_val_if_fail (jack->jack_client != NULL, 0);
  g_return_val_if_fail (jack->atomic_callback_state == CALLBACK_STATE_ACTIVE, 0);

  /* get rid of this *slow* interleaving code */
  float deinterleaved_frame_data[handle->block_length * handle->n_channels];
  float *deinterleaved_frames[handle->n_channels];
  for (guint ch = 0; ch < handle->n_channels; ch++)
    deinterleaved_frames[ch] = &deinterleaved_frame_data[ch * handle->block_length];

  guint frames_left = handle->block_length;
  while (frames_left)
    {
      guint frames_read = jack->input_ringbuffer.read (frames_left, deinterleaved_frames);

      frames_left -= frames_read;

      /* get rid of this *slow* interleaving code */
      for (guint ch = 0; ch < handle->n_channels; ch++)
	{
	  const float *src = deinterleaved_frames[ch];
	  float *dest = &values[ch];

	  for (guint i = 0; i < frames_read; i++)
	    {
	      *dest = src[i];
	      dest += handle->n_channels;
	    }
	}

      values += frames_read * handle->n_channels;

      if (frames_left)
	{
	  std::unique_lock<std::mutex> cond_locker (jack->cond_mutex);

	  /* usually should not timeout, but be notified by the jack callback */
	  if (!jack->input_ringbuffer.get_readable_frames())
	    jack->cond.wait_for (cond_locker, std::chrono::milliseconds (100));

	  if (jack->is_down)
	    {
	      /*
	       * FIXME: we need a way to indicate an error here; beast should provide
	       * an adequate reaction in case the JACK server is down (it should stop
	       * playing the file, and show a dialog, if JACK can not be reconnected)
	       *
	       * if we have a way to abort processing, this if can be moved above
	       * the condition wait; however, right now moving it there means that
	       * beast will render the output as fast as possible when jack dies, and
	       * this will look like a machine lockup
	       */
	      Block::fill (frames_left * handle->n_channels, values, 0.0);   /* <- remove me once we can indicate errors */
	      return handle->block_length * handle->n_channels;
	    }
	}
    }
  return handle->block_length * handle->n_channels;
}

static void
jack_device_write (BsePcmHandle *handle,
                   const gfloat *values)
{
  JackPcmHandle *jack = (JackPcmHandle*) handle;
  assert_return (jack->jack_client != NULL);
  assert_return (jack->atomic_callback_state == CALLBACK_STATE_ACTIVE);

  /* FIXME: *slow* deinterleaving step - get rid of that */
  float deinterleaved_frame_data[handle->block_length * handle->n_channels];
  const float *deinterleaved_frames[handle->n_channels];
  for (guint ch = 0; ch < handle->n_channels; ch++)
    {
      float *channel_data = &deinterleaved_frame_data[ch * handle->block_length];
      for (guint i = 0; i < handle->block_length; i++)
	channel_data[i] = values[ch + i * handle->n_channels];
      deinterleaved_frames[ch] = channel_data;
    }

  guint frames_left = handle->block_length;

  while (frames_left)
    {
      guint frames_written = jack->output_ringbuffer.write (frames_left, deinterleaved_frames);

      frames_left -= frames_written;

      /* advance frame pointers */
      for (guint ch = 0; ch < handle->n_channels; ch++)
	deinterleaved_frames[ch] += frames_written;

      if (frames_left)
	{
	  std::unique_lock<std::mutex> cond_locker (jack->cond_mutex);

	  /* usually should not timeout, but be notified by the jack callback */
	  if (!jack->output_ringbuffer.get_writable_frames())
	    jack->cond.wait_for (cond_locker, std::chrono::milliseconds (100));

	  if (jack->is_down)
	    {
	      /*
	       * FIXME: we need a way to indicate an error here; beast should provide
	       * an adequate reaction in case the JACK server is down (it should stop
	       * playing the file, and show a dialog, if JACK can not be reconnected)
	       *
	       * if we have a way to abort processing, this if can be moved above
	       * the condition wait; however, right now moving it there means that
	       * beast will render the output as fast as possible when jack dies, and
	       * this will look like a machine lockup
	       */
	      return;
	    }
	}
    }
}

static void
bse_pcm_device_jack_class_init (BsePcmDeviceJACKClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  BseDeviceClass *device_class = BSE_DEVICE_CLASS (klass);
  
  parent_class = g_type_class_peek_parent (klass);
  
  gobject_class->finalize = bse_pcm_device_jack_finalize;
  
  device_class->list_devices = bse_pcm_device_jack_list_devices;
  const gchar *name = "jack";
  const gchar *syntax = _("device");
  const gchar *info = /* TRANSLATORS: keep this text to 70 chars in width */
                      _("JACK Audio Connection Kit PCM driver (http://jackaudio.org)\n");

  /* Being BSE_RATING_PREFFERED + 1, should do the right thing: if jackd is running,
   * use it, if jackd isn't running (we'll get an error upon initialization,
   * then), use the next best driver.
   *
   * Right now we will never start the jackd process ourselves.
   */
  bse_device_class_setup (klass, BSE_RATING_PREFERRED + 1, name, syntax, info);
  device_class->open = bse_pcm_device_jack_open;
  device_class->close = bse_pcm_device_jack_close;
}
