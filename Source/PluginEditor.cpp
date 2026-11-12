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
        const bool learning = proc.engine.learnPad.load (std::memory_order_relaxed) == i;
        const auto name = learning ? juce::String ("TOCA UNA NOTA")
                        : loading ? juce::String ("CARGANDO...")
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


bool PadGrid::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.toString().startsWith (BrowserView::kDragPrefix);
}

void PadGrid::itemDragMove (const SourceDetails& d)
{
    const int target = padAt (d.localPosition.toInt());
    if (target != dropTarget)
    {
        dropTarget = target;
        repaint();
    }
}

void PadGrid::itemDragExit (const SourceDetails&)
{
    dropTarget = -1;
    repaint();
}

void PadGrid::itemDropped (const SourceDetails& d)
{
    dropTarget = -1;
    const int pad = padAt (d.localPosition.toInt());
    if (pad >= 0)
    {
        proc.setSelectedPad (pad);
        proc.loadSampleAsync (pad, juce::File (d.description.toString().fromFirstOccurrenceOf (BrowserView::kDragPrefix, false, false)));
    }
    repaint();
}

//==============================================================================
WaveformView::WaveformView (DrumProcessor& p) : proc (p) {}

void WaveformView::setPad (int newPad)
{
    pad = newPad;
    repaint();
}

void WaveformView::mouseDown (const juce::MouseEvent&)
{
    proc.engine.triggerPadFromUI (pad, true, 1.0f);
    proc.engine.triggerPadFromUI (pad, false);
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (kDisplay);
    g.fillRoundedRectangle (bounds, 7.0f);

    const auto area = getLocalBounds().reduced (8, 6);
    const auto sample = proc.engine.getSample (pad);
    if (sample == nullptr || sample->buffer.getNumSamples() == 0)
    {
        g.setColour (kLcdDim);
        g.setFont (mono (12.0f));
        g.drawText (proc.isPadMissing (pad) ? "NO ENCONTRADO" : "SIN SAMPLE: ARRASTRA UN AUDIO A UN PAD",
                    area, juce::Justification::centred);
        return;
    }

    // picos por columna (solo cuando cambia el sample o el ancho)
    if (sample != cachedSample || (int) peaks.size() != area.getWidth())
    {
        cachedSample = sample;
        peaks.assign ((size_t) std::max (1, area.getWidth()), 0.0f);
        const auto& buf = sample->buffer;
        const int len = buf.getNumSamples();
        for (size_t x = 0; x < peaks.size(); ++x)
        {
            const int from = (int) ((juce::int64) x * len / (juce::int64) peaks.size());
            const int to = std::max (from + 1, (int) ((juce::int64) (x + 1) * len / (juce::int64) peaks.size()));
            float peak = 0.0f;
            for (int c = 0; c < buf.getNumChannels(); ++c)
                peak = std::max (peak, buf.getMagnitude (c, from, std::min (to, len) - from));
            peaks[x] = std::min (1.0f, peak);
        }
    }

    auto value = [this] (const char* name) { return proc.apvts.getRawParameterValue (DrumProcessor::padParamId (pad, name))->load(); };
    const bool reversed = value ("rev") > 0.5f;
    const float decay = value ("decay") * 0.01f;
    const int n = (int) peaks.size();
    const int decayEnd = (int) std::round (decay * (float) n);   // columnas que suenan (contando desde donde arranca)
    const float mid = (float) area.getCentreY(), half = (float) area.getHeight() * 0.5f;

    for (int x = 0; x < n; ++x)
    {
        // con REVERSE el sonido arranca a la derecha: lo que corta el DECAY queda a la izquierda
        const int played = reversed ? n - 1 - x : x;
        const bool audible = played < decayEnd;
        const float h = std::max (0.5f, peaks[(size_t) x] * half);
        g.setColour (audible ? kLcd : kLcdDim.withAlpha (0.45f));
        g.fillRect ((float) (area.getX() + x), mid - h, 1.0f, h * 2.0f);
    }

    g.setFont (mono (10.0f));
    g.setColour (kLcdDim);
    const double seconds = sample->buffer.getNumSamples() / sample->sampleRate;
    g.drawText (juce::String (seconds, 2) + " s" + (reversed ? "  REV" : ""), area, juce::Justification::topRight);
}

