#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <bse/bsemathsignal.hh>
#include <bse/gslfft.hh>
#include <complex>
#include <algorithm>

#include "bleputils.hh"

using std::complex;
using std::vector;
using std::string;
using std::max;

static double
sinc (double x)
{
  return fabs (x) > 1e-8 ? sin (x * M_PI) / (x * M_PI) : 1;
}

static double *
complex_ptr (vector<complex<double>>& vec)
{
  return reinterpret_cast<double *> (&vec[0]);
}

static void
print_fir_response (const vector<double>& fir,
                    const string& label)
{
  size_t N = 1024;
  while (fir.size() * 8 >= N)
    N *= 2;

  vector<complex<double>> in_ri (N), out_ri (N);

  for (size_t i = 0; i < N; i++)
    if (i < fir.size())
      in_ri[i] = fir[i];

  gsl_power2_fftac (N, complex_ptr (in_ri), complex_ptr (out_ri));
  for (size_t i = 0; i <= N / 2; i++)
    {
      double d = i / double (N) * 2;
      printf ("%.17g %.17g #%s\n", d, std::abs (out_ri[i]), label.c_str());
    }
}

static vector<double>
mk_fir (int width, int oversample, double low_pass, double beta)
{
  const int N = width * oversample;
  vector<double> fir_filter (2 * N - 1);

  const double stretch = oversample / low_pass * 24000.; /* relative to 48kHz */
  for (size_t i = 0; i < fir_filter.size(); i++)
    {
      double x = i - (N - 1.0);
      fir_filter[i] = sinc (x / stretch) * window_kaiser (x / (N - 1.0), beta) / stretch;
    }
  return fir_filter;
}

static vector<double>
load_fir (const char *fname)
{
  vector<double> fir_filter;

  FILE *f = fopen (fname, "r");
  char str[1024];
  while (fgets (str, 1024, f))
    {
      char *p = str;
      while (*p == ' ' || *p == '\t') // skip leading whitespace
        p++;

      if (*p != '#' && *p != '\n') // could be a number?
        fir_filter.push_back (atof (p));
    }
  fclose (f);

  return fir_filter;
}

static int
next_power_of_two (int N)
{
  int P = 2;
  while (P < N)
    P *= 2;
  return P;
}

static vector<double>
repeat_n (size_t n, const vector<double>& fir)
{
  vector<double> rep_fir;

  for (auto d : fir)
    for (size_t i = 0; i < n; i++)
      rep_fir.push_back (d / n);

  return rep_fir;
}

static vector<double>
interp_linear (size_t n, const vector<double>& fir)
{
  vector<double> interp_fir;

  for (size_t fpos = 0; fpos < fir.size(); fpos++)
    {
      double left = fir[fpos];
      double right = fpos + 1 < fir.size() ? fir[fpos + 1] : 0;
      for (size_t i = 0; i < n; i++)
        {
          double frac = double (i) / n;
          interp_fir.push_back ((left * (1 - frac) + right * frac) / n);
        }
    }
  return interp_fir;
}

