#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "CustomLookAndFeel.h"
#include "UI/Components.h"

//==============================================================================
/**
    The user interface, laid out as rack units at a base width of 1200 px:

      top bar   SIZE / BYPASS / OVERSAMPLING / VIEW / PRESET / LR M S Δ
      rack 1    CUT  SHELF  [LEFT LEVEL RIGHT] low  [LEFT LEVEL RIGHT] high  SHELF  CUT  ENGAGE
      rack 2    STEREO LINK              MID/SIDE                               OUTPUT

    Classic shows rack 1 only, Classic Exp adds rack 2, and Dual Mono shows one full
    rack per channel plus rack 2. The base height follows the view; the editor
    scales the whole panel.
*/
class BaxPanel : public juce::Component,
                 private juce::Timer
{
public:
    static constexpr int kBaseWidth = 1200;

    explicit BaxPanel (BaxAudioProcessor&);
    ~BaxPanel() override;

    static int baseHeightFor (bax::View);
    int getBaseHeight() const { return baseHeightFor (shownView); }

    std::function<void()> onViewChanged;            // base height changed
    std::function<void (float)> onScaleRequested;   // SIZE menu

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Strip
    {
        BaxKnob hp     { BaxKnob::Size::Small, "Low Cut" };
        BaxKnob lsFreq { BaxKnob::Size::Small, "Low Shelf" };
        BaxKnob lsGain { BaxKnob::Size::Large, "Low Level" };
        BaxKnob hsGain { BaxKnob::Size::Large, "High Level" };
        BaxKnob hsFreq { BaxKnob::Size::Small, "High Shelf" };
        BaxKnob lp     { BaxKnob::Size::Small, "High Cut" };
        std::unique_ptr<LedButton> engage;
    };

    /** Numbers printed around a knob. */
    struct Scale
    {
        juce::Point<float> centre;
        float radius = 0.0f;
        juce::StringArray labels;
        juce::Array<float> positions;   // 0..1 along the knob travel
    };

    /** Text drawn on the faceplate. */
    struct Caption
    {
        juce::String text;
        juce::Rectangle<float> area;
        bool accent = false;            // orange heading
    };

    void timerCallback() override;
    void setupStrip (Strip&, int channel);
    void layoutForView();
    void placeSelector (BaxKnob&, float cx, float cy, bax::FreqKind);
    void placeLevel (BaxKnob&, float cx, float cy);
    void syncLinkedParameters();
    void createBackground();
    void drawRack (juce::Graphics&, float y) const;
    void drawLogo (juce::Graphics&, juce::Rectangle<float> area, const juce::String& subtitle) const;
    static float rackY (int index);

    BaxAudioProcessor& processor;
    CustomLookAndFeel lnf;
    juce::Image metal;

    Strip strips[2];
    BaxKnob outputKnob { BaxKnob::Size::Small, "Output" };
    std::unique_ptr<LedButton> linkLed, midSideLed;
    std::unique_ptr<PillToggle> bypassButton;
    std::unique_ptr<MonitorSelector> monitor;
    MenuLabel sizeMenu { "SIZE" }, oversamplingMenu { "OVERSAMPLING" }, viewMenu { "VIEW" }, presetMenu { "PRESET" };
    juce::TooltipWindow tooltipWindow { this, 600 };

    std::vector<std::unique_ptr<KnobAttachment>> knobAttachments;

    std::vector<Scale> scales;
    std::vector<Caption> captions;
    std::vector<juce::String> logoSubtitles;
    int numRacks = 2;

    bax::View shownView = bax::View::ClassicExp;
    bool shownMidSide = false;

    // last values seen by the link sync (normalised), per synced pair
    struct SyncPair { juce::RangedAudioParameter *left, *right; float lastLeft = -1.0f, lastRight = -1.0f; };
    std::vector<SyncPair> freqPairs, gainPairs, enginePairs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BaxPanel)
};

//==============================================================================
/** Resizable editor (fixed aspect ratio per view, 50 % .. 250 %); remembers its scale. */
class BaxAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit BaxAudioProcessorEditor (BaxAudioProcessor&);
    void resized() override;

    float getScale() const { return (float) getWidth() / (float) BaxPanel::kBaseWidth; }

private:
    void applyViewSize (float scale);

    BaxAudioProcessor& processor;
    BaxPanel panel;
    bool ready = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BaxAudioProcessorEditor)
};
