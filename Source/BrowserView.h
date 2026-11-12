#pragma once

#include "Look.h"
#include "PluginProcessor.h"

/** El browser: todo lo que se fue cargando, con su categoria. Clic para escuchar, doble clic para cargarlo
    en el pad elegido, arrastrar a un pad, clic derecho para mas opciones. Exporta lo que se ve como kit. */
class BrowserView final : public juce::Component,
                          private juce::ListBoxModel
{
public:
    explicit BrowserView (DrumProcessor&);

    /** Vuelve a armar la lista. `fromDisk`: relee la biblioteca (otra instancia pudo cambiarla). */
    void refresh (bool fromDisk);
    /** Suma archivos o carpetas soltados encima (las carpetas se recorren enteras). */
    void addDropped (const juce::StringArray& paths);

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr const char* kDragPrefix = "drum5:";

private:
    int getNumRows() override { return entries.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;

    void showRowMenu (int row);
    void showCategoryMenu();
    void askNewCategory (std::function<void (const juce::String&)> then);
    void exportKit (bool zip);
    void showMessage (const juce::String& text);

    DrumProcessor& proc;
    juce::Array<lib::Entry> entries;   // lo que se ve (filtrado)

    juce::Label title, count, message;
    juce::TextEditor search;
    juce::ComboBox category;
    juce::ToggleButton copyMode { "Copiar a la biblioteca" };
    juce::ListBox list { "browser", this };
    juce::TextButton categoriesButton { "CATEGORIAS" }, exportFolderButton { "EXPORTAR CARPETA" }, exportZipButton { "EXPORTAR ZIP" };
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> prompt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserView)
};
