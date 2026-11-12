#pragma once

// Filter design for the BAXR EQ. Pure C++ (no JUCE) so it can be unit-tested
// on its own and reused by the editor for the response display.
//
// Design rules
//  * Shelves are first-order "Baxandall" shelves: H(s) = (s + wc*sqrt(G)) / (s + wc/sqrt(G))
//    for the low shelf and its mirror image for the high shelf. The digital version
//    uses a matched pole (z = exp(sT)) and a numerator matching the analogue magnitude
//    at DC, the midpoint and Nyquist, so there is little cramping near Nyquist.
//  * High-pass: 2nd-order (12 dB/oct) bilinear, pre-warped at f0 (f0 <= 120 Hz, so
//    warping is negligible at every rate).
//  * Low-pass: 2nd-order (12 dB/oct) with matched poles and a magnitude-matched
//    numerator, plus a fitted correction biquad at low sample rates (see lowPassCorrected).

#include <cmath>
#include <algorithm>
#include <complex>

namespace bax::design
{
constexpr double kPi = 3.14159265358979323846;

/** Upper edge of the fitted band: 20 kHz at 44.1 / 48 kHz, 0.45 Fs above. */
inline double fitTop (double fs) { return fs <= 50000.0 ? 20000.0 : 0.45 * fs; }

struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    std::complex<double> response (double omega) const
    {
        const auto z1 = std::polar (1.0, -omega);
        const auto z2 = z1 * z1;
        return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
    }
};

inline double dbToGain (double db) { return std::pow (10.0, db / 20.0); }

//==============================================================================
inline double shelfAnalogueMag2 (bool high, double fc, double G, double w)
{
    const double wc = 2.0 * kPi * fc;
    return high ? G * G * (w * w + wc * wc / G) / (w * w + wc * wc * G)
                : (w * w + wc * wc * G) / (w * w + wc * wc / G);
}

/** Second-order numerator matched to an analogue magnitude-squared at DC, Nyquist and
    wm, over the given denominator (shared by the shelves and the low-pass). */
template <typename Mag2>
inline void matchNumerator (Biquad& b, double fs, double wm, Mag2&& analogueMag2)
{
    auto denMag2 = [&b] (double om)
    {
        const auto z1 = std::polar (1.0, -om);
        return std::norm (1.0 + b.a1 * z1 + b.a2 * z1 * z1);
    };
    const double B0 = analogueMag2 (0.0) * denMag2 (0.0);
    const double B1 = analogueMag2 (kPi * fs) * denMag2 (kPi);
    const double s2 = std::sin (0.5 * wm), c2 = std::cos (0.5 * wm);
    const double phi0 = c2 * c2, phi1 = s2 * s2, phi2 = std::sin (wm) * std::sin (wm);
    const double B2 = (analogueMag2 (wm * fs) * denMag2 (wm) - B0 * phi0 - B1 * phi1) / phi2;

    const double W = std::sqrt (std::max (0.0, B0)), V = std::sqrt (std::max (0.0, B1));
    b.b1 = 0.5 * (W - V);
    const double s = 0.5 * (W + V), p = -0.25 * B2;
    const double disc = std::max (0.0, s * s - 4.0 * p);
    b.b0 = 0.5 * (s + std::sqrt (disc));
    b.b2 = s - b.b0;
}

/** First-order analogue shelf -> digital: matched pole (z = exp(sT)) plus a 2nd-order
    numerator matching the analogue magnitude at DC, Nyquist and wm (rad/sample).
    fc = geometric midpoint of the transition. Continuous in gain (including 0 dB),
    so it can be redesigned every sample during a gain glide. */
inline Biquad shelf (bool high, double fc, double gainDb, double fs, double wm)
{
    const double G  = dbToGain (gainDb);
    const double sg = std::sqrt (G);
    const double wc = 2.0 * kPi * fc;

    Biquad b;
    b.a1 = -std::exp (-(high ? wc * sg : wc / sg) / fs);
    b.a2 = 0.0;
    matchNumerator (b, fs, wm, [=] (double w) { return shelfAnalogueMag2 (high, fc, G, w); });
    return b;
}

