#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
// orden de los parametros de cada pad (indices de PadParams::p)
enum PadParam { pRetrigger = 0, pRate, pMode, pHz, pLatch, pGate, pAccent, pAccentGroup, pRamp, pRampBeats, pPitchVar, kNumPadParams };
const char* const padParamNames[kNumPadParams] = { "rtg", "rate", "mode", "hz", "latch", "gate", "acc", "accgrp", "ramp", "rampbt", "pvar" };
const int accentGroups[] = { 2, 3, 4 };
const float rampBeatChoices[] = { 1.0f, 2.0f, 4.0f };
}

DrumProcessor::DrumProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formats.registerBasicFormats();   // WAV, AIFF, FLAC, OGG y MP3 (JUCE_USE_MP3AUDIOFORMAT)

    for (int i = 0; i < eng::kNumPads; ++i)
        for (int k = 0; k < kNumPadParams; ++k)
            padParams[(size_t) i].p[k] = apvts.getRawParameterValue (padParamId (i, padParamNames[k]));
    swingParam = apvts.getRawParameterValue ("swing");
    humanizeParam = apvts.getRawParameterValue ("hum");
    pressureParam = apvts.getRawParameterValue ("press");
    masterParam = apvts.getRawParameterValue ("master");
    startTimer (500);
}

DrumProcessor::~DrumProcessor()
{
    alive->store (false);
    stopTimer();
    loader.removeAllJobs (true, 10000);
}

juce::String DrumProcessor::padParamId (int pad, const char* name) { return "p" + juce::String (pad + 1) + "_" + name; }

const juce::StringArray& DrumProcessor::rateNames()
{
    static const juce::StringArray names { "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T" };
    return names;
}

juce::AudioProcessorValueTreeState::ParameterLayout DrumProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    const auto pct = AudioParameterFloatAttributes().withLabel ("%");

    for (int i = 0; i < eng::kNumPads; ++i)
    {
        const String n = "Pad " + String (i + 1) + " ";
        auto id = [i] (const char* name) { return ParameterID { padParamId (i, name), 1 }; };
        auto group = std::make_unique<AudioProcessorParameterGroup> ("pad" + String (i + 1), "Pad " + String (i + 1), " | ");
        group->addChild (std::make_unique<AudioParameterBool> (id ("rtg"), n + "Retrigger", true));
        group->addChild (std::make_unique<AudioParameterChoice> (id ("rate"), n + "Rate", rateNames(), 2));
        group->addChild (std::make_unique<AudioParameterChoice> (id ("mode"), n + "Modo", StringArray { "SYNC", "LIBRE" }, 0));
        group->addChild (std::make_unique<AudioParameterFloat> (id ("hz"), n + "Libre", NormalisableRange<float> (1.0f, 30.0f, 0.1f, 0.5f), 8.0f,
                                                                AudioParameterFloatAttributes().withLabel ("Hz")));
        group->addChild (std::make_unique<AudioParameterBool> (id ("latch"), n + "Latch", false));
        group->addChild (std::make_unique<AudioParameterFloat> (id ("gate"), n + "Gate", NormalisableRange<float> (5.0f, 100.0f, 1.0f), 100.0f, pct));
        group->addChild (std::make_unique<AudioParameterFloat> (id ("acc"), n + "Accent", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, pct));
        group->addChild (std::make_unique<AudioParameterChoice> (id ("accgrp"), n + "Accent grupo", StringArray { "2", "3", "4" }, 2));
        group->addChild (std::make_unique<AudioParameterBool> (id ("ramp"), n + "Rate Ramp", false));
        group->addChild (std::make_unique<AudioParameterChoice> (id ("rampbt"), n + "Rate Ramp tiempos", StringArray { "1", "2", "4" }, 1));
        group->addChild (std::make_unique<AudioParameterFloat> (id ("pvar"), n + "Pitch Var", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                                                                AudioParameterFloatAttributes().withLabel ("st")));
        layout.add (std::move (group));
    }

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "swing", 1 }, "Swing", NormalisableRange<float> (50.0f, 75.0f, 1.0f), 50.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "hum", 1 }, "Humanize", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f, pct));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "press", 1 }, "Presion", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "master", 1 }, "Master", NormalisableRange<float> (-48.0f, 6.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));
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
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (masterParam->load(), -48.0f));
}

