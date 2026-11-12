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

// Detent values in host-value order (clockwise), as printed on the reference panel.
// 0 = Out (off)
constexpr double kHpFreqs[kNumDetents] = { 0.0, 12.0, 18.0, 24.0, 30.0, 36.0, 43.0, 54.0 };
constexpr double kLsFreqs[kNumDetents] = { 74.0, 84.0, 98.0, 116.0, 131.0, 166.0, 230.0, 361.0 };
constexpr double kHsFreqs[kNumDetents] = { 18000.0, 7100.0, 4800.0, 3400.0, 2500.0, 2100.0, 1800.0, 1600.0 };
// 0 = Out (off), then the corner moves down
constexpr double kLpFreqs[kNumDetents] = { 0.0, 70000.0, 28000.0, 18000.0, 12600.0, 11100.0, 9000.0, 7500.0 };

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

/** Listening mode at the very end of the chain. Delta = output minus the (latency-aligned) input. */
enum class Monitor { LR = 0, Mid = 1, Side = 2, Delta = 3 };

struct EngineSettings
{
    ChannelSettings a, b;
    bool engagedA = true, engagedB = true;
    bool midSide = false;
    Oversampling oversampling = Oversampling::x1;
    double outputDb = 0.0;
    bool bypass = false;           // global true bypass (200 ms crossfade)
    Monitor monitor = Monitor::LR;
};

} // namespace bax
