#include "Library.h"

namespace lib
{
Library::Library (juce::File data, juce::File copies) : dataFile (std::move (data)), copyFolder (std::move (copies))
{
    reload();
}

juce::File Library::defaultDataFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("drum5").getChildFile ("biblioteca.xml");
}

juce::File Library::defaultCopyFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("drum5").getChildFile ("Biblioteca");
}

void Library::reload()
{
    const juce::ScopedLock sl (lock);
    const juce::InterProcessLock::ScopedLockType fl (fileLock);
    load();
}

void Library::load()
{
    entries.clear();
    userCategories.clear();
    const auto xml = juce::XmlDocument::parse (dataFile);
    if (xml == nullptr || ! xml->hasTagName ("BIBLIOTECA"))
        return;

    copyMode = xml->getBoolAttribute ("copiar", true);
    for (const auto* c : xml->getChildWithTagNameIterator ("CATEGORIA"))
        userCategories.addIfNotAlreadyThere (c->getStringAttribute ("nombre"));
    for (const auto* e : xml->getChildWithTagNameIterator ("SAMPLE"))
    {
        Entry entry;
        entry.id = e->getStringAttribute ("id");
        entry.file = juce::File (e->getStringAttribute ("archivo"));
        entry.original = juce::File (e->getStringAttribute ("original"));
        entry.category = e->getStringAttribute ("categoria", kOther);
        entry.manualCategory = e->getBoolAttribute ("manual");
        entry.added = e->getStringAttribute ("agregado").getLargeIntValue();
        if (entry.id.isNotEmpty() && entry.file != juce::File())
            entries.push_back (entry);
    }
}

void Library::save()
{
    juce::XmlElement xml ("BIBLIOTECA");
    xml.setAttribute ("copiar", copyMode);
    for (const auto& c : userCategories)
        xml.createNewChildElement ("CATEGORIA")->setAttribute ("nombre", c);
    for (const auto& e : entries)
    {
        auto* x = xml.createNewChildElement ("SAMPLE");
        x->setAttribute ("id", e.id);
        x->setAttribute ("archivo", e.file.getFullPathName());
        x->setAttribute ("original", e.original.getFullPathName());
        x->setAttribute ("categoria", e.category);
        x->setAttribute ("manual", e.manualCategory);
        x->setAttribute ("agregado", juce::String (e.added));
    }
    dataFile.getParentDirectory().createDirectory();
    // primero a un archivo aparte y despues se reemplaza: si algo falla, la lista vieja queda entera
    juce::TemporaryFile temp (dataFile);
    if (xml.writeTo (temp.getFile()))
        temp.overwriteTargetFileWithTemporary();
}

juce::File Library::uniqueFile (const juce::File& folder, const juce::String& fileName) const
{
    const auto first = folder.getChildFile (juce::File::createLegalFileName (fileName));
    auto f = first;
    // "Snare 1.wav" -> "Snare 1 (2).wav", "Snare 1 (3).wav"...
    for (int n = 2; f.exists(); ++n)
        f = folder.getChildFile (first.getFileNameWithoutExtension() + " (" + juce::String (n) + ")" + first.getFileExtension());
    return f;
}

juce::File Library::add (const juce::File& file)
{
    const juce::ScopedLock sl (lock);
    const juce::InterProcessLock::ScopedLockType fl (fileLock);
    load();   // por si otra instancia agrego algo

    // ya estaba (por el original o porque es una copia de la biblioteca): se usa lo que hay
    for (const auto& e : entries)
        if (e.file == file || (e.original == file && e.file.existsAsFile()))
            return e.file;

    Entry entry;
    entry.id = juce::Uuid().toString();
    entry.original = file;
    entry.file = file;
    entry.category = classify (file);
    entry.added = juce::Time::currentTimeMillis();

    if (copyMode && ! file.isAChildOf (copyFolder))
    {
        copyFolder.createDirectory();
        const auto copy = uniqueFile (copyFolder, file.getFileName());
        if (file.copyFileTo (copy))
            entry.file = copy;   // si no se pudo copiar (disco lleno, permisos) queda la ruta: no se pierde el sample
    }

    entries.push_back (entry);
    save();
    return entry.file;
}

bool Library::isCopyMode() const
{
    const juce::ScopedLock sl (lock);
    return copyMode;
}

void Library::setCopyMode (bool copy)
{
    const juce::ScopedLock sl (lock);
    const juce::InterProcessLock::ScopedLockType fl (fileLock);
    load();
    copyMode = copy;
    save();
}

