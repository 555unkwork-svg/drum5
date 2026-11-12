#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace eng
{
constexpr int kNumPads = 16;

/** El audio de un pad. Inmutable: para cambiarlo se arma uno nuevo y se intercambia. */
struct Sample
{
    juce::AudioBuffer<float> buffer;   // 1 o 2 canales
    double sampleRate = 44100.0;
};
using SamplePtr = std::shared_ptr<const Sample>;

/** 16 pads, cada uno con su sample. Los golpes suenan con precision de muestra
    (en la posicion exacta del evento MIDI dentro del bloque). */
class DrumEngine
{
public:
    static constexpr int kMaxVoices = 32;
    static constexpr int kDefaultFirstNote = 60;   // C5 en FL Studio, igual que sampler5

    DrumEngine();

    //---- hilo de audio ----
    void prepare (double sampleRate);
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi);

    //---- hilo de mensajes ----
    void setSample (int pad, SamplePtr sample);
    SamplePtr getSample (int pad) const;
    /** Libera samples viejos que el hilo de audio ya solto. Llamar cada tanto. */
    void purgeRetired();
    void triggerPadFromUI (int pad, float velocity);

    //---- cualquier hilo ----
    std::array<std::atomic<int>, kNumPads> padNote;
    /** Cuenta regresiva de "pad encendido" para la interfaz (en bloques). */
    std::array<std::atomic<int>, kNumPads> padFlash;

private:
    struct Voice
    {
        bool active = false;
        int pad = 0;
        SamplePtr src;
        double pos = 0.0, rate = 1.0;
        float gain = 1.0f;
    };

    void fire (int pad, float velocity);
    void render (juce::AudioBuffer<float>& out, int start, int num);

    double hostRate = 44100.0;
    std::array<Voice, kMaxVoices> voices;
    int nextVoice = 0;

    juce::SpinLock swapLock;
    std::array<SamplePtr, kNumPads> shared;    // protegido por swapLock
    std::array<SamplePtr, kNumPads> current;   // copia del hilo de audio
    std::vector<SamplePtr> retired;            // solo hilo de mensajes

    // golpes desde la interfaz: un bit por pad y la fuerza de cada uno
    std::atomic<juce::uint32> uiHits { 0 };
    std::array<std::atomic<float>, kNumPads> uiVelocity;
};
}
