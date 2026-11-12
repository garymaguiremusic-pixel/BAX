// Plugin-level tests: parameter list, state, presets, latency, float/double
// processing, and PNG renders of the editor.
//   BAXR_PluginTests [output folder for PNGs]
// Exit code 0 = all checks passed.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

using namespace juce;

static int failures = 0, checks = 0;

static void check (bool ok, const String& what)
{
    ++checks;
    if (! ok) { ++failures; std::printf ("  FAIL  %s\n", what.toRawUTF8()); }
}

static void testParameters (BaxAudioProcessor& p)
{
    std::printf ("[1] Host parameter order, names and normalised defaults\n");
    struct Expected { const char* name; float def; };
    const Expected expected[] =
    {
        { "HP_freq_left", 0.0f }, { "LS_freq_left", 0.5f }, { "LS_gain_left", 0.5f }, { "HS_gain_left", 0.5f },
        { "HS_freq_left", 0.5f }, { "LP_freq_left", 0.0f }, { "HP_freq_right", 0.0f }, { "LS_freq_right", 0.5f },
        { "reserved", 0.0f }, { "reserved", 0.0f }, { "LS_gain_right", 0.5f }, { "HS_gain_right", 0.5f },
        { "HS_freq_right", 0.5f }, { "LP_freq_right", 0.0f }, { "Output", 0.5f },
        { "Engage_On/Off_left", 0.0f }, { "Engage_On/Off_right", 0.0f }, { "Mid_Side_on/off", 0.0f }, { "Stereo_link", 0.0f },
    };

    const auto& params = p.getParameters();
    check (params.size() == 23, "23 parameters (19 + Oversampling, View, Monitor, Bypass), got " + String (params.size()));
    check (p.getBypassParameter() == p.apvts.getParameter (bax::ParamIDs::bypass), "Bypass is the host bypass parameter");
    for (int i = 0; i < (int) std::size (expected); ++i)
    {
        auto* param = params[i];
        const auto name = param->getName (64);
        check (name == expected[i].name, "param " + String (i + 1) + " name '" + name + "'");
        check (std::abs (param->getDefaultValue() - expected[i].def) < 1.0e-6f,
               "param " + String (i + 1) + " default " + String (param->getDefaultValue()));
        std::printf ("      %2d  %-22s default %.3f  shows '%s'\n", i + 1, name.toRawUTF8(), param->getDefaultValue(),
                     param->getText (param->getDefaultValue(), 32).toRawUTF8());
    }

    // detent display strings
    auto* hp = p.apvts.getParameter (bax::ParamIDs::hpL);
    String row;
    for (int d = 0; d < bax::kNumDetents; ++d)
        row << hp->getText ((float) d / 7.0f, 32) << (d < 7 ? ", " : "");
    std::printf ("      HP detents: %s\n", row.toRawUTF8());
    check (hp->getText (0.0f, 32) == "Out" && hp->getText (1.0f, 32) == "54 Hz", "HP detent labels");
    check (p.apvts.getParameter (bax::ParamIDs::lsFreqL)->getText (0.5f, 32) == "131 Hz", "LS default shows 131 Hz");
    check (p.apvts.getParameter (bax::ParamIDs::hsFreqL)->getText (6.0f / 7.0f, 32) == "7.1 kHz", "HS detent 6 shows 7.1 kHz");
    check (p.apvts.getParameter (bax::ParamIDs::lsGainL)->getText (0.75f, 32) == "+2.5 dB", "gain text +2.5 dB");
    check (std::abs (p.apvts.getParameter (bax::ParamIDs::lpL)->getValueForText ("18 kHz") - 3.0f / 7.0f) < 1.0e-6f, "LP text -> value");
}

static double rms (const AudioBuffer<float>& b, int ch)
{
    return b.getRMSLevel (ch, 0, 480);   // exactly 10 cycles of 1 kHz at 48 kHz
}

