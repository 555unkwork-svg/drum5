#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

/** Colores, tipografia y dibujo comunes de la interfaz. */
namespace ui
{
// Paleta del tema actual. kRed es el color de acento (rojo en el tema original).
inline juce::Colour kBody (0xffededea), kBodyLine (0xffcbccc7), kInk (0xff141516), kInkDim (0xff70736f),
                    kKeyDark (0xff1e1f21), kKeyGrey (0xffb3b5b1), kKeyLight (0xffffffff),
                    kRed (0xffd8121f), kDisplay (0xff0b0c0d), kLcd (0xfff5f3ee), kLcdDim (0xff8d908a),
                    kLedOff (0xff55585a);

/** Temas de color. El 0 es el original. */
juce::StringArray themeNames();
int currentTheme();
void setTheme (int index);

/** Blanco o tinta, lo que se lea mejor sobre ese fondo. */
juce::Colour textOn (juce::Colour background);

/** La tipografia propia del plugin, embebida. */
juce::FontOptions sans (float height, bool bold = true);
juce::FontOptions mono (float height);
juce::String noteName (int note);

/** Dibuja una tecla con relieve y devuelve el rectangulo de su cara superior. */
juce::Rectangle<float> drawKey (juce::Graphics&, juce::Rectangle<float> area, juce::Colour colour,
                                bool down, float radius = 5.0f);
void drawLed (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour colour, bool on);
}

/** Estilo "aparato": cuerpo gris claro, teclas con relieve, pantalla negra y acentos naranjas. */
class HardwareLook final : public juce::LookAndFeel_V4
{
public:
    HardwareLook();
    /** Vuelve a tomar los colores del tema actual. */
    void applyTheme();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};
