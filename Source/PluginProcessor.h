#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <array>

extern "C" {
#include "DSP/distroy_dsp.h"
}

/**
 * DISTROY VST3 plugin processor.
 *
 * Reuses the exact same DSP core (Source/DSP/distroy_dsp.c/h) verified
 * and shipped in the Schwung/Ableton Move version of DISTROY -- same
 * 10 modeled pedals/filters, same right-to-left 8-slot chain, same
 * randomization behavior (no duplicate types, filters' resonance
 * always 0 on randomize for safety).
 *
 * UNLIKE the Move version, this plugin draws its own UI, so the
 * "show the pedal's name on each knob" feature that was confirmed
 * impossible via Schwung's host-controlled display (see the Move
 * project's README for the full saga) is trivial here.
 */
class DistroyAudioProcessor : public juce::AudioProcessor
{
public:
    DistroyAudioProcessor();
    ~DistroyAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    /** Call from the UI thread to request a full re-randomization
     * (new pedal types + new knob/sub-parameter values). Applied
     * safely on the audio thread at the start of the next
     * processBlock() to avoid a data race with the chain being read
     * mid-block. */
    void requestRandomize();

    /** Call from the UI thread to randomize ONLY the 8 knob values,
     * leaving pedal selection and Drive/Tone/Level sub-parameters
     * untouched. Safe to call directly (no audio-thread-deferred
     * pattern needed here) -- these are ordinary APVTS parameters,
     * which JUCE already makes thread-safe for exactly this kind of
     * UI-thread write, unlike requestRandomize() above which touches
     * non-parameter DSP state (pedal type) that DOES need deferring. */
    void requestRandomizeKnobsOnly();

    /** UI-thread-safe read of the pedal name currently occupying a
     * slot (1-8). Safe to call from the message thread at any time --
     * reads a snapshot array that's only written by the audio thread
     * right after a randomize, using relaxed atomics (display-only,
     * a one-block-stale read is harmless). */
    juce::String getSlotPedalName(int oneBasedSlot) const;
    juce::String getSlotModeLabel(int oneBasedSlot) const;

    /** Raw DistroyType enum value for a slot (1-8), for the UI's
     * per-pedal icon coloring. Same lock-free snapshot as the two
     * accessors above. */
    int getSlotTypeIndex(int oneBasedSlot) const;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void applyRandomizeIfPending();
    void syncKnobsFromParameters();
    void publishSlotNamesForUI();

    DistroyChain chainLeft {};
    DistroyChain chainRight {};
    double currentSampleRate = 44100.0;
    BrickwallLimiter limiter {}; /* safety limiter on final stereo output -- see distroy_dsp.h */

    std::atomic<bool> randomizePending { false };
    std::atomic<unsigned int> randomizeSeedCounter { 0 };

    /* Snapshot of slot type indices for lock-free UI reads. Written by
     * the audio thread (after applying a randomize), read by the
     * message thread for display. */
    std::array<std::atomic<int>, DISTROY_NUM_SLOTS> uiSlotType {};

    std::array<std::atomic<float>*, DISTROY_NUM_SLOTS> knobParams {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DistroyAudioProcessor)
};
