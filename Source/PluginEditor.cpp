#include "PluginEditor.h"

namespace DistroyColours
{
    static const juce::Colour background      { 0xff1a1414 };
    static const juce::Colour panel           { 0xff241c1c };
    static const juce::Colour knobBody        { 0xff2e2424 };
    static const juce::Colour knobIndicator   { 0xffe8562f }; // DISTROYBOY orange/red
    static const juce::Colour knobOutline     { 0xff473838 };
    static const juce::Colour titleText       { 0xffe8562f };
    static const juce::Colour taglineText     { 0xffb89b8f };
    static const juce::Colour pedalNameText   { 0xfff0e6e0 };
    static const juce::Colour modeText        { 0xff9a8a84 };
    static const juce::Colour buttonBg        { 0xffe8562f };
    static const juce::Colour buttonText      { 0xff1a1414 };
}

/* Funny random taglines shown under the logo, re-rolled each time the
 * dice (full RNDMIZ) button is clicked. */
static const juce::StringArray kTaglines = {
    "...hurts your feelings",
    "...makes your music suck less!",
    "...take this, mom and dad!",
    "...all your beatz are belong to us",
    "...MOAR ROAR!",
    "...destroying since 1972",
    "...make gooder sounding"
};

//==============================================================================
DistroyLookAndFeel::DistroyLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, DistroyColours::background);
    setColour(juce::TextButton::buttonColourId, DistroyColours::buttonBg);
    setColour(juce::TextButton::textColourOffId, DistroyColours::buttonText);
    setColour(juce::Label::textColourId, DistroyColours::pedalNameText);
}

void DistroyLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPosProportional, float rotaryStartAngle,
                                           float rotaryEndAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height).reduced(4.0f);
    const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    g.setColour(DistroyColours::knobBody);
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour(DistroyColours::knobOutline);
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.5f);

    juce::Path arc;
    const float arcRadius = radius - 4.0f;
    arc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                       rotaryStartAngle, angle, true);
    g.setColour(DistroyColours::knobIndicator);
    g.strokePath(arc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path pointer;
    const float pointerLength = radius * 0.7f;
    const float pointerThickness = 3.0f;
    pointer.addRectangle(-pointerThickness * 0.5f, -pointerLength, pointerThickness, pointerLength);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
    g.setColour(DistroyColours::knobIndicator);
    g.fillPath(pointer);
}

//==============================================================================
DiceButton::DiceButton() : juce::Button("dice")
{
    rng = juce::Random(juce::Time::currentTimeMillis());
}

void DiceButton::rollFace()
{
    faceValue = 1 + rng.nextInt(6);
    repaint();
}

void DiceButton::paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);
    if (shouldDrawButtonAsDown)
        bounds = bounds.reduced(1.5f); // subtle "pressed" shrink

    // Drop shadow
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillRoundedRectangle(bounds.translated(1.5f, 2.0f), 6.0f);

    // Die body: white with a faint warm tint when highlighted
    juce::Colour body = shouldDrawButtonAsHighlighted ? juce::Colour(0xfffff4ee) : juce::Colours::white;
    g.setColour(body);
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(juce::Colour(0xffcfc2ba));
    g.drawRoundedRectangle(bounds, 6.0f, 1.2f);

    // Pips laid out on a 3x3 grid
    const float w = bounds.getWidth();
    const float h = bounds.getHeight();
    const float pipR = juce::jmin(w, h) * 0.09f;
    const float cx = bounds.getX() + w * 0.5f;
    const float cy = bounds.getY() + h * 0.5f;
    const float dx = w * 0.26f;
    const float dy = h * 0.26f;

    auto pip = [&](float px, float py) {
        g.setColour(juce::Colour(0xff2a2020));
        g.fillEllipse(px - pipR, py - pipR, pipR * 2.0f, pipR * 2.0f);
    };

    const float L = cx - dx, R = cx + dx, T = cy - dy, B = cy + dy;

    switch (faceValue) {
        case 1:
            pip(cx, cy);
            break;
        case 2:
            pip(L, T); pip(R, B);
            break;
        case 3:
            pip(L, T); pip(cx, cy); pip(R, B);
            break;
        case 4:
            pip(L, T); pip(R, T); pip(L, B); pip(R, B);
            break;
        case 5:
            pip(L, T); pip(R, T); pip(cx, cy); pip(L, B); pip(R, B);
            break;
        case 6:
        default:
            pip(L, T); pip(R, T); pip(L, cy); pip(R, cy); pip(L, B); pip(R, B);
            break;
    }
}

