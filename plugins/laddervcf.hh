// Licensed GNU LGPL v2.1 or later: http://www.gnu.org/licenses/lgpl.html
//
#ifndef __BSE_LADDER_VCF_HH__
#define __BSE_LADDER_VCF_HH__

namespace Bse {

enum class LadderVCFMode { LP1, LP2, LP3, LP4 };

template<bool OVERSAMPLE, bool NON_LINEAR>
class LadderVCF
{
  struct Channel {
    double x1, x2, x3, x4;
    double y1, y2, y3, y4;

    std::unique_ptr<Bse::Resampler::Resampler2> res_up;
    std::unique_ptr<Bse::Resampler::Resampler2> res_down;
  };
  std::array<Channel, 2> channels;
  LadderVCFMode mode;
  double pre_scale, post_scale;
  double rate;
  double freq_mod_octaves;

public:
  LadderVCF()
  {
    reset();
    set_mode (LadderVCFMode::LP4);
    set_drive (0);
    set_rate (48000);
    set_freq_mod_octaves (5);
  }
  void
  set_mode (LadderVCFMode new_mode)
  {
    mode = new_mode;
  }
  void
  set_drive (double drive_db)
  {
    const double drive_delta_db = 36;

    pre_scale = bse_db_to_factor (drive_db - drive_delta_db);
    post_scale = std::max (1 / pre_scale, 1.0);
  }
  void
  set_rate (double r)
  {
    rate = r;
  }
  void
  set_freq_mod_octaves (double octaves)
  {
    freq_mod_octaves = octaves;
  }
  void
  reset()
  {
    for (auto& c : channels)
      {
        c.x1 = c.x2 = c.x3 = c.x4 = 0;
        c.y1 = c.y2 = c.y3 = c.y4 = 0;

        c.res_up.reset (Bse::Resampler::Resampler2::create (BSE_RESAMPLER2_MODE_UPSAMPLE, BSE_RESAMPLER2_PREC_48DB));
        c.res_down.reset (Bse::Resampler::Resampler2::create (BSE_RESAMPLER2_MODE_DOWNSAMPLE, BSE_RESAMPLER2_PREC_48DB));
      }
  }
  double
  distort (double x)
  {
    if (NON_LINEAR)
      {
        /* shaped somewhat similar to tanh() and others, but faster */
        x = std::clamp (x, -1.0, 1.0);

        return x - x * x * x * (1.0 / 3);
      }
    else
      {
        return x;
      }
  }
private:
  template<LadderVCFMode MODE> inline void
  run (double *in, double *out, double fc, double res)
  {
    fc = M_PI * fc;
    const double g = 0.9892 * fc - 0.4342 * fc * fc + 0.1381 * fc * fc * fc - 0.0202 * fc * fc * fc * fc;
    const double gg = g * g;

    res *= (1.0029 + 0.0526 * fc - 0.0926 * fc * fc + 0.0218 * fc * fc * fc);

    for (size_t i = 0; i < channels.size(); i++)
      {
        Channel& c = channels[i];
        const double x = in[i] * pre_scale;
        const double g_comp = 0.5; // passband gain correction
        const double x0 = distort (x - (c.y4 - g_comp * x) * res * 4) * gg * gg * (1.0 / 1.3 / 1.3 / 1.3 / 1.3);

        c.y1 = x0 + c.x1 * 0.3 + c.y1 * (1 - g);
        c.x1 = x0;

        c.y2 = c.y1 + c.x2 * 0.3 + c.y2 * (1 - g);
        c.x2 = c.y1;

        c.y3 = c.y2 + c.x3 * 0.3 + c.y3 * (1 - g);
        c.x3 = c.y2;

        c.y4 = c.y3 + c.x4 * 0.3 + c.y4 * (1 - g);
        c.x4 = c.y3;

        switch (MODE)
        {
          case LadderVCFMode::LP1:
            out[i] = c.y1 / (gg * g * (1.0 / (1.3 * 1.3 * 1.3))) * post_scale;
            break;
          case LadderVCFMode::LP2:
            out[i] = c.y2 / (gg * (1.0 / (1.3 * 1.3))) * post_scale;
            break;
          case LadderVCFMode::LP3:
            out[i] = c.y3 / (g * (1.0 / 1.3)) * post_scale;
            break;
          case LadderVCFMode::LP4:
            out[i] = c.y4 * post_scale;
            break;
          default:
            BSE_ASSERT_RETURN_UNREACHED();
        }
      }
  }
  template<LadderVCFMode MODE> inline void
  do_run_block (uint          n_samples,
                double        fc,
                double        res,
                const float **inputs,
                float       **outputs,
                const float  *freq_in,
                const float  *freq_mod_in)
  {
    float over_samples[2][2 * n_samples];
    float freq_scale = OVERSAMPLE ? 0.5 : 1.0;
    float nyquist    = rate * 0.5;

    if (OVERSAMPLE)
      {
        for (size_t i = 0; i < channels.size(); i++)
          channels[i].res_up->process_block (inputs[i], n_samples, over_samples[i]);
      }

    fc *= freq_scale;

    uint over_pos = 0;
    for (uint i = 0; i < n_samples; i++)
      {
        // FIXME: handle freq_mod_in without freq_in
        // FIXME: key tracking
        if (freq_in)
          {
            fc = BSE_SIGNAL_TO_FREQ (freq_in[i]) * freq_scale / nyquist;

            if (freq_mod_in)
              fc *= bse_approx5_exp2 (freq_mod_in[i] * freq_mod_octaves);

            fc = CLAMP (fc, 0, 1);
          }
        if (OVERSAMPLE)
          {
            double in[2], out[2];

            in[0] = over_samples[0][over_pos];
            in[1] = over_samples[1][over_pos];
            run<MODE> (in, out, fc, res);
            over_samples[0][over_pos] = out[0];
            over_samples[1][over_pos] = out[1];
            over_pos++;

            in[0] = over_samples[0][over_pos];
            in[1] = over_samples[1][over_pos];
            run<MODE> (in, out, fc, res);
            over_samples[0][over_pos] = out[0];
            over_samples[1][over_pos] = out[1];
            over_pos++;
          }
        else
          {
            double in[2] = { inputs[0][i], inputs[1][i] };
            double out[2];
            run<MODE> (in, out, fc, res);
            outputs[0][i] = out[0];
            outputs[1][i] = out[1];
          }
      }
    if (OVERSAMPLE)
      {
        for (size_t i = 0; i < channels.size(); i++)
          channels[i].res_down->process_block (over_samples[i], 2 * n_samples, outputs[i]);
      }
  }
public:
  void
  run_block (uint           n_samples,
             double         fc,
             double         res,
             const float  **inputs,
             float        **outputs,
             const float   *freq_in,
             const float   *freq_mod_in)
  {
    switch (mode)
    {
      case LadderVCFMode::LP4: do_run_block<LadderVCFMode::LP4> (n_samples, fc, res, inputs, outputs, freq_in, freq_mod_in);
                               break;
      case LadderVCFMode::LP3: do_run_block<LadderVCFMode::LP3> (n_samples, fc, res, inputs, outputs, freq_in, freq_mod_in);
                               break;
      case LadderVCFMode::LP2: do_run_block<LadderVCFMode::LP2> (n_samples, fc, res, inputs, outputs, freq_in, freq_mod_in);
                               break;
      case LadderVCFMode::LP1: do_run_block<LadderVCFMode::LP1> (n_samples, fc, res, inputs, outputs, freq_in, freq_mod_in);
                               break;
    }
  }
};

// fast linear model of the filter
typedef LadderVCF<false, false> LadderVCFLinear;

// slow but accurate non-linear model of the filter (uses oversampling)
typedef LadderVCF<true,  true>  LadderVCFNonLinear;

// fast non-linear version (no oversampling), may have aliasing
typedef LadderVCF<false, true>  LadderVCFNonLinearCheap;

}

#endif
