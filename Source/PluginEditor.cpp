#include "PluginEditor.h"
#include "Presets.h"

using namespace juce;
using namespace bax;

namespace
{
    constexpr int kWidth = BaxPanel::kBaseWidth, kHeight = BaxPanel::kBaseHeight;
    constexpr int kTopStripH = 34, kBottomBarY = 414;
    constexpr float kStripY = 40.0f, kStripH = 250.0f, kStripW = 368.0f;
    constexpr float kRowY = 296.0f, kRowH = 112.0f;
}

//==============================================================================
EngageButton::EngageButton (RangedAudioParameter& p)
    : TextButton ("Engage"),
      attachment (p, [this] (float v)
      {
          const bool on = v < 0.5f;
          setToggleState (on, dontSendNotification);
          setButtonText (on ? "Engaged" : "Off");
      })
{
    setClickingTogglesState (false);
    onClick = [this]
    {
        attachment.setValueAsCompleteGesture (getToggleState() ? 1.0f : 0.0f);
    };
    attachment.sendInitialUpdate();
}

EngageButton::~EngageButton() { onClick = nullptr; }

//==============================================================================
BaxPanel::BaxPanel (BaxAudioProcessor& p) : processor (p)
{
    setLookAndFeel (&lnf);
    setOpaque (true);
    createBackground();

    for (int c = 0; c < 2; ++c)
        setupStrip (strips[c], c);

    auto& apvts = processor.apvts;

    // Output
    outputKnob.bipolar = true;
    outputKnob.coarseStep = 0.5;
    outputKnob.fineStep = 0.1;
    knobAttachments.push_back (std::make_unique<KnobAttachment> (*apvts.getParameter (ParamIDs::output), outputKnob));
    addAndMakeVisible (outputKnob);
    addAndMakeVisible (meterL);
    addAndMakeVisible (meterR);

    const std::tuple<TextButton*, const char*, const char*> buttons[] =
    {
        { &midSideButton, ParamIDs::midSide, "Mid/Side: the two EQ channels work on Mid and Side instead of Left and Right" },
        { &linkButton,    ParamIDs::link,    "Stereo link (Dual Mono view): the second channel follows the first channel's controls" },
    };
    for (auto& [button, id, tip] : buttons)
    {
        button->setClickingTogglesState (true);
        button->setTooltip (tip);
        buttonAttachments.push_back (std::make_unique<ButtonAttachment> (apvts, id, *button));
        addAndMakeVisible (*button);
    }

    // Menus
    viewBox.addItemList (viewNames(), 1);
    viewBox.setTooltip ("View: Classic = one control set for both channels, Classic Exp = Classic with the response display, Dual Mono = independent channels");
    oversamplingBox.addItemList (oversamplingNames(), 1);
    oversamplingBox.setTooltip ("Oversampling of the preamp stage (latency is the same for every setting)");
    for (auto* box : { &viewBox, &oversamplingBox })
    {
        box->setJustificationType (Justification::centredLeft);
        addAndMakeVisible (*box);
    }
    comboAttachments.push_back (std::make_unique<ComboAttachment> (apvts, ParamIDs::view, viewBox));
    comboAttachments.push_back (std::make_unique<ComboAttachment> (apvts, ParamIDs::oversampling, oversamplingBox));

    for (int i = 0; i < numFactoryPresets; ++i)
        presetBox.addItem (String (i + 1).paddedLeft ('0', 2) + "  " + factoryPresets[i].name, i + 1);
    presetBox.setTooltip ("Factory presets");
    presetBox.setJustificationType (Justification::centredLeft);
    presetBox.onChange = [this]
    {
        const int index = presetBox.getSelectedId() - 1;
        if (index >= 0 && index != processor.getCurrentProgram())
            processor.setCurrentProgram (index);
        lastShownProgram = index;
    };
    addAndMakeVisible (presetBox);

    addAndMakeVisible (graph);

    helpButton.setTooltip ("Help");
    helpButton.onClick = [this] { helpOverlay.setVisible (true); helpOverlay.toFront (false); };
    addAndMakeVisible (helpButton);
    addChildComponent (helpOverlay);

    setSize (kWidth, kHeight);   // last: resized() needs every component
    timerCallback();
    startTimerHz (30);
}

