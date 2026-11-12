#include "PluginEditor.h"

using namespace ui;

//==============================================================================
PadGrid::PadGrid (DrumProcessor& p) : proc (p) {}

juce::Rectangle<int> PadGrid::padBounds (int pad) const
{
    // como en una MPC: el pad 1 abajo a la izquierda, el 16 arriba a la derecha
    const int w = getWidth() / 4, h = getHeight() / 4;
    const int col = pad % 4, row = 3 - pad / 4;
    return juce::Rectangle<int> (col * w, row * h, w, h).reduced (5);
}

int PadGrid::padAt (juce::Point<int> p) const
{
    for (int i = 0; i < eng::kNumPads; ++i)
        if (padBounds (i).contains (p))
            return i;
    return -1;
}

void PadGrid::paint (juce::Graphics& g)
{
    const int selected = proc.getSelectedPad();
    for (int i = 0; i < eng::kNumPads; ++i)
    {
        const bool used = proc.getPadFile (i) != juce::File();
        const bool missing = proc.isPadMissing (i);
        const bool lit = proc.engine.padFlash[(size_t) i].load (std::memory_order_relaxed) > 0;
        const auto colour = lit ? kRed : (used ? kKeyDark : kKeyGrey);
        const auto face = drawKey (g, padBounds (i).toFloat(), colour, held == i || lit, 7.0f);
        const auto inner = face.reduced (10.0f, 7.0f);
        const auto text = textOn (colour);

        g.setColour (text);
        g.setFont (sans (20.0f));
        g.drawText (juce::String (i + 1), inner, juce::Justification::topLeft);

        auto lines = inner;
        g.setFont (mono (10.0f));
        g.setColour (text.withAlpha (0.65f));
        g.drawText (noteName (proc.engine.padNote[(size_t) i].load()), lines.removeFromBottom (12.0f),
                    juce::Justification::centredLeft);
        g.setColour (missing && ! lit ? kRed : text.withAlpha (used ? 0.95f : 0.6f));
        const auto name = missing ? juce::String ("NO ENCONTRADO") : (used ? proc.getPadName (i).toUpperCase() : juce::String ("VACIO"));
        g.drawText (name, lines.removeFromBottom (13.0f), juce::Justification::centredLeft, true);

        if (i == selected)
        {
            const auto ledColour = lit ? textOn (kRed) : kRed;
            drawLed (g, { inner.getRight() - 3.0f, inner.getY() + 6.0f }, 4.0f, ledColour, true);
            g.setColour (ledColour);
            g.fillRoundedRectangle (face.getX() + 10.0f, face.getBottom() - 4.0f, face.getWidth() - 20.0f, 2.0f, 1.0f);
        }
        if (i == dropTarget)
        {
            g.setColour (kRed);
            g.drawRoundedRectangle (face.expanded (2.0f), 8.0f, 3.0f);
        }
    }
}

void PadGrid::mouseDown (const juce::MouseEvent& e)
{
    const int pad = padAt (e.getPosition());
    if (pad < 0)
        return;
    proc.setSelectedPad (pad);

    if (e.mods.isPopupMenu())
    {
        if (onPadMenu != nullptr)
            onPadMenu (pad);
        return;
    }

    // mas arriba en el pad suena mas fuerte (igual que en sampler5)
    const auto bounds = padBounds (pad);
    const float height = 1.0f - juce::jlimit (0.0f, 1.0f, (float) (e.y - bounds.getY()) / (float) std::max (1, bounds.getHeight()));
    held = pad;
    proc.engine.triggerPadFromUI (pad, 0.35f + 0.65f * height);
    repaint();
}

void PadGrid::mouseUp (const juce::MouseEvent&)
{
    held = -1;
    repaint();
}

//==============================================================================
DrumEditor::DrumEditor (DrumProcessor& p)
    : AudioProcessorEditor (&p), proc (p), pads (p), masterAttachment (p.apvts, "master", master)
{
    look.applyTheme();
    setLookAndFeel (&look);

    addAndMakeVisible (pads);
    pads.onPadMenu = [this] (int pad) { showPadMenu (pad); };

    addAndMakeVisible (loadButton);
    addAndMakeVisible (clearButton);
    loadButton.onClick = [this] { choosePadFile (proc.getSelectedPad()); };
    clearButton.onClick = [this] { proc.clearPad (proc.getSelectedPad()); };

    master.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    master.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
    master.setTextValueSuffix (" dB");
    addAndMakeVisible (master);

    status.setFont (mono (12.0f));
    status.setColour (juce::Label::textColourId, kInkDim);
    addAndMakeVisible (status);

    proc.addChangeListener (this);
    refresh();
    startTimerHz (30);
    setSize (720, 640);
}

