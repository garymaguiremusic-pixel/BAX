#include "PluginEditor.h"
#include "Presets.h"

using namespace juce;
using namespace bax;

namespace
{
    constexpr int kWidth = BaxPanel::kBaseWidth;
    constexpr float kTopBarH = 24.0f, kRackH = 126.0f, kRackGap = 4.0f;
    constexpr float kFaceX = 72.0f;                    // faceplate inside the rack ears
    constexpr float kKnobRowY = 64.0f;                 // knob centre, relative to the rack
    constexpr float kSelectorD = 50.0f, kLevelD = 76.0f;

    // knob columns (x centres), as on the hardware layout
    constexpr float xCutLow = 220.0f, xShelfLow = 322.0f;
    constexpr float xLowL = 428.0f, xLowR = 536.0f, xHighL = 656.0f, xHighR = 764.0f;
    constexpr float xShelfHigh = 872.0f, xCutHigh = 978.0f, xEngage = 1070.0f;

    String scaleText (FreqKind kind, int d)
    {
        const double hz = freqTable (kind)[d];
        if (hz <= 0.0) return "OUT";
        if (kind == FreqKind::HS || kind == FreqKind::LP)
            return String (hz / 1000.0, std::abs (hz / 1000.0 - std::round (hz / 1000.0)) < 1.0e-6 ? 0 : 1);
        return String (roundToInt (hz));
    }
}

float BaxPanel::rackY (int index)
{
    return kTopBarH + kRackGap + (float) index * (kRackH + kRackGap);
}

int BaxPanel::baseHeightFor (View v)
{
    const int racks = v == View::Classic ? 1 : (v == View::ClassicExp ? 2 : 3);
    return (int) (rackY (racks) - kRackGap + kRackGap);
}

