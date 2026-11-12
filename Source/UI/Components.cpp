#include "Components.h"
#include "../CustomLookAndFeel.h"

using namespace juce;

//==============================================================================
// BaxKnob
//==============================================================================
BaxKnob::BaxKnob (Size s, const String& labelText)
    : Slider (Slider::RotaryVerticalDrag, Slider::NoTextBox), knobSize (s), label (labelText)
{
    setRange (0.0, 1.0, 0.0);
    setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    setName (labelText);
}

String BaxKnob::valueText() const
{
    return displayValue ? displayValue (getValue()) : String (getValue(), 2);
}

String BaxKnob::getTooltip()
{
    String text = label.toUpperCase() + "  " + valueText();
    if (! isEnabled() && inactiveHint.isNotEmpty())
        text << "\n" << inactiveHint;
    return text;
}

void BaxKnob::paint (Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    float pos = (float) valueToProportionOfLength (getValue());
    if (numDetents > 1)   // draw the pointer on the detent actually in use
        pos = (float) bax::detentFromNormalised (pos) / (float) (numDetents - 1);
    const auto rotary = getRotaryParameters();

    auto labelArea = bounds.removeFromBottom (14.0f);
    const float side = std::min (bounds.getWidth(), bounds.getHeight());
    const auto knobArea = bounds.withSizeKeepingCentre (side, side);

    if ((hovering || dragging) && isEnabled())
    {
        g.setFont (CustomLookAndFeel::uiFont (11.0f, true));
        g.setColour (BaxColours::blue);
        g.drawFittedText (valueText(), labelArea.toNearestInt(), Justification::centred, 1, 0.7f);
    }
    else
    {
        CustomLookAndFeel::drawSmallLabel (g, label, labelArea);
    }

    getLookAndFeel().drawRotarySlider (g, (int) knobArea.getX(), (int) knobArea.getY(),
                                       (int) knobArea.getWidth(), (int) knobArea.getHeight(),
                                       pos, rotary.startAngleRadians, rotary.endAngleRadians, *this);
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
    // from a value between steps (e.g. the 0.5 shelf default) go to the neighbouring step
    double base = getValue();
    if (numDetents > 1)
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
        knob.displayValue = [&p] (double v) { return p.getText (p.convertTo0to1 ((float) v), 32); };

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
// LcdDisplay
//==============================================================================
void LcdDisplay::setContent (const String& name, const String& value)
{
    if (name != nameText || value != valueText)
    {
        nameText = name;
        valueText = value;
        repaint();
    }
}

void LcdDisplay::setDimmed (bool shouldDim)
{
    if (dimmed != shouldDim)
    {
        dimmed = shouldDim;
        repaint();
    }
}

void LcdDisplay::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    CustomLookAndFeel::drawLcd (g, b, dimmed);

    const auto textColour = dimmed ? Colour (0xff8a9097) : Colours::white;
    auto textArea = b.reduced (6.0f, 0.0f);
    const auto font = CustomLookAndFeel::uiFont (11.0f, true);
    g.setFont (font);

    const float valueWidth = GlyphArrangement::getStringWidth (font, valueText) + 4.0f;
    auto nameArea = textArea.withTrimmedRight (valueWidth).toNearestInt();

    g.setColour (Colours::black.withAlpha (0.35f));
    g.drawFittedText (nameText, nameArea.translated (0, 1), Justification::centredLeft, 1, 0.75f);
    g.drawText (valueText, textArea.translated (0.0f, 1.0f), Justification::centredRight);
    g.setColour (textColour);
    g.drawFittedText (nameText, nameArea, Justification::centredLeft, 1, 0.75f);
    g.drawText (valueText, textArea, Justification::centredRight);
}

//==============================================================================
// ResponseGraph
//==============================================================================
bool ResponseGraph::State::operator== (const State& o) const
{
    auto same = [] (const bax::ChannelSettings& x, const bax::ChannelSettings& y)
    {
        return x.sameDetents (y) && bax::sameValue (x.lsGainDb, y.lsGainDb) && bax::sameValue (x.hsGainDb, y.hsGainDb);
    };
    return same (a, o.a) && same (b, o.b) && engagedA == o.engagedA && engagedB == o.engagedB
        && midSide == o.midSide && linked == o.linked && bax::sameValue (outputDb, o.outputDb)
        && bax::sameValue (sampleRate, o.sampleRate);
}

