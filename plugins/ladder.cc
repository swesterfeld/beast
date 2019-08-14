// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl.html
#include "ladder.genidl.hh"

namespace Bse {

class Ladder : public LadderBase {
  class Module : public SynthesisModule {
    struct VCF
    {
      double x1 = 0;
      double x2 = 0;
      double x3 = 0;
      double x4 = 0;
      double y1 = 0;
      double y2 = 0;
      double y3 = 0;
      double y4 = 0;
      double
      run (double x, double fc, double res)
      {
        fc = M_PI * fc;
        const double g = 0.9892 * fc - 0.4342 * fc * fc + 0.1381 * fc * fc * fc - 0.0202 * fc * fc * fc * fc;

        x -= y4 * res * 4;

        y1 = (x * 1 / 1.3 + x1 * 0.3/1.3 - y1) * g + y1;
        x1 = x;

        y2 = (y1 * 1 / 1.3 + x2 * 0.3/1.3 - y2) * g + y2;
        x2 = y1;

        y3 = (y2 * 1 / 1.3 + x3 * 0.3/1.3 - y3) * g + y3;
        x3 = y2;

        y4 = (y3 * 1 / 1.3 + x4 * 0.3/1.3 - y4) * g + y4;
        x4 = y3;
        return y4;
      }
    };

    VCF vcf1, vcf2;
    double cutoff    = 0;
    double resonance = 0;
  public:
    void
    reset()
    {
    }
    void
    config (LadderProperties *params)
    {
      cutoff    = params->cutoff / (mix_freq() * 0.5);
      resonance = params->resonance / 100; /* percent */
    }
    void
    process (unsigned int n_values)
    {
      const float *input1 = istream (ICHANNEL_AUDIO_IN1).values;
      const float *input2 = istream (ICHANNEL_AUDIO_IN2).values;
      float *output1 = ostream (OCHANNEL_AUDIO_OUT1).values;
      float *output2 = ostream (OCHANNEL_AUDIO_OUT2).values;

      for (unsigned int i = 0; i < n_values; i++)
        {
          output1[i] = vcf1.run (input1[i], cutoff, resonance);
          output2[i] = vcf2.run (input2[i], cutoff, resonance);
        }
    }
  };
  BSE_EFFECT_INTEGRATE_MODULE (Ladder, Module, LadderProperties);
};

BSE_CXX_DEFINE_EXPORTS();
BSE_CXX_REGISTER_EFFECT (Ladder);

}
