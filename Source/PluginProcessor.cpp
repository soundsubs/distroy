#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>
#include <ctime>

DistroyAudioProcessor::DistroyAudioProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (int i = 0; i < DISTROY_NUM_SLOTS; ++i)
    {
        const auto paramId = "knob" + juce::String(i + 1);
        knobParams[(size_t)i] = apvts.getRawParameterValue(paramId);
        uiSlotType[(size_t)i].store((int)DISTROY_BOSS_OD, std::memory_order_relaxed);
    }

    /* Randomize on first load, same behavior as the Move version's
     * create_instance(). Applied on the audio thread at the start of
     * the first processBlock() once prepareToPlay() has set the real
     * sample rate. */
    randomizePending.store(true, std::memory_order_relaxed);
}

DistroyAudioProcessor::~DistroyAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout DistroyAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    for (int i = 0; i < DISTROY_NUM_SLOTS; ++i)
    {
        const auto paramId = "knob" + juce::String(i + 1);
        const auto paramName = "Slot " + juce::String(i + 1);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { paramId, 1 },
            paramName,
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f),
            0.5f,
            juce::AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction(
                [](float v, int) { return juce::String((int)std::lround(v * 100.0)) + "%"; })));
    }

    return { params.begin(), params.end() };
}

void DistroyAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;
    distroy_chain_init(&chainLeft, currentSampleRate);
    distroy_chain_init(&chainRight, currentSampleRate);
    /* -1dBFS ceiling, 80ms release. Note: LIMITER_LOOKAHEAD_SAMPLES is a
     * fixed sample count (128), so the actual lookahead TIME varies
     * with host sample rate (~2.9ms @44.1kHz, ~1.3ms @96kHz) -- still
     * provides real lookahead protection at any reasonable rate, just
     * not perfectly time-consistent across rates. Known simplification,
     * not a functional bug. */
    brickwall_limiter_init(&limiter, -1.0, 80.0, currentSampleRate);
    /* Leave randomizePending as-is -- if this is the very first
     * prepareToPlay (constructor already requested one), it'll apply
     * on the first processBlock now that chains are correctly
     * sample-rate-initialized. */
}

void DistroyAudioProcessor::releaseResources() {}

bool DistroyAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    return mainOut == juce::AudioChannelSet::mono() || mainOut == juce::AudioChannelSet::stereo();
}

void DistroyAudioProcessor::applyRandomizeIfPending()
{
    bool expected = true;
    if (randomizePending.compare_exchange_strong(expected, false))
    {
        /* Seed mixes wall-clock time with a monotonically increasing
         * counter so rapid repeated RNDMIZ clicks within the same
         * second still produce different chains (same reasoning as
         * the Move version's make_seed()). */
        unsigned int seed = (unsigned int)std::time(nullptr)
                             ^ (randomizeSeedCounter.fetch_add(1) * 2654435761u);
        distroy_chain_randomize_all(&chainLeft, seed);
        distroy_chain_randomize_all(&chainRight, seed);
        publishSlotNamesForUI();
    }
}

void DistroyAudioProcessor::publishSlotNamesForUI()
{
    for (int i = 0; i < DISTROY_NUM_SLOTS; ++i)
        uiSlotType[(size_t)i].store((int)chainLeft.slots[i].type, std::memory_order_relaxed);
}

void DistroyAudioProcessor::syncKnobsFromParameters()
{
    for (int i = 0; i < DISTROY_NUM_SLOTS; ++i)
    {
        const double v = (double)knobParams[(size_t)i]->load();
        chainLeft.slots[i].knob = v;
        chainRight.slots[i].knob = v;
    }
}

void DistroyAudioProcessor::requestRandomize()
{
    randomizePending.store(true, std::memory_order_relaxed);
}

void DistroyAudioProcessor::requestRandomizeKnobsOnly()
{
    /* Safe to call directly here (message/UI thread) -- ordinary APVTS
     * parameter writes, not the audio-thread-deferred pattern
     * requestRandomize() above needs (that one touches non-parameter
     * DSP state -- pedal type -- that could race with processBlock()
     * reading it mid-block; plain parameter values don't have that
     * problem, JUCE's parameter system is already thread-safe for
     * this). */
    juce::Random rng;
    for (int i = 0; i < DISTROY_NUM_SLOTS; ++i)
    {
        const auto paramId = "knob" + juce::String(i + 1);
        if (auto* param = apvts.getParameter(paramId))
            param->setValueNotifyingHost(rng.nextFloat());
    }
}