//==============================================================================
BaxPanel::BaxPanel (BaxAudioProcessor& p) : processor (p)
{
    setLookAndFeel (&lnf);
    setOpaque (true);
    createBackground();

    auto& apvts = processor.apvts;
    for (int c = 0; c < 2; ++c)
        setupStrip (strips[c], c);

    // bottom rack
    outputKnob.bipolar = true;
    outputKnob.coarseStep = 0.5;
    outputKnob.fineStep = 0.1;
    knobAttachments.push_back (std::make_unique<KnobAttachment> (*apvts.getParameter (ParamIDs::output), outputKnob));
    addAndMakeVisible (outputKnob);

    linkLed = std::make_unique<LedButton> (*apvts.getParameter (ParamIDs::link), 1.0f,
        "Stereo link: in Classic views the left and right levels move together; in Dual Mono the right channel follows the left");
    midSideLed = std::make_unique<LedButton> (*apvts.getParameter (ParamIDs::midSide), 1.0f,
        "Mid/Side: the two channels work on Mid and Side instead of Left and Right");
    addAndMakeVisible (*linkLed);
    addAndMakeVisible (*midSideLed);

    // top bar
    bypassButton = std::make_unique<PillToggle> (*apvts.getParameter (ParamIDs::bypass), "Bypass",
                                                 "Bypass the whole plugin (true bypass, 200 ms crossfade, latency kept)");
    addAndMakeVisible (*bypassButton);
    monitor = std::make_unique<MonitorSelector> (*apvts.getParameter (ParamIDs::monitor));
    addAndMakeVisible (*monitor);

    sizeMenu.setTooltip ("Window size (or drag the bottom-right corner)");
    sizeMenu.buildMenu = [this] (PopupMenu& m)
    {
        for (int pct : { 50, 75, 100, 125, 150, 200 })
            m.addItem (String (pct) + " %", [this, pct] { if (onScaleRequested) onScaleRequested ((float) pct / 100.0f); });
    };

    auto choiceMenu = [&apvts] (const char* id)
    {
        return [&apvts, id] (PopupMenu& m)
        {
            auto* param = dynamic_cast<AudioParameterChoice*> (apvts.getParameter (id));
            for (int i = 0; i < param->choices.size(); ++i)
                m.addItem (param->choices[i], true, i == param->getIndex(), [param, i]
                {
                    param->beginChangeGesture();
                    param->setValueNotifyingHost (param->convertTo0to1 ((float) i));
                    param->endChangeGesture();
                });
        };
    };
    auto choiceText = [&apvts] (const char* id)
    {
        return [&apvts, id] { return dynamic_cast<AudioParameterChoice*> (apvts.getParameter (id))->getCurrentChoiceName(); };
    };
    oversamplingMenu.setTooltip ("Oversampling of the preamp stage. Latency is the same for every setting");
    oversamplingMenu.buildMenu = choiceMenu (ParamIDs::oversampling);
    oversamplingMenu.getValueText = choiceText (ParamIDs::oversampling);
    viewMenu.setTooltip ("Classic: one rack.  Classic Exp: adds Stereo Link, Mid/Side and Output.  Dual Mono: a full rack per channel");
    viewMenu.buildMenu = choiceMenu (ParamIDs::view);
    viewMenu.getValueText = choiceText (ParamIDs::view);
    presetMenu.setTooltip ("Factory presets");
    presetMenu.buildMenu = [this] (PopupMenu& m)
    {
        for (int i = 0; i < numFactoryPresets; ++i)
            m.addItem (factoryPresets[i].name, true, i == processor.getCurrentProgram(),
                       [this, i] { processor.setCurrentProgram (i); });
    };
    presetMenu.getValueText = [this] { return processor.getProgramName (processor.getCurrentProgram()); };
    for (auto* m : { &sizeMenu, &oversamplingMenu, &viewMenu, &presetMenu })
        addAndMakeVisible (*m);

    // parameter pairs kept in step by the link logic
    for (int k = 0; k < 7; ++k)
    {
        const char* l[] = { kChannelIDs[0].hp, kChannelIDs[0].lsFreq, kChannelIDs[0].hsFreq, kChannelIDs[0].lp,
                            kChannelIDs[0].engage, kChannelIDs[0].lsGain, kChannelIDs[0].hsGain };
        const char* r[] = { kChannelIDs[1].hp, kChannelIDs[1].lsFreq, kChannelIDs[1].hsFreq, kChannelIDs[1].lp,
                            kChannelIDs[1].engage, kChannelIDs[1].lsGain, kChannelIDs[1].hsGain };
        SyncPair pair { apvts.getParameter (l[k]), apvts.getParameter (r[k]) };
        (k < 4 ? freqPairs : (k == 4 ? enginePairs : gainPairs)).push_back (pair);
    }

    shownView = processor.getView();
    shownMidSide = apvts.getRawParameterValue (ParamIDs::midSide)->load() > 0.5f;
    setSize (kWidth, getBaseHeight());   // last: resized() needs every component
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

    for (auto* k : { &s.hp, &s.lsFreq, &s.hsFreq, &s.lp })
    {
        k->numDetents = kNumDetents;
        k->coarseStep = k->fineStep = 1.0 / (kNumDetents - 1);
    }
    for (auto* k : { &s.lsGain, &s.hsGain })
    {
        k->bipolar = true;
        k->coarseStep = 0.5;   // hardware steps
        k->fineStep = 0.1;
    }

    const std::pair<BaxKnob*, const char*> knobs[] =
    {
        { &s.hp, ids.hp }, { &s.lsFreq, ids.lsFreq }, { &s.lsGain, ids.lsGain },
        { &s.hsGain, ids.hsGain }, { &s.hsFreq, ids.hsFreq }, { &s.lp, ids.lp },
    };
    for (auto& [knob, id] : knobs)
    {
        knobAttachments.push_back (std::make_unique<KnobAttachment> (*apvts.getParameter (id), *knob));
        addAndMakeVisible (*knob);
    }

    s.engage = std::make_unique<LedButton> (*apvts.getParameter (ids.engage), 0.0f,
                                            "Engage: off = true bypass (200 ms crossfade)");
    addAndMakeVisible (*s.engage);
}