//==============================================================================
DrumEditor::DrumEditor (DrumProcessor& p)
    : AudioProcessorEditor (&p), proc (p), pads (p), masterAttachment (p.apvts, "master", master),
      browser (p), waveform (p), pressureAtt (p.apvts, "press", pressure)
{
    look.applyTheme();
    setLookAndFeel (&look);

    addAndMakeVisible (pads);
    pads.onPadMenu = [this] (int pad) { showPadMenu (pad); };

    addAndMakeVisible (loadButton);
    addAndMakeVisible (clearButton);
    loadButton.onClick = [this] { choosePadFile (proc.getSelectedPad()); };
    clearButton.onClick = [this] { proc.clearPad (proc.getSelectedPad()); };
    addAndMakeVisible (browserButton);
    browserButton.setColour (juce::TextButton::buttonOnColourId, kRed);
    browserButton.onClick = [this] { setBrowserMode (! browserMode); };
    browserButton.setTooltip ("Todo lo que fuiste cargando: escuchalo, arrastralo a un pad o exportalo como kit");
    addChildComponent (browser);

    master.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    master.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
    master.setTextValueSuffix (" dB");
    addAndMakeVisible (master);

    status.setFont (mono (12.0f));
    status.setColour (juce::Label::textColourId, kInkDim);
    addAndMakeVisible (status);

    //---- sonido del pad
    padTitle.setFont (sans (18.0f));
    padTitle.setColour (juce::Label::textColourId, kInk);
    addAndMakeVisible (padTitle);
    addAndMakeVisible (waveform);
    setupKnob (vol, "Vol", " dB");
    setupKnob (pan, "Pan", "");
    setupKnob (pitch, "Pitch", " st");
    setupKnob (decay, "Decay", " %");
    for (auto* t : { &reverse, &mute, &monoButton })
        addAndMakeVisible (*t);
    setupCaption (chokeCaption, "CHOKE");
    setupCaption (outputCaption, "SALIDA");
    setupCaption (noteCaption, "NOTA");
    choke.addItemList ({ "Off", "1", "2", "3", "4", "5", "6", "7", "8" }, 1);
    output.addItemList (DrumProcessor::outputNames(), 1);
    addAndMakeVisible (choke);
    addAndMakeVisible (output);
    noteLabel.setFont (mono (13.0f));
    noteLabel.setColour (juce::Label::textColourId, kLcd);
    noteLabel.setColour (juce::Label::backgroundColourId, kDisplay);
    noteLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (noteLabel);
    learnButton.setClickingTogglesState (false);
    learnButton.setColour (juce::TextButton::buttonOnColourId, kRed);
    learnButton.onClick = [this]
    {
        auto& learn = proc.engine.learnPad;
        learn.store (learn.load() == attachedPad ? -1 : attachedPad);
    };
    addAndMakeVisible (learnButton);
    monoButton.setTooltip ("Un golpe nuevo corta el anterior del mismo pad");
    choke.setTooltip ("Los pads del mismo grupo se cortan entre si (ej. hi-hat cerrado y abierto)");
    output.setTooltip ("Salida del plugin: en FL se conectan al mixer desde la pestana de entradas y salidas del plugin");
    learnButton.setTooltip ("Toca una nota en tu controlador para asignarla a este pad");

    //---- MIDI
    midiTitle.setFont (sans (14.0f));
    midiTitle.setText ("MIDI", juce::dontSendNotification);
    midiTitle.setColour (juce::Label::textColourId, kInkDim);
    addAndMakeVisible (midiTitle);
    setupCaption (channelCaption, "CANAL");
    setupCaption (rangeCaption, "RANGO");
    setupCaption (rangeTo, "a");
    rangeTo.setJustificationType (juce::Justification::centred);
    channel.addItem ("Todos", 1);
    for (int c = 1; c <= 16; ++c)
        channel.addItem (juce::String (c), c + 1);
    for (int note = 0; note < 128; ++note)
    {
        rangeLo.addItem (noteName (note), note + 1);
        rangeHi.addItem (noteName (note), note + 1);
    }
    for (auto* c : { &channel, &rangeLo, &rangeHi })
        addAndMakeVisible (*c);
    channelAtt = std::make_unique<ComboAttachment> (proc.apvts, "chan", channel);
    rangeLoAtt = std::make_unique<ComboAttachment> (proc.apvts, "rlo", rangeLo);
    rangeHiAtt = std::make_unique<ComboAttachment> (proc.apvts, "rhi", rangeHi);

    //---- roll del pad
    rollTitle.setFont (sans (14.0f));
    rollTitle.setText ("ROLL", juce::dontSendNotification);
    rollTitle.setColour (juce::Label::textColourId, kInkDim);
    addAndMakeVisible (rollTitle);
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
    setupFader (velSens, "Vel", " %");
    setupFader (voices, "Voces", "");
    swing.attachment = std::make_unique<SliderAttachment> (proc.apvts, "swing", swing.slider);
    velSens.attachment = std::make_unique<SliderAttachment> (proc.apvts, "vel", velSens.slider);
    voices.attachment = std::make_unique<SliderAttachment> (proc.apvts, "voices", voices.slider);
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
    setSize (1340, 720);
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

void DrumEditor::setupKnob (Knob& k, const juce::String& name, const juce::String& suffix)
{
    setupCaption (k.label, name.toUpperCase());
    k.label.setJustificationType (juce::Justification::centred);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 68, 18);
    k.slider.setTextValueSuffix (suffix);
    k.slider.setColour (juce::Slider::textBoxTextColourId, kLcd);
    k.slider.setColour (juce::Slider::textBoxBackgroundColourId, kDisplay);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (k.slider);
}

