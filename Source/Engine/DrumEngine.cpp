#include "DrumEngine.h"

#include <cmath>

namespace eng
{
namespace
{
constexpr int kFlashBlocks = 6;

inline float readSample (const float* data, int length, int index) noexcept
{
    return (index >= 0 && index < length) ? data[index] : 0.0f;
}

/** Interpolacion Hermite de 4 puntos (la misma de sampler5). */
inline float hermite (const float* data, int length, double pos) noexcept
{
    const int i = (int) std::floor (pos);
    const float t = (float) (pos - (double) i);
    const float y0 = readSample (data, length, i - 1);
    const float y1 = readSample (data, length, i);
    const float y2 = readSample (data, length, i + 1);
    const float y3 = readSample (data, length, i + 2);
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}
}

DrumEngine::DrumEngine()
{
    for (int i = 0; i < kNumPads; ++i)
    {
        padNote[(size_t) i].store (kDefaultFirstNote + i);
        padFlash[(size_t) i].store (0);
        uiVelocity[(size_t) i].store (1.0f);
    }
}

void DrumEngine::prepare (double sampleRate)
{
    hostRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    for (auto& v : voices)
    {
        v.active = false;
        v.src.reset();   // no es la ultima referencia: `shared` o `retired` la mantienen viva
    }
}

void DrumEngine::setSample (int pad, SamplePtr sample)
{
    if (pad < 0 || pad >= kNumPads)
        return;
    SamplePtr old;
    {
        const juce::SpinLock::ScopedLockType lock (swapLock);
        old = std::move (shared[(size_t) pad]);
        shared[(size_t) pad] = std::move (sample);
    }
    if (old != nullptr)
        retired.push_back (std::move (old));
    purgeRetired();
}

SamplePtr DrumEngine::getSample (int pad) const
{
    if (pad < 0 || pad >= kNumPads)
        return {};
    const juce::SpinLock::ScopedLockType lock (swapLock);
    return shared[(size_t) pad];
}

void DrumEngine::purgeRetired()
{
    // un sample retirado se puede borrar cuando nadie mas lo usa (use_count == 1 es el nuestro)
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                                   [] (const SamplePtr& p) { return p.use_count() <= 1; }),
                   retired.end());
}

void DrumEngine::triggerPadFromUI (int pad, float velocity)
{
    if (pad < 0 || pad >= kNumPads)
        return;
    uiVelocity[(size_t) pad].store (juce::jlimit (0.0f, 1.0f, velocity));
    uiHits.fetch_or (1u << pad);
}

void DrumEngine::fire (int pad, float velocity)
{
    const auto& src = current[(size_t) pad];
    padFlash[(size_t) pad].store (kFlashBlocks, std::memory_order_relaxed);
    if (src == nullptr || src->buffer.getNumSamples() == 0)
        return;

    // una voz libre; si no hay, se roba en ronda
    int index = -1;
    for (int i = 0; i < kMaxVoices; ++i)
        if (! voices[(size_t) i].active) { index = i; break; }
    if (index < 0)
    {
        index = nextVoice;
        nextVoice = (nextVoice + 1) % kMaxVoices;
    }

    auto& v = voices[(size_t) index];
    v.active = true;
    v.pad = pad;
    v.src = src;   // copia del puntero: el hilo de audio no libera nada (el ultimo dueno es `retired`)
    v.pos = 0.0;
    v.rate = src->sampleRate / hostRate;
    v.gain = velocity;
}

void DrumEngine::render (juce::AudioBuffer<float>& out, int start, int num)
{
    if (num <= 0)
        return;
    const int outChannels = out.getNumChannels();

    for (auto& v : voices)
    {
        if (! v.active)
            continue;
        const auto& buf = v.src->buffer;
        const int len = buf.getNumSamples();
        const int srcChannels = buf.getNumChannels();
        const float* l = buf.getReadPointer (0);
        const float* r = buf.getReadPointer (srcChannels > 1 ? 1 : 0);

        for (int i = 0; i < num; ++i)
        {
            if (v.pos >= (double) len)
            {
                v.active = false;
                v.src.reset();
                break;
            }
            const float sl = hermite (l, len, v.pos) * v.gain;
            const float sr = hermite (r, len, v.pos) * v.gain;
            out.addSample (0, start + i, sl);
            if (outChannels > 1)
                out.addSample (1, start + i, sr);
            v.pos += v.rate;
        }
    }
}

void DrumEngine::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi)
{
    out.clear();
    {
        const juce::SpinLock::ScopedTryLockType lock (swapLock);
        if (lock.isLocked())
            current = shared;   // shortcut: copiar 16 shared_ptr por bloque; barato, pero se puede marcar con un contador si hiciera falta
    }

    for (auto& f : padFlash)
        if (const int n = f.load (std::memory_order_relaxed); n > 0)
            f.store (n - 1, std::memory_order_relaxed);

    if (const auto hits = uiHits.exchange (0); hits != 0)
        for (int p = 0; p < kNumPads; ++p)
            if (hits & (1u << p))
                fire (p, uiVelocity[(size_t) p].load());

    const int numSamples = out.getNumSamples();
    int pos = 0;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (! msg.isNoteOn())
            continue;
        const int time = juce::jlimit (0, numSamples, meta.samplePosition);
        render (out, pos, time - pos);
        pos = time;
        for (int p = 0; p < kNumPads; ++p)
            if (padNote[(size_t) p].load (std::memory_order_relaxed) == msg.getNoteNumber())
                fire (p, msg.getFloatVelocity());
    }
    render (out, pos, numSamples - pos);
}
}
