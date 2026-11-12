#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/EqTables.h"

namespace bax
{
//==============================================================================
// Host parameters, in this exact order (see the spec). Parameter IDs never change.
namespace ParamIDs
{
    inline constexpr const char* hpL     = "HP_freq_left";
    inline constexpr const char* lsFreqL = "LS_freq_left";
    inline constexpr const char* lsGainL = "LS_gain_left";
    inline constexpr const char* hsGainL = "HS_gain_left";
    inline constexpr const char* hsFreqL = "HS_freq_left";
    inline constexpr const char* lpL     = "LP_freq_left";
    inline constexpr const char* hpR     = "HP_freq_right";
    inline constexpr const char* lsFreqR = "LS_freq_right";
    inline constexpr const char* reserved1 = "reserved_1";
    inline constexpr const char* reserved2 = "reserved_2";
    inline constexpr const char* lsGainR = "LS_gain_right";
    inline constexpr const char* hsGainR = "HS_gain_right";
    inline constexpr const char* hsFreqR = "HS_freq_right";
    inline constexpr const char* lpR     = "LP_freq_right";
    inline constexpr const char* output  = "Output";
    inline constexpr const char* engageL = "Engage_left";
    inline constexpr const char* engageR = "Engage_right";
    inline constexpr const char* midSide = "Mid_Side";
    inline constexpr const char* link    = "Stereo_link";
    inline constexpr const char* oversampling = "Oversampling";
    inline constexpr const char* view    = "View";
}

/** The six EQ parameter IDs of one channel. */
struct ChannelIDs { const char *hp, *lsFreq, *lsGain, *hsGain, *hsFreq, *lp, *engage; };
inline constexpr ChannelIDs kChannelIDs[2] =
{
    { ParamIDs::hpL, ParamIDs::lsFreqL, ParamIDs::lsGainL, ParamIDs::hsGainL, ParamIDs::hsFreqL, ParamIDs::lpL, ParamIDs::engageL },
    { ParamIDs::hpR, ParamIDs::lsFreqR, ParamIDs::lsGainR, ParamIDs::hsGainR, ParamIDs::hsFreqR, ParamIDs::lpR, ParamIDs::engageR },
};

enum class View { Classic = 0, ClassicExp = 1, DualMono = 2 };

inline juce::StringArray oversamplingNames() { return { "1X", "2X", "4X" }; }
inline juce::StringArray viewNames()         { return { "Classic", "Classic Exp", "Dual Mono" }; }

//==============================================================================
// Display strings
inline juce::String formatFrequency (double hz)
{
    if (hz >= 1000.0)
    {
        const double k = hz / 1000.0;
        return juce::String (k, std::abs (k - std::round (k)) < 1.0e-6 ? 0 : 1) + " kHz";
    }
    return juce::String (juce::roundToInt (hz)) + " Hz";
}

enum class FreqKind { HP, LS, HS, LP };

inline const double* freqTable (FreqKind k)
{
    switch (k)
    {
        case FreqKind::HP: return kHpFreqs;
        case FreqKind::LS: return kLsFreqs;
        case FreqKind::HS: return kHsFreqs;
        case FreqKind::LP: default: return kLpFreqs;
    }
}

inline juce::String detentText (FreqKind k, int detent)
{
    const double hz = freqTable (k)[juce::jlimit (0, kNumDetents - 1, detent)];
    return hz <= 0.0 ? juce::String ("Off") : formatFrequency (hz);
}

inline juce::String formatGain (double db)
{
    if (std::abs (db) < 0.005) return "0.0 dB";
    return (db > 0.0 ? "+" : "") + juce::String (db, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " dB";
}

inline juce::String formatGainShort (double db)
{
    if (std::abs (db) < 0.05) return "0.0";
    return (db > 0.0 ? "+" : "") + juce::String (db, 1);
}

//==============================================================================
/** Frequency control: host value 0..1 (default 0 for HP/LP, 0.5 for the shelves),
    8 detents at multiples of 1/7. Shown as the detent's label. */
inline std::unique_ptr<juce::AudioParameterFloat> makeFrequencyParam (const char* id, FreqKind kind, float defaultValue)
{
    auto toText = [kind] (float v, int) { return detentText (kind, detentFromNormalised (v)); };
    auto fromText = [kind] (const juce::String& t)
    {
        const auto s = t.trim().toLowerCase();
        if (s.startsWith ("off")) return 0.0f;
        double hz = s.getDoubleValue();
        if (s.contains ("k")) hz *= 1000.0;
        const double* table = freqTable (kind);
        int best = 0;
        for (int d = 0; d < kNumDetents; ++d)
            if (table[d] > 0.0 && (table[best] <= 0.0 || std::abs (std::log (table[d] / hz)) < std::abs (std::log (table[best] / hz))))
                best = d;
        return (float) best / (float) (kNumDetents - 1);
    };

    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, id, juce::NormalisableRange<float> (0.0f, 1.0f), defaultValue,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (toText)
                                            .withValueFromStringFunction (fromText));
}

/** Shelf gain: -5 .. +5 dB, continuous, 0 dB at the centre (host value 0.5). */
inline std::unique_ptr<juce::AudioParameterFloat> makeGainParam (const char* id, double rangeDb)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, id,
        juce::NormalisableRange<float> ((float) -rangeDb, (float) rangeDb), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return formatGain (v); })
            .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); }));
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto choice = [] (const char* id, const char* name, StringArray items, int def, bool automatable = true)
    {
        return std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def,
                                                       AudioParameterChoiceAttributes().withAutomatable (automatable));
    };

    // 1 .. 6: left / channel A
    layout.add (makeFrequencyParam (ParamIDs::hpL,     FreqKind::HP, 0.0f));
    layout.add (makeFrequencyParam (ParamIDs::lsFreqL, FreqKind::LS, 0.5f));
    layout.add (makeGainParam      (ParamIDs::lsGainL, kGainRangeDb));
    layout.add (makeGainParam      (ParamIDs::hsGainL, kGainRangeDb));
    layout.add (makeFrequencyParam (ParamIDs::hsFreqL, FreqKind::HS, 0.5f));
    layout.add (makeFrequencyParam (ParamIDs::lpL,     FreqKind::LP, 0.0f));
    // 7, 8: right / channel B
    layout.add (makeFrequencyParam (ParamIDs::hpR,     FreqKind::HP, 0.0f));
    layout.add (makeFrequencyParam (ParamIDs::lsFreqR, FreqKind::LS, 0.5f));
    // 9, 10: reserved slots (kept so the host-visible order is identical; no effect)
    for (auto* id : { ParamIDs::reserved1, ParamIDs::reserved2 })
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, "reserved",
                                                           NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                                                           AudioParameterFloatAttributes().withAutomatable (false)
                                                               .withStringFromValueFunction ([] (float, int) { return String ("-"); })));
    // 11 .. 14
    layout.add (makeGainParam      (ParamIDs::lsGainR, kGainRangeDb));
    layout.add (makeGainParam      (ParamIDs::hsGainR, kGainRangeDb));
    layout.add (makeFrequencyParam (ParamIDs::hsFreqR, FreqKind::HS, 0.5f));
    layout.add (makeFrequencyParam (ParamIDs::lpR,     FreqKind::LP, 0.0f));
    // 15
    layout.add (makeGainParam (ParamIDs::output, kOutputRangeDb));
    // 16 .. 19
    layout.add (choice (ParamIDs::engageL, "Engage_On/Off_left",  { "On", "Off" }, 0));
    layout.add (choice (ParamIDs::engageR, "Engage_On/Off_right", { "On", "Off" }, 0));
    layout.add (choice (ParamIDs::midSide, "Mid_Side_on/off",     { "Off", "On" }, 0));
    layout.add (choice (ParamIDs::link,    "Stereo_link",         { "Off", "On" }, 0));
    // menus
    layout.add (choice (ParamIDs::oversampling, "Oversampling", oversamplingNames(), 0, false));
    layout.add (choice (ParamIDs::view,         "View",         viewNames(),         2, false));
    return layout;
}

} // namespace bax