static void testProcessing (BaxAudioProcessor& p)
{
    std::printf ("[2] Latency and processing at every native rate (float and double)\n");
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        p.setPlayConfigDetails (2, 2, fs, 512);
        p.prepareToPlay (fs, 512);
        check (p.getLatencySamples() == 47, "latency 47 at " + String (fs));

        AudioBuffer<float> fb (2, 512);
        AudioBuffer<double> db (2, 512);
        MidiBuffer midi;
        fb.clear(); db.clear();
        fb.setSample (0, 10, 0.25f); fb.setSample (1, 10, 0.25f);
        db.setSample (0, 10, 0.25);  db.setSample (1, 10, 0.25);
        p.processBlock (fb, midi);
        p.reset();
        p.processBlock (db, midi);
        int peakF = fb.findMinMax (0, 0, 512).getEnd() > 0.2f ? 0 : -1;
        for (int i = 0; i < 512; ++i) if (std::abs (fb.getSample (0, i)) > 0.2f) peakF = i;
        check (peakF == 10 + 47, "impulse at 10 + 47 (float) at " + String (fs));
        check (std::abs ((double) fb.getSample (0, 57) - db.getSample (0, 57)) < 1.0e-6, "float and double paths agree");
    }

    std::printf ("[3] Engage Off and host bypass keep the latency; Output gain\n");
    const double fs = 48000.0;
    p.prepareToPlay (fs, 512);
    auto* engage = p.apvts.getParameter (bax::ParamIDs::engageL);
    auto* lsGain = p.apvts.getParameter (bax::ParamIDs::lsGainL);
    lsGain->setValueNotifyingHost (1.0f);   // +5 dB
    engage->setValueNotifyingHost (1.0f);   // Off
    p.apvts.getParameter (bax::ParamIDs::engageR)->setValueNotifyingHost (1.0f);
    MidiBuffer midi;
    AudioBuffer<float> b (2, 512);
    for (int block = 0; block < 40; ++block)   // > 200 ms fade
    {
        for (int i = 0; i < 512; ++i)
            for (int c = 0; c < 2; ++c)
                b.setSample (c, i, 0.1f * (float) std::sin (2.0 * MathConstants<double>::pi * 100.0 * (block * 512 + i) / fs));
        p.processBlock (b, midi);
    }
    double err = 0.0;
    for (int i = 0; i < 512; ++i)
    {
        const float want = 0.1f * (float) std::sin (2.0 * MathConstants<double>::pi * 100.0 * (39 * 512 + i - 47) / fs);
        err = std::max (err, (double) std::abs (b.getSample (0, i) - want));
    }
    check (err < 1.0e-6, "Engage Off = input delayed by 47 samples (max err " + String (err) + ")");
    engage->setValueNotifyingHost (0.0f);
    p.apvts.getParameter (bax::ParamIDs::engageR)->setValueNotifyingHost (0.0f);
    lsGain->setValueNotifyingHost (0.5f);

    auto* out = p.apvts.getParameter (bax::ParamIDs::output);
    out->setValueNotifyingHost (out->convertTo0to1 (-6.0f));
    for (int block = 0; block < 20; ++block)
    {
        for (int i = 0; i < 512; ++i)
            for (int c = 0; c < 2; ++c)
                b.setSample (c, i, 0.01f * (float) std::sin (2.0 * MathConstants<double>::pi * 1000.0 * (block * 512 + i) / fs));
        p.processBlock (b, midi);
    }
    const double gainDb = 20.0 * std::log10 (rms (b, 0) / (0.01 / std::sqrt (2.0)));
    check (std::abs (gainDb + 6.0) < 0.05, "Output -6 dB -> " + String (gainDb, 3) + " dB");
    out->setValueNotifyingHost (0.5f);
}

static void testStateAndPresets (BaxAudioProcessor& p)
{
    std::printf ("[4] Presets and state save / restore\n");
    check (p.getNumPrograms() == 12, "12 factory presets");
    for (int i = 0; i < p.getNumPrograms(); ++i)
        std::printf ("      %2d  %s\n", i + 1, p.getProgramName (i).toRawUTF8());

    p.setCurrentProgram (5);   // Drum MS
    check (p.apvts.getRawParameterValue (bax::ParamIDs::midSide)->load() > 0.5f, "Drum MS turns M/S on");
    check (p.getView() == bax::View::DualMono, "Drum MS selects Dual Mono");

    MemoryBlock state;
    p.getStateInformation (state);
    const float hsGainR = p.apvts.getRawParameterValue (bax::ParamIDs::hsGainR)->load();

    p.setCurrentProgram (0);
    check (std::abs (p.apvts.getRawParameterValue (bax::ParamIDs::hsGainR)->load()) < 1.0e-6f, "Init resets gains");

    p.setStateInformation (state.getData(), (int) state.getSize());
    check (std::abs (p.apvts.getRawParameterValue (bax::ParamIDs::hsGainR)->load() - hsGainR) < 1.0e-6f, "state restores HS_gain_right");
    check (p.getCurrentProgram() == 5, "state restores the preset index");
    p.setCurrentProgram (0);
}

