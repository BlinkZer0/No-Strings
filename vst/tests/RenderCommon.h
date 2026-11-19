#pragma once
// ============================================================================
//  Shared helpers for NoStringsRender: WAV writer, pitch estimate, scenario
//  rendering and per-note measurements.
// ============================================================================
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include "Engine.h"

namespace nostrings::test
{
namespace fs = std::filesystem;

struct Context
{
    fs::path outDir = "renders";
    std::string only;
    bool wav = true;
    bool ok = true;
};
// ---------------------------------------------------------------- WAV writer
inline bool writeWav (const fs::path& path, const std::vector<float>& L, const std::vector<float>& R, int sr)
{
    FILE* f = nullptr;
#ifdef _WIN32
    _wfopen_s (&f, path.wstring().c_str(), L"wb");
#else
    f = std::fopen (path.string().c_str(), "wb");
#endif
    if (f == nullptr) return false;
    const uint32_t frames = (uint32_t) L.size(), bytes = frames * 2 * 3;
    auto u32 = [f] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [f] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); u32 (36 + bytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    u32 (16); u16 (1); u16 (2); u32 ((uint32_t) sr); u32 ((uint32_t) sr * 6); u16 (6); u16 (24);
    std::fwrite ("data", 1, 4, f); u32 (bytes);
    std::vector<unsigned char> out ((size_t) bytes);
    size_t k = 0;
    for (uint32_t i = 0; i < frames; ++i)
        for (const float s : { L[i], R[i] })
        {
            const int32_t v = (int32_t) std::lround (clampd (s, -1.0, 1.0) * 8388607.0);
            out[k++] = (unsigned char) (v & 0xff);
            out[k++] = (unsigned char) ((v >> 8) & 0xff);
            out[k++] = (unsigned char) ((v >> 16) & 0xff);
        }
    std::fwrite (out.data(), 1, out.size(), f);
    std::fclose (f);
    return true;
}

// ------------------------------------------------- YIN pitch estimate (Hz)
inline double estimatePitch (const std::vector<float>& x, size_t start, size_t len, int sr, double lo = 30, double hi = 2500)
{
    const int minLag = (int) (sr / hi), maxLag = (int) (sr / lo);
    if (start + len + (size_t) maxLag + 2 > x.size()) return 0;
    std::vector<double> d ((size_t) maxLag + 2, 0.0);
    for (int tau = 1; tau <= maxLag + 1; ++tau)
    {
        double s = 0;
        for (size_t i = 0; i < len; ++i) { const double e = (double) x[start + i] - x[start + i + (size_t) tau]; s += e * e; }
        d[(size_t) tau] = s;
    }
    std::vector<double> cm (d.size(), 1.0);
    double run = 0;
    for (size_t tau = 1; tau < d.size(); ++tau) { run += d[tau]; cm[tau] = run > 0 ? d[tau] * (double) tau / run : 1.0; }
    int best = -1;
    for (int tau = minLag; tau <= maxLag; ++tau)
        if (cm[(size_t) tau] < 0.12)
        {
            while (tau + 1 <= maxLag && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau;
            best = tau; break;
        }
    if (best < 0)
    {
        best = minLag;
        for (int tau = minLag; tau <= maxLag; ++tau) if (cm[(size_t) tau] < cm[(size_t) best]) best = tau;
    }
    const double a = cm[(size_t) best - 1], b = cm[(size_t) best], c = cm[(size_t) best + 1];
    const double den = a - 2 * b + c;
    const double shift = std::fabs (den) > 1e-12 ? 0.5 * (a - c) / den : 0.0;
    return sr / (best + shift);
}

// true once: when the block starting at t contains the moment `at`
inline double gDt = 0;
// Measurement mode: vibrato and reverb off, so pitch, level and decay figures stay exact. Each stage
// sets it before its measurements; renders keep the instrument defaults.
inline bool gMeasure = false;
inline bool hit (double t, double at) { return at >= t && at < t + gDt; }

// Fine pitch: Hann-windowed DFT magnitude scanned in 0.25-cent steps within +-range
// cents of ref. Robust where YIN is fooled (body resonances near the fundamental).
inline double finePitch (const std::vector<float>& x, size_t start, size_t len, int sr, double ref, double range = 50)
{
    if (start + len > x.size()) return 0;
    double best = -1, bestHz = ref;
    for (double c = -range; c <= range; c += 0.25)
    {
        const double f = ref * std::pow (2.0, c / 1200.0);
        double re = 0, im = 0;
        for (size_t i = 0; i < len; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (kTwoPi * (double) i / (double) (len - 1));
            const double ph = kTwoPi * f * (double) (start + i) / sr;
            re += x[start + i] * w * std::cos (ph);
            im += x[start + i] * w * std::sin (ph);
        }
        const double m = re * re + im * im;
        if (m > best) { best = m; bestHz = f; }
    }
    return bestHz;
}

inline double centsBetween (double hz, double ref) { return hz > 0 ? 1200.0 * std::log2 (hz / ref) : 0.0; }

// ------------------------------------------------------------- rendering
struct Take
{
    int sr = 48000;
    std::vector<float> L, R;
    int nanResets = 0;
};

// cb(engine, t) runs once per kChunk block before it renders: trigger notes, move params.
inline Take render (int sr, double seconds, const EngineParams& base, const std::function<void (Engine&, EngineParams&, double)>& cb)
{
    ScopedNoDenormals guard;
    Engine e;
    e.prepare (sr);
    EngineParams p = base;
    if (gMeasure) { p.vibDepth = 0; p.reverbSend = 0; }
    e.setParams (p);
    Take t;
    t.sr = sr;
    const size_t total = (size_t) (seconds * sr);
    t.L.resize (total); t.R.resize (total);
    for (size_t pos = 0; pos < total; pos += kChunk)
    {
        const int n = (int) std::min<size_t> (kChunk, total - pos);
        gDt = (double) kChunk / sr;
        cb (e, p, (double) pos / sr);
        if (gMeasure) { p.vibDepth = 0; p.reverbSend = 0; }
        e.setParams (p);
        e.render (t.L.data() + pos, t.R.data() + pos, n);
    }
    t.nanResets = e.nanResets;
    return t;
}

inline float peakOf (const Take& t) { float p = 0; for (float s : t.L) p = std::max (p, std::fabs (s)); return p; }

// One sustained note: pitch error and Helmholtz score over the steady part, plus
// settle time = when the bow locked into one slip per period for good (0.1 s windows).
struct NoteStats { double cents = 0, helmholtz = 0, settle = 0; float peak = 0; int nan = 0; };

inline NoteStats measureNote (int note, int sr, const EngineParams& p, int vel = 100)
{
    NoteStats st;
    Engine e;
    {
        ScopedNoDenormals guard;
        e.prepare (sr);
        EngineParams q = p;
        q.vibDepth = 0;
        q.reverbSend = 0;
        e.setParams (q);
        e.noteOn (note, vel);
        const double hz = midiToHz (note);
        const int total = (int) (2.5 * sr), steady = (int) (1.2 * sr), win = sr / 10;
        std::vector<float> L ((size_t) total), R ((size_t) total);
        int nextWin = win;
        st.settle = 0;
        for (int pos = 0; pos < total; pos += kChunk)
        {
            if (pos >= nextWin && nextWin <= steady)
            {
                const double ratio = e.slipCount() / (hz * 0.1);
                if (std::fabs (ratio - 1.0) > 0.12) st.settle = (double) nextWin / sr;
                e.resetSlipCount();
                nextWin += win;
            }
            e.render (L.data() + pos, R.data() + pos, std::min (kChunk, total - pos));
        }
        const double est = finePitch (L, (size_t) steady, (size_t) (0.25 * sr), sr, hz);
        st.cents = centsBetween (est, hz);
        st.helmholtz = e.slipCount() / (hz * (double) (total - steady) / sr);
        for (float s : L) st.peak = std::max (st.peak, std::fabs (s));
        st.nan = e.nanResets;
    }
    return st;
}

inline const char* noteName (int n)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static char buf[8];
    std::snprintf (buf, sizeof buf, "%s%d", names[n % 12], n / 12 - 1);
    return buf;
}

// Renders a named take into <out>/<stage>/<name>.wav and prints its level.
inline void saveTake (Context& c, const char* stage, const std::string& name, const Take& t)
{
    if (! c.only.empty() && name.find (c.only) == std::string::npos) return;
    const float pk = peakOf (t);
    if (t.nanResets > 0) c.ok = false;
    std::printf ("  %-34s peak %5.2f (%+5.1f dBFS)  NaN resets %d\n", name.c_str(), pk, 20 * std::log10 (std::max (pk, 1e-9f)), t.nanResets);
    if (c.wav)
    {
        const fs::path dir = c.outDir / stage;
        fs::create_directories (dir);
        writeWav (dir / (name + ".wav"), t.L, t.R, t.sr);
    }
}
} // namespace nostrings::test
