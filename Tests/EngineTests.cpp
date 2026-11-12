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

    Run()
    {
        e.prepare (kRate);
        for (auto& p : e.pads)
            p.retrigger = true;   // las pruebas de roll lo usan encendido (en el plugin viene apagado)
    }

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
    e.pads[0].retrigger = true;
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

static void testEventAtBlockStartWithLateRoll()
{
    std::printf ("Evento MIDI al inicio del bloque con un golpe del roll atrasado (no escribe fuera del buffer)\n");
    eng::DrumEngine e;
    e.prepare (kRate);
    e.pads[0].retrigger = true;
    e.setSample (0, makeClick (50));
    e.setSample (1, makeClick (50));

    // bloques de 500 muestras: el 1/16 (6000 muestras) cae justo al inicio del bloque 12
    constexpr int n = 500;
    juce::AudioBuffer<float> buf (2, n);
    eng::Transport tr { true, 0.0, 120.0 };
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, kNote, 1.0f), 0);
    for (int b = 0; b < 12; ++b)
    {
        e.process (buf, midi, tr);
        midi.clear();
        tr.ppq += n / kBeat;
    }
    // el DAW informa la posicion con un redondeo minimo y hay una nota en la muestra 0 del bloque
    tr.ppq = 0.25 + 1.0e-9;
    midi.addEvent (juce::MidiMessage::noteOn (1, kNote + 1, 1.0f), 0);
    e.process (buf, midi, tr);
    // y un bloque vacio, que algunos DAW mandan
    juce::AudioBuffer<float> empty (2, 0);
    juce::MidiBuffer none;
    e.process (empty, none, tr);
    CHECK (true, "");
}

static void testDefaultIsOneShot()
{
    std::printf ("Por defecto el retrigger viene apagado: mantener la nota suena una sola vez\n");
    eng::DrumEngine e;
    e.prepare (kRate);
    e.setSample (0, makeClick (50));
    juce::AudioBuffer<float> buf (2, kBlock);
    int hits = 0;
    for (int b = 0; b < 100; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, kNote, 1.0f), 0);
        e.process (buf, midi, {});
        for (int i = 0; i < kBlock; ++i)
            if (std::abs (buf.getSample (0, i)) > 1.0e-4f && (i == 0 || std::abs (buf.getSample (0, i - 1)) <= 1.0e-4f))
                ++hits;
    }
    CHECK (hits == 1, "sono %d veces", hits);
}

static void testGridHitAtBlockEnd()
{
    std::printf ("Golpe de la grilla entre las dos ultimas muestras del bloque (antes colgaba el audio)\n");
    // 1/16 a 120 BPM = 6000 muestras; con bloques de 499 el golpe cae en 5988 + 11.x: fraccion antes del final
    for (int n : { 499, 501, 511, 64, 1 })
    {
        eng::DrumEngine e;
        e.prepare (kRate);
        e.pads[0].retrigger = true;
        e.setSample (0, makeClick (10));
        juce::AudioBuffer<float> buf (2, n);
        eng::Transport tr { true, 0.0004, 117.3 };
        int hits = 0;
        float prev = 0.0f;
        const int blocks = 96000 / n;
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, kNote, 1.0f), 0);
            e.process (buf, midi, tr);
            tr.ppq += n * tr.bpm / 60.0 / kRate;
            for (int i = 0; i < n; ++i)
            {
                const float v = std::abs (buf.getSample (0, i));
                if (v > 1.0e-4f && prev <= 1.0e-4f)
                    ++hits;
                prev = v;
            }
        }
        // 2 segundos a 117.3 BPM = 3.91 tiempos = ~15.6 semicorcheas (+ el primer golpe)
        CHECK (hits >= 15 && hits <= 17, "bloques de %d: %d golpes, se esperaban ~16", n, hits);
    }
}

