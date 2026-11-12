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
        const bool loading = proc.isPadLoading (i);
        const bool rolling = proc.engine.padRolling[(size_t) i].load (std::memory_order_relaxed);
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
        const auto name = loading ? juce::String ("CARGANDO...")
                        : missing ? juce::String ("NO ENCONTRADO")
                        : (used ? proc.getPadName (i).toUpperCase() : juce::String ("VACIO"));
        g.drawText (name, lines.removeFromBottom (13.0f), juce::Justification::centredLeft, true);

        if (i == selected)
        {
            const auto ledColour = lit ? textOn (kRed) : kRed;
            drawLed (g, { inner.getRight() - 3.0f, inner.getY() + 6.0f }, 4.0f, ledColour, true);
            g.setColour (ledColour);
            g.fillRoundedRectangle (face.getX() + 10.0f, face.getBottom() - 4.0f, face.getWidth() - 20.0f, 2.0f, 1.0f);
        }
        if (rolling)   // roll andando (con LATCH sigue aunque se suelte el pad)
            drawLed (g, { inner.getRight() - 3.0f, inner.getBottom() - 6.0f }, 3.5f, lit ? textOn (kRed) : kRed, true);
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
    proc.engine.triggerPadFromUI (pad, true, 0.35f + 0.65f * height);
    repaint();
}

void PadGrid::mouseUp (const juce::MouseEvent&)
{
    if (held >= 0)
        proc.engine.triggerPadFromUI (held, false);
    held = -1;
    repaint();
}


//==============================================================================
DrumEditor::DrumEditor (DrumProcessor& p)
    : AudioProcessorEditor (&p), proc (p), pads (p), masterAttachment (p.apvts, "master", master),
      pressureAtt (p.apvts, "press", pressure)
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

    //---- panel del pad
    padTitle.setFont (sans (18.0f));
    padTitle.setColour (juce::Label::textColourId, kInk);
    addAndMakeVisible (padTitle);
    for (auto* t : { &retrigger, &latch, &ramp, &pressure })
        addAndMakeVisible (*t);

    setupCaption (rateCaption, "RATE");
    setupCaption (modeCaption, "MODO");
    for (int i = 0; i < eng::kNumRates; ++i)
    {
        auto& b = rateButtons[(size_t) i];
        b.setButtonText (DrumProcessor::rateNames()[i]);
        b.onClick = [this, i] { setChoice ("rate", i); };
        addAndMakeVisible (b);
    }
    syncButton.onClick = [this] { setChoice ("mode", 0); };
    freeButton.onClick = [this] { setChoice ("mode", 1); };
    for (auto* b : { &syncButton, &freeButton })
        addAndMakeVisible (*b);
    for (auto& b : rateButtons)
        b.setColour (juce::TextButton::buttonColourId, kKeyLight);
    for (auto* b : { &syncButton, &freeButton })
        b->setColour (juce::TextButton::buttonColourId, kKeyLight);
    for (auto* b : { &syncButton, &freeButton })
        b->setColour (juce::TextButton::buttonOnColourId, kRed);
    for (auto& b : rateButtons)
        b.setColour (juce::TextButton::buttonOnColourId, kRed);

    setupFader (freeHz, "Libre", " Hz");
    setupFader (gate, "Gate", " %");
    setupFader (accent, "Accent", " %");
    setupFader (pitchVar, "Pitch Var", " st");
    accentGroup.addItemList ({ "c/ 2", "c/ 3", "c/ 4" }, 1);
    rampBeats.addItemList ({ "1 tiempo", "2 tiempos", "4 tiempos" }, 1);
    addAndMakeVisible (accentGroup);
    addAndMakeVisible (rampBeats);

    //---- generales
    globalTitle.setFont (sans (14.0f));
    globalTitle.setText ("GENERAL", juce::dontSendNotification);
    globalTitle.setColour (juce::Label::textColourId, kInkDim);
    addAndMakeVisible (globalTitle);
    setupFader (swing, "Swing", " %");
    setupFader (humanize, "Humanize", " %");
    swing.attachment = std::make_unique<SliderAttachment> (proc.apvts, "swing", swing.slider);
    humanize.attachment = std::make_unique<SliderAttachment> (proc.apvts, "hum", humanize.slider);
    bpmLabel.setFont (mono (12.0f));
    bpmLabel.setColour (juce::Label::textColourId, kInkDim);
    bpmLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (bpmLabel);

    retrigger.setTooltip ("Mantener el pad repite el golpe a tempo");
    latch.setTooltip ("El roll sigue sin mantener el pad; se corta tocandolo otra vez");
    pressure.setTooltip ("La presion del pad (aftertouch) manda la fuerza de las repeticiones");

    proc.addChangeListener (this);
    refresh();
    startTimerHz (30);
    setSize (1060, 640);
}

