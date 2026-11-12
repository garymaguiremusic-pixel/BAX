#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/EqChannel.h"

//==============================================================================
/**
    Rotary knob (same feel as Virus Delay):
      - vertical drag (whole range in 200 px) and scroll wheel move in coarse steps;
        hold Shift for fine steps
      - double-click resets to the default value
      - small knobs show their label underneath, which swaps to the value while
        hovering or dragging; the tooltip shows the value too
      - detented knobs draw one tick per position and snap to them
    Values are in the parameter's own units. Drawing is in CustomLookAndFeel.
*/
class BaxKnob : public juce::Slider
{
public:
    enum class Size { Large, Small };

    BaxKnob (Size, const juce::String& labelText);

    Size getKnobSize() const noexcept           { return knobSize; }
    void setDefaultValue (double v) noexcept    { defaultValue = v; }
    void setLabel (const juce::String& l)       { if (l != label) { label = l; repaint(); } }

    bool bipolar = false;        // value arc grows from the centre
    int numDetents = 0;          // > 1: stepped knob (frequency selector)
    double coarseStep = 0.5, fineStep = 0.1;

    std::function<void()> onGestureBegin, onGestureEnd;
    std::function<juce::String (double)> displayValue;   // e.g. "+2.5 dB", "230 Hz"
    juce::String inactiveHint;

    juce::String getTooltip() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::String valueText() const;
    double snap (double v, bool fine) const;
    void setValueWithGesture (double v);

    Size knobSize;
    juce::String label;
    double defaultValue = 0.0;
    float lastDragY = 0.0f;
    double dragValue = 0.0;
    bool dragShift = false;
    bool dragging = false, hovering = false;
};

//==============================================================================
/** Connects a BaxKnob to a parameter with proper host gesture handling. */
class KnobAttachment
{
public:
    KnobAttachment (juce::RangedAudioParameter&, BaxKnob&);
    ~KnobAttachment();

private:
    BaxKnob& knob;
    juce::ParameterAttachment attachment;
};

//==============================================================================
/** Blue LCD readout, e.g. "LOW  230 Hz  +2.5". */
class LcdDisplay : public juce::Component
{
public:
    void setContent (const juce::String& name, const juce::String& value);
    void setDimmed (bool shouldDim);
    void paint (juce::Graphics&) override;

private:
    juce::String nameText, valueText;
    bool dimmed = false;
};

//==============================================================================
/** Frequency response of both EQ channels, drawn on the blue LCD. */
class ResponseGraph : public juce::Component
{
public:
    struct State
    {
        bax::ChannelSettings a, b;
        bool engagedA = true, engagedB = true, midSide = false, linked = false;
        double outputDb = 0.0, sampleRate = 48000.0;
        bool operator== (const State&) const;
    };

    void setState (const State&);
    void paint (juce::Graphics&) override;

private:
    State state;
    bool hasState = false;
    bax::DesignTable table;   // rebuilt only when the sample rate changes
};

//==============================================================================
/** Small vertical output meter (dBFS, -48 .. +6). */
class LevelMeter : public juce::Component
{
public:
    void setLevel (float linearPeak);
    void paint (juce::Graphics&) override;

private:
    float levelDb = -100.0f;
};

//==============================================================================
/** Round "?" button in the bottom bar. */
class HelpButton : public juce::Button
{
public:
    HelpButton() : juce::Button ("help") {}
    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;
};

/** Quick reference shown over the editor; click anywhere to close. */
class HelpOverlay : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { setVisible (false); }
};
