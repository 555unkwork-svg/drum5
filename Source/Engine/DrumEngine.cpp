#include "DrumEngine.h"

#include <cmath>

namespace eng
{
namespace
{
constexpr int kFlashBlocks = 6;
constexpr double kFirstHitGap = 0.3;   // si la grilla cae a menos de 30 % de un paso del primer golpe, se saltea (evita golpes dobles)
constexpr float kGateFadeMs = 2.0f;
constexpr float kHumanizeMaxMs = 8.0f;
constexpr float kHumanizeMaxVel = 0.15f;
constexpr double kPitchVarBeats = 4.0;  // Pitch Var llega a su valor en un compas

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

double rateBeats (Rate r)
{
    switch (r)
    {
        case Rate::r8:   return 0.5;          // 48 ticks
        case Rate::r8t:  return 1.0 / 3.0;    // 32 ticks
        case Rate::r16:  return 0.25;         // 24 ticks
        case Rate::r16t: return 1.0 / 6.0;    // 16 ticks
        case Rate::r32:  return 0.125;        // 12 ticks
        case Rate::r32t: return 1.0 / 12.0;   // 8 ticks
    }
    return 0.25;
}

Rate fasterRate (Rate r)
{
    switch (r)
    {
        case Rate::r8:   return Rate::r16;
        case Rate::r8t:  return Rate::r16t;
        case Rate::r16:  return Rate::r32;
        case Rate::r16t: return Rate::r32t;
        default:         return r;
    }
}

bool rateSwings (Rate r) { return r == Rate::r8 || r == Rate::r16; }

DrumEngine::DrumEngine()
{
    for (int i = 0; i < kNumPads; ++i)
    {
        padNote[(size_t) i].store (kDefaultFirstNote + i);
        padFlash[(size_t) i].store (0);
        padRolling[(size_t) i].store (false);
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
    for (auto& r : rolls)
        r = Roll();
    expectedBeat = -1.0;
    wasPlaying = false;
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

void DrumEngine::triggerPadFromUI (int pad, bool down, float velocity)
{
    if (pad < 0 || pad >= kNumPads)
        return;
    if (down)
    {
        uiVelocity[(size_t) pad].store (juce::jlimit (0.0f, 1.0f, velocity));
        uiDown.fetch_or (1u << pad);
    }
    else
    {
        uiUp.fetch_or (1u << pad);
    }
}

//==============================================================================
double DrumEngine::currentStepBeats (int pad, double beatNow) const
{
    const auto& ps = pads[(size_t) pad];
    auto rate = ps.rate;
    if (ps.ramp && beatNow - rolls[(size_t) pad].startBeat >= ps.rampBeats - 1.0e-9)
        rate = fasterRate (rate);
    return rateBeats (rate);
}

double DrumEngine::nextGridAfter (double beat, double step, bool swing) const
{
    // los pasos impares se atrasan: con 66 % el segundo 1/16 de cada par cae como un tresillo
    const double offset = swing ? (globals.swing - 0.5) * 2.0 * step : 0.0;
    const auto k0 = (juce::int64) std::floor (beat / step) - 1;
    for (auto k = k0; k < k0 + 4; ++k)
    {
        const double t = (double) k * step + ((k % 2 != 0) ? offset : 0.0);
        if (t > beat)
            return t;
    }
    return (double) (k0 + 4) * step;
}

void DrumEngine::scheduleNext (int pad, double hitBeat, double hitSample)
{
    auto& r = rolls[(size_t) pad];
    const auto& ps = pads[(size_t) pad];
    if (! ps.retrigger)
    {
        r.active = false;
        return;
    }
    const double elapsed = hitBeat - r.startBeat;
    if (ps.freeMode)
    {
        const double hz = ps.freeHz * ((ps.ramp && elapsed >= ps.rampBeats - 1.0e-9) ? 2.0 : 1.0);
        r.nextSample = hitSample + hostRate / std::max (0.1, hz);
    }
    else
    {
        const double step = currentStepBeats (pad, hitBeat);
        auto rate = ps.rate;
        if (ps.ramp && elapsed >= ps.rampBeats - 1.0e-9)
            rate = fasterRate (rate);
        const double minGap = r.hits <= 1 ? kFirstHitGap * step : 1.0e-6;
        r.nextBeat = nextGridAfter (hitBeat + minGap, step, rateSwings (rate));
    }
}

void DrumEngine::fireHit (int pad, int time, bool first)
{
    auto& r = rolls[(size_t) pad];
    const auto& ps = pads[(size_t) pad];
    const double hitBeat = beatAt (time);

    float vel = (globals.pressure && ! first && r.pressure >= 0.0f) ? r.pressure : r.velocity;
    if (ps.accent > 0.0f && r.hits % std::max (1, ps.accentGroup) != 0)
        vel *= 1.0f - ps.accent;

    float delaySamples = 0.0f;
    if (globals.humanize > 0.0f && ! first)
    {
        vel *= 1.0f + (random.nextFloat() * 2.0f - 1.0f) * kHumanizeMaxVel * globals.humanize;
        delaySamples = random.nextFloat() * globals.humanize * kHumanizeMaxMs * 0.001f * (float) hostRate;
    }
    vel = juce::jlimit (0.0f, 1.0f, vel);

    const double semis = ps.pitchVar * std::min (1.0, (hitBeat - r.startBeat) / kPitchVarBeats);

    juce::int64 gate = -1;
    if (ps.retrigger && ps.gate < 0.999f)
    {
        const double interval = ps.freeMode ? hostRate / std::max (0.1f, ps.freeHz)
                                            : currentStepBeats (pad, hitBeat) / beatsPerSample;
        gate = std::max<juce::int64> (1, (juce::int64) (ps.gate * interval));
    }

    padFlash[(size_t) pad].store (kFlashBlocks, std::memory_order_relaxed);
    ++r.hits;
    scheduleNext (pad, hitBeat, (double) time);

    const auto& src = current[(size_t) pad];
    if (src == nullptr || src->buffer.getNumSamples() == 0 || vel <= 0.0f)
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
    v.rate = src->sampleRate / hostRate * std::pow (2.0, semis / 12.0);
    v.pos = -(double) delaySamples * v.rate;   // humanizar: arranca un poco despues (se leen ceros)
    v.gain = vel;
    v.gateLeft = gate;
    v.fade = 1.0f;
}

void DrumEngine::noteOn (int pad, float velocity, int time)
{
    auto& r = rolls[(size_t) pad];
    const auto& ps = pads[(size_t) pad];

    // LATCH: tocar el pad de un roll trabado lo apaga
    if (ps.latch && ps.retrigger && r.active)
    {
        r.active = false;
        r.held = false;
        return;
    }

    // con el proyecto parado, el reloj propio arranca en el golpe si no hay otro roll andando
    if (! wasPlaying && std::none_of (rolls.begin(), rolls.end(), [] (const Roll& x) { return x.active; }))
        blockBeat = -time * beatsPerSample;

    r = Roll();
    r.active = true;
    r.held = true;
    r.velocity = velocity;
    r.startBeat = beatAt (time);
    fireHit (pad, time, true);
}

void DrumEngine::noteOff (int pad)
{
    auto& r = rolls[(size_t) pad];
    r.held = false;
    if (! pads[(size_t) pad].latch)
        r.active = false;
}

void DrumEngine::runRolls (juce::AudioBuffer<float>& out, int from, int to)
{
    for (;;)
    {
        int pad = -1;
        double best = (double) to;
        for (int p = 0; p < kNumPads; ++p)
        {
            const auto& r = rolls[(size_t) p];
            if (! r.active)
                continue;
            const double t = pads[(size_t) p].freeMode ? r.nextSample : (r.nextBeat - blockBeat) / beatsPerSample;
            if (t < best)
            {
                best = t;
                pad = p;
            }
        }
        if (pad < 0)
            break;
        const int time = juce::jlimit (from, to - 1, (int) std::ceil (best - 1.0e-6));
        render (out, from, time - from);
        from = time;
        fireHit (pad, time, false);
    }
    render (out, from, to - from);
}

void DrumEngine::render (juce::AudioBuffer<float>& out, int start, int num)
{
    if (num <= 0)
        return;
    const int outChannels = out.getNumChannels();
    const float fadeStep = 1.0f / (kGateFadeMs * 0.001f * (float) hostRate);

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
            if (v.pos >= (double) len || v.fade <= 0.0f)
            {
                v.active = false;
                v.src.reset();
                break;
            }
            if (v.pos >= 0.0)
            {
                if (v.gateLeft == 0)
                    v.fade -= fadeStep;
                else if (v.gateLeft > 0)
                    --v.gateLeft;
            }
            const float g = v.gain * std::max (0.0f, v.fade);
            out.addSample (0, start + i, hermite (l, len, v.pos) * g);
            if (outChannels > 1)
                out.addSample (1, start + i, hermite (r, len, v.pos) * g);
            v.pos += v.rate;
        }
    }
}

void DrumEngine::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Transport& transport)
{
    out.clear();
    {
        const juce::SpinLock::ScopedTryLockType lock (swapLock);
        if (lock.isLocked())
            current = shared;
    }

    for (auto& f : padFlash)
        if (const int n = f.load (std::memory_order_relaxed); n > 0)
            f.store (n - 1, std::memory_order_relaxed);

    //---- reloj del bloque
    const int numSamples = out.getNumSamples();
    const double bpm = transport.bpm > 0.0 ? transport.bpm : 120.0;
    beatsPerSample = bpm / 60.0 / hostRate;
    if (transport.playing)
    {
        blockBeat = transport.ppq;
        // arranque del play, loop o salto: los rolls vuelven a la grilla desde aca
        const bool jumped = ! wasPlaying || std::abs (blockBeat - expectedBeat) > 1.0e-3;
        if (jumped)
            for (int p = 0; p < kNumPads; ++p)
                if (rolls[(size_t) p].active && ! pads[(size_t) p].freeMode)
                {
                    auto& r = rolls[(size_t) p];
                    r.startBeat = expectedBeat >= 0.0 ? blockBeat - (expectedBeat - r.startBeat) : blockBeat;
                    const auto rate = pads[(size_t) p].rate;
                    r.nextBeat = nextGridAfter (blockBeat - 1.0e-9, currentStepBeats (p, blockBeat), rateSwings (rate));
                }
    }
    else
    {
        blockBeat = expectedBeat >= 0.0 ? expectedBeat : 0.0;   // reloj propio, sigue de corrido
    }
    wasPlaying = transport.playing;

    // LATCH apagado con el pad suelto: el roll termina
    for (int p = 0; p < kNumPads; ++p)
        if (rolls[(size_t) p].active && ! rolls[(size_t) p].held && ! pads[(size_t) p].latch)
            rolls[(size_t) p].active = false;

    //---- interfaz (al inicio del bloque)
    if (const auto up = uiUp.exchange (0); up != 0)
        for (int p = 0; p < kNumPads; ++p)
            if (up & (1u << p))
                noteOff (p);
    if (const auto down = uiDown.exchange (0); down != 0)
        for (int p = 0; p < kNumPads; ++p)
            if (down & (1u << p))
                noteOn (p, uiVelocity[(size_t) p].load(), 0);

    //---- MIDI y rolls, en orden
    int pos = 0;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const bool on = msg.isNoteOn(), off = msg.isNoteOff();
        const bool chPressure = msg.isChannelPressure(), polyPressure = msg.isAftertouch();
        if (! (on || off || chPressure || polyPressure))
            continue;

        const int time = juce::jlimit (0, numSamples, meta.samplePosition);
        runRolls (out, pos, time);
        pos = time;

        if (chPressure)
        {
            const float value = (float) msg.getChannelPressureValue() / 127.0f;
            for (auto& r : rolls)
                if (r.active)
                    r.pressure = value;
            continue;
        }
        for (int p = 0; p < kNumPads; ++p)
        {
            if (padNote[(size_t) p].load (std::memory_order_relaxed) != msg.getNoteNumber())
                continue;
            if (polyPressure)
                rolls[(size_t) p].pressure = (float) msg.getAfterTouchValue() / 127.0f;
            else if (on)
                noteOn (p, msg.getFloatVelocity(), time);
            else
                noteOff (p);
        }
    }
    runRolls (out, pos, numSamples);

    //---- fin del bloque
    for (int p = 0; p < kNumPads; ++p)
    {
        auto& r = rolls[(size_t) p];
        if (r.active && pads[(size_t) p].freeMode)
            r.nextSample -= numSamples;
        padRolling[(size_t) p].store (r.active, std::memory_order_relaxed);
    }
    expectedBeat = blockBeat + numSamples * beatsPerSample;
}
}
