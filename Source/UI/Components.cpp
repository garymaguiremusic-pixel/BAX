#include "Components.h"
#include "../CustomLookAndFeel.h"

using namespace juce;

//==============================================================================
// BaxKnob
//==============================================================================
BaxKnob::BaxKnob (Size s, const String& name)
    : Slider (Slider::RotaryVerticalDrag, Slider::NoTextBox), knobSize (s)
{
    setRange (0.0, 1.0, 0.0);
    setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    setName (name);
}

String BaxKnob::valueText() const
{
    return displayValue ? displayValue (getValue()) : String (getValue(), 2);
}

String BaxKnob::getTooltip()
{
    return getName().toUpperCase() + "  " + valueText();
}

void BaxKnob::paint (Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto bubble = bounds.removeFromBottom ((float) kBubbleHeight);
    const float side = std::min (bounds.getWidth(), bounds.getHeight());
    const auto knobArea = bounds.withSizeKeepingCentre (side, side);

    float pos = (float) valueToProportionOfLength (getValue());
    if (numDetents > 1)   // draw the pointer on the detent actually in use
        pos = (float) bax::detentFromNormalised (pos) / (float) (numDetents - 1);
    const auto rotary = getRotaryParameters();

    getLookAndFeel().drawRotarySlider (g, (int) knobArea.getX(), (int) knobArea.getY(),
                                       (int) knobArea.getWidth(), (int) knobArea.getHeight(),
                                       pos, rotary.startAngleRadians, rotary.endAngleRadians, *this);

    if ((hovering || dragging) && isEnabled())
    {
        const auto text = valueText();
        const auto font = CustomLookAndFeel::uiFont (11.0f, true);
        const float w = std::min (bubble.getWidth(), GlyphArrangement::getStringWidth (font, text) + 12.0f);
        auto pill = bubble.withSizeKeepingCentre (w, (float) kBubbleHeight - 1.0f);
        g.setColour (Colour (0xf0101418));
        g.fillRoundedRectangle (pill, 3.0f);
        g.setColour (BaxColours::blue.withAlpha (0.7f));
        g.drawRoundedRectangle (pill.reduced (0.5f), 3.0f, 1.0f);
        g.setFont (font);
        g.setColour (Colours::white);
        g.drawFittedText (text, pill.toNearestInt(), Justification::centred, 1, 0.7f);
    }
}

double BaxKnob::snap (double v, bool fine) const
{
    const double step = fine ? fineStep : coarseStep;
    if (step <= 0.0) return v;
    const double lo = getMinimum();
    return jlimit (getMinimum(), getMaximum(), lo + std::round ((v - lo) / step) * step);
}

void BaxKnob::setValueWithGesture (double v)
{
    if (onGestureBegin) onGestureBegin();
    setValue (jlimit (getMinimum(), getMaximum(), v), sendNotificationSync);
    if (onGestureEnd) onGestureEnd();
}

void BaxKnob::mouseDown (const MouseEvent& e)
{
    if (! isEnabled())
        return;
    dragging = true;
    lastDragY = e.position.y;
    dragValue = getValue();
    dragShift = e.mods.isShiftDown();
    if (onGestureBegin) onGestureBegin();
    repaint();
}

void BaxKnob::mouseDrag (const MouseEvent& e)
{
    if (! isEnabled() || ! dragging)
        return;

    const bool shift = e.mods.isShiftDown();
    if (shift != dragShift)
    {
        dragShift = shift;
        dragValue = getValue();
    }

    const double pixels = (double) (lastDragY - e.position.y);
    lastDragY = e.position.y;

    const double range = getMaximum() - getMinimum();
    dragValue += pixels * range / (shift && numDetents <= 1 ? 800.0 : 200.0);
    dragValue = jlimit (getMinimum(), getMaximum(), dragValue);

    const double snapped = snap (dragValue, shift);
    if (std::abs (snapped - getValue()) > 1.0e-9)
        setValue (snapped, sendNotificationSync);
}

void BaxKnob::mouseUp (const MouseEvent&)
{
    if (! dragging)
        return;
    dragging = false;
    if (onGestureEnd) onGestureEnd();
    repaint();
}

void BaxKnob::mouseDoubleClick (const MouseEvent&)
{
    if (isEnabled())
        setValueWithGesture (defaultValue);
}

void BaxKnob::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    if (! isEnabled())
        return;

    const float dir = (wheel.isReversed ? -1.0f : 1.0f) * (std::abs (wheel.deltaY) > 0.0f ? wheel.deltaY : wheel.deltaX);
    if (std::abs (dir) < 1.0e-6f)
        return;

    const bool fine = e.mods.isShiftDown();
    const double step = fine ? fineStep : coarseStep;
    double base = getValue();
    if (numDetents > 1)   // from a value between detents (e.g. 0.5) go to the neighbouring one
        base = (double) bax::detentFromNormalised (base) / (double) (numDetents - 1);
    setValueWithGesture (snap (base + (dir > 0.0f ? step : -step), fine));
}

