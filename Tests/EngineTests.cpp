// Pruebas del motor con audio sintetico. Devuelve 0 si pasa todo.

#include "Engine/DrumEngine.h"

#include <cmath>
#include <cstdio>

static int failures = 0;

#define CHECK(cond, ...) \
    do { if (! (cond)) { ++failures; std::printf ("  FALLA: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (false)

static constexpr double kRate = 48000.0;
static constexpr int kBlock = 512;

/** Un sample de mentira: `length` muestras a 1.0 (facil de detectar). */
static eng::SamplePtr makeClick (int length, double rate = kRate)
{
    auto s = std::make_shared<eng::Sample>();
    s->sampleRate = rate;
    s->buffer.setSize (1, length);
    for (int i = 0; i < length; ++i)
        s->buffer.setSample (0, i, 1.0f);
    return s;
}

static int firstNonZero (const juce::AudioBuffer<float>& b, int channel)
{
    for (int i = 0; i < b.getNumSamples(); ++i)
        if (std::abs (b.getSample (channel, i)) > 1.0e-6f)
            return i;
    return -1;
}

static void testSampleAccurate()
{
    std::printf ("Golpe en la muestra exacta del evento MIDI\n");
    eng::DrumEngine e;
    e.prepare (kRate);
    e.setSample (3, makeClick (2000));

    juce::AudioBuffer<float> out (2, kBlock);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, eng::DrumEngine::kDefaultFirstNote + 3, (juce::uint8) 127), 137);
    e.process (out, midi);

    CHECK (firstNonZero (out, 0) == 137, "empieza en %d, se esperaba 137", firstNonZero (out, 0));
    CHECK (firstNonZero (out, 1) == 137, "el canal derecho empieza en %d", firstNonZero (out, 1));
    CHECK (std::abs (out.getSample (0, 300) - 1.0f) < 1.0e-3f, "nivel %f, se esperaba 1.0 a velocity 127", out.getSample (0, 300));
}

static void testVelocityAndEmptyPad()
{
    std::printf ("Velocity y pad vacio\n");
    eng::DrumEngine e;
    e.prepare (kRate);
    e.setSample (0, makeClick (2000));

    juce::AudioBuffer<float> out (2, kBlock);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, eng::DrumEngine::kDefaultFirstNote, 0.5f), 0);
    midi.addEvent (juce::MidiMessage::noteOn (1, eng::DrumEngine::kDefaultFirstNote + 5, 1.0f), 0);   // pad sin sample
    e.process (out, midi);

    CHECK (std::abs (out.getSample (0, 10) - 0.5f) < 0.01f, "nivel %f, se esperaba ~0.5", out.getSample (0, 10));
    CHECK (e.padFlash[5].load() > 0, "el pad vacio tambien tiene que encender su luz");
}

static void testResampleAndEnd()
{
    std::printf ("Sample a otra frecuencia de muestreo y fin del sonido\n");
    eng::DrumEngine e;
    e.prepare (kRate);
    e.setSample (1, makeClick (100, kRate / 2.0));   // 100 muestras a 24 kHz = 200 muestras a 48 kHz

    juce::AudioBuffer<float> out (2, kBlock);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, eng::DrumEngine::kDefaultFirstNote + 1, 1.0f), 0);
    e.process (out, midi);

    int last = -1;
    for (int i = 0; i < kBlock; ++i)
        if (std::abs (out.getSample (0, i)) > 1.0e-6f)
            last = i;
    CHECK (last >= 195 && last <= 201, "termina en %d, se esperaba ~199", last);

    juce::MidiBuffer none;
    e.process (out, none);
    CHECK (firstNonZero (out, 0) < 0, "el bloque siguiente tiene que estar en silencio");
}

static void testUiTrigger()
{
    std::printf ("Golpe desde la interfaz\n");
    eng::DrumEngine e;
    e.prepare (kRate);
    e.setSample (15, makeClick (2000));
    e.triggerPadFromUI (15, 0.25f);

    juce::AudioBuffer<float> out (2, kBlock);
    juce::MidiBuffer midi;
    e.process (out, midi);
    CHECK (std::abs (out.getSample (0, 0) - 0.25f) < 0.01f, "nivel %f, se esperaba 0.25", out.getSample (0, 0));
}

static void testManyHits()
{
    std::printf ("Mas golpes que voces: no se cuelga ni explota\n");
    eng::DrumEngine e;
    e.prepare (kRate);
    e.setSample (0, makeClick (48000));

    juce::AudioBuffer<float> out (2, kBlock);
    juce::MidiBuffer midi;
    for (int i = 0; i < 64; ++i)
        midi.addEvent (juce::MidiMessage::noteOn (1, eng::DrumEngine::kDefaultFirstNote, 1.0f), i * 4);
    e.process (out, midi);
    const float peak = out.getMagnitude (0, 0, kBlock);
    CHECK (peak <= (float) eng::DrumEngine::kMaxVoices + 0.01f, "pico %f, mas voces que el maximo", peak);
}

int main()
{
    testSampleAccurate();
    testVelocityAndEmptyPad();
    testResampleAndEnd();
    testUiTrigger();
    testManyHits();
    std::printf (failures == 0 ? "OK: todo paso\n" : "%d falla(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