BaxPanel::~BaxPanel()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void BaxPanel::setupStrip (Strip& s, int channel)
{
    auto& apvts = processor.apvts;
    const auto& ids = kChannelIDs[channel];

    struct KnobSetup { BaxKnob& knob; const char* id; bool detented; };
    const KnobSetup knobs[] =
    {
        { s.lsGain, ids.lsGain, false }, { s.hsGain, ids.hsGain, false },
        { s.hp, ids.hp, true }, { s.lsFreq, ids.lsFreq, true }, { s.hsFreq, ids.hsFreq, true }, { s.lp, ids.lp, true },
    };
    for (auto& k : knobs)
    {
        if (k.detented)
        {
            k.knob.numDetents = kNumDetents;
            k.knob.coarseStep = k.knob.fineStep = 1.0 / (kNumDetents - 1);
        }
        else
        {
            k.knob.bipolar = true;
            k.knob.coarseStep = 0.5;   // hardware steps
            k.knob.fineStep = 0.1;
        }
        knobAttachments.push_back (std::make_unique<KnobAttachment> (*apvts.getParameter (k.id), k.knob));
        addAndMakeVisible (k.knob);
    }

    s.engage = std::make_unique<EngageButton> (*apvts.getParameter (ids.engage));
    s.engage->setTooltip ("Engage: off = true bypass of this channel (200 ms crossfade)");
    addAndMakeVisible (*s.engage);
    addAndMakeVisible (s.lowLcd);
    addAndMakeVisible (s.highLcd);

    Component* list[] = { &s.lsGain, &s.hsGain, &s.hp, &s.lsFreq, &s.hsFreq, &s.lp, s.engage.get() };
    std::copy (std::begin (list), std::end (list), s.controls);
}

//==============================================================================
void BaxPanel::layoutStrip (Strip& s, Rectangle<float> panel)
{
    s.bounds = panel;
    const int x0 = (int) panel.getX(), y0 = (int) panel.getY();
    const int lowCx = x0 + 92, highCx = x0 + 276;

    s.engage->setBounds (x0 + 10, y0 + 25, 74, 19);

    s.lsGain.setBounds (lowCx - 52, y0 + 40, 104, 118);
    s.hsGain.setBounds (highCx - 52, y0 + 40, 104, 118);
    s.lowLcd.setBounds (lowCx - 76, y0 + 160, 152, 22);
    s.highLcd.setBounds (highCx - 76, y0 + 160, 152, 22);

    s.hp.setBounds     (lowCx - 76, y0 + 186, 64, 60);
    s.lsFreq.setBounds (lowCx + 12, y0 + 186, 64, 60);
    s.hsFreq.setBounds (highCx - 76, y0 + 186, 64, 60);
    s.lp.setBounds     (highCx + 12, y0 + 186, 64, 60);
}

void BaxPanel::resized()
{
    // top strip
    viewBox.setBounds (262, 7, 112, 21);
    oversamplingBox.setBounds (410, 7, 62, 21);
    presetBox.setBounds (534, 7, 218, 21);

    layoutStrip (strips[0], { 8.0f, kStripY, kStripW, kStripH });
    layoutStrip (strips[1], { 8.0f + kStripW + 8.0f, kStripY, kStripW, kStripH });

    graphPanel  = { 8.0f, kRowY, 552.0f, kRowH };
    outputPanel = { 568.0f, kRowY, 184.0f, kRowH };
    graph.setBounds (graphPanel.reduced (8.0f, 0.0f).withTrimmedTop (24.0f).withTrimmedBottom (6.0f).toNearestInt());

    outputKnob.setBounds (576, 322, 66, 80);
    meterL.setBounds (646, 324, 9, 72);
    meterR.setBounds (658, 324, 9, 72);
    midSideButton.setBounds (678, 330, 66, 22);
    linkButton.setBounds (678, 366, 66, 22);

    helpButton.setBounds (kWidth - 26, kBottomBarY + 6, 20, 20);
    helpOverlay.setBounds (getLocalBounds());
}

//==============================================================================
void BaxPanel::setActive (Component& c, bool active, const String& hint)
{
    if (c.isEnabled() != active)
    {
        c.setEnabled (active);
        c.setAlpha (active ? 1.0f : 0.35f);
    }
    if (auto* k = dynamic_cast<BaxKnob*> (&c))
        k->inactiveHint = hint;
}