//==============================================================================
StompSwitchButton::StompSwitchButton() : juce::Button("stomp") {}

void StompSwitchButton::paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);
    const float diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
    auto square = juce::Rectangle<float>(diameter, diameter).withCentre(bounds.getCentre());

    const float pressOffset = shouldDrawButtonAsDown ? 1.5f : 0.0f;

    // Drop shadow beneath the whole switch housing
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillEllipse(square.translated(0.0f, 2.5f));

    // Outer chrome bevel ring -- radial-ish gradient for a metal look
    juce::ColourGradient ringGrad(juce::Colour(0xffd8d8dc), square.getCentreX(), square.getY(),
                                   juce::Colour(0xff58585c), square.getCentreX(), square.getBottom(),
                                   false);
    ringGrad.addColour(0.5, juce::Colour(0xff9a9aa0));
    g.setGradientFill(ringGrad);
    g.fillEllipse(square);

    // Inner cap -- smaller circle, presses inward when clicked
    auto cap = square.reduced(diameter * 0.14f).translated(0.0f, pressOffset);
    juce::Colour capTop = shouldDrawButtonAsHighlighted ? juce::Colour(0xff4a4a4e) : juce::Colour(0xff3a3a3e);
    juce::ColourGradient capGrad(capTop, cap.getCentreX(), cap.getY(),
                                  juce::Colour(0xff141416), cap.getCentreX(), cap.getBottom(),
                                  false);
    g.setGradientFill(capGrad);
    g.fillEllipse(cap);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawEllipse(cap, 1.0f);

    // Cross-shaped screw slot in the middle, like a real footswitch cap
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    auto capCentre = cap.getCentre();
    const float slotLen = cap.getWidth() * 0.28f;
    g.drawLine(capCentre.x - slotLen, capCentre.y, capCentre.x + slotLen, capCentre.y, 2.0f);

    // LED indicator -- lights orange while physically held down
    const float ledR = diameter * 0.06f;
    auto ledPos = juce::Point<float>(square.getCentreX(), square.getY() + diameter * 0.17f);
    g.setColour(shouldDrawButtonAsDown ? DistroyColours::knobIndicator : juce::Colour(0xff3a2a22));
    g.fillEllipse(ledPos.x - ledR, ledPos.y - ledR, ledR * 2.0f, ledR * 2.0f);
    if (shouldDrawButtonAsDown)
    {
        // faint glow
        g.setColour(DistroyColours::knobIndicator.withAlpha(0.35f));
        g.fillEllipse(ledPos.x - ledR * 2.2f, ledPos.y - ledR * 2.2f, ledR * 4.4f, ledR * 4.4f);
    }
}

//==============================================================================
DistroyAudioProcessorEditor::DistroyAudioProcessorEditor(DistroyAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&lookAndFeel);

    titleLabel.setText("DISTROYBOY", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(28.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, DistroyColours::titleText);
    titleLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(titleLabel);

    taglineLabel.setFont(juce::Font(12.0f, juce::Font::italic));
    taglineLabel.setColour(juce::Label::textColourId, DistroyColours::taglineText);
    taglineLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(taglineLabel);
    pickRandomTagline();

    rndmizButton.onClick = [this] {
        processorRef.requestRandomize();
        rndmizButton.rollFace();
        pickRandomTagline();
    };
    addAndMakeVisible(rndmizButton);

    rndmzKnobsOnlyButton.onClick = [this] { processorRef.requestRandomizeKnobsOnly(); };
    addAndMakeVisible(rndmzKnobsOnlyButton);

    rndmzKnobsOnlyCaption.setText("knobs only", juce::dontSendNotification);
    rndmzKnobsOnlyCaption.setJustificationType(juce::Justification::centred);
    rndmzKnobsOnlyCaption.setColour(juce::Label::textColourId, DistroyColours::modeText);
    rndmzKnobsOnlyCaption.setFont(juce::Font(10.0f));
    addAndMakeVisible(rndmzKnobsOnlyCaption);

    for (int i = 0; i < kNumSlots; ++i)
    {
        auto& slider = knobSliders[(size_t)i];
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 18);
        slider.setColour(juce::Slider::textBoxTextColourId, DistroyColours::pedalNameText);
        slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(slider);

        sliderAttachments[(size_t)i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processorRef.apvts, "knob" + juce::String(i + 1), slider);

        auto& nameLabel = pedalNameLabels[(size_t)i];
        nameLabel.setJustificationType(juce::Justification::centred);
        nameLabel.setColour(juce::Label::textColourId, DistroyColours::pedalNameText);
        nameLabel.setFont(juce::Font(13.0f, juce::Font::bold));
        addAndMakeVisible(nameLabel);

        auto& modeLabel = modeLabels[(size_t)i];
        modeLabel.setJustificationType(juce::Justification::centred);
        modeLabel.setColour(juce::Label::textColourId, DistroyColours::modeText);
        modeLabel.setFont(juce::Font(10.0f));
        addAndMakeVisible(modeLabel);
    }

    refreshSlotLabels();
    startTimerHz(10); // poll for pedal-name/icon changes after RNDMIZ

    setResizable(true, true);
    setResizeLimits(640, 220, 1600, 640);
    setSize(920, 320);
}

