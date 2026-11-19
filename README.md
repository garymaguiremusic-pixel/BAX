# BAXR

A stereo Baxandall-style mastering EQ plugin (VST3, Windows x64) built with JUCE 8. The GUI is
laid out as rack units in the style of a hardware Baxandall EQ, and uses the same black / orange
theme as LeadVerb: brushed black metal, chrome and black knobs, light-grey labels and orange as
the only accent. Everything is drawn in code (no image assets). The window is resizable, keeps its
aspect ratio and remembers its size. There is no licensing, copy protection or demo mode.

![Classic Exp view](docs/screenshot.png)

## Download

Every push to `main` builds the plugin on Windows, runs all the tests, and publishes
`BAXR-VST3-Windows-x64.zip` to the [`builds` branch](../../tree/builds). Unzip it and copy
`BAXR.vst3` to `C:\Program Files\Common Files\VST3\`.

## Layout

**Top bar:**
* **SIZE**: 50, 75, 100, 125, 150 or 200 % (you can also drag the bottom-right corner).
* **BYPASS**: true bypass of the whole plugin, with a 200 ms crossfade. This is also the host's bypass.
* **OVERSAMPLING**: 1X, 2X or 4X.
* **VIEW**: Classic, Classic Exp or Dual Mono.
* **PRESET**: the factory presets.
* **LR / M / S / Δ** listening:
  * **LR**: normal stereo
  * **M**: mid only
  * **S**: side only
  * **Δ**: only what the EQ adds or removes (output minus the latency-aligned input)

**EQ rack**, from left to right:

| Control | Values |
|---|---|
| **CUT** (12 dB/oct low cut) | Out, 12, 18, 24, 30, 36, 43, 54 Hz |
| **SHELF** (low shelf frequency) | 74, 84, 98, 116, 131, 166, 230, 361 Hz |
| **LOW FREQUENCY** LEFT / RIGHT level | -5 .. +5 dB |
| **HIGH FREQUENCY** LEFT / RIGHT level | -5 .. +5 dB |
| **SHELF** (high shelf frequency) | 1.6, 1.8, 2.1, 2.5, 3.4, 4.8, 7.1, 18 kHz (the label sits about an octave below the shoulder) |
| **CUT** (12 dB/oct high cut) | Out, 70, 28, 18, 12.6, 11.1, 9, 7.5 kHz |
| **ENGAGE** | true bypass with a 200 ms crossfade |

Level knobs move in 0.5 dB steps when dragged; hold Shift for 0.1 dB. Automation can set any value.

**Second rack:**
* **STEREO LINK**: left and right move together.
* **MID/SIDE**: the two channels become Mid and Side.
* **OUTPUT**: -10 .. +10 dB.

### Views

| View | Racks | What it does |
|---|---|---|
| **Classic** | EQ rack only | One set of frequency selectors and one Engage switch for both channels. Separate left and right levels. |
| **Classic Exp** | adds the Stereo Link / Mid/Side / Output rack | As Classic. With Stereo Link on, the left and right levels move together. |
| **Dual Mono** | one full EQ rack per channel, plus the second rack | Fully independent channels. With Stereo Link on, the right channel follows the left. |

![Dual Mono view in M/S mode](docs/screenshot-dual-mono.png)

Knobs work like Virus Delay's: drag or use the scroll wheel, hold Shift for fine steps,
double-click to reset, and hover to see the value.

**Presets:**
* Init
* Acoustic Guitar Control
* Bass Bump 'n Tuck
* Drum Driver
* Drum Hype
* Drum MS
* Home HiFi
* Mix Elixer
* Pads Lift
* Track Everything
* Vocal Sheen
* Zeroed Out 4X

### Host parameters (in this order)

1. HP_freq_left
2. LS_freq_left
3. LS_gain_left
4. HS_gain_left
5. HS_freq_left
6. LP_freq_left
7. HP_freq_right
8. LS_freq_right
9. reserved
10. reserved
11. LS_gain_right
12. HS_gain_right
13. HS_freq_right
14. LP_freq_right
15. Output
16. Engage_On/Off_left
17. Engage_On/Off_right
18. Mid_Side_on/off
19. Stereo_link
20. Oversampling (not automatable)
21. View (not automatable)
22. Monitor (not automatable)
23. Bypass (the host's bypass)

Frequency parameters use host values 0..1 in steps of 1/7. The shelf frequencies default to 0.5,
which selects the 5th position. Gain parameters put 0 dB at 0.5.

## Signal path

```
in -> L/R or M/S -> [HP -> low shelf -> high shelf -> LP] x 2 channels -> engage mix
   -> preamp (1X / 2X / 4X) -> DC block (2 Hz) -> output gain -> true-bypass crossfade
   -> LR / M / S / delta monitoring -> out
