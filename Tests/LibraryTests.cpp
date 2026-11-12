// Pruebas del browser: deteccion de tipo, biblioteca (copia o ruta), categorias propias y exportar kit.

#include "Library/Library.h"

#include <cstdio>

static int failures = 0;

#define CHECK(cond, ...) \
    do { if (! (cond)) { ++failures; std::printf ("  FALLA: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (false)

static void testClassifier()
{
    std::printf ("Deteccion de tipo por nombre y carpeta\n");
    const std::pair<const char*, const char*> cases[] = {
        // los nombres de la captura de referencia
        { "Hi Hat - Bilardo @1chiruu.wav", "HH Cerrado" },
        { "HAT OPEN.wav", "HH Abierto" },
        { "OPEN HAT - TRANZ.wav", "HH Abierto" },
        { "CLAP - BANDERAZO.wav", "Clap" },
        { "SNARE - COSTANERA.wav", "Snare" },
        { "CHANT - GOL GOL GOL.wav", "Chant" },
        { "PERC - EL TATA.wav", "Perc" },
        { "CRASH - ITALIA 90.wav", "Crash" },
        { "TOM.wav", "Tom" },
        // convenciones comunes de kits
        { "Kick 808.wav", "Kick" },
        { "Kick808_Hard.wav", "Kick" },
        { "808 - Mafia.wav", "808" },
        { "808s_dark.wav", "808" },
        { "OpenHat_02.wav", "HH Abierto" },
        { "HiHat 3.wav", "HH Cerrado" },
        { "hh_01.wav", "HH Cerrado" },
        { "OH 1.wav", "HH Abierto" },
        { "Rimshot tight.wav", "Rim" },
        { "Ride bell.wav", "Ride" },
        { "Cymbal swell.wav", "Cymbal" },
        { "Vox adlib yeah.wav", "Vox" },
        { "Riser 8 bars.wav", "FX" },
        { "Shaker loop.wav", "Perc" },
        { "Reverse Snare.wav", "Snare" },
        { "Redoblante seco.wav", "Snare" },
        { "Bombo grave.wav", "Kick" },
        { "Sub Bass C.wav", "808" },
    };
    for (const auto& [name, expected] : cases)
    {
        const auto got = lib::classify (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name));
        CHECK (got == expected, "\"%s\" -> %s, se esperaba %s", name, got.toRawUTF8(), expected);
    }

    // por carpeta cuando el nombre no dice nada
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory);
    CHECK (lib::classify (root.getChildFile ("Mi Kit/Snares/hit 3.wav")) == "Snare", "carpeta Snares");
    CHECK (lib::classify (root.getChildFile ("Kit Trap/Hi-Hats/Closed/04.wav")) == "HH Cerrado", "carpeta Hi-Hats/Closed");
    CHECK (lib::classify (root.getChildFile ("Kit Trap/Open Hats/04.wav")) == "HH Abierto", "carpeta Open Hats");
    CHECK (lib::classify (root.getChildFile ("Drums/Kicks/sample 1.wav")) == "Kick", "carpeta Kicks");
    CHECK (lib::classify (root.getChildFile ("cosas/algo.wav")) == "Otros", "sin pistas: Otros");
    // el nombre manda sobre la carpeta
    CHECK (lib::classify (root.getChildFile ("Drums/Kicks/clap layer.wav")) == "Clap", "el nombre gana a la carpeta");
}

static juce::File makeFile (const juce::File& dir, const juce::String& path, const juce::String& content = "audio")
{
    auto f = dir.getChildFile (path);
    f.getParentDirectory().createDirectory();
    f.replaceWithText (content);
    return f;
}

static void testLibrary (const juce::File& root)
{
    std::printf ("Biblioteca: copia, ruta, duplicados y persistencia\n");
    const auto data = root.getChildFile ("datos/biblioteca.xml");
    const auto copies = root.getChildFile ("Biblioteca");
    const auto snare = makeFile (root, "Kit/Snares/hit.wav", "snare");
    const auto hat = makeFile (root, "Kit/Hats/hit.wav", "hat");
    const auto clap = makeFile (root, "Otro/Clap fuerte.wav", "clap");

    {
        lib::Library l (data, copies);
        CHECK (l.isCopyMode(), "por defecto copia");
        const auto a = l.add (snare);
        CHECK (a.isAChildOf (copies) && a.loadFileAsString() == "snare", "en modo copia devuelve la copia");
        const auto b = l.add (hat);
        CHECK (b != a && b.isAChildOf (copies), "dos archivos con el mismo nombre no se pisan (%s)", b.getFileName().toRawUTF8());
        CHECK (l.add (snare) == a && l.add (a) == a && l.size() == 2, "cargar otra vez el mismo sample no lo repite");

        l.setCopyMode (false);
        CHECK (l.add (clap) == clap, "en modo ruta devuelve el original");
        CHECK (l.size() == 3, "tres samples");
    }
    {
        lib::Library l (data, copies);   // otra instancia lee lo mismo del disco
        CHECK (l.size() == 3 && ! l.isCopyMode(), "la biblioteca se recupera del disco (%d)", l.size());
        const auto snares = l.getEntries ("Snare");
        CHECK (snares.size() == 1 && snares[0].original == snare, "filtro por categoria");
        CHECK (l.getEntries ({}, "fuerte").size() == 1, "busqueda por nombre");
        CHECK (l.getEntries ({}, "hh cerrado").size() == 1, "busqueda por categoria");
        CHECK (l.getEntries().getFirst().original == clap, "las mas nuevas primero");

        CHECK (l.addCategory ("Mis Golpes"), "crear categoria propia");
        CHECK (! l.addCategory ("snare") && ! l.addCategory ("  "), "no se repiten categorias ni se crean vacias");
        l.setCategory (snares[0].id, "Mis Golpes");
    }
    {
        lib::Library l (data, copies);
        CHECK (l.getCategories().contains ("Mis Golpes") && l.isUserCategory ("Mis Golpes"), "la categoria propia se guarda");
        CHECK (l.getEntries ("Mis Golpes").size() == 1, "el sample quedo en la categoria propia");
        l.removeCategory ("Mis Golpes");
        CHECK (l.getEntries ("Snare").size() == 1, "al borrar la categoria el sample vuelve a la adivinada");

        const auto id = l.getEntries ({}, "fuerte")[0].id;
        l.remove (id);
        CHECK (l.size() == 2 && clap.existsAsFile(), "quitar del browser no borra el archivo");
    }
}

static void testExport (const juce::File& root)
{
    std::printf ("Exportar kit: carpeta y zip, con subcarpetas por categoria\n");
    const auto data = root.getChildFile ("datos2/biblioteca.xml");
    const auto copies = root.getChildFile ("Biblioteca2");
    lib::Library l (data, copies);
    l.add (makeFile (root, "A/Snare 1.wav", "s1"));
    l.add (makeFile (root, "B/Snare 1.wav", "s1b"));   // mismo nombre, otra carpeta
    l.add (makeFile (root, "A/Kick.wav", "k"));
    const auto all = l.getEntries();

    juce::String error;
    const auto folder = root.getChildFile ("Export/Mi Kit");
    CHECK (l.exportKit (all, folder, false, error) == 3 && error.isEmpty(), "carpeta: 3 samples (%s)", error.toRawUTF8());
    CHECK (folder.getChildFile ("Snare/Snare 1.wav").existsAsFile(), "carpeta: Snare/Snare 1.wav");
    CHECK (folder.getChildFile ("Snare").getNumberOfChildFiles (juce::File::findFiles) == 2, "carpeta: los dos snares, sin pisarse");
    CHECK (folder.getChildFile ("Kick/Kick.wav").loadFileAsString() == "k", "carpeta: Kick/Kick.wav");

    const auto zipFile = root.getChildFile ("Export/Mi Kit.zip");
    CHECK (l.exportKit (l.getEntries ("Snare"), zipFile, true, error) == 2, "zip: solo lo filtrado (2 snares)");
    juce::ZipFile zip (zipFile);
    CHECK (zip.getNumEntries() == 2, "zip: 2 archivos, tiene %d", zip.getNumEntries());
    CHECK (zip.getEntry ("Mi Kit/Snare/Snare 1.wav") != nullptr, "zip: Mi Kit/Snare/Snare 1.wav");
    CHECK (zip.getEntry ("Mi Kit/Snare/Snare 1 (2).wav") != nullptr, "zip: el segundo snare con otro nombre");

    // un sample que ya no existe se avisa y el resto se exporta igual
    auto withMissing = all;
    lib::Entry ghost;
    ghost.file = root.getChildFile ("no existe.wav");
    ghost.category = "Kick";
    withMissing.add (ghost);
    CHECK (l.exportKit (withMissing, root.getChildFile ("Export/Kit 2"), false, error) == 3 && error.isNotEmpty(),
           "un sample que falta se avisa y el resto se exporta");
}

int main()
{
    const auto root = juce::File::createTempFile ("drum5-biblioteca");
    root.createDirectory();
    testClassifier();
    testLibrary (root);
    testExport (root);
    root.deleteRecursively();
    std::printf (failures == 0 ? "OK: todo paso\n" : "%d falla(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
