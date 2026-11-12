// Pruebas del motor con audio sintetico. Devuelve 0 si pasa todo.

#include "Engine/DrumEngine.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;

#define CHECK(cond, ...) \
    do { if (! (cond)) { ++failures; std::printf ("  FALLA: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (false)

static constexpr double kRate = 48000.0;
static constexpr int kBlock = 512;
static constexpr int kNote = eng::DrumEngine::kDefaultFirstNote;
static constexpr double kBeat = 24000.0;   // muestras por negra a 120 BPM y 48 kHz

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

/** Corre el motor bloque a bloque y junta todo lo que suena. */
struct Run
{
    eng::DrumEngine e;
    bool playing = false;
    double ppqStart = 0.0;
    std::vector<std::pair<int, juce::MidiMessage>> events;   // (muestra absoluta, mensaje)
    std::vector<float> out;

    Run() { e.prepare (kRate); }

    void at (int sample, const juce::MidiMessage& m) { events.push_back ({ sample, m }); }

    void run (int totalSamples)
    {
        out.assign ((size_t) totalSamples, 0.0f);
        juce::AudioBuffer<float> buf (2, kBlock);
        for (int start = 0; start < totalSamples; start += kBlock)
        {
            juce::MidiBuffer midi;
            for (auto& [t, m] : events)
                if (t >= start && t < start + kBlock)
                    midi.addEvent (m, t - start);
            eng::Transport tr;
            tr.playing = playing;
            tr.bpm = 120.0;
            tr.ppq = ppqStart + start / kBeat;
            e.process (buf, midi, tr);
            for (int i = 0; i < kBlock && start + i < totalSamples; ++i)
                out[(size_t) (start + i)] = buf.getSample (0, i);
        }
    }

    /** Donde arranca cada golpe (el click dura menos que el intervalo, asi que hay silencio entre golpes). */
    std::vector<int> onsets() const
    {
        std::vector<int> r;
        for (size_t i = 0; i < out.size(); ++i)
            if (std::abs (out[i]) > 1.0e-4f && (i == 0 || std::abs (out[i - 1]) <= 1.0e-4f))
                r.push_back ((int) i);
        return r;
    }
};

static bool near (int a, double b, double tol = 2.0) { return std::abs ((double) a - b) <= tol; }

static void printList (const char* what, const std::vector<int>& v)
{
    std::printf ("    %s:", what);
    for (size_t i = 0; i < v.size() && i < 10; ++i)
        std::printf (" %d", v[i]);
    std::printf ("\n");
}

static void expectOnsets (const Run& r, std::initializer_list<double> expected, const char* what)
{
    const auto on = r.onsets();
    bool ok = on.size() >= expected.size();
    size_t i = 0;
    for (double x : expected)
    {
        if (i < on.size() && ! near (on[i], x))
            ok = false;
        ++i;
    }
    CHECK (ok, "%s", what);
    if (! ok)
        printList ("golpes", on);
}

//==============================================================================
static void testSampleAccurate()
{
    std::printf ("Primer golpe en la muestra exacta del evento MIDI\n");
    Run r;
    r.e.pads[3].retrigger = false;
    r.e.setSample (3, makeClick (2000));
    r.at (137, juce::MidiMessage::noteOn (1, kNote + 3, (juce::uint8) 127));
    r.run (kBlock * 8);
    expectOnsets (r, { 137 }, "el golpe tiene que empezar en 137");
    CHECK (r.onsets().size() == 1, "con RETRIGGER apagado suena una sola vez (%d)", (int) r.onsets().size());
    CHECK (std::abs (r.out[300] - 1.0f) < 1.0e-3f, "nivel %f, se esperaba 1.0 a velocity 127", r.out[300]);
}

static void testRollStopped()
{
    std::printf ("Roll con el proyecto parado: primer golpe al instante y despues 1/16 al tempo\n");
    Run r;
    r.e.setSample (0, makeClick (50));
    r.at (100, juce::MidiMessage::noteOn (1, kNote, 1.0f));
    r.at (100 + 20000, juce::MidiMessage::noteOff (1, kNote));
    r.run (40000);
    expectOnsets (r, { 100, 6100, 12100, 18100 }, "golpes cada 6000 muestras desde el primero");
    CHECK (r.onsets().size() == 4, "al soltar la nota el roll termina (%d golpes)", (int) r.onsets().size());
}

static void testRollOnProjectGrid()
{
    std::printf ("Roll con el proyecto en play: los golpes caen en la grilla del proyecto\n");
    Run r;
    r.playing = true;
    r.ppqStart = 0.1;   // el pad se toca un poco despues del tiempo
    r.e.setSample (0, makeClick (50));
    r.at (0, juce::MidiMessage::noteOn (1, kNote, 1.0f));
    r.run (20000);
    expectOnsets (r, { 0, 0.15 * kBeat, 0.4 * kBeat, 0.65 * kBeat }, "primer golpe al instante y despues en 0.25, 0.5 y 0.75 del tiempo");
}

static void testFirstHitSkipsCloseGrid()
{
    std::printf ("Si la grilla cae pegada al primer golpe, se saltea (no hay golpe doble)\n");
    Run r;
    r.playing = true;
    r.ppqStart = 0.24;   // a 0.01 negras del proximo 1/16
    r.e.setSample (0, makeClick (50));
    r.at (0, juce::MidiMessage::noteOn (1, kNote, 1.0f));
    r.run (20000);
    expectOnsets (r, { 0, 0.26 * kBeat }, "el segundo golpe tiene que ir en 0.5, no en 0.25");
}

static void testSwing()
{
    std::printf ("Swing 66 %% en 1/16: los pasos impares se atrasan como en la MPC\n");
    Run r;
    r.playing = true;
    r.e.globals.swing = 0.66f;
    r.e.setSample (0, makeClick (50));
    r.at (0, juce::MidiMessage::noteOn (1, kNote, 1.0f));
    r.run (30000);
    // grilla 1/16 = 0.25 negras; los impares se corren 0.32 de paso
    expectOnsets (r, { 0, 0.33 * kBeat, 0.5 * kBeat, 0.83 * kBeat, 1.0 * kBeat }, "golpes en 0, 0.33, 0.5, 0.83, 1.0");

    Run t;
    t.playing = true;
    t.e.globals.swing = 0.66f;
    t.e.pads[0].rate = eng::Rate::r16t;   // los tresillos no llevan swing
    t.e.setSample (0, makeClick (50));
    t.at (0, juce::MidiMessage::noteOn (1, kNote, 1.0f));
    t.run (15000);
    expectOnsets (t, { 0, kBeat / 6.0, kBeat / 3.0, kBeat / 2.0 }, "1/16T sin swing: golpes parejos cada 1/6 de tiempo");
}

static void testLatch()
{
    std::printf ("LATCH: el roll sigue al soltar y se corta tocando el pad otra vez\n");
    Run r;
    r.e.pads[0].latch = true;
    r.e.setSample (0, makeClick (50));
    r.at (0, juce::MidiMessage::noteOn (1, kNote, 1.0f));
    r.at (1000, juce::MidiMessage::noteOff (1, kNote));
    r.at (25000, juce::MidiMessage::noteOn (1, kNote, 1.0f));
    r.at (25500, juce::MidiMessage::noteOff (1, kNote));
    r.run (40000);
    CHECK (r.onsets().size() == 5, "se esperaban 5 golpes (0, 6000, 12000, 18000, 24000), hubo %d", (int) r.onsets().size());
}

static void testFreeMode()
{
    std::printf ("Modo LIBRE a 10 Hz: un golpe cada 4800 muestras, sin grilla\n");
    Run r;
    r.playing = true;
    r.ppqStart = 0.1;
    r.e.pads[0].freeMode = true;
    r.e.pads[0].freeHz = 10.0f;
    r.e.setSample (0, makeClick (50));
    r.at (333, juce::MidiMessage::noteOn (1, kNote, 1.0f));
    r.run (16000);
    expectOnsets (r, { 333, 5133, 9933, 14733 }, "golpes cada 4800 muestras desde la nota");
}

static void testAccentAndGate()
{
    std::printf ("Accent cada 2 golpes al 50 %% y Gate al 50 %%\n");
    Run r;
    r.e.pads[0].accent = 0.5f;
    r.e.pads[0].accentGroup = 2;
    r.e.pads[0].gate = 0.5f;
    r.e.setSample (0, makeClick (20000));
    r.at (0, juce::MidiMessage::noteOn (1, kNote, 1.0f));
    r.run (25000);
    CHECK (std::abs (r.out[100] - 1.0f) < 0.01f, "golpe 1 acentuado: %f", r.out[100]);
    CHECK (std::abs (r.out[6100] - 0.5f) < 0.01f, "golpe 2 suave: %f", r.out[6100]);
    CHECK (std::abs (r.out[12100] - 1.0f) < 0.01f, "golpe 3 acentuado: %f", r.out[12100]);
    CHECK (std::abs (r.out[3500]) < 1.0e-4f, "con gate 50 %% el golpe se corta antes de 3000 + fundido: %f", r.out[3500]);
    CHECK (r.out[2900] > 0.9f, "antes del gate tiene que sonar: %f", r.out[2900]);
}

static void testRamp()
{
    std::printf ("Rate Ramp: despues de 1 tiempo pasa de 1/16 a 1/32\n");
    Run r;
    r.e.pads[0].ramp = true;
    r.e.pads[0].rampBeats = 1.0f;
    r.e.setSample (0, makeClick (50));
    r.at (0, juce::MidiMessage::noteOn (1, kNote, 1.0f));
    r.run (40000);
    expectOnsets (r, { 0, 6000, 12000, 18000, 24000, 27000, 30000, 33000 }, "cada 6000 hasta el tiempo 1, despues cada 3000");
}

static void testPressure()
{
    std::printf ("PRESION: con el boton encendido, el aftertouch manda la fuerza de las repeticiones\n");
    for (const bool on : { false, true })
    {
        Run r;
        r.e.globals.pressure = on;
        r.e.setSample (0, makeClick (50));
        r.at (0, juce::MidiMessage::noteOn (1, kNote, 1.0f));
        r.at (1000, juce::MidiMessage::channelPressureChange (1, 64));
        r.run (14000);
        const float second = r.out[6010];
        const float expected = on ? 64.0f / 127.0f : 1.0f;
        CHECK (std::abs (second - expected) < 0.01f, "PRESION %s: segundo golpe %f, se esperaba %f", on ? "encendido" : "apagado", second, expected);
    }
}

static void testLoopJump()
{
    std::printf ("Loop del proyecto: al volver al inicio el roll vuelve a la grilla\n");
    eng::DrumEngine e;
    e.prepare (kRate);
    e.setSample (0, makeClick (50));
    juce::AudioBuffer<float> buf (2, kBlock);
    eng::Transport tr { true, 3.9, 120.0 };
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, kNote, 1.0f), 0);
    e.process (buf, midi, tr);

    tr.ppq = 0.0;   // salto al inicio del loop
    juce::MidiBuffer none;
    e.process (buf, none, tr);
    CHECK (std::abs (buf.getSample (0, 0) - 1.0f) < 0.01f, "tiene que sonar en el tiempo 0 del loop: %f", buf.getSample (0, 0));
}

static void testResampleAndManyHits()
{
    std::printf ("Sample a otra frecuencia de muestreo y mas golpes que voces\n");
    Run r;
    r.e.pads[1].retrigger = false;
    r.e.setSample (1, makeClick (100, kRate / 2.0));   // 100 muestras a 24 kHz = 200 a 48 kHz
    r.at (0, juce::MidiMessage::noteOn (1, kNote + 1, 1.0f));
    r.run (kBlock);
    int last = -1;
    for (int i = 0; i < kBlock; ++i)
        if (std::abs (r.out[(size_t) i]) > 1.0e-6f)
            last = i;
    CHECK (last >= 195 && last <= 201, "termina en %d, se esperaba ~199", last);

    eng::DrumEngine e;
    e.prepare (kRate);
    e.pads[0].retrigger = false;
    e.setSample (0, makeClick (48000));
    juce::AudioBuffer<float> buf (2, kBlock);
    juce::MidiBuffer midi;
    for (int i = 0; i < 100; ++i)
        midi.addEvent (juce::MidiMessage::noteOn (1, kNote, 1.0f), i * 4);
    e.process (buf, midi, {});
    CHECK (buf.getMagnitude (0, 0, kBlock) <= (float) eng::DrumEngine::kMaxVoices + 0.01f, "mas voces que el maximo");
}

int main()
{
    testSampleAccurate();
    testRollStopped();
    testRollOnProjectGrid();
    testFirstHitSkipsCloseGrid();
    testSwing();
    testLatch();
    testFreeMode();
    testAccentAndGate();
    testRamp();
    testPressure();
    testLoopJump();
    testResampleAndManyHits();
    std::printf (failures == 0 ? "OK: todo paso\n" : "%d falla(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