```

* **Shelves**: first-order analogue Baxandall shelves, converted to digital with a matched pole and a
  numerator matched in magnitude at DC, Nyquist and a per-detent point. This avoids bilinear
  "cramping" near 20 kHz.
* **Low-pass**: matched poles plus a fitted correction section, so the 18/28/70 kHz corners stay
  correct at 44.1 kHz.
* **Preamp**: a gentle, slightly asymmetric soft saturation with unity small-signal gain. At 1 kHz it
  measures:

  | Level | H2 | H3 | Gain change |
  |---|---|---|---|
  | -18 dBFS | -63 dBc | -67 dBc | — |
  | 0 dBFS | -47 dBc | -31 dBc | -0.7 dB |

* **Oversampling**: linear-phase Kaiser half-band filters.
* **Latency**: 47 samples at every sample rate and every oversampling setting, so switching never
  changes the plugin delay compensation. Bypass, host bypass and Engage Off keep the same delay.
* **Precision**: everything runs in double precision, per sample. With the same settings, the output
  is bit-identical whatever the host block size.
* **Transitions**: shelf gains and output glide over 20 ms. Frequency, M/S and oversampling changes
  crossfade over 30 ms. Engage crossfades over 200 ms.

## Tests

Both test programs run in CI on every push. The plugin is published only if every check passes.

* `tests/EngineTests.cpp` runs 52 DSP checks with no JUCE dependency:
  * every detent and gain against the analogue prototypes, at 44.1, 48, 88.2, 96 and 192 kHz
  * latency at every rate and oversampling setting
  * bit-identical output across block sizes 1 to 4096, including random block sizes
  * click-free parameter changes
  * Engage Off gives a bit-exact delayed dry signal
  * M/S correctness, harmonics and aliasing, and digital silence in giving silence out
  * global Bypass, and LR / M / S / Δ monitoring
* `tests/PluginTests.cpp` runs 79 checks on the real processor and editor:
  * parameter order, names, defaults and display strings
  * float and double processing, latency, Output gain, presets, and state save/restore
  * window size for each view, the link behaviour, and resizing; it also renders the GUI to PNG

Results from this build:

| Check | Result |
|---|---|
| EQ vs analogue prototype, 20 Hz–20 kHz, at 44.1 / 48 kHz | ≤ 0.22 dB (worst case: 18 kHz shelf at +5 dB, near 19 kHz) |
| Same, at 88.2 kHz and above | ≤ 0.06 dB |
| Same, 20 kHz to 0.45 × sample rate | ≤ 0.42 dB |
| Engine level at -40 dBFS vs the EQ design | ≤ 0.001 dB |
| Alias of 15 kHz at 0 dBFS (sample rate 48 kHz) | -31 dBc at 1X, -118 dBc at 2X and 4X |

The VST3 itself was also loaded in a third-party VST3 host (pedalboard, on Linux). It reported
47 samples of latency and showed every parameter. The shelf boosts measured as designed: the left
channel's low shelf (131 Hz) at +5 dB gave +4.34 dB at 50 Hz, exactly the analogue value, with the
right channel untouched. The output
was identical across host block sizes.

**Not done:** a null test against the original hardware-capture plugin. That needs the original
installed on a Windows machine. The detent frequencies are read from the reference panel layout.
The shelf placement, filter Q and preamp curve are design values, not measurements.

## Building

The CI does this on Windows. To build locally you need CMake 3.22+ and Visual Studio 2022 (JUCE
8.0.4 is downloaded automatically):

```
cmake -B build -A x64 -DBAXR_BUILD_TESTS=ON
cmake --build build --config Release --target BAXR_VST3
```

The plugin is written to `build/BAXR_artefacts/Release/VST3/BAXR.vst3`.