void BaxPanel::updateViewState()
{
    const auto view = processor.getView();
    const bool classic = view != View::DualMono;
    const bool linked = processor.isLinked();
    const bool ms = processor.apvts.getRawParameterValue (ParamIDs::midSide)->load() > 0.5f;

    // Classic views: one control set; the second strip is replaced by a plate.
    for (auto* c : strips[1].controls)
        c->setVisible (! classic);
    strips[1].lowLcd.setVisible (! classic);
    strips[1].highLcd.setVisible (! classic);

    for (auto* c : strips[1].controls)
        setActive (*c, ! linked, "Linked: this channel follows the first channel");
    strips[1].lowLcd.setDimmed (linked);
    strips[1].highLcd.setDimmed (linked);

    setActive (linkButton, ! classic);
    graph.setVisible (view != View::Classic);

    if (view != currentView || linked != linkedShown || ms != midSideShown)
    {
        currentView = view;
        linkedShown = linked;
        midSideShown = ms;
        repaint();
    }
}

void BaxPanel::timerCallback()
{
    updateViewState();

    const auto settings = processor.readSettings();
    for (int c = 0; c < 2; ++c)
    {
        const auto& cs = c == 0 ? settings.a : settings.b;
        auto& s = strips[c];
        s.lowLcd.setContent ("LOW " + detentText (FreqKind::LS, cs.lsFreq), formatGainShort (cs.lsGainDb) + " dB");
        s.highLcd.setContent ("HIGH " + detentText (FreqKind::HS, cs.hsFreq), formatGainShort (cs.hsGainDb) + " dB");
        const bool engaged = c == 0 ? settings.engagedA : settings.engagedB;
        if (! linkedShown || c == 0)
        {
            s.lowLcd.setDimmed (! engaged);
            s.highLcd.setDimmed (! engaged);
        }
    }

    ResponseGraph::State gs;
    gs.a = settings.a;
    gs.b = settings.b;
    gs.engagedA = settings.engagedA;
    gs.engagedB = settings.engagedB;
    gs.midSide = settings.midSide;
    gs.linked = processor.isLinked();
    gs.outputDb = settings.outputDb;
    gs.sampleRate = processor.displaySampleRate.load();
    graph.setState (gs);

    meterL.setLevel (processor.outputPeak[0].load());
    meterR.setLevel (processor.outputPeak[1].load());

    const int program = processor.getCurrentProgram();
    if (program != lastShownProgram)
    {
        lastShownProgram = program;
        presetBox.setSelectedId (program + 1, dontSendNotification);
    }
}

//==============================================================================
void BaxPanel::createBackground()
{
    // Rendered at 2x so it stays sharp when the window is enlarged.
    background = Image (Image::RGB, kWidth * 2, kHeight * 2, true);
    Graphics g (background);
    g.addTransform (AffineTransform::scale (2.0f));

    g.setGradientFill (ColourGradient (Colour (0xff3b3e42), 0.0f, 0.0f, Colour (0xff1e2023), 0.0f, (float) kHeight, false));
    g.fillAll();

    // brushed metal: faint full-width lines plus random short streaks
    Random rng (0xba5e);
    for (int y = 0; y < kHeight; ++y)
    {
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (rng.nextFloat() * 0.035f));
        g.fillRect (0, y, kWidth, 1);
    }
    for (int i = 0; i < 2200; ++i)
    {
        const int x = rng.nextInt (kWidth), y = rng.nextInt (kHeight), len = 20 + rng.nextInt (140);
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (0.015f + rng.nextFloat() * 0.03f));
        g.fillRect (x, y, len, 1);
    }

    ColourGradient vignette (Colours::transparentBlack, kWidth * 0.5f, kHeight * 0.45f,
                             Colours::black.withAlpha (0.35f), 0.0f, 0.0f, true);
    g.setGradientFill (vignette);
    g.fillAll();
}

