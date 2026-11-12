#pragma once

#include <juce_core/juce_core.h>

namespace lib
{
/** Categorias que drum5 reconoce solo, en el orden en que se muestran. "Otros" es lo que no reconoce. */
const juce::StringArray& builtInCategories();
inline const char* const kOther = "Otros";

/** Adivina la categoria de un sample por su nombre y, si no alcanza, por las carpetas donde esta
    (de la mas cercana a la mas lejana). Ej: "Drums/Snares/hit 3.wav" -> "Snare". */
juce::String classify (const juce::File& file);

/** Lo mismo, a partir de un texto suelto (un nombre de archivo o de carpeta). Vacio si no reconoce nada. */
juce::String classifyText (const juce::String& text);
}