static void testStress()
{
    std::printf ("Estres: bloques al azar (y vacios), tempos, redondeos, loops, play/stop y notas al azar\n");
    eng::DrumEngine e;
    e.prepare (kRate);
    for (int p = 0; p < eng::kNumPads; ++p)
        e.setSample (p, makeClick (200 + p * 37));
    juce::Random rng (1234);
    juce::AudioBuffer<float> buf (2, 1024);
    eng::Transport tr { true, 0.0, 120.0 };
    bool finite = true;
    // salidas como las pide un DAW con 3 activas (6 canales) en un buffer de 2: las de afuera tienen que ir a los primeros
    for (int i = 0; i < eng::DrumEngine::kNumOutputs; ++i)
        e.outputChannel[(size_t) i] = i < 3 ? i * 2 : -1;

    for (int b = 0; b < 30000; ++b)
    {
        const int n = rng.nextInt (8) == 0 ? 0 : 1 + rng.nextInt (1024);
        buf.setSize (2, n, false, false, true);

        if (rng.nextInt (500) == 0) tr.playing = ! tr.playing;
        if (rng.nextInt (700) == 0) tr.ppq = rng.nextDouble() * 16.0;            // loop o salto
        if (rng.nextInt (900) == 0) tr.bpm = 60.0 + rng.nextDouble() * 140.0;    // cambio de tempo
        if (rng.nextInt (300) == 0)
        {
            auto& s = e.pads[(size_t) rng.nextInt (eng::kNumPads)];
            s.retrigger = rng.nextBool();
            s.rate = (eng::Rate) rng.nextInt (eng::kNumRates);
            s.freeMode = rng.nextInt (4) == 0;
            s.freeHz = 1.0f + rng.nextFloat() * 29.0f;
            s.latch = rng.nextInt (5) == 0;
            s.gate = 0.05f + rng.nextFloat() * 0.95f;
            s.accent = rng.nextFloat();
            s.accentGroup = 2 + rng.nextInt (3);
            s.ramp = rng.nextBool();
            s.rampBeats = (float) (1 << rng.nextInt (3));
            s.pitchVar = rng.nextFloat() * 24.0f - 12.0f;
            s.gainDb = rng.nextFloat() * 54.0f - 48.0f;
            s.pan = rng.nextFloat() * 2.0f - 1.0f;
            s.pitch = rng.nextFloat() * 48.0f - 24.0f;
            s.decay = 0.05f + rng.nextFloat() * 0.95f;
            s.reverse = rng.nextBool();
            s.mute = rng.nextInt (6) == 0;
            s.mono = rng.nextBool();
            s.choke = rng.nextInt (9);
            s.output = rng.nextInt (eng::DrumEngine::kNumOutputs);
            e.globals.maxVoices = 1 + rng.nextInt (eng::DrumEngine::kMaxVoices);
            e.globals.velSens = rng.nextFloat();
            e.globals.swing = 0.5f + rng.nextFloat() * 0.25f;
            e.globals.humanize = rng.nextFloat();
            e.globals.pressure = rng.nextBool();
        }

        juce::MidiBuffer midi;
        const int events = rng.nextInt (4);
        for (int k = 0; k < events; ++k)
        {
            const int t = n > 0 ? (rng.nextInt (3) == 0 ? 0 : rng.nextInt (n)) : 0;   // muchas en la muestra 0
            const int note = kNote + rng.nextInt (eng::kNumPads);
            switch (rng.nextInt (4))
            {
                case 0:  midi.addEvent (juce::MidiMessage::noteOn (1, note, rng.nextFloat()), t); break;
                case 1:  midi.addEvent (juce::MidiMessage::noteOff (1, note), t); break;
                case 2:  midi.addEvent (juce::MidiMessage::channelPressureChange (1, rng.nextInt (128)), t); break;
                default: midi.addEvent (juce::MidiMessage::aftertouchChange (1, note, rng.nextInt (128)), t); break;
            }
        }

        e.process (buf, midi, tr);
        for (int c = 0; c < buf.getNumChannels() && finite; ++c)
            for (int i = 0; i < n; ++i)
                if (! std::isfinite (buf.getSample (c, i))) { finite = false; break; }

        // el DAW informa la posicion siguiente con un redondeo al azar
        tr.ppq += (tr.playing ? n * tr.bpm / 60.0 / kRate : 0.0) + (rng.nextDouble() - 0.5) * 2.0e-9;
    }
    CHECK (finite, "aparecio un valor no numerico en la salida");
}

/** Toca un golpe en un motor ya preparado y devuelve el bloque (canales: 2 o mas). */
static juce::AudioBuffer<float> hit (eng::DrumEngine& e, std::initializer_list<std::pair<int, juce::MidiMessage>> events,
                                     int channels = 2, int length = 4096)
{
    juce::AudioBuffer<float> buf (channels, length);
    juce::MidiBuffer midi;
    for (auto& [t, m] : events)
        midi.addEvent (m, t);
    e.process (buf, midi, {});
    return buf;
}

static eng::SamplePtr makeRamp (int length)
{
    auto s = std::make_shared<eng::Sample>();
    s->sampleRate = kRate;
    s->buffer.setSize (1, length);
    for (int i = 0; i < length; ++i)
        s->buffer.setSample (0, i, (float) (i + 1) / (float) length);
    return s;
}