void BaxPanel::drawWordmark (Graphics& g, Rectangle<float> area)
{
    // Stylised lowercase wordmark: chrome "baxr", blue "eq", and a swoosh.
    const float baseline = area.getBottom() - 7.0f;

    GlyphArrangement main;
    main.addLineOfText (Font (FontOptions (27.0f, Font::bold | Font::italic)), "baxr", area.getX(), baseline);
    Path mainPath;
    main.createPath (mainPath);
    const auto mainBounds = mainPath.getBounds();

    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillPath (mainPath, AffineTransform::translation (1.0f, 1.5f));
    ColourGradient chrome (Colour (0xffffffff), 0.0f, mainBounds.getY(), Colour (0xff8c949c), 0.0f, mainBounds.getBottom(), false);
    chrome.addColour (0.5, Colour (0xffd5dade));
    chrome.addColour (0.55, Colour (0xffa4acb3));
    g.setGradientFill (chrome);
    g.fillPath (mainPath);

    GlyphArrangement second;
    second.addLineOfText (Font (FontOptions (19.0f, Font::plain)), "eq", mainBounds.getRight() + 6.0f, baseline);
    Path secondPath;
    second.createPath (secondPath);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillPath (secondPath, AffineTransform::translation (1.0f, 1.0f));
    g.setColour (BaxColours::blue);
    g.fillPath (secondPath);

    Path swoosh;
    const float sx = mainBounds.getX() + 2.0f, sy = baseline + 3.5f;
    const float ex = secondPath.getBounds().getRight();
    swoosh.startNewSubPath (sx, sy);
    swoosh.quadraticTo ((sx + ex) * 0.5f, sy + 3.5f, ex + 6.0f, sy - 2.0f);
    g.setGradientFill (ColourGradient (BaxColours::blue.withAlpha (0.0f), sx, sy, BaxColours::blue, ex, sy, false));
    g.strokePath (swoosh, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

void BaxPanel::paint (Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat());

    // --- top strip ------------------------------------------------------------------
    auto top = Rectangle<float> (0.0f, 0.0f, (float) kWidth, (float) kTopStripH);
    g.setGradientFill (ColourGradient (Colour (0xff4a4e53), 0.0f, 0.0f, Colour (0xff2b2e32), 0.0f, top.getBottom(), false));
    g.fillRect (top);
    g.setColour (Colours::white.withAlpha (0.18f));
    g.drawHorizontalLine (0, 0.0f, (float) kWidth);
    g.setColour (BaxColours::bevelDark);
    g.drawHorizontalLine (kTopStripH - 1, 0.0f, (float) kWidth);
    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (kTopStripH, 0.0f, (float) kWidth);

    drawWordmark (g, { 14.0f, 0.0f, 200.0f, (float) kTopStripH });
    CustomLookAndFeel::drawSmallLabel (g, "View",   { 214.0f, 8.0f, 44.0f, 19.0f }, Justification::centredRight);
    CustomLookAndFeel::drawSmallLabel (g, "OS",     { 378.0f, 8.0f, 28.0f, 19.0f }, Justification::centredRight);
    CustomLookAndFeel::drawSmallLabel (g, "Preset", { 484.0f, 8.0f, 46.0f, 19.0f }, Justification::centredRight);

    // --- channel strips ---------------------------------------------------------------
    const bool classic = currentView != View::DualMono;
    const String nameA = midSideShown ? "Mid" : (classic ? "Stereo" : "Left");
    const String nameB = midSideShown ? "Side" : "Right";

    for (int c = 0; c < 2; ++c)
    {
        const auto& s = strips[c];
        const String title = c == 0 ? (classic && midSideShown ? "Mid + Side" : nameA)
                                    : (classic ? "Classic View" : nameB + (linkedShown ? "  (linked)" : ""));
        CustomLookAndFeel::drawPanel (g, s.bounds, title);

        if (c == 1 && classic)
        {
            // plate shown in place of the second strip
            auto plate = s.bounds.reduced (20.0f, 0.0f).withTrimmedTop (40.0f).withTrimmedBottom (24.0f);
            CustomLookAndFeel::drawInsetBox (g, plate, 4.0f);
            g.setFont (CustomLookAndFeel::uiFont (30.0f, true));
            g.setColour (Colour (0xffc8cdd2).withAlpha (0.9f));
            g.drawText ("BAXR", plate.removeFromTop (plate.getHeight() * 0.55f), Justification::centredBottom);
            g.setFont (CustomLookAndFeel::uiFont (11.0f, true));
            g.setColour (BaxColours::labelText);
            g.drawText ("ONE CONTROL SET DRIVES BOTH CHANNELS", plate.removeFromTop (22.0f), Justification::centred);
            g.setColour (BaxColours::labelText.withAlpha (0.6f));
            g.drawText ("Choose Dual Mono for independent channels", plate.removeFromTop (18.0f), Justification::centred);
            continue;
        }

        // divider between the low and the high section
        const float dx = s.bounds.getX() + s.bounds.getWidth() * 0.5f;
        g.setColour (BaxColours::bevelDark);
        g.drawVerticalLine ((int) dx, s.bounds.getY() + 50.0f, s.bounds.getBottom() - 10.0f);
        g.setColour (Colours::white.withAlpha (0.07f));
        g.drawVerticalLine ((int) dx + 1, s.bounds.getY() + 50.0f, s.bounds.getBottom() - 10.0f);
    }

    // --- bottom row --------------------------------------------------------------------
    CustomLookAndFeel::drawPanel (g, graphPanel, currentView == View::Classic ? "Baxandall Equalizer" : "Response");
    if (currentView == View::Classic)
    {
        auto plate = graphPanel.reduced (8.0f, 0.0f).withTrimmedTop (24.0f).withTrimmedBottom (6.0f);
        CustomLookAndFeel::drawInsetBox (g, plate, 3.0f);
        g.setFont (CustomLookAndFeel::uiFont (12.0f, true));
        g.setColour (BaxColours::labelText);
        g.drawText ("CLASSIC VIEW  -  choose Classic Exp or Dual Mono for the response display", plate, Justification::centred);
    }

    CustomLookAndFeel::drawPanel (g, outputPanel, "Output");
    CustomLookAndFeel::drawSmallLabel (g, "L  R", { 642.0f, 398.0f, 30.0f, 10.0f }, Justification::centred);
    CustomLookAndFeel::drawSmallLabel (g, "Mode",   { 678.0f, 318.0f, 66.0f, 10.0f }, Justification::centredLeft);
    CustomLookAndFeel::drawSmallLabel (g, "Stereo", { 678.0f, 354.0f, 66.0f, 10.0f }, Justification::centredLeft);

    // --- bottom bar --------------------------------------------------------------------
    auto bar = Rectangle<float> (0.0f, (float) kBottomBarY, (float) kWidth, (float) (kHeight - kBottomBarY));
    g.setGradientFill (ColourGradient (Colour (0xff17191b), 0.0f, bar.getY(), Colour (0xff0c0d0e), 0.0f, bar.getBottom(), false));
    g.fillRect (bar);
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawHorizontalLine (kBottomBarY, 0.0f, (float) kWidth);

    auto tab = Rectangle<float> (8.0f, bar.getY() + 7.0f, 52.0f, 18.0f);
    g.setGradientFill (ColourGradient (BaxColours::headerTop, 0.0f, tab.getY(), BaxColours::headerBottom, 0.0f, tab.getBottom(), false));
    g.fillRoundedRectangle (tab, 3.0f);
    g.setColour (BaxColours::bevelDark);
    g.drawRoundedRectangle (tab, 3.0f, 1.0f);
    g.setColour (BaxColours::headerText);
    g.setFont (CustomLookAndFeel::uiFont (10.5f, true));
    g.drawText ("EQ", tab, Justification::centred);

    auto inset = bar.reduced (70.0f, 7.0f);
    CustomLookAndFeel::drawInsetBox (g, inset, 3.0f);
    g.setFont (CustomLookAndFeel::uiFont (13.0f, true));
    g.setColour (Colour (0xffc8cdd2));
    g.drawText ("BAXR  BAXANDALL EQ", inset, Justification::centred);
}

//==============================================================================
BaxAudioProcessorEditor::BaxAudioProcessorEditor (BaxAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), panel (p)
{
    const int savedWidth = processor.editorWidth.load();
    addAndMakeVisible (panel);

    setResizable (true, true);
    setResizeLimits (kWidth * 6 / 10, kHeight * 6 / 10, kWidth * 5 / 2, kHeight * 5 / 2);
    getConstrainer()->setFixedAspectRatio ((double) kWidth / (double) kHeight);

    const int width = savedWidth > 0 ? jlimit (kWidth * 6 / 10, kWidth * 5 / 2, savedWidth) : kWidth;
    setSize (width, roundToInt (width * (double) kHeight / (double) kWidth));
    sizeRestored = true;
}

void BaxAudioProcessorEditor::resized()
{
    panel.setBounds (0, 0, kWidth, kHeight);
    panel.setTransform (AffineTransform::scale ((float) getWidth() / (float) kWidth));

    if (sizeRestored)
        processor.editorWidth.store (getWidth());
}
