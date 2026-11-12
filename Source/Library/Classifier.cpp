#include "Classifier.h"

namespace lib
{
const juce::StringArray& builtInCategories()
{
    static const juce::StringArray names { "Kick", "808", "Snare", "Clap", "HH Cerrado", "HH Abierto", "Rim", "Tom", "Perc",
                                           "Crash", "Ride", "Cymbal", "FX", "Vox", "Chant", kOther };
    return names;
}

namespace
{
/** Una regla: si alguna palabra del nombre coincide, es esa categoria.
    `exact`: palabras cortas que tienen que ser la palabra entera ("oh", "sd").
    `prefix`: la palabra empieza asi ("snares", "kicks", "hihat"). */
struct Rule
{
    const char* category;
    juce::StringArray exact, prefix;
};

// el orden importa: lo mas especifico primero (hat abierto antes que hat, kick antes que 808)
const std::vector<Rule>& rules()
{
    static const std::vector<Rule> r {
        { "HH Abierto", { "oh", "ohh", "ohat", "ophat" }, { "openhat", "openhh", "openhihat", "hhopen", "hatopen", "hihatopen" } },
        { "Kick",       { "bd", "kik", "kck", "kk" },     { "kick", "bombo", "bassdrum" } },
        { "808",        { "808", "808s", "sub", "subs" }, { "bass", "subbass" } },
        { "Snare",      { "sd", "snr", "sn" },            { "snare", "redoblante", "caja" } },
        { "Clap",       { "clp", "cp" },                  { "clap", "palma", "aplauso" } },
        { "Rim",        { "rs" },                         { "rim", "sidestick", "crossstick", "stick" } },
        { "HH Cerrado", { "hh", "chh", "ch", "hhc" },     { "hat", "hihat", "closedhat", "closedhh", "hhclosed", "hatclosed" } },
        { "Tom",        {},                               { "tom" } },
        { "Crash",      {},                               { "crash" } },
        { "Ride",       {},                               { "ride" } },
        { "Cymbal",     { "cym", "cy" },                  { "cymbal", "platillo", "china", "splash" } },
        { "Chant",      { "hey", "gol", "oe", "ole" },    { "chant", "canto", "cantito", "hinchada", "tribuna" } },
        { "Vox",        { "vo" },                         { "vox", "vocal", "voice", "voz", "adlib", "shout", "phrase" } },
        { "FX",         { "fx", "sfx" },                  { "riser", "sweep", "impact", "noise", "uplifter", "downlifter",
                                                            "transition", "laser", "whoosh", "reverse", "glitch" } },
        { "Perc",       {},                               { "perc", "shaker", "shk", "conga", "bongo", "cowbell", "tamb",
                                                            "block", "clave", "timbal", "guiro", "triangle", "snap", "click" } },
    };
    return r;
}

/** Parte un nombre en palabras: separa por simbolos, por letras/numeros y por mayusculas ("OpenHat_02" -> open, hat, 02). */
juce::StringArray words (const juce::String& text)
{
    juce::StringArray out;
    juce::String current;
    auto flush = [&] { if (current.isNotEmpty()) out.add (current.toLowerCase()); current.clear(); };

    juce::juce_wchar prev = 0;
    for (auto c : text)
    {
        const bool alpha = juce::CharacterFunctions::isLetter (c);
        const bool digit = juce::CharacterFunctions::isDigit (c);
        if (! alpha && ! digit)
        {
            flush();
        }
        else
        {
            const bool prevAlpha = juce::CharacterFunctions::isLetter (prev);
            const bool prevDigit = juce::CharacterFunctions::isDigit (prev);
            const bool camel = alpha && prevAlpha && juce::CharacterFunctions::isUpperCase (c) && juce::CharacterFunctions::isLowerCase (prev);
            // "808s" y "s808" quedan juntos; "Kick808" se separa
            if (camel || (digit && prevAlpha && current.length() > 1) || (alpha && prevDigit && ! (c == 's' || c == 'S')))
                flush();
            current += c;
        }
        prev = (alpha || digit) ? c : 0;
    }
    flush();
    return out;
}

bool matches (const Rule& rule, const juce::StringArray& w)
{
    for (int i = 0; i < w.size(); ++i)
    {
        if (rule.exact.contains (w[i]))
            return true;
        // la palabra sola o pegada a la siguiente ("open hat" -> "openhat", "hi hat" -> "hihat")
        const juce::String joined = i + 1 < w.size() ? w[i] + w[i + 1] : juce::String();
        const juce::String joined3 = i + 2 < w.size() ? joined + w[i + 2] : juce::String();
        for (const auto& p : rule.prefix)
            if (w[i].startsWith (p) || (joined.isNotEmpty() && joined.startsWith (p)) || (joined3.isNotEmpty() && joined3.startsWith (p)))
                return true;
    }
    return false;
}
}

juce::String classifyText (const juce::String& text)
{
    const auto w = words (text);
    for (const auto& rule : rules())
        if (matches (rule, w))
            return rule.category;
    return {};
}

juce::String classify (const juce::File& file)
{
    if (auto c = classifyText (file.getFileNameWithoutExtension()); c.isNotEmpty())
        return c;
    // las carpetas, de la mas cercana a la mas lejana (como mucho 4 niveles)
    auto dir = file.getParentDirectory();
    for (int level = 0; level < 4 && dir != dir.getParentDirectory(); ++level, dir = dir.getParentDirectory())
        if (auto c = classifyText (dir.getFileName()); c.isNotEmpty())
            return c;
    return kOther;
}
}