static int lastSound (const juce::AudioBuffer<float>& b, int ch = 0)
{
    int last = -1;
    for (int i = 0; i < b.getNumSamples(); ++i)
        if (std::abs (b.getSample (ch, i)) > 1.0e-5f)
            last = i;
    return last;
}

static void testPadSound()
{
    std::printf ("Sonido por pad: VOL, PAN, PITCH, DECAY, REVERSE y MUTE\n");
    const auto on = juce::MidiMessage::noteOn (1, kNote, 1.0f);
    {
        eng::DrumEngine e; e.prepare (kRate); e.setSample (0, makeClick (1000));
        e.pads[0].gainDb = -6.0f;
        const auto b = hit (e, { { 0, on } });
        CHECK (std::abs (b.getSample (0, 10) - 0.501f) < 0.01f, "VOL -6 dB: %f", b.getSample (0, 10));
    }
    {
        eng::DrumEngine e; e.prepare (kRate); e.setSample (0, makeClick (1000));
        e.pads[0].pan = -1.0f;
        const auto b = hit (e, { { 0, on } });
        CHECK (std::abs (b.getSample (0, 10) - 1.0f) < 0.01f && std::abs (b.getSample (1, 10)) < 1.0e-4f,
               "PAN a la izquierda: L %f R %f", b.getSample (0, 10), b.getSample (1, 10));
    }
    {
        eng::DrumEngine e; e.prepare (kRate); e.setSample (0, makeClick (1000));
        e.pads[0].pitch = 12.0f;
        const auto b = hit (e, { { 0, on } });
        CHECK (std::abs (lastSound (b) - 500) <= 2, "PITCH +12: dura %d, se esperaba ~500", lastSound (b));
    }
    {
        eng::DrumEngine e; e.prepare (kRate); e.setSample (0, makeClick (1000));
        e.pads[0].decay = 0.5f;
        const auto b = hit (e, { { 0, on } });
        CHECK (std::abs (lastSound (b) - 500) <= 2, "DECAY 50 %%: dura %d, se esperaba ~500", lastSound (b));
        CHECK (b.getSample (0, 300) > 0.99f && b.getSample (0, 450) < 0.5f, "DECAY: entero hasta el fundido y despues baja");
    }
    {
        eng::DrumEngine e; e.prepare (kRate); e.setSample (0, makeRamp (1000));
        e.pads[0].reverse = true;
        const auto b = hit (e, { { 0, on } });
        CHECK (b.getSample (0, 0) > 0.99f && b.getSample (0, 998) < 0.01f, "REVERSE: empieza por el final (%f, %f)",
               b.getSample (0, 0), b.getSample (0, 998));
    }
    {
        eng::DrumEngine e; e.prepare (kRate); e.setSample (0, makeClick (1000));
        e.pads[0].mute = true;
        const auto b = hit (e, { { 0, on } });
        CHECK (lastSound (b) < 0, "MUTE: no tiene que sonar");
        CHECK (e.padFlash[0].load() > 0, "MUTE: la luz del pad igual se enciende");
    }
}

static void testMonoChokeVoices()
{
    std::printf ("MONO, CHOKE, VOCES y VEL\n");
    const auto on0 = juce::MidiMessage::noteOn (1, kNote, 1.0f);
    const auto on1 = juce::MidiMessage::noteOn (1, kNote + 1, 1.0f);
    {
        eng::DrumEngine e; e.prepare (kRate); e.setSample (0, makeClick (3000));
        const auto poly = hit (e, { { 0, on0 }, { 1000, on0 } });
        CHECK (std::abs (poly.getSample (0, 1500) - 2.0f) < 0.01f, "POLY: los dos golpes se suman (%f)", poly.getSample (0, 1500));
    }
    {
        eng::DrumEngine e; e.prepare (kRate); e.setSample (0, makeClick (3000));
        e.pads[0].mono = true;
        const auto b = hit (e, { { 0, on0 }, { 1000, on0 } });
        CHECK (std::abs (b.getSample (0, 1500) - 1.0f) < 0.01f, "MONO: el golpe nuevo corta el anterior (%f)", b.getSample (0, 1500));
    }
    {
        eng::DrumEngine e; e.prepare (kRate); e.setSample (0, makeClick (3000)); e.setSample (1, makeClick (3000));
        e.pads[0].choke = 3; e.pads[1].choke = 3;
        const auto b = hit (e, { { 0, on0 }, { 1000, on1 } });
        CHECK (std::abs (b.getSample (0, 1500) - 1.0f) < 0.01f, "CHOKE mismo grupo: el pad 2 corta al 1 (%f)", b.getSample (0, 1500));
    }
    {
        eng::DrumEngine e; e.prepare (kRate); e.setSample (0, makeClick (3000)); e.setSample (1, makeClick (3000));
        e.pads[0].choke = 3; e.pads[1].choke = 4;
        const auto b = hit (e, { { 0, on0 }, { 1000, on1 } });
        CHECK (std::abs (b.getSample (0, 1500) - 2.0f) < 0.01f, "CHOKE otro grupo: no se cortan (%f)", b.getSample (0, 1500));
    }
    {
        eng::DrumEngine e; e.prepare (kRate); e.setSample (0, makeClick (3000));
        e.globals.maxVoices = 1;
        const auto b = hit (e, { { 0, on0 }, { 1000, on0 } });
        CHECK (std::abs (b.getSample (0, 1500) - 1.0f) < 0.01f, "VOCES 1: suena un golpe a la vez (%f)", b.getSample (0, 1500));
    }
    {
        eng::DrumEngine e; e.prepare (kRate); e.setSample (0, makeClick (3000));
        e.globals.velSens = 0.0f;
        const auto b = hit (e, { { 0, juce::MidiMessage::noteOn (1, kNote, 0.3f) } });
        CHECK (std::abs (b.getSample (0, 10) - 1.0f) < 0.01f, "VEL 0 %%: siempre a tope (%f)", b.getSample (0, 10));
    }
}

