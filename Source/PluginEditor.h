#pragma once

#include "Look.h"
#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

/** Los 16 pads: clic para tocar (mas arriba suena mas fuerte), clic derecho para el menu,
    y se puede soltar un archivo encima de cada uno. */
class PadGrid final : public juce::Component
{
public:
    explicit PadGrid (DrumProcessor&);

    std::function<void (int pad)> onPadMenu;
    juce::Rectangle<int> padBounds (int pad) const;
    int padAt (juce::Point<int>) const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    int dropTarget = -1;   // pad resaltado mientras se arrastra un archivo

private:
    DrumProcessor& proc;
    int held = -1;
};

class DrumEditor final : public juce::AudioProcessorEditor,
                         public juce::FileDragAndDropTarget,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    explicit DrumEditor (DrumProcessor&);
    ~DrumEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override { pads.repaint(); }
    void refresh();
    void choosePadFile (int pad);
    void showPadMenu (int pad);
    /** Carga los archivos en pads seguidos desde `firstPad`. Devuelve cuantos fallaron. */
    int loadFiles (const juce::StringArray& files, int firstPad);
    int padAtEditorPoint (int x, int y) const;

    DrumProcessor& proc;
    HardwareLook look;
    PadGrid pads;
    juce::TextButton loadButton { "CARGAR" }, clearButton { "VACIAR" };
    juce::Slider master;
    juce::AudioProcessorValueTreeState::SliderAttachment masterAttachment;
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumEditor)
};
