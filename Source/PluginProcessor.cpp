#include "PluginProcessor.h"
#include "PluginEditor.h"

DrumProcessor::DrumProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formats.registerBasicFormats();   // WAV, AIFF, FLAC, OGG y MP3 (JUCE_USE_MP3AUDIOFORMAT)
    startTimer (500);
}

DrumProcessor::~DrumProcessor() { stopTimer(); }

juce::AudioProcessorValueTreeState::ParameterLayout DrumProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "master", 1 }, "Master", juce::NormalisableRange<float> (-48.0f, 6.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));
    return layout;
}

bool DrumProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void DrumProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    masterGain.reset (sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("master")->load(), -48.0f));
}

void DrumProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    engine.process (buffer, midi);
    masterGain.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("master")->load(), -48.0f));
    masterGain.applyGain (buffer, buffer.getNumSamples());
}

juce::AudioProcessorEditor* DrumProcessor::createEditor() { return new DrumEditor (*this); }

//==============================================================================
bool DrumProcessor::loadSample (int pad, const juce::File& file)
{
    if (pad < 0 || pad >= eng::kNumPads)
        return false;
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return false;

    // shortcut: se lee en el hilo de mensajes; para drums cortos es instantaneo. Pasar a un hilo aparte si se cargan kits grandes.
    const auto length = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (kMaxSeconds * reader->sampleRate));
    auto sample = std::make_shared<eng::Sample>();
    sample->sampleRate = reader->sampleRate;
    sample->buffer.setSize ((int) juce::jlimit (1u, 2u, reader->numChannels), length);
    if (! reader->read (&sample->buffer, 0, length, 0, true, true))
        return false;

    engine.setSample (pad, std::move (sample));
    padFiles[(size_t) pad] = file;
    missing[(size_t) pad] = false;
    sendChangeMessage();
    return true;
}

void DrumProcessor::clearPad (int pad)
{
    if (pad < 0 || pad >= eng::kNumPads)
        return;
    engine.setSample (pad, nullptr);
    padFiles[(size_t) pad] = juce::File();
    missing[(size_t) pad] = false;
    sendChangeMessage();
}

juce::String DrumProcessor::getPadName (int pad) const
{
    const auto& f = padFiles[(size_t) pad];
    return f == juce::File() ? juce::String() : f.getFileNameWithoutExtension();
}

void DrumProcessor::setSelectedPad (int pad)
{
    selectedPad = juce::jlimit (0, eng::kNumPads - 1, pad);
    sendChangeMessage();
}

//==============================================================================
void DrumProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::ValueTree pads ("PADS");
    for (int i = 0; i < eng::kNumPads; ++i)
    {
        juce::ValueTree p ("PAD");
        p.setProperty ("index", i, nullptr);
        p.setProperty ("file", padFiles[(size_t) i].getFullPathName(), nullptr);
        p.setProperty ("note", engine.padNote[(size_t) i].load(), nullptr);
        pads.appendChild (p, nullptr);
    }
    state.removeChild (state.getChildWithName ("PADS"), nullptr);
    state.appendChild (pads, nullptr);
    state.setProperty ("selectedPad", selectedPad, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DrumProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    const auto pads = state.getChildWithName ("PADS");
    state.removeChild (pads, nullptr);
    apvts.replaceState (state);
    selectedPad = juce::jlimit (0, eng::kNumPads - 1, (int) state.getProperty ("selectedPad", 0));

    for (int i = 0; i < eng::kNumPads; ++i)
        clearPad (i);
    for (const auto& p : pads)
    {
        const int i = p.getProperty ("index", -1);
        if (i < 0 || i >= eng::kNumPads)
            continue;
        engine.padNote[(size_t) i].store (juce::jlimit (0, 127, (int) p.getProperty ("note", eng::DrumEngine::kDefaultFirstNote + i)));
        const juce::String path = p.getProperty ("file").toString();
        if (path.isEmpty() || ! juce::File::isAbsolutePath (path))
            continue;
        const juce::File file (path);
        if (! loadSample (i, file))
        {
            // se recuerda la ruta para avisar en el pad y no perderla al volver a guardar
            padFiles[(size_t) i] = file;
            missing[(size_t) i] = true;
        }
    }
    sendChangeMessage();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DrumProcessor(); }
