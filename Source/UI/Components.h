#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/EqTables.h"

//==============================================================================
/**
    Rotary knob (same feel as Virus Delay):
      - vertical drag (whole range in 200 px) and scroll wheel move in coarse steps;
        hold Shift for fine steps
      - double-click resets to the default value
      - while hovering or dragging, the value is shown in a small bubble under the knob
      - detented knobs draw one tick per position and snap to them
    The component is the knob (a square) plus a 16 px strip below it for the bubble.
    Values are in the parameter's own units. Drawing is in CustomLookAndFeel.
*/
class BaxKnob : public juce::Slider
{
public:
    enum class Size { Large, Small };
    static constexpr int kBubbleHeight = 16;

    BaxKnob (Size, const juce::String& name);

    Size getKnobSize() const noexcept           { return knobSize; }
    void setDefaultValue (double v) noexcept    { defaultValue = v; }

    bool bipolar = false;        // value arc grows from the centre
    int numDetents = 0;          // > 1: stepped knob (frequency selector)
    double coarseStep = 0.5, fineStep = 0.1;

    std::function<void()> onGestureBegin, onGestureEnd;
    std::function<juce::String (double)> displayValue;   // e.g. "+2.5 dB", "230 Hz"

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
/** Round push switch with an LED in a chrome bezel (Engage, Stereo Link, Mid/Side).
    Lit when the parameter's value is "on"; onValue is the plain value meaning on. */
class LedButton : public juce::Button
{
public:
    LedButton (juce::RangedAudioParameter&, float onValue, const juce::String& tooltip);
    ~LedButton() override;
    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;

private:
    juce::ParameterAttachment attachment;
    float onValue;
};

//==============================================================================
/** Top-bar text menu, drawn as "TITLE ▼ VALUE". Click to open the menu. */
class MenuLabel : public juce::Component,
                  public juce::SettableTooltipClient
{
public:
    explicit MenuLabel (const juce::String& title);

    std::function<juce::String()> getValueText;          // shown after the arrow (may be empty)
    std::function<void (juce::PopupMenu&)> buildMenu;     // fill the menu (items carry their own actions)

    void refresh();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override  { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { hover = false; repaint(); }

private:
    juce::String title, value;
    bool hover = false;
};

//==============================================================================
/** Top-bar pill toggle attached to a bool parameter (BYPASS). */
class PillToggle : public juce::Button
{
public:
    PillToggle (juce::RangedAudioParameter&, const juce::String& text, const juce::String& tooltip);
    ~PillToggle() override;
    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;

private:
    juce::ParameterAttachment attachment;
};

//==============================================================================
/** "LR  M  S  Δ" listening selector attached to the Monitor parameter. */
class MonitorSelector : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    explicit MonitorSelector (juce::RangedAudioParameter&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override   { hoverIndex = -1; repaint(); }

private:
    int indexAt (juce::Point<float>) const;
    juce::Rectangle<float> segment (int i) const;

    juce::ParameterAttachment attachment;
    int selected = 0, hoverIndex = -1;
};
