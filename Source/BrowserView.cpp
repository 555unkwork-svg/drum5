#include "BrowserView.h"

using namespace ui;

BrowserView::BrowserView (DrumProcessor& p) : proc (p)
{
    title.setText ("BROWSER", juce::dontSendNotification);
    title.setFont (sans (14.0f));
    title.setColour (juce::Label::textColourId, kInkDim);
    addAndMakeVisible (title);

    for (auto* l : { &count, &message })
    {
        l->setFont (mono (11.0f));
        l->setColour (juce::Label::textColourId, kInkDim);
        addAndMakeVisible (*l);
    }
    count.setJustificationType (juce::Justification::centredRight);

    search.setTextToShowWhenEmpty ("Buscar (nombre o categoria)", kInkDim);
    search.setFont (mono (13.0f));
    search.setColour (juce::TextEditor::backgroundColourId, kKeyLight);
    search.setColour (juce::TextEditor::textColourId, kInk);
    search.setColour (juce::TextEditor::outlineColourId, kBodyLine);
    search.setColour (juce::TextEditor::focusedOutlineColourId, kRed);
    search.onTextChange = [this] { refresh (false); };
    addAndMakeVisible (search);

    category.onChange = [this] { refresh (false); };
    addAndMakeVisible (category);

    copyMode.setTooltip ("Encendido: cada sample se copia a Documentos/drum5/Biblioteca y sigue ahi aunque borres el original.\n"
                         "Apagado: se guarda solo la ruta (no ocupa lugar, pero si moves el archivo se pierde).");
    copyMode.setToggleState (proc.library->isCopyMode(), juce::dontSendNotification);
    copyMode.onClick = [this] { proc.library->setCopyMode (copyMode.getToggleState()); };
    addAndMakeVisible (copyMode);

    list.setRowHeight (26);
    list.setMultipleSelectionEnabled (false);
    list.setColour (juce::ListBox::backgroundColourId, kDisplay);
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (list);

    categoriesButton.onClick = [this] { showCategoryMenu(); };
    exportFolderButton.onClick = [this] { exportKit (false); };
    exportZipButton.onClick = [this] { exportKit (true); };
    for (auto* b : { &categoriesButton, &exportFolderButton, &exportZipButton })
        addAndMakeVisible (*b);
    exportFolderButton.setTooltip ("Exporta lo que se ve en la lista: una carpeta con una subcarpeta por categoria");
    exportZipButton.setTooltip ("Igual que EXPORTAR CARPETA, pero en un .zip");

    refresh (true);
}

void BrowserView::refresh (bool fromDisk)
{
    auto& library = *proc.library;
    if (fromDisk)
    {
        library.reload();
        copyMode.setToggleState (library.isCopyMode(), juce::dontSendNotification);
    }

    // el filtro de categorias, con cuantos hay en cada una (las vacias tambien, para poder elegirlas)
    const auto selected = category.getSelectedId() > 1 ? category.getText().upToLastOccurrenceOf ("  (", false, false) : juce::String();
    category.clear (juce::dontSendNotification);
    category.addItem ("Todas  (" + juce::String (library.size()) + ")", 1);
    const auto names = library.getCategories();
    for (int i = 0; i < names.size(); ++i)
    {
        const int n = library.getEntries (names[i]).size();
        category.addItem (names[i] + "  (" + juce::String (n) + ")", i + 2);
        if (names[i] == selected)
            category.setSelectedId (i + 2, juce::dontSendNotification);
    }
    if (category.getSelectedId() == 0)
        category.setSelectedId (1, juce::dontSendNotification);

    const auto filter = category.getSelectedId() > 1 ? names[category.getSelectedId() - 2] : juce::String();
    entries = library.getEntries (filter, search.getText());
    list.updateContent();
    list.repaint();
    count.setText (juce::String (entries.size()) + (entries.size() == 1 ? " sample" : " samples"), juce::dontSendNotification);
    if (library.size() == 0)
        showMessage ("Vacio: cada sample que cargas en un pad entra aca. Tambien podes soltar archivos o carpetas sobre la lista.");
}

void BrowserView::showMessage (const juce::String& text)
{
    message.setText (text, juce::dontSendNotification);
}

