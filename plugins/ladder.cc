// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl.html
#include "ladder.genidl.hh"
#include "laddervcf.hh"

namespace Bse {

class Ladder : public LadderBase {
  class Module : public SynthesisModule {
    LadderVCF vcf1, vcf2;

    double cutoff    = 0;
    double resonance = 0;
  public:
    void
    reset()
    {
      vcf1.reset();
      vcf2.reset();
    }
    void
    config (LadderProperties *params)
    {
      cutoff    = params->cutoff / (mix_freq() * 0.5);
      resonance = params->resonance / 100; /* percent */

      LadderVCF::Mode m = LadderVCF::Mode::LP4;
      switch (params->filter)
      {
        case LADDER_FILTER_LP4: m = LadderVCF::Mode::LP4; break;
        case LADDER_FILTER_LP2: m = LadderVCF::Mode::LP2; break;
        case LADDER_FILTER_HP4: m = LadderVCF::Mode::HP4; break;
        case LADDER_FILTER_HP2: m = LadderVCF::Mode::HP2; break;
      }
      vcf1.set_mode (m);
      vcf2.set_mode (m);
      vcf1.set_drive (params->drive_db);
      vcf2.set_drive (params->drive_db);
      vcf1.set_rate (mix_freq());
      vcf2.set_rate (mix_freq());
      vcf1.set_freq_mod_octaves (params->freq_mod_octaves);
      vcf2.set_freq_mod_octaves (params->freq_mod_octaves);
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
      const float *input1 = istream (ICHANNEL_AUDIO_IN1).values;
      const float *input2 = istream (ICHANNEL_AUDIO_IN2).values;
      float *output1 = ostream (OCHANNEL_AUDIO_OUT1).values;
      float *output2 = ostream (OCHANNEL_AUDIO_OUT2).values;

      vcf1.run_block (n_values, cutoff, resonance, input1, output1, istream_ptr (ICHANNEL_FREQ_IN), istream_ptr (ICHANNEL_FREQ_MOD_IN));
      vcf2.run_block (n_values, cutoff, resonance, input2, output2, istream_ptr (ICHANNEL_FREQ_IN), istream_ptr (ICHANNEL_FREQ_MOD_IN));
    }
  };
  BSE_EFFECT_INTEGRATE_MODULE (Ladder, Module, LadderProperties);
};

BSE_CXX_DEFINE_EXPORTS();
BSE_CXX_REGISTER_ALL_TYPES_FROM_LADDER_IDL();

}