DrumEditor::~DrumEditor()
{
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void DrumEditor::setupCaption (juce::Label& l, const juce::String& text)
{
    l.setText (text, juce::dontSendNotification);
    l.setFont (mono (11.0f));
    l.setColour (juce::Label::textColourId, kInkDim);
    addAndMakeVisible (l);
}

void DrumEditor::setupFader (Fader& f, const juce::String& name, const juce::String& suffix)
{
    setupCaption (f.label, name.toUpperCase());
    f.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
    f.slider.setTextValueSuffix (suffix);
    f.slider.setColour (juce::Slider::textBoxTextColourId, kLcd);
    f.slider.setColour (juce::Slider::textBoxBackgroundColourId, kDisplay);
    f.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (f.slider);
}

void DrumEditor::layoutFader (Fader& f, juce::Rectangle<int> row)
{
    f.label.setBounds (row.removeFromLeft (84));
    f.slider.setBounds (row);
}

void DrumEditor::setChoice (const char* name, int index)
{
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (DrumProcessor::padParamId (attachedPad, name))))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) index));
        param->endChangeGesture();
    }
}

void DrumEditor::attachPad (int pad)
{
    if (pad == attachedPad)
        return;
    attachedPad = pad;

    // primero se sueltan los anteriores: un control no puede tener dos a la vez
    retriggerAtt.reset(); latchAtt.reset(); rampAtt.reset();
    accentGroupAtt.reset(); rampBeatsAtt.reset();
    for (auto* f : { &freeHz, &gate, &accent, &pitchVar })
        f->attachment.reset();

    auto id = [pad] (const char* name) { return DrumProcessor::padParamId (pad, name); };
    retriggerAtt = std::make_unique<ButtonAttachment> (proc.apvts, id ("rtg"), retrigger);
    latchAtt = std::make_unique<ButtonAttachment> (proc.apvts, id ("latch"), latch);
    rampAtt = std::make_unique<ButtonAttachment> (proc.apvts, id ("ramp"), ramp);
    accentGroupAtt = std::make_unique<ComboAttachment> (proc.apvts, id ("accgrp"), accentGroup);
    rampBeatsAtt = std::make_unique<ComboAttachment> (proc.apvts, id ("rampbt"), rampBeats);
    freeHz.attachment = std::make_unique<SliderAttachment> (proc.apvts, id ("hz"), freeHz.slider);
    gate.attachment = std::make_unique<SliderAttachment> (proc.apvts, id ("gate"), gate.slider);
    accent.attachment = std::make_unique<SliderAttachment> (proc.apvts, id ("acc"), accent.slider);
    pitchVar.attachment = std::make_unique<SliderAttachment> (proc.apvts, id ("pvar"), pitchVar.slider);
    timerCallback();
}

void DrumEditor::refresh()
{
    const int pad = proc.getSelectedPad();
    attachPad (pad);
    const auto name = proc.getPadName (pad);
    padTitle.setText ("PAD " + juce::String (pad + 1) + "   " + (name.isEmpty() ? juce::String ("VACIO") : name.toUpperCase()),
                      juce::dontSendNotification);
    if (const auto error = proc.takeLoadError(); error.isNotEmpty())
        status.setText (error, juce::dontSendNotification);
    else
        status.setText ("Mantene un pad para el roll. Clic derecho en un pad: cargar o vaciar. Arrastra samples encima de los pads.",
                        juce::dontSendNotification);
    pads.repaint();
}

