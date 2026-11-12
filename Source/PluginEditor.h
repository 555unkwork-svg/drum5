#pragma once

#include "Look.h"
#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

/** Los 16 pads: clic para tocar (mas arriba suena mas fuerte; mantener = roll), clic derecho para el menu,
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
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

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
    juce::TextButton loadButton { "CARGAR" }, clearButton { "VACIAR" };
    juce::Slider master;
    juce::AudioProcessorValueTreeState::SliderAttachment masterAttachment;
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;

    //---- panel del pad elegido
    int attachedPad = -1;
    juce::Rectangle<int> panelBounds, globalBounds;
    juce::Label padTitle;
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
    Fader swing, humanize;
    juce::ToggleButton pressure { "Presion" };
    ButtonAttachment pressureAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumEditor)
};
