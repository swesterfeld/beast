// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl.html
#include "ladder.genidl.hh"

namespace Bse {

class Ladder : public LadderBase {
  class Module : public SynthesisModule {
    struct VCF
    {
      double x1, x2, x3, x4;
      double y1, y2, y3, y4;
      double a, b, c, d, e;
      enum class Mode { HP4, HP2, LP4, LP2 };
      VCF()
      {
        reset();
        set_mode (Mode::LP4);
      }
      void
      set_mode (Mode mode)
      {
        switch (mode) {
          case Mode::LP4: a = 0; b =  0; c =  0; d =  0; e = 1; break;
          case Mode::LP2: a = 0; b =  0; c =  1; d =  0; e = 0; break;
          case Mode::HP2: a = 1; b = -2; c =  1; d =  0; e = 0; break;
          case Mode::HP4: a = 1; b = -4; c =  6; d = -4; e = 1; break;
        }
      }
      void
      reset()
      {
        x1 = x2 = x3 = x4 = 0;
        y1 = y2 = y3 = y4 = 0;
      }
      double
      run (double x, double fc, double res)
      {
        constexpr double pre_scale = 0.1;
        constexpr double post_scale = 1 / pre_scale;
        x *= pre_scale;

        fc = M_PI * fc;
        const double g = 0.9892 * fc - 0.4342 * fc * fc + 0.1381 * fc * fc * fc - 0.0202 * fc * fc * fc * fc;

        res *= (1.0029 + 0.0526 * fc - 0.0926 * fc * fc + 0.0218 * fc * fc * fc);
        x = bse_approx4_tanh (x - y4 * res * 4);

        y1 = (x * 1 / 1.3 + x1 * 0.3/1.3 - y1) * g + y1;
        x1 = x;

        y2 = (y1 * 1 / 1.3 + x2 * 0.3/1.3 - y2) * g + y2;
        x2 = y1;

        y3 = (y2 * 1 / 1.3 + x3 * 0.3/1.3 - y3) * g + y3;
        x3 = y2;

        y4 = (y3 * 1 / 1.3 + x4 * 0.3/1.3 - y4) * g + y4;
        x4 = y3;
        return (x * a + y1 * b + y2 * c + y3 * d + y4 * e) * post_scale;
      }
      Bse::Resampler::Resampler2 *res_up = nullptr;
      Bse::Resampler::Resampler2 *res_down = nullptr;
      double
      run2 (double x, double fc, double res)
      {
        if (1)
          {
            if (!res_up)
              res_up = Bse::Resampler::Resampler2::create (BSE_RESAMPLER2_MODE_UPSAMPLE, BSE_RESAMPLER2_PREC_120DB);
            if (!res_down)
              res_down = Bse::Resampler::Resampler2::create (BSE_RESAMPLER2_MODE_DOWNSAMPLE, BSE_RESAMPLER2_PREC_120DB);

            float block[1];
            float up_block[2];
            block[0] = x;
            res_up->process_block (block, 1, up_block);
            up_block[0] = run (up_block[0], fc * 0.5, res);
            up_block[1] = run (up_block[1], fc * 0.5, res);
            res_down->process_block (up_block, 2, block);
            return block[0];
          }
        else if (0)
          {
            run (x, fc * 0.5, res);
            return run (x, fc * 0.5, res);
          }
        else if (0)
          {
            run (x, fc * 0.5, res);
            return run (0, fc * 0.5, res);
          }
        else
          return run (x, fc, res);
      }
    };

    VCF vcf1, vcf2;
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
          output1[i] = vcf1.run2 (input1[i], cutoff, resonance);
          output2[i] = vcf2.run2 (input2[i], cutoff, resonance);
        }
    }
  };
  BSE_EFFECT_INTEGRATE_MODULE (Ladder, Module, LadderProperties);
};

BSE_CXX_DEFINE_EXPORTS();
BSE_CXX_REGISTER_EFFECT (Ladder);

}