/** Picks the third match frequency of a shelf (per detent, done once per sample rate)
    to minimise the worst dB error against the analogue shelf over +-5 dB. Keeps the
    14 / 20 kHz shelves within ~0.2 dB at 44.1 kHz and ~0.05 dB at 88.2 kHz and up. */
inline double bestShelfMatchFrequency (bool high, double fc, double fs)
{
    const double fTop = fitTop (fs);
    double best = 1.0e9, bestWm = std::min (2.0 * kPi * fc / fs, 0.5 * kPi);
    for (int i = 1; i < 64; ++i)
    {
        const double wm = kPi * (double) i / 64.0;
        double worst = 0.0;
        for (double g : { -5.0, 5.0 })
        {
            const auto b = shelf (high, fc, g, fs, wm);
            for (double f = 20.0; f <= fTop * 1.0001 && worst < best; f *= 1.05)
            {
                const double ref = 10.0 * std::log10 (shelfAnalogueMag2 (high, fc, dbToGain (g), 2.0 * kPi * f));
                const double got = 20.0 * std::log10 (std::abs (b.response (2.0 * kPi * f / fs)));
                worst = std::max (worst, std::abs (got - ref) * (f <= 20000.0 ? 1.0 : 0.25));
            }
        }
        if (worst < best) { best = worst; bestWm = wm; }
    }
    return bestWm;
}

/** Analogue reference magnitude of the shelf (for tests). */
inline double shelfAnalogueDb (bool high, double fc, double gainDb, double f)
{
    const double G = dbToGain (gainDb), w = 2.0 * kPi * f, wc = 2.0 * kPi * fc;
    const double m2 = high ? G * G * (w * w + wc * wc / G) / (w * w + wc * wc * G)
                           : (w * w + wc * wc * G) / (w * w + wc * wc / G);
    return 10.0 * std::log10 (m2);
}

//==============================================================================
/** 2nd-order high-pass, bilinear with pre-warping (RBJ form). */
inline Biquad highPass (double f0, double q, double fs)
{
    const double w0 = 2.0 * kPi * f0 / fs;
    const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    Biquad b;
    b.b0 = (1.0 + cw) * 0.5 / a0;
    b.b1 = -(1.0 + cw) / a0;
    b.b2 = b.b0;
    b.a1 = -2.0 * cw / a0;
    b.a2 = (1.0 - alpha) / a0;
    return b;
}

inline double lowPassAnalogueMag2 (double f0, double q, double w)
{
    const double w0 = 2.0 * kPi * f0;
    const double re = w0 * w0 - w * w, im = w0 * w / q;
    return (w0 * w0 * w0 * w0) / (re * re + im * im);
}

inline double highPassAnalogueMag2 (double f0, double q, double w)
{
    const double w0 = 2.0 * kPi * f0;
    const double re = w0 * w0 - w * w, im = w0 * w / q;
    return (w * w * w * w) / (re * re + im * im);
}

/** 2nd-order low-pass with matched poles and a numerator that matches the analogue
    magnitude at DC, at Nyquist and at one more frequency wm. */
inline Biquad lowPassMatchedAt (double f0, double q, double fs, double wm)
{
    Biquad b;
    const double T = 1.0 / fs, w0 = 2.0 * kPi * f0;
    const double zeta = 1.0 / (2.0 * q);
    const double r = std::exp (-zeta * w0 * T);
    b.a1 = zeta < 1.0 ? -2.0 * r * std::cos (w0 * T * std::sqrt (1.0 - zeta * zeta))
                      : -2.0 * r * std::cosh (w0 * T * std::sqrt (zeta * zeta - 1.0));
    b.a2 = r * r;

    matchNumerator (b, fs, wm, [=] (double w) { return lowPassAnalogueMag2 (f0, q, w); });
    return b;
}