void DrumEditor::layoutKnob (Knob& k, juce::Rectangle<int> area)
{
    k.label.setBounds (area.removeFromTop (14));
    k.slider.setBounds (area.reduced (4, 0));
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
    for (auto* k : { &vol, &pan, &pitch, &decay })
        k->attachment.reset();
    reverseAtt.reset(); muteAtt.reset(); monoAtt.reset(); chokeAtt.reset(); outputAtt.reset();

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
    vol.attachment = std::make_unique<SliderAttachment> (proc.apvts, id ("vol"), vol.slider);
    pan.attachment = std::make_unique<SliderAttachment> (proc.apvts, id ("pan"), pan.slider);
    pitch.attachment = std::make_unique<SliderAttachment> (proc.apvts, id ("pitch"), pitch.slider);
    decay.attachment = std::make_unique<SliderAttachment> (proc.apvts, id ("decay"), decay.slider);
    reverseAtt = std::make_unique<ButtonAttachment> (proc.apvts, id ("rev"), reverse);
    muteAtt = std::make_unique<ButtonAttachment> (proc.apvts, id ("mute"), mute);
    monoAtt = std::make_unique<ButtonAttachment> (proc.apvts, id ("mono"), monoButton);
    chokeAtt = std::make_unique<ComboAttachment> (proc.apvts, id ("choke"), choke);
    outputAtt = std::make_unique<ComboAttachment> (proc.apvts, id ("out"), output);
    waveform.setPad (pad);
    timerCallback();
}

void DrumEditor::setBrowserMode (bool on)
{
    browserMode = on;
    browserButton.setToggleState (on, juce::dontSendNotification);
    if (on)
        browser.refresh (true);   // otra instancia pudo haber sumado samples
    resized();
    refresh();
    repaint();
}

void DrumEditor::refresh()
{
    if (browserMode)
        browser.refresh (false);
    const int pad = proc.getSelectedPad();
    attachPad (pad);
    const auto name = proc.getPadName (pad);
    padTitle.setText ("PAD " + juce::String (pad + 1) + "   " + (name.isEmpty() ? juce::String ("VACIO") : name.toUpperCase()),
                      juce::dontSendNotification);
    if (const auto error = proc.takeLoadError(); error.isNotEmpty())
        status.setText (error, juce::dontSendNotification);
    else
        status.setText (browserMode ? "Browser: clic para escuchar, doble clic carga en el pad elegido, o arrastra el sample a un pad. Clic derecho: mas opciones."
                                    : "Mantene un pad para el roll. Clic derecho en un pad: cargar o vaciar. Arrastra samples encima de los pads.",
                        juce::dontSendNotification);
    pads.repaint();
}