static void testEditor (BaxAudioProcessor& p, const File& outDir)
{
    std::printf ("[5] Editor: views, window sizes, link behaviour, PNG renders\n");
    auto setParam = [&] (const char* id, float plain)
    {
        auto* prm = p.apvts.getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
    };
    auto plain = [&] (const char* id) { return p.apvts.getRawParameterValue (id)->load(); };
    auto pump = [] { for (int i = 0; i < 4; ++i) MessageManager::getInstance()->runDispatchLoopUntil (40); };

    p.setCurrentProgram (0);
    setParam (bax::ParamIDs::view, 1.0f);   // Classic Exp (default)
    std::unique_ptr<AudioProcessorEditor> editor (p.createEditor());
    check (editor != nullptr, "editor created");
    check (editor->isResizable(), "editor is resizable");
    pump();
    check (editor->getWidth() == 1200 && editor->getHeight() == 288, "Classic Exp: 1200 x 288, got "
           + String (editor->getWidth()) + " x " + String (editor->getHeight()));

    auto snap = [&] (const String& name)
    {
        pump();
        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        if (outDir.getFullPathName().isNotEmpty())
        {
            auto f = outDir.getChildFile (name + ".png");
            f.deleteFile();
            FileOutputStream os (f);
            PNGImageFormat().writeImageToStream (img, os);
            std::printf ("      wrote %s\n", f.getFullPathName().toRawUTF8());
        }
    };

    p.setCurrentProgram (10);   // Vocal Sheen
    snap ("baxr_classic_exp");

    setParam (bax::ParamIDs::view, 0.0f);
    pump();
    check (editor->getWidth() == 1200 && editor->getHeight() == 158, "Classic: 1200 x 158");
    snap ("baxr_classic");

    setParam (bax::ParamIDs::view, 2.0f);
    pump();
    check (editor->getWidth() == 1200 && editor->getHeight() == 418, "Dual Mono: 1200 x 418");
    p.setCurrentProgram (5);    // Drum MS
    snap ("baxr_dual_mono_ms");

    // Classic views: right frequency selectors and Engage follow the left ones
    p.setCurrentProgram (0);
    setParam (bax::ParamIDs::view, 1.0f);
    pump();
    setParam (bax::ParamIDs::hpL, 5.0f / 7.0f);
    setParam (bax::ParamIDs::lsGainL, 3.0f);
    pump();
    check (std::abs (plain (bax::ParamIDs::hpR) - 5.0f / 7.0f) < 1.0e-5f, "Classic: right HP follows left");
    check (std::abs (plain (bax::ParamIDs::lsGainR)) < 1.0e-5f, "Classic, link off: right level stays independent");
    setParam (bax::ParamIDs::link, 1.0f);
    pump();
    check (std::abs (plain (bax::ParamIDs::lsGainR) - 3.0f) < 1.0e-5f, "Classic, link on: right level follows left");
    setParam (bax::ParamIDs::hsGainR, -2.0f);   // moving the right one moves the left too
    pump();
    check (std::abs (plain (bax::ParamIDs::hsGainL) + 2.0f) < 1.0e-5f, "link on: moving right moves left");
    setParam (bax::ParamIDs::link, 0.0f);

    // SIZE / resizing keeps the aspect ratio and is remembered
    editor->setSize (1800, 432);
    pump();
    check (std::abs (editor->getWidth() - 1800) <= 2 && std::abs (editor->getHeight() - 432) <= 2, "scales to 150 %");
    check (std::abs (p.editorScale.load() - 1.5f) < 0.01f, "scale is remembered");
    snap ("baxr_150pct");
    editor.reset();
    p.editorScale.store (1.0f);
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI juce;
    const File outDir = argc > 1 ? File (String (argv[1])) : File();
    if (outDir.getFullPathName().isNotEmpty())
        outDir.createDirectory();

    {
        BaxAudioProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        testParameters (p);
        testProcessing (p);
        testStateAndPresets (p);
        p.prepareToPlay (48000.0, 512);
        testEditor (p, outDir);
    }

    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
