#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "CustomLookAndFeel.h"
#include "UI/Components.h"

//==============================================================================
/** Engage switch: lit when the channel is engaged (parameter choice 0 = On). */
class EngageButton : public juce::TextButton
{
public:
    explicit EngageButton (juce::RangedAudioParameter&);
    ~EngageButton() override;

private:
    juce::ParameterAttachment attachment;
};

//==============================================================================
/** The whole user interface at its base size (760 x 446); the editor scales it. */
class BaxPanel : public juce::Component,
                 private juce::Timer
{
public:
    static constexpr int kBaseWidth = 760, kBaseHeight = 446;

    explicit BaxPanel (BaxAudioProcessor&);
    ~BaxPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Strip
    {
        BaxKnob lsGain { BaxKnob::Size::Large, "Low" };
        BaxKnob hsGain { BaxKnob::Size::Large, "High" };
        BaxKnob hp     { BaxKnob::Size::Small, "HP" };
        BaxKnob lsFreq { BaxKnob::Size::Small, "Low Freq" };
        BaxKnob hsFreq { BaxKnob::Size::Small, "High Freq" };
        BaxKnob lp     { BaxKnob::Size::Small, "LP" };
        std::unique_ptr<EngageButton> engage;
        LcdDisplay lowLcd, highLcd;
        juce::Rectangle<float> bounds;

        juce::Component* controls[7] {};
    };

    void timerCallback() override;
    void setupStrip (Strip&, int channel);
    void layoutStrip (Strip&, juce::Rectangle<float> panel);
    void updateViewState();
    void createBackground();
    void drawWordmark (juce::Graphics&, juce::Rectangle<float> area);
    static void setActive (juce::Component&, bool active, const juce::String& hint = {});

    BaxAudioProcessor& processor;
    CustomLookAndFeel lnf;
    juce::Image background;

    Strip strips[2];
    BaxKnob outputKnob { BaxKnob::Size::Small, "Output" };
    juce::TextButton midSideButton { "M/S" }, linkButton { "Link" };
    juce::ComboBox viewBox, oversamplingBox, presetBox;
    LevelMeter meterL, meterR;
    ResponseGraph graph;
    HelpButton helpButton;
    HelpOverlay helpOverlay;
    juce::TooltipWindow tooltipWindow { this, 500 };

    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::vector<std::unique_ptr<KnobAttachment>> knobAttachments;
    std::vector<std::unique_ptr<ComboAttachment>> comboAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

    juce::Rectangle<float> graphPanel, outputPanel;
    bax::View currentView = bax::View::DualMono;
    bool linkedShown = false, midSideShown = false;
    int lastShownProgram = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BaxPanel)
};

//==============================================================================
/** Resizable editor (fixed aspect ratio, 60 % .. 250 %); remembers its size. */
class BaxAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit BaxAudioProcessorEditor (BaxAudioProcessor&);
    void resized() override;

private:
    BaxAudioProcessor& processor;
    BaxPanel panel;
    bool sizeRestored = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BaxAudioProcessorEditor)
};
