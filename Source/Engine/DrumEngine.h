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

/** Divisiones del Note Repeat de la MPC2000XL ("Timing" en el manual). */
enum class Rate { r8 = 0, r8t, r16, r16t, r32, r32t };
constexpr int kNumRates = 6;
/** Largo de cada division en negras (la MPC trabaja a 96 ticks por negra: 1/16 = 24 ticks). */
double rateBeats (Rate);
/** La division siguiente para Rate Ramp (1/16 -> 1/32); la mas rapida se queda igual. */
Rate fasterRate (Rate);
/** Como en la MPC, el swing solo se aplica a 1/8 y 1/16. */
bool rateSwings (Rate);

struct PadSettings
{
    // sonido
    float gainDb = 0.0f;       // VOL
    float pan = 0.0f;          // -1..1
    float pitch = 0.0f;        // semitonos
    float decay = 1.0f;        // 0.05..1: parte del sample que suena; lo que sobra se apaga con un fundido
    bool reverse = false;
    bool mute = false;
    bool mono = false;         // un golpe nuevo corta el anterior del mismo pad
    int choke = 0;             // 0 = sin grupo; 1..8: los pads del mismo grupo se cortan entre si
    int output = 0;            // 0 = Main; 1..15 = Salida 2..16

    // roll
    bool retrigger = false;
    Rate rate = Rate::r16;
    bool freeMode = false;     // LIBRE: golpes por segundo, sin grilla
    float freeHz = 8.0f;
    bool latch = false;        // el roll sigue sin mantener el pad; se corta tocandolo otra vez
    float gate = 1.0f;         // 0.05..1: largo de cada golpe respecto del intervalo; 1 = suena entero
    float accent = 0.0f;       // 0..1: cuanto mas suaves van los golpes no acentuados
    int accentGroup = 4;       // se acentua el primero de cada grupo de 2, 3 o 4 golpes
    bool ramp = false;         // Rate Ramp: pasa a la division siguiente
    float rampBeats = 2.0f;    // ... despues de estos tiempos
    float pitchVar = 0.0f;     // semitonos a los que llega el roll en 1 compas (4 tiempos)
};

struct GlobalSettings
{
    float swing = 0.5f;        // 0.5 = sin swing, hasta 0.75 (como la MPC)
    float humanize = 0.0f;     // 0..1: pequenas variaciones de tiempo (siempre tarde) y fuerza
    bool pressure = false;     // PRESION: la presion del pad (aftertouch) manda la fuerza de las repeticiones
    float velSens = 1.0f;      // VEL: 0 = siempre a tope, 1 = sigue la velocity
    int maxVoices = 32;        // VOCES: golpes sonando a la vez (los mas viejos se cortan)
    int midiChannel = 0;       // 0 = todos
    int rangeLo = 0, rangeHi = 127;   // notas fuera del rango se ignoran
};

/** Datos del host para el bloque actual. */
struct Transport
{
    bool playing = false;
    double ppq = 0.0;          // posicion en negras al inicio del bloque (solo si playing)
    double bpm = 120.0;
};

/** 16 pads con roll al estilo del Note Repeat de la MPC: el primer golpe suena al instante y los
    siguientes caen en la grilla del proyecto. Todo con precision de muestra. */
class DrumEngine
{
public:
    static constexpr int kMaxVoices = 64;
    static constexpr int kNumOutputs = 16;   // Main + Salida 2..16
    static constexpr int kDefaultFirstNote = 60;   // C5 en FL Studio, igual que sampler5

    DrumEngine();