void DrumEditor::timerCallback()
{
    pads.repaint();
    if (attachedPad < 0)
        return;

    auto value = [this] (const char* name) { return proc.apvts.getRawParameterValue (DrumProcessor::padParamId (attachedPad, name))->load(); };
    const bool on = value ("rtg") > 0.5f;
    const bool free = value ("mode") > 0.5f;
    const int rate = (int) value ("rate");
    for (int i = 0; i < eng::kNumRates; ++i)
    {
        rateButtons[(size_t) i].setToggleState (i == rate, juce::dontSendNotification);
        rateButtons[(size_t) i].setEnabled (on && ! free);
    }
    syncButton.setToggleState (! free, juce::dontSendNotification);
    freeButton.setToggleState (free, juce::dontSendNotification);
    for (auto* c : std::initializer_list<juce::Component*> { &syncButton, &freeButton, &latch, &ramp, &rampBeats,
                                                             &gate.slider, &accent.slider, &accentGroup, &pitchVar.slider })
        c->setEnabled (on);
    freeHz.slider.setEnabled (on && free);

    bpmLabel.setText ("TEMPO " + juce::String (proc.getHostBpm(), 2), juce::dontSendNotification);
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
    g.fillRect (panelBounds.getX() - 10, panelBounds.getY(), 1, panelBounds.getHeight());
    g.fillRect (panelBounds.getX(), padTitle.getBottom() + 2, panelBounds.getWidth(), 1);
    g.fillRect (panelBounds.getX(), globalTitle.getBottom() + 2, panelBounds.getWidth(), 1);
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

    pads.setBounds (area.removeFromLeft (590));
    area.removeFromLeft (22);
    panelBounds = area;

    //---- panel del pad
    auto panel = area;
    auto row = [&panel] (int h = 30)
    {
        auto r = panel.removeFromTop (h);
        panel.removeFromTop (8);
        return r;
    };

    padTitle.setBounds (row (26));
    {
        auto r = row();
        retrigger.setBounds (r.removeFromLeft (140));
        r.removeFromLeft (8);
        latch.setBounds (r.removeFromLeft (100));
    }
    {
        auto r = row();
        rateCaption.setBounds (r.removeFromLeft (52));
        const int w = r.getWidth() / eng::kNumRates;
        for (auto& b : rateButtons)
            b.setBounds (r.removeFromLeft (w).reduced (2, 0));
    }
    {
        auto r = row();
        modeCaption.setBounds (r.removeFromLeft (52));
        syncButton.setBounds (r.removeFromLeft (80).reduced (2, 0));
        freeButton.setBounds (r.removeFromLeft (80).reduced (2, 0));
    }
    layoutFader (freeHz, row());
    layoutFader (gate, row());
    {
        auto r = row();
        accentGroup.setBounds (r.removeFromRight (70));
        r.removeFromRight (6);
        layoutFader (accent, r);
    }
    {
        auto r = row();
        ramp.setBounds (r.removeFromLeft (140));
        r.removeFromLeft (8);
        rampBeats.setBounds (r.removeFromLeft (110));
    }
    layoutFader (pitchVar, row());

    //---- generales (abajo del panel)
    panel.removeFromTop (6);
    {
        auto r = row (22);
        bpmLabel.setBounds (r.removeFromRight (140));
        globalTitle.setBounds (r);
    }
    layoutFader (swing, row());
    layoutFader (humanize, row());
    pressure.setBounds (row().removeFromLeft (140));
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

void DrumEditor::loadFiles (const juce::StringArray& files, int firstPad)
{
    // shortcut: un archivo que no se puede leer igual ocupa su lugar en la fila (se avisa abajo); revisar si molesta al soltar carpetas mezcladas
    int pad = firstPad;
    for (const auto& path : files)
    {
        if (pad >= eng::kNumPads)
            break;
        proc.loadSampleAsync (pad++, juce::File (path));
    }
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