void BrowserView::addDropped (const juce::StringArray& paths)
{
    juce::Array<juce::File> files;
    for (const auto& path : paths)
    {
        const juce::File f (path);
        if (f.isDirectory())
            files.addArray (f.findChildFiles (juce::File::findFiles, true, proc.formats.getWildcardForAllFormats()));
        else
            files.add (f);
    }
    showMessage ("Sumando " + juce::String (files.size()) + " archivo(s) al browser...");

    // en el hilo de carga: copiar muchos archivos no traba la interfaz
    proc.runInBackground ([&library = *proc.library, &formats = proc.formats, files, safe = juce::Component::SafePointer<BrowserView> (this)]
    {
        int added = 0, skipped = 0;
        for (const auto& f : files)
        {
            // solo audio que se pueda leer
            if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f)); reader != nullptr)
            {
                library.add (f);
                ++added;
            }
            else
            {
                ++skipped;
            }
        }
        juce::MessageManager::callAsync ([safe, added, skipped]
        {
            if (safe == nullptr)
                return;
            safe->refresh (false);
            safe->showMessage (juce::String (added) + " sample(s) en el browser"
                               + (skipped > 0 ? ", " + juce::String (skipped) + " archivo(s) no eran audio" : juce::String()));
        });
    });
}

void BrowserView::paint (juce::Graphics& g)
{
    g.setColour (kBodyLine);
    g.fillRect (0, title.getBottom() + 2, getWidth(), 1);
}

void BrowserView::resized()
{
    auto area = getLocalBounds();
    {
        auto r = area.removeFromTop (22);
        count.setBounds (r.removeFromRight (140));
        title.setBounds (r);
    }
    area.removeFromTop (10);
    {
        auto r = area.removeFromTop (30);
        copyMode.setBounds (r.removeFromRight (230));
        r.removeFromRight (10);
        category.setBounds (r.removeFromRight (200));
        r.removeFromRight (10);
        search.setBounds (r);
    }
    area.removeFromTop (8);
    {
        auto r = area.removeFromBottom (30);
        categoriesButton.setBounds (r.removeFromLeft (130));
        exportZipButton.setBounds (r.removeFromRight (140));
        r.removeFromRight (8);
        exportFolderButton.setBounds (r.removeFromRight (170));
    }
    area.removeFromBottom (4);
    message.setBounds (area.removeFromBottom (20));
    area.removeFromBottom (4);
    list.setBounds (area);
}

//==============================================================================
void BrowserView::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= entries.size())
        return;
    const auto& e = entries.getReference (row);
    const bool missing = ! e.file.existsAsFile();

    if (selected)
        g.fillAll (kRed);
    else if (row % 2 == 1)
        g.fillAll (juce::Colours::white.withAlpha (0.04f));

    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (10, 0);

    // la categoria como etiqueta a la derecha
    const auto tag = e.category.toUpperCase();
    const juce::Font tagFont (mono (10.5f));
    const int tagW = (int) std::ceil (juce::GlyphArrangement::getStringWidth (tagFont, tag)) + 16;
    const auto tagBox = area.removeFromRight (tagW).reduced (0, 5).toFloat();
    g.setColour (selected ? textOn (kRed).withAlpha (0.2f) : kLcdDim.withAlpha (0.35f));
    g.fillRoundedRectangle (tagBox, 4.0f);
    g.setColour (selected ? textOn (kRed) : kLcd);
    g.setFont (tagFont);
    g.drawText (tag, tagBox, juce::Justification::centred);

    area.removeFromRight (8);
    g.setFont (mono (13.0f));
    g.setColour (selected ? textOn (kRed) : (missing ? kLcdDim : kLcd));
    g.drawText (e.file.getFileNameWithoutExtension() + (missing ? "  (NO ENCONTRADO)" : ""), area, juce::Justification::centredLeft, true);
}

void BrowserView::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= entries.size())
        return;
    if (e.mods.isPopupMenu())
        showRowMenu (row);
    else
        proc.previewFile (entries[row].file);
}

void BrowserView::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < entries.size())
        proc.loadSampleAsync (proc.getSelectedPad(), entries[row].file);
}

juce::var BrowserView::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    if (rows.isEmpty() || rows[0] >= entries.size())
        return {};
    return juce::String (kDragPrefix) + entries[rows[0]].file.getFullPathName();
}