//==============================================================================
void BaxPanel::placeSelector (BaxKnob& k, float cx, float cy, FreqKind kind)
{
    k.setVisible (true);
    k.setBounds (roundToInt (cx - kSelectorD * 0.5f), roundToInt (cy - kSelectorD * 0.5f),
                 (int) kSelectorD, (int) kSelectorD + BaxKnob::kBubbleHeight);
    Scale s;
    s.centre = { cx, cy };
    s.radius = kSelectorD * 0.5f + 11.0f;
    for (int d = 0; d < kNumDetents; ++d)
    {
        s.labels.add (scaleText (kind, d));
        s.positions.add ((float) d / (float) (kNumDetents - 1));
    }
    scales.push_back (s);
}

void BaxPanel::placeLevel (BaxKnob& k, float cx, float cy)
{
    k.setVisible (true);
    k.setBounds (roundToInt (cx - kLevelD * 0.5f), roundToInt (cy - kLevelD * 0.5f),
                 (int) kLevelD, (int) kLevelD + BaxKnob::kBubbleHeight);
    Scale s;
    s.centre = { cx, cy };
    s.radius = kLevelD * 0.5f + 8.0f;
    for (float v : { -5.0f, -3.0f, 3.0f, 5.0f })
    {
        s.labels.add (v > 0.0f ? "+" + String ((int) v) : String ((int) v));
        s.positions.add ((v + 5.0f) / 10.0f);
    }
    scales.push_back (s);
}

void BaxPanel::layoutForView()
{
    scales.clear();
    captions.clear();
    logoSubtitles.clear();

    const bool dual = shownView == View::DualMono;
    numRacks = shownView == View::Classic ? 1 : (shownView == View::ClassicExp ? 2 : 3);
    const String nameL = shownMidSide ? "MID" : "LEFT", nameR = shownMidSide ? "SIDE" : "RIGHT";

    for (auto& s : strips)
        for (Component* c : { (Component*) &s.hp, (Component*) &s.lsFreq, (Component*) &s.lsGain, (Component*) &s.hsGain,
                              (Component*) &s.hsFreq, (Component*) &s.lp, (Component*) s.engage.get() })
            c->setVisible (false);

    auto addCaption = [this] (const String& text, float cx, float y, float w, bool accent)
    {
        captions.push_back ({ text, { cx - w * 0.5f, y, w, 12.0f }, accent });
    };

    auto layoutRack = [&] (int rack, Strip& s, Strip* rightLevels, const String& subtitle)
    {
        const float y = rackY (rack), cy = y + kKnobRowY;
        placeSelector (s.hp, xCutLow, cy, FreqKind::HP);
        placeSelector (s.lsFreq, xShelfLow, cy, FreqKind::LS);
        placeSelector (s.hsFreq, xShelfHigh, cy, FreqKind::HS);
        placeSelector (s.lp, xCutHigh, cy, FreqKind::LP);

        if (rightLevels != nullptr)
        {
            placeLevel (s.lsGain, xLowL, cy);
            placeLevel (rightLevels->lsGain, xLowR, cy);
            placeLevel (s.hsGain, xHighL, cy);
            placeLevel (rightLevels->hsGain, xHighR, cy);
            for (float x : { xLowL, xHighL })  addCaption (nameL, x, y + 12.0f, 60.0f, false);
            for (float x : { xLowR, xHighR })  addCaption (nameR, x, y + 12.0f, 60.0f, false);
            for (float x : { (xLowL + xLowR) * 0.5f, (xHighL + xHighR) * 0.5f })
                addCaption ("LEVEL dB", x, y + 12.0f, 80.0f, false);
        }
        else
        {
            const float xl = (xLowL + xLowR) * 0.5f, xh = (xHighL + xHighR) * 0.5f;
            placeLevel (s.lsGain, xl, cy);
            placeLevel (s.hsGain, xh, cy);
            for (float x : { xl, xh }) addCaption ("LEVEL dB", x, y + 12.0f, 80.0f, false);
        }

        s.engage->setVisible (true);
        s.engage->setBounds (roundToInt (xEngage - 20.0f), roundToInt (cy - 26.0f), 40, 40);
        addCaption ("ENGAGE", xEngage, cy + 18.0f, 80.0f, false);

        addCaption ("CUT",   xCutLow,    y + 12.0f, 60.0f, true);
        addCaption ("SHELF", xShelfLow,  y + 12.0f, 60.0f, true);
        addCaption ("SHELF", xShelfHigh, y + 12.0f, 60.0f, true);
        addCaption ("CUT",   xCutHigh,   y + 12.0f, 60.0f, true);
        addCaption ("LOW FREQ  Hz",   (xCutLow + xShelfLow) * 0.5f,   y + 106.0f, 120.0f, true);
        addCaption ("LOW FREQUENCY",  (xLowL + xLowR) * 0.5f,         y + 106.0f, 120.0f, true);
        addCaption ("HIGH FREQUENCY", (xHighL + xHighR) * 0.5f,       y + 106.0f, 120.0f, true);
        addCaption ("HIGH FREQ  kHz", (xShelfHigh + xCutHigh) * 0.5f, y + 106.0f, 120.0f, true);
        logoSubtitles.push_back (subtitle);
    };

    if (dual)
    {
        layoutRack (0, strips[0], nullptr, nameL);
        layoutRack (1, strips[1], nullptr, nameR);
    }
    else
    {
        layoutRack (0, strips[0], &strips[1], shownMidSide ? "MID / SIDE" : "STEREO");
    }

    // Stereo link / Mid-Side / Output rack
    const bool hasControlRack = numRacks >= 2;
    for (Component* c : { (Component*) linkLed.get(), (Component*) midSideLed.get(), (Component*) &outputKnob })
        c->setVisible (hasControlRack);
    if (hasControlRack)
    {
        const float y = rackY (numRacks - 1), cy = y + kKnobRowY;
        linkLed->setBounds (140 - 20, roundToInt (cy - 26.0f), 40, 40);
        addCaption ("STEREO", 140.0f, cy + 18.0f, 80.0f, false);
        addCaption ("LINK",   140.0f, cy + 30.0f, 80.0f, false);
        midSideLed->setBounds (600 - 20, roundToInt (cy - 26.0f), 40, 40);
        addCaption ("MID/SIDE", 600.0f, cy + 18.0f, 80.0f, false);

        outputKnob.setBounds (roundToInt (1065.0f - kSelectorD * 0.5f), roundToInt (cy - kSelectorD * 0.5f),
                              (int) kSelectorD, (int) kSelectorD + BaxKnob::kBubbleHeight);
        Scale s;
        s.centre = { 1065.0f, cy };
        s.radius = kSelectorD * 0.5f + 11.0f;
        for (float v : { -10.0f, -5.0f, 5.0f, 10.0f })
        {
            s.labels.add (v > 0.0f ? "+" + String ((int) v) : String ((int) v));
            s.positions.add ((v + 10.0f) / 20.0f);
        }
        scales.push_back (s);
        addCaption ("OUTPUT", 1065.0f, y + 12.0f, 80.0f, true);
    }

    repaint();
}

