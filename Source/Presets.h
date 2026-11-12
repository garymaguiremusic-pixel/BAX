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
    bool link = true;
    int oversampling = 0;   // 0 = 1X, 1 = 2X, 2 = 4X
    float outputDb = 0.0f;
};

// Factory presets. Frequencies use the detent tables in DSP/EqTables.h:
//   HP  Off 12 18 24 36 48 72 120 Hz        LS  40 75 105 150 230 300 450 600 Hz
//   HS  1.5 2.4 3.5 5 7 10 14 20 kHz        LP  Off 70 50 35 24 18 12 6.5 kHz
inline const FactoryPreset factoryPresets[] =
{
    { "Init", {}, {} },
    { "Acoustic Guitar Control",
      { detent (6), detent (4), -2.5f, 1.5f, detent (5), detent (2) },
      { detent (6), detent (4), -2.5f, 1.5f, detent (5), detent (2) } },
    { "Bass Bump 'n Tuck",
      { detent (3), detent (1), 3.5f, -1.5f, detent (2), detent (5) },
      { detent (3), detent (1), 3.5f, -1.5f, detent (2), detent (5) } },
    { "Drum Driver",
      { detent (2), detent (3), 3.0f, 3.0f, detent (4), detent (1) },
      { detent (2), detent (3), 3.0f, 3.0f, detent (4), detent (1) }, false, true, 1, -1.0f },
    { "Drum Hype",
      { detent (3), detent (2), 4.5f, 4.0f, detent (5), detent (1) },
      { detent (3), detent (2), 4.5f, 4.0f, detent (5), detent (1) }, false, true, 1, -2.0f },
    { "Drum MS",
      { detent (2), detent (2), 3.0f, 1.0f, detent (4), detent (1) },     // mid: punch
      { detent (5), detent (5), -2.0f, 3.5f, detent (6), detent (1) },    // side: tighter low end, air
      true, false, 1, -1.0f },
    { "Home HiFi",
      { detent (1), detent (2), 2.5f, 2.0f, detent (6), 0.0f },
      { detent (1), detent (2), 2.5f, 2.0f, detent (6), 0.0f } },
    { "Mix Elixer",
      { detent (2), detent (1), 1.5f, 1.5f, detent (7), detent (1) },
      { detent (2), detent (1), 1.5f, 1.5f, detent (7), detent (1) }, false, true, 2, -0.5f },
    { "Pads Lift",
      { detent (5), detent (5), -1.5f, 3.0f, detent (3), 0.0f },
      { detent (5), detent (5), -1.5f, 3.0f, detent (3), 0.0f } },
    { "Track Everything",
      { detent (2), 0.5f, 0.0f, 0.0f, 0.5f, detent (1) },
      { detent (2), 0.5f, 0.0f, 0.0f, 0.5f, detent (1) } },
    { "Vocal Sheen",
      { detent (6), detent (5), -1.5f, 3.5f, detent (6), detent (2) },
      { detent (6), detent (5), -1.5f, 3.5f, detent (6), detent (2) } },
    { "Zeroed Out 4X", {}, {}, false, true, 2, 0.0f },
};

inline constexpr int numFactoryPresets = (int) (sizeof (factoryPresets) / sizeof (factoryPresets[0]));

} // namespace bax