void DrumProcessor::readSettings()
{
    for (int i = 0; i < eng::kNumPads; ++i)
    {
        const auto& pp = padParams[(size_t) i].p;
        auto& s = engine.pads[(size_t) i];
        s.retrigger = pp[pRetrigger]->load() > 0.5f;
        s.rate = (eng::Rate) juce::jlimit (0, eng::kNumRates - 1, (int) pp[pRate]->load());
        s.freeMode = pp[pMode]->load() > 0.5f;
        s.freeHz = pp[pHz]->load();
        s.latch = pp[pLatch]->load() > 0.5f;
        s.gate = pp[pGate]->load() * 0.01f;
        s.accent = pp[pAccent]->load() * 0.01f;
        s.accentGroup = accentGroups[juce::jlimit (0, 2, (int) pp[pAccentGroup]->load())];
        s.ramp = pp[pRamp]->load() > 0.5f;
        s.rampBeats = rampBeatChoices[juce::jlimit (0, 2, (int) pp[pRampBeats]->load())];
        s.pitchVar = pp[pPitchVar]->load();
    }
    engine.globals.swing = swingParam->load() * 0.01f;
    engine.globals.humanize = humanizeParam->load() * 0.01f;
    engine.globals.pressure = pressureParam->load() > 0.5f;
}

void DrumProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    readSettings();

    eng::Transport transport;
    if (auto* head = getPlayHead())
        if (auto position = head->getPosition())
        {
            if (auto bpm = position->getBpm())
                transport.bpm = *bpm;
            if (auto ppq = position->getPpqPosition(); ppq && position->getIsPlaying())
            {
                transport.playing = true;
                transport.ppq = *ppq;
            }
        }
    hostBpm.store (transport.bpm);

    engine.process (buffer, midi, transport);
    masterGain.setTargetValue (juce::Decibels::decibelsToGain (masterParam->load(), -48.0f));
    masterGain.applyGain (buffer, buffer.getNumSamples());
}

juce::AudioProcessorEditor* DrumProcessor::createEditor() { return new DrumEditor (*this); }

//==============================================================================
eng::SamplePtr DrumProcessor::readSample (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return {};

    const auto length = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (kMaxSeconds * reader->sampleRate));
    auto sample = std::make_shared<eng::Sample>();
    sample->sampleRate = reader->sampleRate;
    sample->buffer.setSize ((int) juce::jlimit (1u, 2u, reader->numChannels), length);
    if (! reader->read (&sample->buffer, 0, length, 0, true, true))
        return {};
    return sample;
}

void DrumProcessor::applySample (int pad, const juce::File& file, eng::SamplePtr sample)
{
    engine.setSample (pad, std::move (sample));
    padFiles[(size_t) pad] = file;
    missing[(size_t) pad] = false;
    sendChangeMessage();
}

bool DrumProcessor::loadSample (int pad, const juce::File& file)
{
    if (pad < 0 || pad >= eng::kNumPads)
        return false;
    auto sample = readSample (file);
    if (sample == nullptr)
        return false;
    applySample (pad, file, std::move (sample));
    return true;
}

void DrumProcessor::loadSampleAsync (int pad, const juce::File& file)
{
    if (pad < 0 || pad >= eng::kNumPads)
        return;
    ++loading[(size_t) pad];
    sendChangeMessage();

    loader.addJob ([this, pad, file, flag = alive]
    {
        auto sample = readSample (file);
        juce::MessageManager::callAsync ([this, pad, file, flag, sample = std::move (sample)]() mutable
        {
            if (! flag->load())
                return;
            --loading[(size_t) pad];
            if (sample != nullptr)
                applySample (pad, file, std::move (sample));
            else
            {
                loadError = "No se pudo leer \"" + file.getFileName() + "\" (formatos: WAV, AIFF, FLAC, MP3, OGG)";
                sendChangeMessage();
            }
        });
    });
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
