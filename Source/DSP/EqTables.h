#pragma once

// Control laws shared by the DSP, the parameters and the editor.
//
// Frequency controls have 8 detents (host value 0..1 in steps of 1/7). The tables
// below are the panel labels. They are the single place to edit once the original's
// display strings have been dumped (tools/measure/dump_params.py -> params.json).

#include <algorithm>
#include <cmath>

namespace bax
{
constexpr int kNumDetents = 8;

/** Exact comparison (used to detect "value changed"), without -Wfloat-equal noise. */
inline bool sameValue (double a, double b) noexcept { return ! (a < b) && ! (b < a); }

// 0 = Off
constexpr double kHpFreqs[kNumDetents] = { 0.0, 12.0, 18.0, 24.0, 36.0, 48.0, 72.0, 120.0 };
constexpr double kLsFreqs[kNumDetents] = { 40.0, 75.0, 105.0, 150.0, 230.0, 300.0, 450.0, 600.0 };
constexpr double kHsFreqs[kNumDetents] = { 1500.0, 2400.0, 3500.0, 5000.0, 7000.0, 10000.0, 14000.0, 20000.0 };
// 0 = Off, then the corner moves down
constexpr double kLpFreqs[kNumDetents] = { 0.0, 70000.0, 50000.0, 35000.0, 24000.0, 18000.0, 12000.0, 6500.0 };

constexpr double kGainRangeDb   = 5.0;    // shelves: -5 .. +5 dB
constexpr double kOutputRangeDb = 10.0;   // output:  -10 .. +10 dB
constexpr double kFilterQ       = 0.7071067811865476;

// The printed HS label sits about an octave below the shoulder of the shelf, so the
// transition midpoint is placed half an octave above the label. The LS label is
// the midpoint itself.
constexpr double kHsMidpointRatio = 1.4142135623730951;
constexpr double kLsMidpointRatio = 1.0;

/** Host value (0..1) -> detent index 0..7. 0.5 (the default) rounds up to index 4. */
inline int detentFromNormalised (double v)
{
    return std::clamp ((int) std::floor (v * (kNumDetents - 1) + 0.5), 0, kNumDetents - 1);
}

/** Settings of one EQ channel (A = left or mid, B = right or side). */
struct ChannelSettings
{
    int hp = 0, lsFreq = 4, hsFreq = 4, lp = 0;  // detent indices
    double lsGainDb = 0.0, hsGainDb = 0.0;

    bool sameDetents (const ChannelSettings& o) const
    {
        return hp == o.hp && lsFreq == o.lsFreq && hsFreq == o.hsFreq && lp == o.lp;
    }
};

enum class Oversampling { x1 = 0, x2 = 1, x4 = 2 };

struct EngineSettings
{
    ChannelSettings a, b;
    bool engagedA = true, engagedB = true;
    bool midSide = false;
    Oversampling oversampling = Oversampling::x1;
    double outputDb = 0.0;
};

} // namespace bax