DrumEditor::~DrumEditor()
{
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void DrumEditor::refresh()
{
    const int pad = proc.getSelectedPad();
    const auto name = proc.getPadName (pad);
    status.setText ("PAD " + juce::String (pad + 1) + "  " + (name.isEmpty() ? juce::String ("VACIO") : name.toUpperCase()),
                    juce::dontSendNotification);
    pads.repaint();
}

void DrumEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBody);

    // marca: el numero va en rojo, como en sampler5
    auto logo = juce::Rectangle<int> (22, 14, 300, 40);
    const juce::String name (JucePlugin_Name);
    const auto word = name.trimCharactersAtEnd ("0123456789");
    const juce::Font logoFont (sans (30.0f));
    const int wordWidth = (int) std::ceil (juce::GlyphArrangement::getStringWidth (logoFont, word));
    g.setFont (logoFont);
    g.setColour (kInk);
    g.drawText (word, logo.removeFromLeft (wordWidth), juce::Justification::centredLeft);
    g.setColour (kRed);
    g.drawText (name.substring (word.length()), logo, juce::Justification::centredLeft);

    g.setFont (mono (11.0f));
    g.setColour (kInkDim);
    g.drawText ("MASTER", master.getBounds().withHeight (14).translated (0, -14), juce::Justification::centred);

    // mini pantalla detras del valor del MASTER, como en sampler5
    g.setColour (kDisplay);
    g.fillRoundedRectangle (master.getBounds().removeFromBottom (18).reduced (4, 0).toFloat(), 4.0f);

    g.setColour (kBodyLine);
    g.fillRect (18, pads.getY() - 8, getWidth() - 36, 1);
    g.fillRect (18, status.getY() - 6, getWidth() - 36, 1);
}

void DrumEditor::resized()
{
    auto area = getLocalBounds().reduced (18, 12);
    auto header = area.removeFromTop (78);
    master.setBounds (header.removeFromRight (80).withTrimmedTop (14));
    header.removeFromRight (12);
    clearButton.setBounds (header.removeFromRight (90).withSizeKeepingCentre (90, 30));
    header.removeFromRight (8);
    loadButton.setBounds (header.removeFromRight (90).withSizeKeepingCentre (90, 30));

    status.setBounds (area.removeFromBottom (22));
    area.removeFromBottom (10);
    area.removeFromTop (10);
    pads.setBounds (area);
}

//==============================================================================
void DrumEditor::choosePadFile (int pad)
{
    chooser = std::make_unique<juce::FileChooser> ("Elegir un sample para el pad " + juce::String (pad + 1), juce::File(),
                                                   proc.formats.getWildcardForAllFormats());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, pad] (const juce::FileChooser& fc)
                          {
                              juce::StringArray paths;
                              for (const auto& f : fc.getResults())
                                  paths.add (f.getFullPathName());
                              loadFiles (paths, pad);
                          });
}

void DrumEditor::showPadMenu (int pad)
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("PAD " + juce::String (pad + 1));
    menu.addItem ("Cargar sample...", [this, pad] { choosePadFile (pad); });
    menu.addItem ("Vaciar", proc.getPadFile (pad) != juce::File(), false, [this, pad] { proc.clearPad (pad); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

int DrumEditor::loadFiles (const juce::StringArray& files, int firstPad)
{
    int pad = firstPad, failed = 0;
    for (const auto& path : files)
    {
        if (pad >= eng::kNumPads)
            break;
        if (proc.loadSample (pad, juce::File (path)))
            ++pad;
        else
            ++failed;
    }
    if (failed > 0)
        status.setText (juce::String (failed) + (failed == 1 ? " archivo no se pudo leer" : " archivos no se pudieron leer")
                            + " (formatos: WAV, AIFF, FLAC, MP3, OGG)",
                        juce::dontSendNotification);
    return failed;
}

int DrumEditor::padAtEditorPoint (int x, int y) const
{
    return pads.padAt (pads.getLocalPoint (this, juce::Point<int> (x, y)));
}

void DrumEditor::fileDragMove (const juce::StringArray&, int x, int y)
{
    const int target = padAtEditorPoint (x, y);
    if (target != pads.dropTarget)
    {
        pads.dropTarget = target;
        pads.repaint();
    }
}

void DrumEditor::fileDragExit (const juce::StringArray&)
{
    pads.dropTarget = -1;
    pads.repaint();
}

void DrumEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    pads.dropTarget = -1;
    // soltado sobre un pad: desde ese pad; afuera de los pads: desde el pad elegido
    const int target = padAtEditorPoint (x, y);
    const int first = target >= 0 ? target : proc.getSelectedPad();
    proc.setSelectedPad (first);
    loadFiles (files, first);
}