void BaxPanel::resized()
{
    sizeMenu.setBounds (14, 3, 64, 18);
    bypassButton->setBounds (104, 4, 66, 16);
    oversamplingMenu.setBounds (236, 3, 170, 18);
    viewMenu.setBounds (470, 3, 220, 18);
    presetMenu.setBounds (740, 3, 260, 18);
    monitor->setBounds (1036, 3, 150, 18);
    layoutForView();
}

//==============================================================================
void BaxPanel::syncLinkedParameters()
{
    // Classic views: the right channel's frequency selectors and Engage follow the left.
    // Stereo link: the levels too (and, in Dual Mono, everything).
    const bool classic = shownView != View::DualMono;
    const bool link = processor.isLinkOn();

    auto sync = [] (std::vector<SyncPair>& pairs, bool active)
    {
        for (auto& p : pairs)
        {
            const float l = p.left->getValue(), r = p.right->getValue();
            if (active && std::abs (l - r) > 1.0e-6f)
            {
                // whichever side moved since the last tick wins; otherwise left wins
                const bool rightMoved = std::abs (r - p.lastRight) > 1.0e-6f && std::abs (l - p.lastLeft) <= 1.0e-6f;
                auto* target = rightMoved ? p.left : p.right;
                const float value = rightMoved ? r : l;
                target->beginChangeGesture();
                target->setValueNotifyingHost (value);
                target->endChangeGesture();
            }
            p.lastLeft = p.left->getValue();
            p.lastRight = p.right->getValue();
        }
    };

    sync (freqPairs, classic || link);
    sync (enginePairs, classic || link);
    sync (gainPairs, link);
}