static void testMidiFiltersAndLearn()
{
    std::printf ("Canal MIDI, rango MIDI y MIDI Learn\n");
    {
        eng::DrumEngine e; e.prepare (kRate); e.setSample (0, makeClick (1000));
        e.globals.midiChannel = 2;
        CHECK (lastSound (hit (e, { { 0, juce::MidiMessage::noteOn (1, kNote, 1.0f) } })) < 0, "canal 2: una nota del canal 1 no suena");
        CHECK (lastSound (hit (e, { { 0, juce::MidiMessage::noteOn (2, kNote, 1.0f) } })) >= 0, "canal 2: una nota del canal 2 suena");
    }
    {
        eng::DrumEngine e; e.prepare (kRate); e.setSample (0, makeClick (1000));
        e.globals.rangeLo = 61;
        CHECK (lastSound (hit (e, { { 0, juce::MidiMessage::noteOn (1, kNote, 1.0f) } })) < 0, "rango desde 61: la nota 60 no suena");
    }
    {
        eng::DrumEngine e; e.prepare (kRate); e.setSample (5, makeClick (1000));
        e.learnPad.store (5);
        const auto b = hit (e, { { 0, juce::MidiMessage::noteOn (1, 40, 1.0f) } });
        CHECK (e.padNote[5].load() == 40, "LEARN: el pad 6 tendria que quedar en la nota 40 (%d)", e.padNote[5].load());
        CHECK (e.lastLearnedPad.load() == 5 && e.learnPad.load() == -1, "LEARN: termina despues de una nota");
        CHECK (lastSound (b) >= 0, "LEARN: la nota aprendida tambien suena");
    }
}

static void testOutputs()
{
    std::printf ("Multi-out: cada pad sale por su salida; si el DAW la apaga, va a Main\n");
    eng::DrumEngine e; e.prepare (kRate); e.setSample (0, makeClick (1000));
    e.pads[0].output = 3;
    for (int i = 0; i < eng::DrumEngine::kNumOutputs; ++i)
        e.outputChannel[(size_t) i] = i < 4 ? i * 2 : -1;   // 4 salidas activas: 8 canales
    auto b = hit (e, { { 0, juce::MidiMessage::noteOn (1, kNote, 1.0f) } }, 8);
    CHECK (lastSound (b, 6) >= 0 && lastSound (b, 7) >= 0, "la salida 4 (canales 7 y 8) tiene que sonar");
    CHECK (lastSound (b, 0) < 0, "Main no tiene que sonar");

    e.pads[0].output = 9;   // salida apagada
    b = hit (e, { { 0, juce::MidiMessage::noteOn (1, kNote, 1.0f) } }, 8);
    CHECK (lastSound (b, 0) >= 0 && lastSound (b, 6) < 0, "una salida apagada va a Main");
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
    testEventAtBlockStartWithLateRoll();
    testDefaultIsOneShot();
    testGridHitAtBlockEnd();
    testStress();
    testPadSound();
    testMonoChokeVoices();
    testMidiFiltersAndLearn();
    testOutputs();
    std::printf (failures == 0 ? "OK: todo paso\n" : "%d falla(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
