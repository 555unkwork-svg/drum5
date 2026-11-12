#pragma once

#include "Engine/DrumEngine.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

/** Procesador del plugin: samples de los pads, parametros y estado guardado.
    Los samples se cargan y se cambian solo desde el hilo de mensajes. */
class DrumProcessor final : public juce::AudioProcessor,
                            public juce::ChangeBroadcaster,
                            private juce::Timer
{
public:
    static constexpr double kMaxSeconds = 30.0;   // largo maximo de un sample

    DrumProcessor();
    ~DrumProcessor() override;

    //---- AudioProcessor ----
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //---- pads (hilo de mensajes) ----
    /** Carga un archivo en un pad. Devuelve false (y deja el pad como estaba) si no se pudo leer. */
    bool loadSample (int pad, const juce::File& file);
    void clearPad (int pad);
    juce::File getPadFile (int pad) const { return padFiles[(size_t) pad]; }
    /** true si el pad tiene una ruta guardada pero el archivo no esta (se movio o se borro). */
    bool isPadMissing (int pad) const { return missing[(size_t) pad]; }
    juce::String getPadName (int pad) const;

    int getSelectedPad() const { return selectedPad; }
    void setSelectedPad (int pad);

    juce::AudioFormatManager formats;
    juce::AudioProcessorValueTreeState apvts;
    eng::DrumEngine engine;

private:
    void timerCallback() override { engine.purgeRetired(); }
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::array<juce::File, eng::kNumPads> padFiles;
    std::array<bool, eng::kNumPads> missing {};
    int selectedPad = 0;
    juce::LinearSmoothedValue<float> masterGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumProcessor)
};