/** 2nd-order low-pass. The third match frequency is chosen by a small search that
    minimises the worst dB error against the analogue prototype from 20 Hz to
    fitTop(Fs), counting only where the response is above -30 dB. Runs
    only when a detent or the sample rate changes. */
inline Biquad lowPass (double f0, double q, double fs)
{
    const double fTop = fitTop (fs);
    double bestErr = 1.0e9;
    Biquad best = lowPassMatchedAt (f0, q, fs, std::min (2.0 * kPi * f0 / fs, 0.5 * kPi));

    for (int i = 1; i < 64; ++i)
    {
        const double wm = kPi * (double) i / 64.0;
        const auto b = lowPassMatchedAt (f0, q, fs, wm);
        double worst = 0.0;
        for (double f = 20.0; f <= fTop * 1.0001; f *= 1.06)
        {
            const double ref = 10.0 * std::log10 (lowPassAnalogueMag2 (f0, q, 2.0 * kPi * f));
            if (ref < -30.0) continue;
            const double got = 20.0 * std::log10 (std::max (1.0e-12, std::abs (b.response (2.0 * kPi * f / fs))));
            worst = std::max (worst, std::abs (got - ref));
        }
        if (worst < bestErr) { bestErr = worst; best = b; }
    }
    return best;
}

/** Low-pass as a matched biquad plus a fitted 2nd-order correction section. Above
    ~80 kHz sampling the single biquad is already accurate and the correction stays
    flat. At 44.1 / 48 kHz the correction brings the cascade to within ~0.1 dB of the
    analogue prototype down to -30 dB (one biquad alone is off by up to 0.7 dB near
    20 kHz). Deterministic coordinate-descent fit; ~10 ms, so call it off the audio thread. */
struct LowPassPair
{
    Biquad main, corr;
    std::complex<double> response (double omega) const { return main.response (omega) * corr.response (omega); }
};

inline double lowPassFitError (const LowPassPair& p, double f0, double q, double fs)
{
    // minimax dB error; above 20 kHz (only fitted at 88.2 kHz and up) it counts a tenth
    double worst = 0.0;
    const double fTop = fitTop (fs);
    for (double f = 20.0; f <= fTop * 1.0001; f *= 1.03)
    {
        const double ref = 10.0 * std::log10 (lowPassAnalogueMag2 (f0, q, 2.0 * kPi * f));
        if (ref < -30.0) continue;
        const double got = 20.0 * std::log10 (std::max (1.0e-12, std::abs (p.response (2.0 * kPi * f / fs))));
        worst = std::max (worst, std::abs (got - ref) * (f <= 20000.0 ? 1.0 : 0.1));
    }
    return worst;
}

inline LowPassPair lowPassCorrected (double f0, double q, double fs)
{
    LowPassPair p;
    p.main = lowPass (f0, q, fs);
    double best = lowPassFitError (p, f0, q, fs);
    if (best < 0.005)
        return p;

    double c[5] = { 1.0, 0.0, 0.0, 0.0, 0.0 }, step[5] = { 0.1, 0.1, 0.1, 0.1, 0.1 };
    auto apply = [&p] (const double* t) { p.corr.b0 = t[0]; p.corr.b1 = t[1]; p.corr.b2 = t[2]; p.corr.a1 = t[3]; p.corr.a2 = t[4]; };
    for (int it = 0; it < 4000 && step[0] > 1.0e-7; ++it)
    {
        bool improved = false;
        for (int k = 0; k < 5; ++k)
            for (int sg = -1; sg <= 1; sg += 2)
            {
                double t[5] = { c[0], c[1], c[2], c[3], c[4] };
                t[k] += sg * step[k];
                if (std::abs (t[4]) >= 0.98 || std::abs (t[3]) >= 1.0 + t[4] - 0.001)   // keep it stable
                    continue;
                apply (t);
                const double e = lowPassFitError (p, f0, q, fs);
                if (e < best) { best = e; std::copy (t, t + 5, c); improved = true; }
            }
        if (! improved)
            for (auto& s : step) s *= 0.6;
    }
    apply (c);
    return p;
}

} // namespace bax::design
