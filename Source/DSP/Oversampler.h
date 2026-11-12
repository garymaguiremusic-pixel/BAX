#pragma once

// Linear-phase half-band oversampling (Kaiser-windowed, polyphase) for the preamp
// stage. Every path is latency-compensated to the same integer latency, so the
// oversampling factor can be switched without changing the plugin's latency.
//
//   2X : one 79-tap stage                 -> 39 samples
//   4X : 79-tap stage + 33-tap stage      -> 47 samples
//   1X : plain delay                      -> 47 samples

#include <array>
#include <cmath>
#include <vector>
#include "FilterDesign.h"

namespace bax
{
//==============================================================================
inline double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1.0e-17 * sum)
            break;
    }
    return sum;
}

/** Kaiser-windowed half-band low-pass, unity DC gain. */
inline std::vector<double> designHalfBand (int numTaps, double beta)
{
    std::vector<double> h ((size_t) numTaps);
    const int c = (numTaps - 1) / 2;
    const double i0b = besselI0 (beta);
    double sum = 0.0;
    for (int n = 0; n < numTaps; ++n)
    {
        const int k = n - c;
        const double sinc = k == 0 ? 0.5 : std::sin (design::kPi * k / 2.0) / (design::kPi * k);
        const double r = (double) k / (double) c;
        const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
        h[(size_t) n] = sinc * w;
        sum += h[(size_t) n];
    }
    for (auto& v : h) v /= sum;
    return h;
}

//==============================================================================
/** Fixed-length history ring that keeps a linear copy so dot products are contiguous. */
template <int N>
struct History
{
    std::array<double, (size_t) (2 * N)> buf {};
    int pos = 0;

    void reset() noexcept { buf.fill (0.0); pos = 0; }
    inline void push (double x) noexcept
    {
        pos = (pos == 0 ? N : pos) - 1;
        buf[(size_t) pos] = x;
        buf[(size_t) pos + N] = x;
    }
    /** x[n - k] for k = 0 .. N-1 */
    inline const double* data() const noexcept { return buf.data() + pos; }
};

/** One 2x half-band stage (upsampler + matching downsampler) for one channel. */
template <int Taps>
class HalfBandStage
{
public:
    static constexpr int kCentre = (Taps - 1) / 2;
    static constexpr int kLatencyLow = kCentre;   // up + down, in low-rate samples

    explicit HalfBandStage (double beta = 8.0)
    {
        const auto h = designHalfBand (Taps, beta);
        for (int i = 0; i < Taps; ++i) taps[(size_t) i] = h[(size_t) i];
        // polyphase upsampler: phase p uses taps p, p+2, ... (gain 2)
        for (int i = 0; i < Taps; ++i)
        {
            if (i % 2 == 0) up0[(size_t) (i / 2)] = 2.0 * taps[(size_t) i];
            else            up1[(size_t) (i / 2)] = 2.0 * taps[(size_t) i];
        }
    }

    void reset() noexcept { upHist.reset(); downHist.reset(); }

    inline void upsample (double x, double& y0, double& y1) noexcept
    {
        upHist.push (x);
        const double* d = upHist.data();
        double a = 0.0, b = 0.0;
        for (int k = 0; k < kUp0; ++k) a += up0[(size_t) k] * d[k];
        for (int k = 0; k < kUp1; ++k) b += up1[(size_t) k] * d[k];
        y0 = a;
        y1 = b;
    }

    inline double downsample (double x0, double x1) noexcept
    {
        downHist.push (x0);
        const double* d = downHist.data();
        double y = 0.0;
        for (int k = 0; k < Taps; ++k) y += taps[(size_t) k] * d[k];
        downHist.push (x1);
        return y;
    }

private:
    static constexpr int kUp0 = (Taps + 1) / 2, kUp1 = Taps / 2;
    std::array<double, (size_t) Taps> taps {};
    std::array<double, (size_t) kUp0> up0 {};
    std::array<double, (size_t) kUp1> up1 {};
    History<kUp0> upHist;
    History<Taps> downHist;
};

//==============================================================================
/** Simple integer delay. */
template <int MaxDelay>
struct DelayLine
{
    std::array<double, (size_t) (MaxDelay + 1)> buf {};
    int pos = 0, delay = 0;

    void reset() noexcept { buf.fill (0.0); pos = 0; }
    inline double process (double x) noexcept
    {
        if (delay == 0) return x;
        buf[(size_t) pos] = x;
        int r = pos - delay;
        if (r < 0) r += MaxDelay + 1;
        pos = (pos + 1 == MaxDelay + 1) ? 0 : pos + 1;
        return buf[(size_t) r];
    }
};

//==============================================================================
/** The "HyperPre" style preamp: a gentle, slightly asymmetric soft saturation with
    unity small-signal gain. At -18 dBFS: H2 ~ -63 dBc, H3 ~ -72 dBc. */
struct Preamp
{
    static constexpr double kDrive = 0.6, kBias = 0.03;

    static inline double process (double x) noexcept
    {
        static const double t0 = std::tanh (kDrive * kBias);
        static const double g0 = kDrive * (1.0 - t0 * t0);
        return (std::tanh (kDrive * (x + kBias)) - t0) / g0;
    }
};

/** Preamp at 1x, 2x or 4x, compensated to kLatency. One channel. */
class PreampPath
{
public:
    using Stage1 = HalfBandStage<79>;
    using Stage2 = HalfBandStage<33>;
    static constexpr int kLatency2x = Stage1::kLatencyLow;                                // 39
    static constexpr int kLatency4x = Stage1::kLatencyLow + Stage2::kLatencyLow / 2;      // 47
    static constexpr int kLatency = kLatency4x;

    void setFactor (int f) noexcept
    {
        factor = f;
        comp.delay = factor == 1 ? kLatency : (factor == 2 ? kLatency - kLatency2x : 0);
    }
    int getFactor() const noexcept { return factor; }

    void reset() noexcept { s1.reset(); s2.reset(); comp.reset(); }

    inline double process (double x) noexcept
    {
        double y;
        if (factor == 1)
        {
            y = Preamp::process (x);
        }
        else if (factor == 2)
        {
            double a, b;
            s1.upsample (x, a, b);
            y = s1.downsample (Preamp::process (a), Preamp::process (b));
        }
        else
        {
            double a, b, a0, a1, b0, b1;
            s1.upsample (x, a, b);
            s2.upsample (a, a0, a1);
            s2.upsample (b, b0, b1);
            const double da = s2.downsample (Preamp::process (a0), Preamp::process (a1));
            const double db = s2.downsample (Preamp::process (b0), Preamp::process (b1));
            y = s1.downsample (da, db);
        }
        return comp.process (y);
    }

private:
    int factor = 1;
    Stage1 s1;
    Stage2 s2;
    DelayLine<kLatency> comp;
};

static_assert (PreampPath::kLatency2x == 39 && PreampPath::kLatency4x == 47, "latency layout changed");

} // namespace bax