int
main (int argc, char **argv)
{
  const bool trace = (argc == 2 && strcmp (argv[1], "trace") == 0);

  vector<double> fir_filter;

  if (1)
    {
      /* design low pass FIR filter */
      fir_filter = mk_fir (/* width */ 16, /* oversample */ 64, /* lp */ 20000., /* beta */ 5);
    }
  else
    {
      /* load low pass FIR filter */
      fir_filter = load_fir ("rdemo2fir");
    }
  assert ((fir_filter.size() & 1) == 1); // odd length
  const int N = (fir_filter.size() + 1) / 2;

  if (trace)
    {
      for (size_t i = 0; i < fir_filter.size(); i++)
        printf ("%.17g #Fin\n", fir_filter[i]);

      print_fir_response (fir_filter, "Hin");
    }

  const int M = next_power_of_two (N * 128);
  vector<complex<double>> theta_ri (M);
  vector<complex<double>> mag_ri (M);
  vector<complex<double>> fir_ri (M);

  /* build zero padded impulse response */

  for (size_t i = 0; i < fir_filter.size(); i++)
    fir_ri[i] = fir_filter[i];

  gsl_power2_fftac (M, complex_ptr (fir_ri), complex_ptr (mag_ri));   /* get spectrum */
  for (int i = 0; i < M; i++)
    {
      mag_ri[i] = std::complex<double> (mag_ri[i].real(), -mag_ri[i].imag()); // FIXME: should be done by fft wrapper

      auto a = mag_ri[i] * std::complex<double> (cos (double (i) * (N - 1) * 2 * PI / M), -sin (double (i) * (N - 1) * 2 * PI / M));
      mag_ri[i] = a.real();
    }

  double offset = 0.0;
  for (int i = 0; i < M; i++)
    offset = max (-mag_ri[i].real(), offset);

  /* sqrt for magnitudes */
  for (int i = 0; i < M; i++)
    mag_ri[i] = sqrt (mag_ri[i].real() + offset) + 1e-10;

  /* compute log|X(n)| */
  for (int i = 0; i < M; i++)
    {
      theta_ri[i] = log (mag_ri[i].real());
    }

  vector<complex<double>> theta_ri_ifft (M);
  gsl_power2_fftsc (M, complex_ptr (theta_ri), complex_ptr (theta_ri_ifft));    /* IFFT */

  /* pointwise multiplication with sig vector */
  for (int i = 0; i < M; i++)
    {
      double sign;
      if (i % (M/2) == 0)
        {
          sign = 0;
        }
      else if (i < M/2)
        {
          sign = 1;
        }
      else
        {
          sign = -1;
        }
      sign /= M; // FIXME: should be done by fft wrapper
      theta_ri_ifft[i] *= sign;
    }
  gsl_power2_fftac (M, complex_ptr (theta_ri_ifft), complex_ptr (theta_ri));   /* FFT */

  for (int i = 0; i < M; i++)
    {
      /* multiplication with -j */
      theta_ri[i] *= complex<double> (0, -1);
    }

  // so far, we have computed
  // theta[i] = -j * DFT (sign[i] * IDFT (a[i]))

  vector<complex<double>> minphase_spect (M);
  for (int i = 0; i < M; i++)
    {
      /* |X[i]| * exp(j*t(i)) */
      complex<double> j (0, 1);
      minphase_spect[i] = mag_ri[i].real() * exp (j * theta_ri[i]);
    }

  vector<complex<double>> minphase_fir (M);
  vector<double> minphase (N);
  gsl_power2_fftsc (M, complex_ptr (minphase_spect), complex_ptr (minphase_fir));   /* IFFT */
  for (size_t i = 0; i < M; i++)
    {
      if (trace)
        printf ("# minphase_fir[%zd] = %f %f\n", i, minphase_fir[i].real(), minphase_fir[i].imag());

      if (i < minphase.size())
        {
          minphase[i] = minphase_fir[i].real() / M;
          if (trace)
            printf ("%f #Fout\n", minphase[i]);
        }
    }

  if (trace)
    {
      /* minimum phase filter magnitude response */
      print_fir_response (minphase, "Hout");

      print_fir_response (repeat_n (16, minphase), "Hrep");
      print_fir_response (interp_linear (16, minphase), "Hlin");
    }
  else
    {
      printf ("#include \"bleposc.hh\"\n");
      printf ("const float Osc::blep_delta[%zd] = {\n", minphase.size() + 1);
    }

  double blep_acc = 0;
  for (size_t i = 0; i < minphase.size(); i++)
    {
      blep_acc += minphase[i];
      if (trace)
        printf ("%.17g #blep\n", blep_acc);
      else
        printf ("  %.17g,\n", 1 - blep_acc);
    }

  if (!trace)
    {
      printf ("  0\n");
      printf ("};\n");
    }
}