    //---- hilo de audio ----
    void prepare (double sampleRate);
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Transport&);

    /** Se escriben desde el hilo de audio justo antes de process(). */
    std::array<PadSettings, kNumPads> pads;
    GlobalSettings globals;
    /** Primer canal (el izquierdo) de cada salida en el buffer de process(); -1 = esa salida no esta activa y va a Main. */
    std::array<int, kNumOutputs> outputChannel {};

    //---- hilo de mensajes ----
    void setSample (int pad, SamplePtr sample);
    SamplePtr getSample (int pad) const;
    /** Libera samples viejos que el hilo de audio ya solto. Llamar cada tanto. */
    void purgeRetired();
    void triggerPadFromUI (int pad, bool down, float velocity = 1.0f);

    //---- cualquier hilo ----
    std::array<std::atomic<int>, kNumPads> padNote;
    /** Cuenta regresiva de "pad encendido" para la interfaz (en bloques). */
    std::array<std::atomic<int>, kNumPads> padFlash;
    /** true mientras el pad tiene un roll andando (para la interfaz). */
    std::array<std::atomic<bool>, kNumPads> padRolling;
    std::atomic<int> learnPad { -1 };          // MIDI Learn: pad esperando una nota, o -1
    std::atomic<int> lastLearnedPad { -1 };    // lo lee la interfaz y lo vuelve a -1

private:
    struct Voice
    {
        bool active = false;
        int pad = 0;
        SamplePtr src;
        double pos = 0.0, rate = 1.0;
        float gainL = 1.0f, gainR = 1.0f;
        int outL = 0, outR = 1;      // canales del buffer
        bool reverse = false;
        double endPos = 0.0;         // DECAY: hasta donde suena (en muestras del sample)
        double fadeLen = 0.0;        // largo del fundido final; 0 = sin fundido
        juce::int64 gateLeft = -1;   // muestras hasta cortar; -1 = sin corte
        float fade = 1.0f;           // baja a 0 despues del gate, del choke o del mono
        juce::uint32 age = 0;
    };

    struct Roll
    {
        bool active = false;
        bool held = false;           // la nota (o el clic) sigue apretada
        float velocity = 1.0f;       // fuerza del primer golpe
        float pressure = -1.0f;      // ultimo aftertouch 0..1; -1 = todavia no llego
        int hits = 0;                // golpes dados (el primero es el 0)
        double startBeat = 0.0;      // en negras del reloj usado
        double nextBeat = 0.0;       // proximo golpe (modo SYNC)
        double nextSample = 0.0;     // proximo golpe (modo LIBRE), en muestras desde el inicio del bloque
    };

    void noteOn (int pad, float velocity, int time);
    void noteOff (int pad);
    void fireHit (int pad, int time, bool first);
    void scheduleNext (int pad, double hitBeat, double hitSample);
    void runRolls (juce::AudioBuffer<float>& out, int from, int to);
    void render (juce::AudioBuffer<float>& out, int start, int num);
    double currentStepBeats (int pad, double beatNow) const;
    double nextGridAfter (double beat, double step, bool swing) const;
    double beatAt (int sampleInBlock) const { return blockBeat + sampleInBlock * beatsPerSample; }

    double hostRate = 44100.0;
    std::array<Voice, kMaxVoices> voices;
    std::array<Roll, kNumPads> rolls;
    juce::uint32 ageCounter = 0;
    juce::Random random;

    // reloj en negras: el del proyecto si esta en play; si no, uno propio al tempo del proyecto
    double blockBeat = 0.0, beatsPerSample = 0.0, internalBeat = 0.0, expectedBeat = -1.0;
    bool wasPlaying = false;

    juce::SpinLock swapLock;
    std::array<SamplePtr, kNumPads> shared;    // protegido por swapLock
    std::array<SamplePtr, kNumPads> current;   // copia del hilo de audio
    std::vector<SamplePtr> retired;            // nunca el hilo de audio
    juce::CriticalSection retiredLock;         // por si el DAW carga un proyecto desde otro hilo

    // golpes desde la interfaz: un bit por pad para apretar y otro para soltar
    std::atomic<juce::uint32> uiDown { 0 }, uiUp { 0 };
    std::array<std::atomic<float>, kNumPads> uiVelocity;
};
}