void BaxPanel::timerCallback()
{
    const auto view = processor.getView();
    const bool ms = processor.apvts.getRawParameterValue (ParamIDs::midSide)->load() > 0.5f;
    if (view != shownView || ms != shownMidSide)
    {
        const bool heightChanged = view != shownView;
        shownView = view;
        shownMidSide = ms;
        if (heightChanged)
        {
            setSize (kWidth, getBaseHeight());
            if (onViewChanged) onViewChanged();
        }
        layoutForView();
    }

    syncLinkedParameters();

    for (auto* m : { &oversamplingMenu, &viewMenu, &presetMenu })
        m->refresh();
}

//==============================================================================
void BaxPanel::createBackground()
{
    // brushed-metal streaks on a transparent layer, drawn over the rack gradients;
    // rendered at 2x so it stays sharp when enlarged
    const int h = baseHeightFor (View::DualMono);
    metal = Image (Image::ARGB, kWidth * 2, h * 2, true);
    Graphics g (metal);
    g.addTransform (AffineTransform::scale (2.0f));

    Random rng (0xba5e);
    for (int y = 0; y < h; ++y)
    {
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (rng.nextFloat() * 0.03f));
        g.fillRect (0, y, kWidth, 1);
    }
    for (int i = 0; i < 3000; ++i)
    {
        const int x = rng.nextInt (kWidth), y = rng.nextInt (h), len = 20 + rng.nextInt (160);
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (0.012f + rng.nextFloat() * 0.025f));
        g.fillRect (x, y, len, 1);
    }
}