DistroyAudioProcessorEditor::~DistroyAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void DistroyAudioProcessorEditor::pickRandomTagline()
{
    juce::Random r(juce::Time::currentTimeMillis() + (juce::int64)this);
    taglineLabel.setText(kTaglines[r.nextInt(kTaglines.size())], juce::dontSendNotification);
}

juce::Colour DistroyAudioProcessorEditor::typeIconColour(int t)
{
    // Rough real-world-pedal-inspired colour per type, for the tiny
    // per-slot icon. Order matches the DistroyType enum in distroy_dsp.h.
    switch (t)
    {
        case DISTROY_BOSS_OD:        return juce::Colour(0xffe8c93a); // yellow
        case DISTROY_FUZZ:           return juce::Colour(0xffb0b0b0); // gray/silver
        case DISTROY_METAL:          return juce::Colour(0xffe8792a); // orange (MT-2)
        case DISTROY_TUBESCREAMER:   return juce::Colour(0xff4caf50); // green (TS9)
        case DISTROY_BIG_MUFF:       return juce::Colour(0xff6b7a3a); // olive
        case DISTROY_SANSAMP:        return juce::Colour(0xff2a2a2a); // black
        case DISTROY_RAT:            return juce::Colour(0xff555555); // dark gray
        case DISTROY_GEIGER_COUNTER: return juce::Colour(0xffe8d92a); // yellow (per spec example)
        case DISTROY_MOOG_LADDER:    return juce::Colour(0xffe8e0d0); // cream/white
        case DISTROY_KORG_MS20:      return juce::Colour(0xffe0602a); // orange-red
        case DISTROY_MUTRON:         return juce::Colour(0xffe8a02a); // yellow-orange
        case DISTROY_CRYBABY:        return juce::Colour(0xffc0392b); // red
        case DISTROY_JENSEN:         return juce::Colour(0xffc9a45c); // gold
        case DISTROY_LUNDAHL:        return juce::Colour(0xff5b8ab5); // steel blue
        case DISTROY_LOFI:           return juce::Colour(0xff8a8a8a); // gray/digital
        case DISTROY_FZ1W:           return juce::Colour(0xffcfd4d8); // silver/chrome
        case DISTROY_CLIP:           return juce::Colour(0xfff0f0f0); // white
        case DISTROY_REKT:           return juce::Colour(0xffd0332a); // red
        case DISTROY_WHAM:           return juce::Colour(0xff9b30c9); // purple/magenta
        case DISTROY_TAPE:           return juce::Colour(0xffb08a5a); // tan/brown
        case DISTROY_SPKR:           return juce::Colour(0xff2a2a2a); // black
        default:                     return juce::Colours::grey;
    }
}

