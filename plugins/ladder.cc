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

        /* FIXME: resamplers should have a state reset function, so delete/new can be avoided */
        res_up.reset (Bse::Resampler::Resampler2::create (BSE_RESAMPLER2_MODE_UPSAMPLE, BSE_RESAMPLER2_PREC_120DB));
        res_down.reset (Bse::Resampler::Resampler2::create (BSE_RESAMPLER2_MODE_DOWNSAMPLE, BSE_RESAMPLER2_PREC_120DB));
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
      std::unique_ptr<Bse::Resampler::Resampler2> res_up;
      std::unique_ptr<Bse::Resampler::Resampler2> res_down;
      void
      run_block (size_t       n_samples,
                 double       fc,
                 double       res,
                 const float *in_samples,
                 float       *out_samples)
      {
        float over_samples[2 * n_samples];

        res_up->process_block (in_samples, n_samples, over_samples);
        fc *= 0.5; // oversampling
        for (size_t i = 0; i < 2 * n_samples; i++)
          over_samples[i] = run (over_samples[i], fc, res);
        res_down->process_block (over_samples, 2 * n_samples, out_samples);
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

      VCF::Mode m = VCF::Mode::LP4;
      switch (params->filter)
      {
        case LADDER_FILTER_LP4: m = VCF::Mode::LP4; break;
        case LADDER_FILTER_LP2: m = VCF::Mode::LP2; break;
        case LADDER_FILTER_HP4: m = VCF::Mode::HP4; break;
        case LADDER_FILTER_HP2: m = VCF::Mode::HP2; break;
      }
      vcf1.set_mode (m);
      vcf2.set_mode (m);
    }
    void
    process (unsigned int n_values)
    {
      const float *input1 = istream (ICHANNEL_AUDIO_IN1).values;
      const float *input2 = istream (ICHANNEL_AUDIO_IN2).values;
      float *output1 = ostream (OCHANNEL_AUDIO_OUT1).values;
      float *output2 = ostream (OCHANNEL_AUDIO_OUT2).values;

      vcf1.run_block (n_values, cutoff, resonance, input1, output1);
      vcf2.run_block (n_values, cutoff, resonance, input2, output2);
    }
  };
  BSE_EFFECT_INTEGRATE_MODULE (Ladder, Module, LadderProperties);
};

BSE_CXX_DEFINE_EXPORTS();
BSE_CXX_REGISTER_ALL_TYPES_FROM_LADDER_IDL();

}