void ResponseGraph::setState (const State& s)
{
    if (hasState && s == state)
        return;
    state = s;
    hasState = true;
    table.build (state.sampleRate);
    repaint();
}

void ResponseGraph::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    CustomLookAndFeel::drawLcd (g, b, false);
    auto plot = b.reduced (8.0f, 6.0f).withTrimmedLeft (18.0f).withTrimmedBottom (10.0f);

    constexpr double fMin = 20.0, fMax = 20000.0, dbRange = 9.0;
    auto xOf = [&] (double f) { return plot.getX() + plot.getWidth() * (float) (std::log (f / fMin) / std::log (fMax / fMin)); };
    auto yOf = [&] (double db) { return plot.getCentreY() - plot.getHeight() * 0.5f * (float) (jlimit (-dbRange, dbRange, db) / dbRange); };

    // grid
    g.setFont (CustomLookAndFeel::uiFont (9.0f, true));
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = xOf (f);
        g.setColour (Colours::white.withAlpha (bax::sameValue (f, 100.0) || bax::sameValue (f, 1000.0) || bax::sameValue (f, 10000.0) ? 0.16f : 0.08f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (Colours::white.withAlpha (0.55f));
        const String label = f >= 1000.0 ? String ((int) (f / 1000.0)) + "k" : String ((int) f);
        g.drawText (label, Rectangle<float> (x - 15.0f, plot.getBottom() + 1.0f, 30.0f, 10.0f), Justification::centred);
    }
    for (double db : { -8.0, -4.0, 0.0, 4.0, 8.0 })
    {
        const float y = yOf (db);
        g.setColour (Colours::white.withAlpha (bax::sameValue (db, 0.0) ? 0.25f : 0.08f));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (Colours::white.withAlpha (0.55f));
        g.drawText (db > 0.0 ? "+" + String ((int) db) : String ((int) db), Rectangle<float> (b.getX() + 2.0f, y - 5.0f, 22.0f, 10.0f),
                    Justification::centredRight);
    }

    if (! hasState)
        return;

    g.saveState();
    g.reduceClipRegion (plot.expanded (0.0f, 3.0f).toNearestInt());

    auto curve = [&] (const bax::ChannelSettings& s, bool engaged)
    {
        Path p;
        for (int i = 0; i <= 240; ++i)
        {
            const double f = fMin * std::pow (fMax / fMin, i / 240.0);
            double db = state.outputDb;
            if (engaged) db += bax::EqChannel::responseDb (s, state.sampleRate, table, f);
            const float x = xOf (f), y = yOf (db);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };

    const Colour colA (0xffffffff), colB (0xffffc35a);
    auto drawCurve = [&] (const Path& p, Colour c)
    {
        g.setColour (c.withAlpha (0.25f));
        g.strokePath (p, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (c);
        g.strokePath (p, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
    };

    const String nameA = state.midSide ? "MID" : "LEFT", nameB = state.midSide ? "SIDE" : "RIGHT";
    const bool single = state.linked && state.engagedA == state.engagedB;
    if (! single)
        drawCurve (curve (state.b, state.engagedB), colB);
    drawCurve (curve (state.a, state.engagedA), colA);
    g.restoreState();

    // legend
    g.setFont (CustomLookAndFeel::uiFont (10.0f, true));
    auto legend = Rectangle<float> (plot.getRight() - 120.0f, plot.getY() + 2.0f, 118.0f, 12.0f);
    g.setColour (colA);
    g.drawText (single ? nameA + " + " + nameB : nameA + (state.engagedA ? "" : " (OFF)"), legend, Justification::centredRight);
    if (! single)
    {
        g.setColour (colB);
        g.drawText (nameB + (state.engagedB ? "" : " (OFF)"), legend.translated (0.0f, 12.0f), Justification::centredRight);
    }
}

//==============================================================================
// LevelMeter
//==============================================================================
void LevelMeter::setLevel (float linearPeak)
{
    const float db = linearPeak > 1.0e-6f ? 20.0f * std::log10 (linearPeak) : -100.0f;
    if (std::abs (db - levelDb) > 0.25f)
    {
        levelDb = db;
        repaint();
    }
}

void LevelMeter::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    CustomLookAndFeel::drawInsetBox (g, b, 2.0f);
    auto inner = b.reduced (2.0f);

    constexpr float lo = -48.0f, hi = 6.0f;
    const float norm = jlimit (0.0f, 1.0f, (levelDb - lo) / (hi - lo));
    if (norm > 0.0f)
    {
        auto bar = inner.withTrimmedTop (inner.getHeight() * (1.0f - norm));
        const float zeroY = inner.getBottom() - inner.getHeight() * (-lo / (hi - lo));
        g.setGradientFill (ColourGradient (BaxColours::blue, 0.0f, inner.getBottom(), Colour (0xff9fd4ff), 0.0f, zeroY, false));
        g.fillRect (bar.withTop (std::max (bar.getY(), zeroY)));
        if (bar.getY() < zeroY)
        {
            g.setColour (BaxColours::red);
            g.fillRect (bar.withBottom (zeroY));
        }
    }
    // 0 dB mark
    g.setColour (Colours::white.withAlpha (0.35f));
    g.drawHorizontalLine ((int) (inner.getBottom() - inner.getHeight() * (-lo / (hi - lo))), inner.getX(), inner.getRight());
}

//==============================================================================
// Help
//==============================================================================
void HelpButton::paintButton (Graphics& g, bool isHighlighted, bool isDown)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillEllipse (b.translated (0.0f, 1.0f));
    g.setGradientFill (ColourGradient (isHighlighted ? Colour (0xff8a9096) : Colour (0xff6f757b), 0.0f, b.getY(),
                                       Colour (0xff35393d), 0.0f, b.getBottom(), false));
    g.fillEllipse (isDown ? b.reduced (0.5f) : b);
    g.setColour (BaxColours::bevelDark);
    g.drawEllipse (b, 1.0f);
    g.setColour (Colours::white);
    g.setFont (CustomLookAndFeel::uiFont (12.0f, true));
    g.drawText ("?", b, Justification::centred);
}

void HelpOverlay::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.72f));

    auto box = getLocalBounds().toFloat().reduced (40.0f, 30.0f);
    CustomLookAndFeel::drawPanel (g, box, "BAXR - Quick Help");

    const char* lines[] =
    {
        "LOW / HIGH:  broad Baxandall shelves, -5 .. +5 dB (0.5 dB steps, Shift = 0.1 dB)",
        "LOW FREQ / HIGH FREQ:  8-position shelf frequency selectors",
        "HP / LP:  12 dB/oct high-pass and low-pass filters, 8 positions each (first = Off)",
        "ENGAGE:  true bypass of that channel, with a 200 ms crossfade",
        "VIEW:  Classic = one control set for both channels.  Classic Exp = Classic plus",
        "           the response display.  Dual Mono = independent left / right (or mid / side)",
        "M/S:  the two channels work on Mid and Side instead of Left and Right",
        "LINK:  in Dual Mono, the right (side) channel follows the left (mid) controls",
        "OVERSAMPLING:  1X / 2X / 4X for the preamp stage. Latency stays at 47 samples",
        "OUTPUT:  -10 .. +10 dB.  Meters show the output peak level per channel",
        "Knobs: drag, Shift = fine, scroll wheel, double-click = default. Drag the corner to resize.",
        "",
        "Click anywhere to close."
    };

    auto text = box.reduced (16.0f, 0.0f).withTrimmedTop (30.0f);
    g.setFont (CustomLookAndFeel::uiFont (12.0f));
    for (auto* line : lines)
    {
        g.setColour (String (line).startsWith ("Click") ? BaxColours::blue : Colour (0xffdfe3e6));
        g.drawFittedText (line, text.removeFromTop (21.0f).toNearestInt(), Justification::centredLeft, 1, 0.8f);
    }
}
