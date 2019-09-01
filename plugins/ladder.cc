// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl.html
#include "ladder.genidl.hh"
#include "laddervcf.hh"

namespace Bse {

class Ladder : public LadderBase {
  class Module : public SynthesisModule {
    LadderVCFLinear vcf_l;
    LadderVCFNonLinear vcf_nl;
    LadderVCFNonLinearCheap vcf_nlc;

    double cutoff    = 0;
    double resonance = 0;
    LadderImplType ladder_impl;
  public:
    void
    reset()
    {
      vcf_l.reset();
      vcf_nl.reset();
      vcf_nlc.reset();
    }
    void
    config (LadderProperties *params)
    {
      cutoff    = params->cutoff / (mix_freq() * 0.5);
      resonance = params->resonance / 100; /* percent */
      ladder_impl = params->impl;

      auto cfg = [&] (auto& vcf)
        {
          LadderVCFMode m = LadderVCFMode::LP4;
          switch (params->filter)
          {
            case LADDER_FILTER_LP4: m = LadderVCFMode::LP4; break;
            case LADDER_FILTER_LP3: m = LadderVCFMode::LP3; break;
            case LADDER_FILTER_LP2: m = LadderVCFMode::LP2; break;
            case LADDER_FILTER_LP1: m = LadderVCFMode::LP1; break;
          }
          vcf.set_mode (m);
          vcf.set_drive (params->drive_db);
          vcf.set_rate (mix_freq());
          vcf.set_freq_mod_octaves (params->freq_mod_octaves);
          vcf.set_key_tracking (params->key_tracking / 100);
        };
      cfg (vcf_l);
      cfg (vcf_nl);
      cfg (vcf_nlc);
    }
    const float *
    istream_ptr (uint index)
    {
      const IStream& is = istream (index);
      return is.connected ? is.values : nullptr;
    }
    void
    process (unsigned int n_values)
    {
      const float *inputs[2]  = { istream (ICHANNEL_AUDIO_IN1).values,  istream (ICHANNEL_AUDIO_IN2).values };
      float       *outputs[2] = { ostream (OCHANNEL_AUDIO_OUT1).values, ostream (OCHANNEL_AUDIO_OUT2).values };

      auto run = [&] (auto& vcf)
        {
          vcf.run_block (n_values, cutoff, resonance, inputs, outputs,
                         istream_ptr (ICHANNEL_FREQ_IN),
                         istream_ptr (ICHANNEL_FREQ_MOD_IN),
                         istream_ptr (ICHANNEL_KEY_FREQ_IN));
        };
      switch (ladder_impl)
      {
        case LADDER_IMPL_LINEAR:
          run (vcf_l);
          break;
        case LADDER_IMPL_NON_LINEAR:
          run (vcf_nl);
          break;
        case LADDER_IMPL_NON_LINEAR_CHEAP:
          run (vcf_nlc);
          break;
      }
    }
  };
  BSE_EFFECT_INTEGRATE_MODULE (Ladder, Module, LadderProperties);
};

BSE_CXX_DEFINE_EXPORTS();
BSE_CXX_REGISTER_ALL_TYPES_FROM_LADDER_IDL();

}
