#pragma once

#include "Classifier.h"

namespace lib
{
struct Entry
{
    juce::String id;
    juce::File file;          // el archivo que se usa (la copia, o el original si se guarda solo la ruta)
    juce::File original;      // de donde vino
    juce::String category;
    bool manualCategory = false;   // la eligio el usuario (no se vuelve a adivinar)
    juce::int64 added = 0;         // milisegundos desde 1970
};

/** El browser de drum5: cada sample que se carga en un pad entra aca y queda para siempre.
    Es una sola biblioteca para todas las instancias del plugin (un archivo en disco).
    Se puede usar desde cualquier hilo. */
class Library
{
public:
    /** `dataFile`: donde se guarda la lista. `copyFolder`: donde van las copias. */
    Library (juce::File dataFile, juce::File copyFolder);

    static juce::File defaultDataFile();
    static juce::File defaultCopyFolder();

    /** Vuelve a leer la lista del disco (otra instancia pudo haberla cambiado). */
    void reload();

    /** Suma un sample (si ya estaba, no lo repite). Devuelve el archivo que conviene cargar en el pad:
        la copia de la biblioteca en modo copia, o el mismo archivo si se guarda solo la ruta. */
    juce::File add (const juce::File& file);

    /** true: copia cada sample a la carpeta de la biblioteca. false: guarda solo la ruta. */
    bool isCopyMode() const;
    void setCopyMode (bool copy);

    /** Las entradas, filtradas por categoria ("" = todas) y por texto (en el nombre o la categoria). Las mas nuevas primero. */
    juce::Array<Entry> getEntries (const juce::String& category = {}, const juce::String& search = {}) const;
    int size() const;

    /** Las categorias: las propias de drum5 y despues las creadas por el usuario. */
    juce::StringArray getCategories() const;
    bool addCategory (const juce::String& name);
    /** Borra una categoria propia del usuario; sus samples vuelven a la que drum5 adivina. */
    void removeCategory (const juce::String& name);
    bool isUserCategory (const juce::String& name) const;

    void setCategory (const juce::String& id, const juce::String& category);
    /** Saca un sample del browser. No borra ningun archivo (puede estar en uso en un proyecto). */
    void remove (const juce::String& id);

    /** Exporta las entradas como un kit: `destination` es la carpeta del kit (o el .zip), con una subcarpeta por categoria.
        Devuelve cuantos samples se exportaron; `error` explica lo que fallo. */
    int exportKit (const juce::Array<Entry>& entries, const juce::File& destination, bool zip, juce::String& error) const;

private:
    void load();    // con `lock` tomado
    void save();    // con `lock` tomado
    juce::File uniqueFile (const juce::File& folder, const juce::String& fileName) const;

    juce::File dataFile, copyFolder;
    mutable juce::CriticalSection lock;
    juce::InterProcessLock fileLock { "drum5-biblioteca" };
    std::vector<Entry> entries;
    juce::StringArray userCategories;
    bool copyMode = true;
};
}