void BaxKnob::mouseEnter (const MouseEvent&) { hovering = true;  repaint(); }
void BaxKnob::mouseExit (const MouseEvent&)  { hovering = false; repaint(); }

//==============================================================================
// KnobAttachment
//==============================================================================
KnobAttachment::KnobAttachment (RangedAudioParameter& p, BaxKnob& k)
    : knob (k),
      attachment (p, [this] (float v) { knob.setValue (v, dontSendNotification); knob.repaint(); })
{
    const auto range = p.getNormalisableRange();
    knob.setRange (range.start, range.end, 0.0);
    knob.setDefaultValue (p.convertFrom0to1 (p.getDefaultValue()));
    if (! knob.displayValue)
        knob.displayValue = [param = &p] (double v) { return param->getText (param->convertTo0to1 ((float) v), 32); };

    knob.onGestureBegin = [this] { attachment.beginGesture(); };
    knob.onGestureEnd   = [this] { attachment.endGesture(); };
    knob.onValueChange  = [this] { attachment.setValueAsPartOfGesture ((float) knob.getValue()); };

    attachment.sendInitialUpdate();
}

KnobAttachment::~KnobAttachment()
{
    knob.onGestureBegin = nullptr;
    knob.onGestureEnd = nullptr;
    knob.onValueChange = nullptr;
}

//==============================================================================
// LedButton
//==============================================================================
LedButton::LedButton (RangedAudioParameter& p, float on, const String& tooltip)
    : Button (p.getName (32)),
      attachment (p, [this] (float v)
      {
          setToggleState (std::abs (v - onValue) < 0.5f, dontSendNotification);
          repaint();
      }),
      onValue (on)
{
    setTooltip (tooltip);
    const float offValue = std::abs (on) < 0.5f ? p.getNormalisableRange().end : 0.0f;
    onClick = [this, offValue]
    {
        attachment.setValueAsCompleteGesture (getToggleState() ? offValue : onValue);
    };
    attachment.sendInitialUpdate();
}

LedButton::~LedButton() { onClick = nullptr; }

void LedButton::paintButton (Graphics& g, bool isHighlighted, bool isDown)
{
    const auto b = getLocalBounds().toFloat();
    const float d = std::min (b.getWidth(), b.getHeight()) - 4.0f;
    const auto bezel = Rectangle<float> (d, d).withCentre (b.getCentre());

    // chrome bezel
    Path ring;
    ring.addEllipse (bezel);
    DropShadow (Colours::black.withAlpha (0.7f), 6, { 0, 2 }).drawForPath (g, ring);
    g.setGradientFill (ColourGradient (Colour (0xffe7eaed), bezel.getX(), bezel.getY(),
                                       Colour (0xff5d646b), bezel.getRight(), bezel.getBottom(), false));
    g.fillEllipse (bezel);

    // black cap
    const auto cap = bezel.reduced (d * 0.12f);
    g.setGradientFill (ColourGradient (isHighlighted ? Colour (0xff3a3f45) : Colour (0xff2c3035), cap.getCentreX(), cap.getY(),
                                       Colour (0xff0d0f11), cap.getCentreX(), cap.getBottom(), false));
    g.fillEllipse (isDown ? cap.reduced (0.6f) : cap);
    g.setColour (Colours::black);
    g.drawEllipse (cap, 0.8f);

    // LED
    const auto led = cap.withSizeKeepingCentre (d * 0.38f, d * 0.38f);
    if (getToggleState())
    {
        g.setColour (BaxColours::blueGlow.withAlpha (0.35f));
        g.fillEllipse (led.expanded (d * 0.12f));
        g.setGradientFill (ColourGradient (Colour (0xffd8eeff), led.getCentreX(), led.getY(),
                                           BaxColours::blue, led.getCentreX(), led.getBottom(), false));
    }
    else
    {
        g.setGradientFill (ColourGradient (Colour (0xff2a3542), led.getCentreX(), led.getY(),
                                           Colour (0xff10161d), led.getCentreX(), led.getBottom(), false));
    }
    g.fillEllipse (led);
    g.setColour (Colours::black.withAlpha (0.8f));
    g.drawEllipse (led, 0.8f);
    g.setColour (Colours::white.withAlpha (getToggleState() ? 0.7f : 0.15f));
    g.fillEllipse (led.reduced (led.getWidth() * 0.3f).translated (-led.getWidth() * 0.12f, -led.getHeight() * 0.14f));
}

//==============================================================================
// MenuLabel
//==============================================================================
MenuLabel::MenuLabel (const String& t) : title (t)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void MenuLabel::refresh()
{
    const auto v = getValueText ? getValueText() : String();
    if (v != value)
    {
        value = v;
        repaint();
    }
}