juce::Array<Entry> Library::getEntries (const juce::String& category, const juce::String& search) const
{
    const juce::ScopedLock sl (lock);
    juce::Array<Entry> out;
    const auto words = juce::StringArray::fromTokens (search.toLowerCase(), " ", "");
    for (auto it = entries.rbegin(); it != entries.rend(); ++it)
    {
        if (category.isNotEmpty() && it->category != category)
            continue;
        const auto haystack = (it->file.getFileNameWithoutExtension() + " " + it->category).toLowerCase();
        bool ok = true;
        for (const auto& w : words)
            if (w.isNotEmpty() && ! haystack.contains (w))
                ok = false;
        if (ok)
            out.add (*it);
    }
    return out;
}

int Library::size() const
{
    const juce::ScopedLock sl (lock);
    return (int) entries.size();
}

juce::StringArray Library::getCategories() const
{
    const juce::ScopedLock sl (lock);
    auto all = builtInCategories();
    all.addArray (userCategories);
    return all;
}

bool Library::isUserCategory (const juce::String& name) const
{
    const juce::ScopedLock sl (lock);
    return userCategories.contains (name);
}

bool Library::addCategory (const juce::String& raw)
{
    const auto name = raw.trim();
    if (name.isEmpty() || builtInCategories().contains (name, true))
        return false;
    const juce::ScopedLock sl (lock);
    const juce::InterProcessLock::ScopedLockType fl (fileLock);
    load();
    if (userCategories.contains (name, true))
        return false;
    userCategories.add (name);
    save();
    return true;
}

void Library::removeCategory (const juce::String& name)
{
    const juce::ScopedLock sl (lock);
    const juce::InterProcessLock::ScopedLockType fl (fileLock);
    load();
    userCategories.removeString (name);
    for (auto& e : entries)
        if (e.category == name)
        {
            e.category = classify (e.original);
            e.manualCategory = false;
        }
    save();
}

void Library::setCategory (const juce::String& id, const juce::String& category)
{
    const juce::ScopedLock sl (lock);
    const juce::InterProcessLock::ScopedLockType fl (fileLock);
    load();
    for (auto& e : entries)
        if (e.id == id)
        {
            e.category = category;
            e.manualCategory = true;
        }
    save();
}

void Library::remove (const juce::String& id)
{
    const juce::ScopedLock sl (lock);
    const juce::InterProcessLock::ScopedLockType fl (fileLock);
    load();
    entries.erase (std::remove_if (entries.begin(), entries.end(), [&] (const Entry& e) { return e.id == id; }), entries.end());
    save();
}

int Library::exportKit (const juce::Array<Entry>& list, const juce::File& destination, bool zip, juce::String& error) const
{
    error.clear();
    int count = 0;
    juce::StringArray missing;
    const auto kitName = destination.getFileNameWithoutExtension();

    if (zip)
    {
        juce::ZipFile::Builder builder;
        juce::StringArray used;
        for (const auto& e : list)
        {
            if (! e.file.existsAsFile())
            {
                missing.add (e.file.getFileName());
                continue;
            }
            // dentro del zip: Kit/Categoria/nombre (sin repetir nombres)
            auto path = kitName + "/" + juce::File::createLegalFileName (e.category) + "/" + juce::File::createLegalFileName (e.file.getFileName());
            for (int n = 2; used.contains (path, true); ++n)
                path = kitName + "/" + juce::File::createLegalFileName (e.category) + "/"
                     + e.file.getFileNameWithoutExtension() + " (" + juce::String (n) + ")" + e.file.getFileExtension();
            used.add (path);
            builder.addFile (e.file, 0, path);
            ++count;
        }
        destination.deleteFile();
        juce::FileOutputStream out (destination);
        if (! out.openedOk() || ! builder.writeToStream (out, nullptr))
        {
            error = "No se pudo escribir " + destination.getFullPathName();
            return 0;
        }
    }
    else
    {
        if (! destination.createDirectory())
        {
            error = "No se pudo crear la carpeta " + destination.getFullPathName();
            return 0;
        }
        for (const auto& e : list)
        {
            if (! e.file.existsAsFile())
            {
                missing.add (e.file.getFileName());
                continue;
            }
            const auto folder = destination.getChildFile (juce::File::createLegalFileName (e.category));
            folder.createDirectory();
            if (e.file.copyFileTo (uniqueFile (folder, e.file.getFileName())))
                ++count;
            else
                missing.add (e.file.getFileName());
        }
    }

    if (! missing.isEmpty())
        error = juce::String (missing.size()) + " sample(s) no se encontraron o no se pudieron copiar: "
              + missing.joinIntoString (", ").substring (0, 200);
    return count;
}
}