static void drawScrew (Graphics& g, Point<float> c, float r)
{
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillEllipse (Rectangle<float> (r * 2.0f + 2.0f, r * 2.0f + 2.0f).withCentre (c.translated (0.0f, 0.8f)));
    g.setGradientFill (ColourGradient (BaxColours::labelText, c.x - r, c.y - r, Colour (0xff434346), c.x + r, c.y + r, false));
    g.fillEllipse (Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    g.setColour (Colour (0xff1a1a1c));
    g.drawLine (c.x - r * 0.6f, c.y - r * 0.6f, c.x + r * 0.6f, c.y + r * 0.6f, 1.2f);
    g.drawLine (c.x - r * 0.6f, c.y + r * 0.6f, c.x + r * 0.6f, c.y - r * 0.6f, 1.2f);
}

void BaxPanel::drawRack (Graphics& g, float y) const
{
    const auto rack = Rectangle<float> (0.0f, y, (float) kWidth, kRackH);

    // rack frame with ears: a dark header-grey gradient
    g.setGradientFill (ColourGradient (BaxColours::headerBottom, 0.0f, rack.getY(), BaxColours::background, 0.0f, rack.getBottom(), false));
    g.fillRoundedRectangle (rack, 4.0f);
    g.setColour (Colours::white.withAlpha (0.14f));
    g.drawHorizontalLine ((int) rack.getY() + 1, 4.0f, rack.getRight() - 4.0f);
    g.setColour (Colours::black);
    g.drawRoundedRectangle (rack, 4.0f, 1.0f);

    for (bool left : { true, false })
    {
        const float earX = left ? 0.0f : (float) kWidth - kFaceX;
        // rack-mount slots
        for (float sy : { y + 20.0f, y + kRackH - 20.0f })
        {
            auto slot = Rectangle<float> (18.0f, 10.0f).withCentre ({ left ? 18.0f : (float) kWidth - 18.0f, sy });
            g.setColour (BaxColours::insetTop);
            g.fillRoundedRectangle (slot, 5.0f);
            g.setColour (Colours::white.withAlpha (0.12f));
            g.drawRoundedRectangle (slot.translated (0.0f, 0.8f), 5.0f, 0.8f);
        }
        // vents
        for (int col = 0; col < 2; ++col)
            for (int row = 0; row < 8; ++row)
            {
                const float vx = earX + (left ? 36.0f : 12.0f) + (float) col * 13.0f;
                auto vent = Rectangle<float> (vx, y + 24.0f + (float) row * 10.5f, 10.0f, 4.0f);
                g.setColour (BaxColours::insetTop);
                g.fillRoundedRectangle (vent, 2.0f);
                g.setColour (Colours::white.withAlpha (0.08f));
                g.drawHorizontalLine ((int) vent.getBottom(), vent.getX() + 1.0f, vent.getRight() - 1.0f);
            }
    }

    // faceplate
    auto face = Rectangle<float> (kFaceX, y + 6.0f, (float) kWidth - 2.0f * kFaceX, kRackH - 12.0f);
    DropShadow (Colours::black.withAlpha (0.6f), 8, { 0, 2 }).drawForRectangle (g, face.toNearestInt());
    g.setGradientFill (ColourGradient (BaxColours::panelTop, 0.0f, face.getY(), BaxColours::panelBottom, 0.0f, face.getBottom(), false));
    g.fillRoundedRectangle (face, 6.0f);
    g.setColour (Colours::white.withAlpha (0.16f));
    g.drawHorizontalLine ((int) face.getY() + 1, face.getX() + 6.0f, face.getRight() - 6.0f);
    g.setColour (BaxColours::bevelDark);
    g.drawRoundedRectangle (face, 6.0f, 1.0f);

    for (auto c : { face.getTopLeft().translated (10.0f, 10.0f), face.getTopRight().translated (-10.0f, 10.0f),
                    face.getBottomLeft().translated (10.0f, -10.0f), face.getBottomRight().translated (-10.0f, -10.0f) })
        drawScrew (g, c, 4.0f);
}

void BaxPanel::drawLogo (Graphics& g, Rectangle<float> area, const String& subtitle) const
{
    // stylised wordmark: chrome "baxr" over orange "eq", with an orange swoosh
    GlyphArrangement main;
    main.addLineOfText (Font (FontOptions (30.0f, Font::bold | Font::italic)), "baxr", 0.0f, 0.0f);
    Path mainPath;
    main.createPath (mainPath);
    auto mb = mainPath.getBounds();
    const auto t = AffineTransform::translation (area.getCentreX() - mb.getCentreX(), area.getY() + 22.0f - mb.getBottom() + 10.0f);
    mainPath.applyTransform (t);
    mb = mainPath.getBounds();

    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillPath (mainPath, AffineTransform::translation (1.0f, 1.5f));
    ColourGradient chrome (Colour (0xffffffff), 0.0f, mb.getY(), Colour (0xff8c949c), 0.0f, mb.getBottom(), false);
    chrome.addColour (0.5, Colour (0xffd5dade));
    chrome.addColour (0.55, Colour (0xffa4acb3));
    g.setGradientFill (chrome);
    g.fillPath (mainPath);

    GlyphArrangement eq;
    eq.addLineOfText (Font (FontOptions (20.0f, Font::plain)), "eq", 0.0f, 0.0f);
    Path eqPath;
    eq.createPath (eqPath);
    const auto eb = eqPath.getBounds();
    eqPath.applyTransform (AffineTransform::translation (area.getCentreX() - eb.getCentreX(), mb.getBottom() + 4.0f - eb.getY()));
    g.setColour (BaxColours::accent);
    g.fillPath (eqPath);

    Path swoosh;
    const float sx = mb.getX(), sy = eqPath.getBounds().getBottom() + 5.0f, ex = mb.getRight();
    swoosh.startNewSubPath (sx, sy);
    swoosh.quadraticTo ((sx + ex) * 0.5f, sy + 4.0f, ex + 4.0f, sy - 3.0f);
    g.setGradientFill (ColourGradient (BaxColours::accent.withAlpha (0.0f), sx, sy, BaxColours::accent, ex, sy, false));
    g.strokePath (swoosh, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));

    if (subtitle.isNotEmpty())
    {
        g.setFont (CustomLookAndFeel::uiFont (11.0f, true));
        g.setColour (BaxColours::labelText);
        g.drawText (subtitle, Rectangle<float> (area.getX(), sy + 8.0f, area.getWidth(), 14.0f), Justification::centred);
    }
}

