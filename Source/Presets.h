#pragma once

#include "DSP/EqTables.h"

namespace bax
{
/** Detent index -> host value. */
constexpr float detent (int d) { return (float) d / (float) (kNumDetents - 1); }

struct PresetChannel
{
    float hp = 0.0f, lsFreq = 0.5f, lsGain = 0.0f, hsGain = 0.0f, hsFreq = 0.5f, lp = 0.0f;   // freqs: host value, gains: dB
};

struct FactoryPreset
{
    const char* name;
    PresetChannel a, b;
    bool midSide = false;
    bool link = false;
    int oversampling = 0;   // 0 = 1X, 1 = 2X, 2 = 4X
    float outputDb = 0.0f;
};

// Factory presets. Detent indices (host value = index / 7), see DSP/EqTables.h:
//   index:  0     1     2     3     4      5      6     7
//   CUT     Out   12    18    24    30     36     43    54 Hz
//   SHELF   74    84    98    116   131    166    230   361 Hz
//   SHELF   1.6k  1.8k  2.1k  2.5k  3.4k   4.8k   7.1k  18k Hz
//   CUT     Out   70k   28k   18k   12.6k  11.1k  9k    7.5k Hz
// Each channel: { HP, low shelf freq, low level dB, high level dB, high shelf freq, LP }
inline const FactoryPreset factoryPresets[] =
{
    { "Init", {}, {} },
    { "Acoustic Guitar Control",
      { detent (7), detent (6), -2.5f, 1.5f, detent (5), detent (3) },
      { detent (7), detent (6), -2.5f, 1.5f, detent (5), detent (3) } },
    { "Bass Bump 'n Tuck",
      { detent (4), detent (1), 3.5f, -1.5f, detent (3), detent (6) },
      { detent (4), detent (1), 3.5f, -1.5f, detent (3), detent (6) } },
    { "Drum Driver",
      { detent (2), detent (3), 3.0f, 3.0f, detent (4), detent (1) },
      { detent (2), detent (3), 3.0f, 3.0f, detent (4), detent (1) }, false, false, 1, -1.0f },
    { "Drum Hype",
      { detent (3), detent (2), 4.5f, 4.0f, detent (6), detent (1) },
      { detent (3), detent (2), 4.5f, 4.0f, detent (6), detent (1) }, false, false, 1, -2.0f },
    { "Drum MS",
      { detent (2), detent (2), 3.0f, 1.0f, detent (4), detent (1) },     // mid: punch
      { detent (6), detent (5), -2.0f, 3.5f, detent (7), detent (1) },    // side: tighter lows, air
      true, false, 1, -1.0f },
    { "Home HiFi",
      { detent (1), detent (2), 2.5f, 2.0f, detent (6), 0.0f },
      { detent (1), detent (2), 2.5f, 2.0f, detent (6), 0.0f } },
    { "Mix Elixer",
      { detent (2), detent (1), 1.5f, 1.5f, detent (7), detent (1) },
      { detent (2), detent (1), 1.5f, 1.5f, detent (7), detent (1) }, false, false, 2, -0.5f },
    { "Pads Lift",
      { detent (6), detent (5), -1.5f, 3.0f, detent (4), 0.0f },
      { detent (6), detent (5), -1.5f, 3.0f, detent (4), 0.0f } },
    { "Track Everything",
      { detent (2), 0.5f, 0.0f, 0.0f, 0.5f, detent (1) },
      { detent (2), 0.5f, 0.0f, 0.0f, 0.5f, detent (1) } },
    { "Vocal Sheen",
      { detent (7), detent (5), -1.5f, 3.5f, detent (6), detent (2) },
      { detent (7), detent (5), -1.5f, 3.5f, detent (6), detent (2) } },
    { "Zeroed Out 4X", {}, {}, false, false, 2, 0.0f },
};

inline constexpr int numFactoryPresets = (int) (sizeof (factoryPresets) / sizeof (factoryPresets[0]));

} // namespace bax
