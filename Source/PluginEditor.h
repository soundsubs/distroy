#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

/** Dark, DISTROYBOY-branded look for the rotary knobs. */
class DistroyLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DistroyLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;
};

/** White dice icon button, used for the full-chain RNDMIZ control (top
 * left). Shows a random 1-6 pip face, re-rolled each time it's clicked
 * -- purely a cosmetic flourish matching the "randomize" theme, has no
 * bearing on the actual DSP randomization. */
class DiceButton : public juce::Button
{
public:
    DiceButton();
    void paintButton(juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void rollFace();

private:
    int faceValue = 5;
    juce::Random rng;
};

/** A guitar-pedal-style metal footswitch, used for the knobs-only RNDMZ
 * control (bottom). Chrome bevel ring, pressed-in animation, small LED
 * that lights up while physically held down. */
class StompSwitchButton : public juce::Button
{
public:
    StompSwitchButton();
    void paintButton(juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
};

class DistroyAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    explicit DistroyAudioProcessorEditor(DistroyAudioProcessor&);
    ~DistroyAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshSlotLabels();
    void pickRandomTagline();
    static juce::Colour typeIconColour(int distroyTypeIndex);
    static void drawPedalIcon(juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour colour);

    DistroyAudioProcessor& processorRef;
    DistroyLookAndFeel lookAndFeel;

    juce::Label titleLabel;
    juce::Label taglineLabel;

    DiceButton rndmizButton;                 // top-left: full randomize
    StompSwitchButton rndmzKnobsOnlyButton;   // bottom: knobs-only randomize
    juce::Label rndmzKnobsOnlyCaption;

    static constexpr int kNumSlots = DISTROY_NUM_SLOTS;

    std::array<juce::Slider, kNumSlots> knobSliders;
    std::array<juce::Label, kNumSlots> pedalNameLabels;
    std::array<juce::Label, kNumSlots> modeLabels;
    /* Fixed-size icon bounds, cached in resized() -- deliberately NOT
     * scaled with window resizing (small/barely-visible per spec, stays
     * a constant pixel size regardless of window size). */
    std::array<juce::Rectangle<int>, kNumSlots> iconBounds;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, kNumSlots> sliderAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DistroyAudioProcessorEditor)
};