void BaxPanel::paint (Graphics& g)
{
    // window background
    g.setGradientFill (ColourGradient (Colour (0xff1a1a1c), 0.0f, 0.0f, Colour (0xff070708), 0.0f, (float) getHeight(), false));
    g.fillAll();

    // --- top bar --------------------------------------------------------------------
    auto bar = Rectangle<float> (0.0f, 0.0f, (float) kWidth, kTopBarH);
    g.setGradientFill (ColourGradient (Colour (0xff222224), 0.0f, 0.0f, Colour (0xff111112), 0.0f, bar.getBottom(), false));
    g.fillRect (bar);
    g.setColour (BaxColours::bevelDark);
    g.drawHorizontalLine ((int) bar.getBottom() - 1, 0.0f, (float) kWidth);

    // --- racks ----------------------------------------------------------------------
    for (int r = 0; r < numRacks; ++r)
        drawRack (g, rackY (r));
    g.drawImage (metal, Rectangle<float> (0.0f, 0.0f, (float) kWidth, (float) baseHeightFor (View::DualMono)));

    for (size_t r = 0; r < logoSubtitles.size(); ++r)
        drawLogo (g, { 80.0f, rackY ((int) r) + 18.0f, 86.0f, 90.0f }, logoSubtitles[r]);

    // --- printed scales around the knobs -----------------------------------------------
    const auto rotStart = MathConstants<float>::pi * 1.25f, rotEnd = MathConstants<float>::pi * 2.75f;
    g.setFont (CustomLookAndFeel::uiFont (9.5f, true));
    for (const auto& s : scales)
        for (int i = 0; i < s.labels.size(); ++i)
        {
            const float a = rotStart + s.positions[i] * (rotEnd - rotStart);
            const float x = s.centre.x + std::sin (a) * s.radius, y = s.centre.y - std::cos (a) * s.radius;
            const auto box = Rectangle<float> (34.0f, 12.0f).withCentre ({ x, y });
            g.setColour (Colours::black.withAlpha (0.5f));
            g.drawText (s.labels[i], box.translated (0.0f, 1.0f), Justification::centred);
            g.setColour (s.labels[i] == "OUT" ? BaxColours::accent : BaxColours::lightGrey);
            g.drawText (s.labels[i], box, Justification::centred);
        }

    // --- captions ---------------------------------------------------------------------
    for (const auto& c : captions)
    {
        g.setFont (CustomLookAndFeel::uiFont (10.5f, true));
        g.setColour (Colours::black.withAlpha (0.5f));
        g.drawText (c.text, c.area.translated (0.0f, 1.0f), Justification::centred);
        g.setColour (c.accent ? BaxColours::accent : BaxColours::labelText);
        g.drawText (c.text, c.area, Justification::centred);
    }
}

//==============================================================================
BaxAudioProcessorEditor::BaxAudioProcessorEditor (BaxAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), panel (p)
{
    addAndMakeVisible (panel);
    setResizable (true, true);

    panel.onViewChanged = [this] { applyViewSize (getScale()); };
    panel.onScaleRequested = [this] (float s) { applyViewSize (s); };

    applyViewSize (jlimit (0.5f, 2.5f, processor.editorScale.load()));
    ready = true;
}

void BaxAudioProcessorEditor::applyViewSize (float scale)
{
    const int w = BaxPanel::kBaseWidth, h = panel.getBaseHeight();
    const bool wasReady = ready;
    ready = false;   // the limits below can resize the window: don't store that scale
    setResizeLimits (w / 2, h / 2, w * 5 / 2, h * 5 / 2);
    getConstrainer()->setFixedAspectRatio ((double) w / (double) h);
    ready = wasReady;
    setSize (roundToInt ((float) w * scale), roundToInt ((float) h * scale));
}

void BaxAudioProcessorEditor::resized()
{
    panel.setBounds (0, 0, BaxPanel::kBaseWidth, panel.getBaseHeight());
    panel.setTransform (AffineTransform::scale (getScale()));
    if (ready)
        processor.editorScale.store (getScale());
}
