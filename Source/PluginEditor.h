#pragma once

#include "BrowserView.h"
#include "Look.h"
#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

/** Los 16 pads: clic para tocar (mas arriba suena mas fuerte; mantener = roll), clic derecho para el menu,
    y se puede soltar un archivo encima de cada uno. */
class PadGrid final : public juce::Component,
                      public juce::DragAndDropTarget
{
public:
    explicit PadGrid (DrumProcessor&);

    std::function<void (int pad)> onPadMenu;
    juce::Rectangle<int> padBounds (int pad) const;
    int padAt (juce::Point<int>) const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // arrastrar un sample del browser a un pad
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    int dropTarget = -1;   // pad resaltado mientras se arrastra un archivo

private:
    DrumProcessor& proc;
    int held = -1;
};

/** Forma de onda del pad elegido, con REVERSE y DECAY dibujados encima. */
class WaveformView final : public juce::Component
{
public:
    explicit WaveformView (DrumProcessor&);
    void setPad (int pad);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;   // clic: toca el pad

private:
    DrumProcessor& proc;
    int pad = 0;
    eng::SamplePtr cachedSample;
    std::vector<float> peaks;   // pico por columna, calculado al cambiar el sample o el ancho
};

class DrumEditor final : public juce::AudioProcessorEditor,
                         public juce::FileDragAndDropTarget,
                         public juce::DragAndDropContainer,
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
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    /** Una perilla con su nombre arriba y el valor abajo. */
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    /** Un fader horizontal con su nombre a la izquierda. */
    struct Fader
    {
        juce::Slider slider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override;
    void refresh();
    void attachPad (int pad);
    void setupFader (Fader&, const juce::String& name, const juce::String& suffix);
    void setupKnob (Knob&, const juce::String& name, const juce::String& suffix);
    void layoutKnob (Knob&, juce::Rectangle<int>);
    void setupCaption (juce::Label&, const juce::String& text);
    void layoutFader (Fader&, juce::Rectangle<int> row);
    void setChoice (const char* name, int index);
    void choosePadFile (int pad);
    void showPadMenu (int pad);
    void loadFiles (const juce::StringArray& files, int firstPad);
    int padAtEditorPoint (int x, int y) const;

    DrumProcessor& proc;
    HardwareLook look;
    PadGrid pads;
    juce::TextButton loadButton { "CARGAR" }, clearButton { "VACIAR" }, browserButton { "BROWSER" };
    BrowserView browser;
    bool browserMode = false;

public:
    void setBrowserMode (bool on);   // publico para las pruebas (captura del browser)

private:
    juce::Slider master;
    juce::AudioProcessorValueTreeState::SliderAttachment masterAttachment;
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;

    //---- sonido del pad elegido (columna del medio)
    int attachedPad = -1;
    juce::Rectangle<int> soundBounds, panelBounds;
    juce::Label padTitle;
    WaveformView waveform;
    Knob vol, pan, pitch, decay;
    juce::ToggleButton reverse { "Reverse" }, mute { "Mute" }, monoButton { "Mono" };
    std::unique_ptr<ButtonAttachment> reverseAtt, muteAtt, monoAtt;
    juce::Label chokeCaption, outputCaption, noteCaption, noteLabel;
    juce::ComboBox choke, output;
    std::unique_ptr<ComboAttachment> chokeAtt, outputAtt;
    juce::TextButton learnButton { "LEARN" };

    //---- MIDI (generales)
    juce::Label midiTitle, channelCaption, rangeCaption, rangeTo;
    juce::ComboBox channel, rangeLo, rangeHi;
    std::unique_ptr<ComboAttachment> channelAtt, rangeLoAtt, rangeHiAtt;

    //---- roll del pad elegido (columna derecha)
    juce::Label rollTitle;
    juce::ToggleButton retrigger { "Retrigger" }, latch { "Latch" }, ramp { "Rate Ramp" };
    std::unique_ptr<ButtonAttachment> retriggerAtt, latchAtt, rampAtt;
    juce::Label rateCaption, modeCaption;
    std::array<juce::TextButton, eng::kNumRates> rateButtons;
    juce::TextButton syncButton { "SYNC" }, freeButton { "LIBRE" };
    Fader freeHz, gate, accent, pitchVar;
    juce::ComboBox accentGroup, rampBeats;
    std::unique_ptr<ComboAttachment> accentGroupAtt, rampBeatsAtt;

    //---- generales
    juce::Label globalTitle, bpmLabel;
    Fader swing, humanize, velSens, voices;
    juce::ToggleButton pressure { "Presion" };
    ButtonAttachment pressureAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumEditor)
};