void MenuLabel::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto font = CustomLookAndFeel::uiFont (12.0f, true);
    g.setFont (font);

    const float titleW = GlyphArrangement::getStringWidth (font, title);
    auto area = b;
    g.setColour (hover ? Colours::white : Colour (0xffdfe3e6));
    g.drawText (title, area.removeFromLeft (titleW + 2.0f), Justification::centredLeft);

    // arrow
    const float ax = area.getX() + 6.0f, ay = b.getCentreY();
    Path arrow;
    arrow.addTriangle (ax - 4.0f, ay - 2.5f, ax + 4.0f, ay - 2.5f, ax, ay + 3.0f);
    g.fillPath (arrow);
    area.removeFromLeft (14.0f);

    if (value.isNotEmpty())
    {
        g.setColour (BaxColours::blue);
        g.drawFittedText (value.toUpperCase(), area.toNearestInt(), Justification::centredLeft, 1, 0.8f);
    }
}

void MenuLabel::mouseDown (const MouseEvent&)
{
    if (! buildMenu)
        return;
    PopupMenu menu;
    buildMenu (menu);
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
}

//==============================================================================
// PillToggle
//==============================================================================
PillToggle::PillToggle (RangedAudioParameter& p, const String& buttonText, const String& tooltip)
    : Button (buttonText),
      attachment (p, [this] (float v) { setToggleState (v > 0.5f, dontSendNotification); repaint(); })
{
    setTooltip (tooltip);
    onClick = [this] { attachment.setValueAsCompleteGesture (getToggleState() ? 0.0f : 1.0f); };
    attachment.sendInitialUpdate();
}

PillToggle::~PillToggle() { onClick = nullptr; }

void PillToggle::paintButton (Graphics& g, bool isHighlighted, bool)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const float r = b.getHeight() * 0.5f;
    if (getToggleState())
    {
        g.setGradientFill (ColourGradient (Colour (0xff2f86e0), 0.0f, b.getY(), Colour (0xff15508f), 0.0f, b.getBottom(), false));
        g.fillRoundedRectangle (b, r);
    }
    g.setColour (getToggleState() ? BaxColours::blue : Colour (0xffb9bec3).withAlpha (isHighlighted ? 1.0f : 0.75f));
    g.drawRoundedRectangle (b, r, 1.2f);
    g.setColour (getToggleState() || isHighlighted ? Colours::white : Colour (0xffdfe3e6));
    g.setFont (CustomLookAndFeel::uiFont (11.0f, true));
    g.drawText (getButtonText().toUpperCase(), b, Justification::centred);
}

//==============================================================================
// MonitorSelector
//==============================================================================
MonitorSelector::MonitorSelector (RangedAudioParameter& p)
    : attachment (p, [this] (float v) { selected = jlimit (0, 3, (int) v); repaint(); })
{
    setTooltip (String::fromUTF8 ("Listen: LR = normal stereo, M = mid only, S = side only, \xce\x94 = only what the EQ adds or removes"));
    attachment.sendInitialUpdate();
}

Rectangle<float> MonitorSelector::segment (int i) const
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const float w = b.getWidth() / 4.0f;
    return { b.getX() + w * (float) i, b.getY(), w, b.getHeight() };
}

int MonitorSelector::indexAt (Point<float> p) const
{
    for (int i = 0; i < 4; ++i)
        if (segment (i).contains (p)) return i;
    return -1;
}

void MonitorSelector::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const float r = b.getHeight() * 0.5f;
    g.setColour (Colour (0xff0b0c0e));
    g.fillRoundedRectangle (b, r);

    static const char* labels[] = { "LR", "M", "S", "\xce\x94" };
    g.setFont (CustomLookAndFeel::uiFont (11.5f, true));
    for (int i = 0; i < 4; ++i)
    {
        auto s = segment (i);
        if (i == selected)
        {
            g.setGradientFill (ColourGradient (Colour (0xff2f86e0), 0.0f, s.getY(), Colour (0xff15508f), 0.0f, s.getBottom(), false));
            g.fillRoundedRectangle (s.reduced (1.0f), r - 1.0f);
        }
        g.setColour (i == selected || i == hoverIndex ? Colours::white : Colour (0xffb9bec3));
        g.drawText (String::fromUTF8 (labels[i]), s, Justification::centred);
    }
    g.setColour (Colour (0xffb9bec3).withAlpha (0.6f));
    g.drawRoundedRectangle (b, r, 1.0f);
}

void MonitorSelector::mouseDown (const MouseEvent& e)
{
    const int i = indexAt (e.position);
    if (i >= 0 && i != selected)
        attachment.setValueAsCompleteGesture ((float) i);
}

void MonitorSelector::mouseMove (const MouseEvent& e)
{
    const int i = indexAt (e.position);
    if (i != hoverIndex) { hoverIndex = i; repaint(); }
}
