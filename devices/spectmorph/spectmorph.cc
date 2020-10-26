// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl.html
#include "bse/processor.hh"
#include "bse/signalmath.hh"
#include "bse/internal.hh"

#include <spectmorph.hh>
#include <spectmorphglui.hh>

namespace Bse {

using namespace AudioSignal;

class PluginInitializer
{
public:
  PluginInitializer() { SpectMorph::sm_plugin_init(); }
  ~PluginInitializer() { SpectMorph::sm_plugin_cleanup(); }
};

// == SpectMorph ==
class SpectMorphDevice : public AudioSignal::Processor {
  PluginInitializer plugin_initializer_;
  OBusId stereo_out_;

  SpectMorph::Project project_;
  SpectMorph::EventLoop event_loop_;
  SpectMorph::MorphPlanWindow *window_ = nullptr;
  std::thread ui_thread_;
  std::atomic<bool> ui_quit_;
  std::atomic<bool> show_ui_;

  static constexpr const int PID_EDIT = 1;

  void
  edit_open() // TODO: should override function from base class
  {
    window_ = new SpectMorph::MorphPlanWindow (event_loop_, "SpectMorph BEAST", /* win_id */ 0, false, project_.morph_plan());
    window_->set_close_callback ([&]() { set_param (PID_EDIT, 0); });
    window_->show();
  }

  void
  edit_idle() // TODO: should override function from base class
  {
    event_loop_.process_events();
  }

  void
  edit_close() // TODO: should override function from base class
  {
    delete window_;
    window_ = nullptr;
  }

  void // TODO: should be implemented elsewhere
  update_ui()
  {
    if (show_ui_ && !window_)
      edit_open();

    if (!show_ui_ && window_)
      edit_close();

    if (window_)
      edit_idle();
  }
  void
  initialize () override
  {
    ui_quit_ = false;
    ui_thread_ = std::thread ([this]()
      {
        while (!ui_quit_) // TODO: cleanup thread somewhere: { ui_quit_ = true; ui_thread_.join(); }
          {
            usleep (1000 * 1000 / 60); // 60 fps
            update_ui();
          }
      });

    project_.set_mix_freq (sample_rate());

    start_param_group ("User Interface");
    add_param (PID_EDIT, "Show Editor",  "Edit", false);
  }
  void
  query_info (ProcessorInfo &info) const override
  {
    info.uri = "Bse.SpectMorph";
    // info.version = "0";
    info.label = "SpectMorph";
    info.category = "Synth";
  }
  void
  configure (uint n_ibusses, const SpeakerArrangement *ibusses, uint n_obusses, const SpeakerArrangement *obusses) override
  {
    remove_all_buses();
    prepare_event_input();
    stereo_out_ = add_output_bus ("Stereo Out", SpeakerArrangement::STEREO);
    assert_return (bus_info (stereo_out_).ident == "stereo-out");
  }
  void
  reset () override
  {
    // synth_.system_reset(); FIXME

    adjust_params (true);
  }
  void
  adjust_param (Id32 tag) override
  {
    switch (tag)
      {
        case PID_EDIT: show_ui_ = get_param (tag);
                       break;
      }
  }
  void
  render (uint n_frames) override
  {
    SpectMorph::MidiSynth *midi_synth = project_.midi_synth();
    project_.try_update_synth();

    adjust_params (false);

    EventRange erange = get_event_input();
    for (const auto &ev : erange)
      {
        unsigned char midi_bytes[3] = {0, };
        const int time_stamp = std::max<int> (ev.frame, 0);
        switch (ev.message())
          {
          case Message::NOTE_OFF:
            midi_bytes[0] = 0x80 | ev.channel;
            midi_bytes[1] = ev.key;
            midi_synth->add_midi_event (time_stamp, midi_bytes);
            break;
          case Message::NOTE_ON:
            midi_bytes[0] = 0x90 | ev.channel;
            midi_bytes[1] = ev.key;
            midi_bytes[2] = std::clamp (bse_ftoi (ev.velocity * 127), 0, 127);
            midi_synth->add_midi_event (time_stamp, midi_bytes);
            break;
          // TODO: CC events
          case Message::ALL_NOTES_OFF:
          case Message::ALL_SOUND_OFF:
            // TODO
            break;
          default: ;
          }
      }

    float *output[2] = {
      oblock (stereo_out_, 0),
      oblock (stereo_out_, 1)
    };
    // SpectMorph is currently mono, but will in the future support stereo,
    // so we register as stereo plugin now, and fake stereo until implemented
    midi_synth->process (output[0], n_frames);
    std::copy (output[0], output[0] + n_frames, output[1]);
  }
};

static auto spectmorph = Bse::enroll_asp<SpectMorphDevice>();

} // Bse
