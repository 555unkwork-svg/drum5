// Prueba del procesador real (sin DAW): carga de archivos y estado guardado del proyecto.

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <cstdio>
#include <thread>

static int failures = 0;

#define CHECK(cond, ...) \
    do { if (! (cond)) { ++failures; std::printf ("  FALLA: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (false)

template <typename Fn>
static void onMessageThread (Fn&& fn)
{
    juce::WaitableEvent done;
    juce::MessageManager::callAsync ([&] { fn(); done.signal(); });
    done.wait();
}

/** Espera (hasta 2 s) a que termine la carga en segundo plano del pad. */
static void waitLoaded (DrumProcessor& p, int pad)
{
    for (int i = 0; i < 200; ++i)
    {
        bool loading = true;
        onMessageThread ([&] { loading = p.isPadLoading (pad); });
        if (! loading)
            return;
        juce::Thread::sleep (10);
    }
}

static juce::File writeWav (const juce::File& dir, const juce::String& name, int length)
{
    auto file = dir.getChildFile (name);
    juce::AudioBuffer<float> buf (2, length);
    for (int i = 0; i < length; ++i)
        for (int c = 0; c < 2; ++c)
            buf.setSample (c, i, 0.5f);
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (44100.0)
                                                                               .withNumChannels (2)
                                                                               .withBitsPerSample (24));
    if (writer != nullptr)
        writer->writeFromAudioSampleBuffer (buf, 0, length);
    return file;
}

static void runTests (const juce::String& screenshot)
{
    const auto dir = juce::File::createTempFile ("drum5-tests");
    dir.createDirectory();

    std::printf ("Cargar archivos, guardar y recuperar el proyecto\n");
    const auto kick = writeWav (dir, "Kick 808.wav", 4410);
    const auto snare = writeWav (dir, "snare_01.wav", 4410);
    const auto bad = dir.getChildFile ("no es audio.wav");
    bad.replaceWithText ("hola");

    juce::MemoryBlock saved;
    {
        DrumProcessor p;
        p.prepareToPlay (44100.0, 512);
        CHECK (p.loadSample (0, kick), "no cargo el kick");
        CHECK (p.loadSample (4, snare), "no cargo el snare");
        CHECK (! p.loadSample (5, bad), "tendria que rechazar un archivo que no es audio");
        CHECK (p.getPadFile (5) == juce::File(), "un archivo rechazado no tiene que quedar en el pad");
        CHECK (p.getPadName (0) == "Kick 808", "nombre del pad: %s", p.getPadName (0).toRawUTF8());
        p.engine.padNote[4].store (40);
        p.setSelectedPad (4);
        p.getStateInformation (saved);

        // el pad suena por MIDI
        juce::AudioBuffer<float> out (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 40, 1.0f), 10);
        p.processBlock (out, midi);
        CHECK (out.getMagnitude (0, 10, 100) > 0.1f, "el snare no suena con la nota 40");
    }

    std::printf ("Los ajustes del pad llegan al motor y la carga en segundo plano funciona\n");
    {
        DrumProcessor p;
        p.prepareToPlay (44100.0, 512);
        bool loadingNow = false;
        onMessageThread ([&] { p.loadSampleAsync (2, kick); loadingNow = p.isPadLoading (2); });
        CHECK (loadingNow, "el pad tiene que figurar CARGANDO");
        waitLoaded (p, 2);
        CHECK (p.getPadFile (2) == kick, "la carga en segundo plano no termino");

        onMessageThread ([&] { p.loadSampleAsync (3, bad); });
        waitLoaded (p, 3);
        juce::String error;
        onMessageThread ([&] { error = p.takeLoadError(); });
        CHECK (error.isNotEmpty(), "un archivo que no es audio tiene que avisar");

        // retrigger apagado por parametro: una sola vez en 2 segundos de nota mantenida
        if (auto* param = p.apvts.getParameter (DrumProcessor::padParamId (2, "rtg")))
            param->setValueNotifyingHost (0.0f);
        int hits = 0;
        float prev = 0.0f;
        for (int block = 0; block < 172; ++block)
        {
            juce::AudioBuffer<float> out (2, 512);
            juce::MidiBuffer midi;
            if (block == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 62, 1.0f), 0);
            p.processBlock (out, midi);
            for (int i = 0; i < 512; ++i)
            {
                const float v = std::abs (out.getSample (0, i));
                if (v > 1.0e-4f && prev <= 1.0e-4f)
                    ++hits;
                prev = v;
            }
        }
        CHECK (hits == 1, "con RETRIGGER apagado tiene que sonar una vez, sono %d", hits);
    }

    snare.moveFileTo (dir.getChildFile ("movido.wav"));   // simula un sample que se movio de lugar
    {
        DrumProcessor p;
        p.setStateInformation (saved.getData(), (int) saved.getSize());
        CHECK (p.getPadFile (0) == kick && ! p.isPadMissing (0), "el kick no se recupero");
        CHECK (p.engine.getSample (0) != nullptr, "el kick no tiene audio despues de recuperar");
        CHECK (p.isPadMissing (4), "el snare movido tendria que figurar como NO ENCONTRADO");
        CHECK (p.getPadFile (4) == snare, "la ruta del snare tiene que conservarse");
        CHECK (p.engine.padNote[4].load() == 40, "la nota del pad 5 no se recupero");
        CHECK (p.getSelectedPad() == 4, "el pad elegido no se recupero");

        // guardar otra vez no pierde la ruta del archivo que falta
        juce::MemoryBlock again;
        p.getStateInformation (again);
        DrumProcessor q;
        q.setStateInformation (again.getData(), (int) again.getSize());
        CHECK (q.getPadFile (4) == snare, "la ruta del snare se perdio al guardar de nuevo");
    }

    // captura de la interfaz con dos pads cargados (para revisarla sin abrir un DAW)
    if (screenshot.isNotEmpty())
        onMessageThread ([&]
        {
            DrumProcessor p;
            p.loadSample (0, kick);
            p.loadSample (13, dir.getChildFile ("movido.wav"));
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
            juce::File (screenshot).deleteFile();
            juce::FileOutputStream out { juce::File (screenshot) };
            juce::PNGImageFormat().writeImageToStream (image, out);
            CHECK (out.getStatus().wasOk(), "no se pudo guardar la captura");
        });

    dir.deleteRecursively();
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String screenshot = argc > 1 ? juce::String (argv[1]) : juce::String();

    // las pruebas corren en otro hilo; este atiende los mensajes (cargas en segundo plano, interfaz)
    std::thread worker ([&]
    {
        runTests (screenshot);
        juce::MessageManager::getInstance()->stopDispatchLoop();
    });
    juce::MessageManager::getInstance()->runDispatchLoop();
    worker.join();

    std::printf (failures == 0 ? "OK: todo paso\n" : "%d falla(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