juce::String DistroyAudioProcessor::getSlotPedalName(int oneBasedSlot) const
{
    if (oneBasedSlot < 1 || oneBasedSlot > DISTROY_NUM_SLOTS)
        return {};
    const int t = uiSlotType[(size_t)(oneBasedSlot - 1)].load(std::memory_order_relaxed);
    return juce::String(distroy_type_info((DistroyType)t)->name);
}

juce::String DistroyAudioProcessor::getSlotModeLabel(int oneBasedSlot) const
{
    if (oneBasedSlot < 1 || oneBasedSlot > DISTROY_NUM_SLOTS)
        return {};
    const int t = uiSlotType[(size_t)(oneBasedSlot - 1)].load(std::memory_order_relaxed);
    const auto* info = distroy_type_info((DistroyType)t);
    return juce::String(distroy_knob_mode_label(info->knob_mode));
}

int DistroyAudioProcessor::getSlotTypeIndex(int oneBasedSlot) const
{
    if (oneBasedSlot < 1 || oneBasedSlot > DISTROY_NUM_SLOTS)
        return 0;
    return uiSlotType[(size_t)(oneBasedSlot - 1)].load(std::memory_order_relaxed);
}

void DistroyAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    applyRandomizeIfPending();
    syncKnobsFromParameters();

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    float* left = numChannels > 0 ? buffer.getWritePointer(0) : nullptr;
    float* right = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int n = 0; n < numSamples; ++n)
    {
        double l = 0.0, r = 0.0;
        bool hasRight = (right != nullptr);

        if (left != nullptr)
            l = distroy_chain_process(&chainLeft, (double)left[n]);
        if (hasRight)
            r = distroy_chain_process(&chainRight, (double)right[n]);
        else
            r = l; /* mono bus: feed the limiter the same value on both
                       "channels" so its stereo-linked peak detection
                       still works correctly (max(l,r) just equals l). */

        /* Brickwall safety limiter: stereo-linked, look-ahead ceiling
         * at -1dBFS. See distroy_dsp.h's BrickwallLimiter for the full
         * design -- smooth gain reduction before peaks hit, with a
         * hard-clamp backstop as the actual safety guarantee. */
        brickwall_limiter_process(&limiter, &l, &r);

        if (left != nullptr) left[n] = (float)l;
        if (hasRight) right[n] = (float)r;
    }
}

juce::AudioProcessorEditor* DistroyAudioProcessor::createEditor()
{
    return new DistroyAudioProcessorEditor(*this);
}

void DistroyAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    /* Full session recall: APVTS handles the 8 knob values, but which
     * pedal occupies each slot (and its randomized Drive/Tone/Level
     * sub-parameters) lives outside APVTS -- stored as a custom child
     * element so reloading a saved project restores the exact same
     * chain, not just knob positions. */
    auto state = apvts.copyState();
    auto xml = state.createXml();

    auto* chainXml = xml->createNewChildElement("DistroyChainState");
    for (int i = 0; i < DISTROY_NUM_SLOTS; ++i)
    {
        auto* slotXml = chainXml->createNewChildElement("Slot");
        slotXml->setAttribute("index", i);
        slotXml->setAttribute("type", (int)chainLeft.slots[i].type);
        slotXml->setAttribute("subDrive", chainLeft.slots[i].sub_drive);
        slotXml->setAttribute("subTone", chainLeft.slots[i].sub_tone);
        slotXml->setAttribute("subLevel", chainLeft.slots[i].sub_level);
    }

    copyXmlToBinary(*xml, destData);
}

void DistroyAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml == nullptr || !xml->hasTagName(apvts.state.getType()))
        return;

    apvts.replaceState(juce::ValueTree::fromXml(*xml));

    if (auto* chainXml = xml->getChildByName("DistroyChainState"))
    {
        for (auto* slotXml : chainXml->getChildIterator())
        {
            if (!slotXml->hasTagName("Slot"))
                continue;

            const int i = slotXml->getIntAttribute("index");
            if (i < 0 || i >= DISTROY_NUM_SLOTS)
                continue;

            const DistroyType t = (DistroyType)slotXml->getIntAttribute("type");
            distroy_block_set_type(&chainLeft.slots[i], t);
            distroy_block_set_type(&chainRight.slots[i], t);

            const double drive = slotXml->getDoubleAttribute("subDrive", 0.6);
            const double tone = slotXml->getDoubleAttribute("subTone", 0.0);
            const double level = slotXml->getDoubleAttribute("subLevel", 0.5);

            chainLeft.slots[i].sub_drive = drive;
            chainLeft.slots[i].sub_tone = tone;
            chainLeft.slots[i].sub_level = level;
            chainRight.slots[i].sub_drive = drive;
            chainRight.slots[i].sub_tone = tone;
            chainRight.slots[i].sub_level = level;
        }
        publishSlotNamesForUI();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DistroyAudioProcessor();
}