void DrumEditor::timerCallback()
{
    pads.repaint();
    waveform.repaint();
    if (attachedPad < 0)
        return;

    if (proc.engine.lastLearnedPad.exchange (-1) >= 0)
        proc.updateHostDisplay();   // el proyecto cambio (nota nueva): que el DAW lo sepa
    learnButton.setToggleState (proc.engine.learnPad.load() == attachedPad, juce::dontSendNotification);
    noteLabel.setText (noteName (proc.engine.padNote[(size_t) attachedPad].load()), juce::dontSendNotification);

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
    if (browserMode)
    {
        g.fillRect (soundBounds.getX() - 11, soundBounds.getY(), 1, soundBounds.getHeight());
        return;
    }

    // separadores de columnas y lineas bajo los titulos
    for (const auto& col : { soundBounds, panelBounds })
        g.fillRect (col.getX() - 11, col.getY(), 1, col.getHeight());
    g.fillRect (soundBounds.getX(), padTitle.getBottom() + 2, soundBounds.getWidth(), 1);
    g.fillRect (soundBounds.getX(), midiTitle.getBottom() + 2, soundBounds.getWidth(), 1);
    g.fillRect (panelBounds.getX(), rollTitle.getBottom() + 2, panelBounds.getWidth(), 1);
    g.fillRect (panelBounds.getX(), globalTitle.getBottom() + 2, panelBounds.getWidth(), 1);

    // mini pantallas detras de los valores de las perillas
    g.setColour (kDisplay);
    for (auto* k : { &vol, &pan, &pitch, &decay })
        g.fillRoundedRectangle (k->slider.getBounds().removeFromBottom (18).reduced (2, 0).toFloat(), 4.0f);
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
    header.removeFromRight (24);
    browserButton.setBounds (header.removeFromRight (110).withSizeKeepingCentre (110, 30));

    status.setBounds (area.removeFromBottom (22));
    area.removeFromBottom (10);
    area.removeFromTop (10);

    // tres columnas: pads | sonido y MIDI del pad | roll y generales
    pads.setBounds (area.removeFromLeft (area.getHeight()));
    area.removeFromLeft (22);
    soundBounds = area.removeFromLeft ((area.getWidth() - 22) / 2);
    area.removeFromLeft (22);
    panelBounds = area;

    auto rows = [] (juce::Rectangle<int>& col)
    {
        return [&col] (int h = 30)
        {
            auto r = col.removeFromTop (h);
            col.removeFromTop (8);
            return r;
        };
    };

    //---- columna del medio: sonido
    {
        auto col = soundBounds;
        auto row = rows (col);
        padTitle.setBounds (row (26));
        waveform.setBounds (row (96));
        {
            auto r = row (86);
            const int w = r.getWidth() / 4;
            for (auto* k : { &vol, &pan, &pitch, &decay })
                layoutKnob (*k, r.removeFromLeft (w));
        }
        {
            auto r = row();
            const int w = (r.getWidth() - 16) / 3;
            reverse.setBounds (r.removeFromLeft (w));
            r.removeFromLeft (8);
            mute.setBounds (r.removeFromLeft (w));
            r.removeFromLeft (8);
            monoButton.setBounds (r);
        }
        {
            auto r = row();
            chokeCaption.setBounds (r.removeFromLeft (56));
            choke.setBounds (r.removeFromLeft (80));
            r.removeFromLeft (16);
            outputCaption.setBounds (r.removeFromLeft (60));
            output.setBounds (r);
        }
        {
            auto r = row();
            noteCaption.setBounds (r.removeFromLeft (56));
            noteLabel.setBounds (r.removeFromLeft (80).reduced (0, 3));
            r.removeFromLeft (16);
            learnButton.setBounds (r.removeFromLeft (100));
        }

        col.removeFromTop (6);
        midiTitle.setBounds (row (22));
        {
            auto r = row();
            channelCaption.setBounds (r.removeFromLeft (56));
            channel.setBounds (r.removeFromLeft (100));
        }
        {
            auto r = row();
            rangeCaption.setBounds (r.removeFromLeft (56));
            rangeLo.setBounds (r.removeFromLeft (90));
            rangeTo.setBounds (r.removeFromLeft (24));
            rangeHi.setBounds (r.removeFromLeft (90));
        }
    }

    //---- columna derecha: roll y generales
    {
        auto col = panelBounds;
        auto row = rows (col);
        rollTitle.setBounds (row (22));
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

        col.removeFromTop (6);
        {
            auto r = row (22);
            bpmLabel.setBounds (r.removeFromRight (140));
            globalTitle.setBounds (r);
        }
        layoutFader (swing, row());
        layoutFader (humanize, row());
        layoutFader (velSens, row());
        layoutFader (voices, row());
        pressure.setBounds (row().removeFromLeft (140));
    }

    // el browser ocupa las dos columnas de la derecha; los pads quedan siempre
    const auto right = soundBounds.getUnion (panelBounds);
    browser.setBounds (right);
    browser.setVisible (browserMode);
    for (auto* child : getChildren())
        if (child != &browser && right.intersects (child->getBounds()))
            child->setVisible (! browserMode);
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
    // sobre el browser: se suman a la biblioteca (las carpetas, enteras) sin cargarlos en un pad
    if (browserMode && browser.getBounds().contains (x, y))
    {
        browser.addDropped (files);
        return;
    }
    // soltado sobre un pad: desde ese pad; afuera de los pads: desde el pad elegido
    const int target = padAtEditorPoint (x, y);
    const int first = target >= 0 ? target : proc.getSelectedPad();
    proc.setSelectedPad (first);
    loadFiles (files, first);
}