void BrowserView::showRowMenu (int row)
{
    const auto entry = entries[row];
    const int pad = proc.getSelectedPad();

    juce::PopupMenu categories;
    for (const auto& c : proc.library->getCategories())
        categories.addItem (c, true, c == entry.category, [this, id = entry.id, c]
        {
            proc.library->setCategory (id, c);
            refresh (false);
        });
    categories.addSeparator();
    categories.addItem ("Nueva categoria...", [this, id = entry.id]
    {
        askNewCategory ([this, id] (const juce::String& name)
        {
            proc.library->setCategory (id, name);
            refresh (false);
        });
    });

    juce::PopupMenu menu;
    menu.addSectionHeader (entry.file.getFileNameWithoutExtension());
    menu.addItem ("Cargar en el pad " + juce::String (pad + 1), [this, pad, f = entry.file] { proc.loadSampleAsync (pad, f); });
    menu.addItem ("Escuchar", [this, f = entry.file] { proc.previewFile (f); });
    menu.addSubMenu ("Categoria", categories);
    menu.addItem ("Mostrar en la carpeta", entry.file.exists(), false, [f = entry.file] { f.revealToUser(); });
    menu.addSeparator();
    menu.addItem ("Quitar del browser (no borra el archivo)", [this, id = entry.id]
    {
        proc.library->remove (id);
        refresh (false);
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void BrowserView::showCategoryMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Crear categoria...", [this] { askNewCategory ([this] (const juce::String&) { refresh (false); }); });
    juce::PopupMenu remove;
    for (const auto& c : proc.library->getCategories())
        if (proc.library->isUserCategory (c))
            remove.addItem (c, [this, c]
            {
                proc.library->removeCategory (c);
                refresh (false);
                showMessage ("Se borro la categoria \"" + c + "\": sus samples volvieron a la que drum5 adivina");
            });
    menu.addSubMenu ("Borrar una categoria propia", remove, remove.getNumItems() > 0);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (categoriesButton));
}

void BrowserView::askNewCategory (std::function<void (const juce::String&)> then)
{
    prompt = std::make_unique<juce::AlertWindow> ("Nueva categoria", "Nombre de la categoria (ej. \"Hats Bilardo\", \"Chants\"):",
                                                  juce::MessageBoxIconType::NoIcon, this);
    prompt->addTextEditor ("nombre", {});
    prompt->addButton ("CREAR", 1, juce::KeyPress (juce::KeyPress::returnKey));
    prompt->addButton ("CANCELAR", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    prompt->enterModalState (true, juce::ModalCallbackFunction::create ([this, then = std::move (then)] (int result)
    {
        const auto name = prompt != nullptr ? prompt->getTextEditorContents ("nombre").trim() : juce::String();
        prompt.reset();
        if (result != 1 || name.isEmpty())
            return;
        if (! proc.library->addCategory (name) && ! proc.library->isUserCategory (name))
        {
            showMessage ("\"" + name + "\" ya es una categoria de drum5");
            return;
        }
        then (name);
        showMessage ("Categoria \"" + name + "\" creada");
    }), false);
}

void BrowserView::exportKit (bool zip)
{
    if (entries.isEmpty())
    {
        showMessage ("No hay samples para exportar (revisa la busqueda o la categoria)");
        return;
    }
    const auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("drum5 Kit" + juce::String (zip ? ".zip" : ""));
    chooser = std::make_unique<juce::FileChooser> (zip ? "Guardar el kit como .zip" : "Nombre de la carpeta del kit", start, zip ? "*.zip" : "");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, zip, list = entries] (const juce::FileChooser& fc)
    {
        auto dest = fc.getResult();
        if (dest == juce::File())
            return;
        if (zip && ! dest.hasFileExtension ("zip"))
            dest = dest.withFileExtension ("zip");
        showMessage ("Exportando " + juce::String (list.size()) + " samples...");

        proc.runInBackground ([&library = *proc.library, list, dest, zip, safe = juce::Component::SafePointer<BrowserView> (this)]
        {
            juce::String error;
            const int n = library.exportKit (list, dest, zip, error);
            juce::MessageManager::callAsync ([safe, n, error, dest]
            {
                if (safe == nullptr)
                    return;
                safe->showMessage (error.isNotEmpty() && n == 0 ? error
                                   : "Kit exportado: " + juce::String (n) + " samples en " + dest.getFullPathName()
                                         + (error.isNotEmpty() ? ". " + error : juce::String()));
                if (n > 0)
                    dest.revealToUser();
            });
        });
    });
}