void DistroyAudioProcessorEditor::drawPedalIcon(juce::Graphics& g, juce::Rectangle<int> b, juce::Colour colour)
{
    // Tiny, deliberately blocky/"8-bit" pixel-art guitar pedal
    // silhouette: rounded body + footswitch dot + two knob dots.
    // Drawn small and low-opacity ("barely visible" per spec).
    g.setColour(colour.withAlpha(0.4f));
    auto body = b.toFloat();
    g.fillRect(body); // deliberately sharp/blocky, not rounded -- "8-bit" look

    g.setColour(juce::Colours::black.withAlpha(0.35f));
    // footswitch (bottom center)
    const float swR = body.getHeight() * 0.22f;
    g.fillEllipse(body.getCentreX() - swR, body.getBottom() - swR * 2.2f, swR * 2.0f, swR * 2.0f);
    // two knob pixels (top)
    const float kR = body.getHeight() * 0.14f;
    g.fillRect(body.getX() + body.getWidth() * 0.22f - kR, body.getY() + body.getHeight() * 0.18f, kR * 2.0f, kR * 2.0f);
    g.fillRect(body.getX() + body.getWidth() * 0.68f - kR, body.getY() + body.getHeight() * 0.18f, kR * 2.0f, kR * 2.0f);
}

void DistroyAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(DistroyColours::background);

    g.setColour(DistroyColours::panel);
    g.fillRoundedRectangle(getLocalBounds().reduced(8).toFloat(), 8.0f);

    for (int i = 0; i < kNumSlots; ++i)
    {
        const int typeIdx = processorRef.getSlotTypeIndex(i + 1);
        drawPedalIcon(g, iconBounds[(size_t)i], typeIconColour(typeIdx));
    }
}

void DistroyAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(20);
    const int totalHeight = juce::jmax(area.getHeight(), 1);

    auto header = area.removeFromTop(juce::jmax(48, (int)(totalHeight * 0.18f)));

    // Dice (full randomize) button, upper LEFT, roughly square.
    const int diceSize = juce::jmin(header.getHeight(), 52);
    rndmizButton.setBounds(header.removeFromLeft(diceSize).withSizeKeepingCentre(diceSize, diceSize));

    // Title + tagline, upper RIGHT.
    auto titleArea = header.removeFromRight(juce::jmax(220, header.getWidth() / 2));
    titleLabel.setBounds(titleArea.removeFromTop(titleArea.getHeight() * 2 / 3));
    taglineLabel.setBounds(titleArea);

    // Reserve the bottom strip BEFORE laying out the knob row, so the
    // knobs' height calculation already accounts for it.
    auto bottomStrip = area.removeFromBottom(juce::jmax(40, (int)(totalHeight * 0.13f)));
    auto stompArea = bottomStrip.removeFromRight(100);
    const int stompSize = juce::jmin(stompArea.getHeight(), 44);
    rndmzKnobsOnlyButton.setBounds(stompArea.withSizeKeepingCentre(stompSize, stompSize));
    rndmzKnobsOnlyCaption.setBounds(bottomStrip.removeFromRight(90));

    area.removeFromTop(juce::jmax(6, (int)(totalHeight * 0.03f)));

    const int nameLabelHeight = juce::jmax(16, (int)(area.getHeight() * 0.12f));
    const int modeLabelHeight = juce::jmax(12, (int)(area.getHeight() * 0.09f));
    const int slotWidth = area.getWidth() / kNumSlots;

    for (int i = 0; i < kNumSlots; ++i)
    {
        auto slotArea = area.withX(area.getX() + i * slotWidth).withWidth(slotWidth).reduced(6, 0);

        pedalNameLabels[(size_t)i].setBounds(slotArea.removeFromTop(nameLabelHeight));

        // Fixed-size icon (NOT scaled with window size, per spec),
        // tucked just under the pedal name label.
        const int iconW = 14, iconH = 9;
        iconBounds[(size_t)i] = juce::Rectangle<int>(
            slotArea.getCentreX() - iconW / 2, slotArea.getY() + 2, iconW, iconH);
        slotArea.removeFromTop(iconH + 4);

        modeLabels[(size_t)i].setBounds(slotArea.removeFromBottom(modeLabelHeight));
        knobSliders[(size_t)i].setBounds(slotArea);
    }
}

void DistroyAudioProcessorEditor::timerCallback()
{
    refreshSlotLabels();
    repaint(); // picks up any pedal-type change for the per-slot icons
}

void DistroyAudioProcessorEditor::refreshSlotLabels()
{
    for (int i = 0; i < kNumSlots; ++i)
    {
        const int oneBased = i + 1;
        pedalNameLabels[(size_t)i].setText(processorRef.getSlotPedalName(oneBased), juce::dontSendNotification);
        modeLabels[(size_t)i].setText(processorRef.getSlotModeLabel(oneBased), juce::dontSendNotification);
    }
}
