#pragma once
// ============================================================================
//  Sympathetic strings: a bank of 4..12 strings that are never played, only
//  driven through the bridge. Each is a single waveguide loop (round trip with
//  both end reflections folded in) with the same per-note loss filter design
//  as the main strings, tuned exactly at its fundamental. A played note that
//  shares partials with a sympathetic string sets it ringing; the rest stay
//  quiet. Their output goes back into the body with the bridge signal, so the
//  halo is coloured by the same resonances.
//
//  The bank only listens to the bridge (the body never feeds it), so there is
//  no feedback loop. When it is off, process() is never called: zero CPU, and
//  the lines are cleared when it comes back on.
//
//  Tuning modes: Scale (key + scale, ascending from the key in octave 3),
//  Open strings (unison with the body's open strings, then octaves above), Custom (one
//  MIDI note per string).
// ============================================================================
#include <array>
#include "dsp/Common.h"
#include "dsp/Delay.h"
#include "dsp/Filters.h"
#include "body/BodyProfiles.h"

namespace nostrings
{
constexpr int kMaxSympathetic = 12;

enum SympMode { sympScale, sympOpenStrings, sympCustom, kNumSympModes };
enum SympScale { scaleMajor, scaleMinor, scaleDorian, scaleMixolydian, scalePentatonic, scaleHarmonicMinor, scaleDrone, kNumScales };

inline const char* sympModeName (int m)  { static const char* n[] = { "Scale", "Open Strings", "Custom" }; return n[m >= 0 && m < kNumSympModes ? m : 0]; }
inline const char* scaleName (int s)
{
    static const char* n[] = { "Major", "Minor", "Dorian", "Mixolydian", "Pentatonic", "Harmonic Minor", "Drone (root + fifth)" };
    return n[s >= 0 && s < kNumScales ? s : 0];
}
inline const char* keyName (int k)
{
    static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n[((k % 12) + 12) % 12];
}

struct SympSettings
{
    bool   on = false;
    double amount = 0.5;     // 0..1
    int    count = 8;        // 4..12
    int    mode = sympOpenStrings;
    int    key = 2;          // 0 = C .. 11 = B (default D)
    int    scale = scaleMajor;
    double decay = 6.0;      // s, T60 of each string's fundamental
    double brightness = 0.5; // 0 dark .. 1 bright
    int    custom[kMaxSympathetic] { 38, 45, 50, 57, 62, 69, 74, 81, 43, 55, 67, 79 };

    bool operator== (const SympSettings& o) const
    {
        if (on != o.on || amount != o.amount || count != o.count || mode != o.mode || key != o.key || scale != o.scale
            || decay != o.decay || brightness != o.brightness) return false;
        for (int i = 0; i < kMaxSympathetic; ++i) if (custom[i] != o.custom[i]) return false;
        return true;
    }
    bool operator!= (const SympSettings& o) const { return ! (*this == o); }
};

// The MIDI notes the strings are tuned to.
inline void sympatheticNotes (const SympSettings& s, const BodyProfile& body, int* out, int count)
{
    static const int major[] = { 0, 2, 4, 5, 7, 9, 11 }, minor[] = { 0, 2, 3, 5, 7, 8, 10 }, dorian[] = { 0, 2, 3, 5, 7, 9, 10 },
                     mixo[] = { 0, 2, 4, 5, 7, 9, 10 }, penta[] = { 0, 2, 4, 7, 9 }, harm[] = { 0, 2, 3, 5, 7, 8, 11 }, drone[] = { 0, 7 };
    const int* deg = major; int len = 7;
    switch (s.scale)
    {
        case scaleMinor: deg = minor; break;
        case scaleDorian: deg = dorian; break;
        case scaleMixolydian: deg = mixo; break;
        case scalePentatonic: deg = penta; len = 5; break;
        case scaleHarmonicMinor: deg = harm; break;
        case scaleDrone: deg = drone; len = 2; break;
        default: break;
    }
    for (int i = 0; i < count; ++i)
    {
        switch (s.mode)
        {
            case sympScale:       out[i] = 48 + ((s.key % 12) + 12) % 12 + deg[i % len] + 12 * (i / len); break;
            case sympOpenStrings: out[i] = body.strings[i % body.numStrings] + 12 * (i / body.numStrings); break;
            default:              out[i] = s.custom[i]; break;
        }
        out[i] = std::min (std::max (out[i], 24), 108);
    }
}

class SympatheticBank
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& l : lines) l.allocate ((int) std::ceil (fs / 30.0) + 8);
        hpCoef = std::exp (-kTwoPi * 40.0 / fs);
        clear();
    }

    void clear()
    {
        for (auto& l : lines) l.clear();
        for (auto& f : loss) f.reset();
        hpX1 = hpY1 = 0;
    }

    void configure (const SympSettings& s, const BodyProfile& body)
    {
        const int previous = count;
        count = std::min (std::max (s.count, 4), kMaxSympathetic);
        for (int i = previous; i < count; ++i) { lines[(size_t) i].clear(); loss[(size_t) i].reset(); } // strings coming back in start silent
        sympatheticNotes (s, body, notes.data(), count);
        const double hfRatio = lerp (0.03, 0.4, clampd (s.brightness, 0, 1));
        for (int i = 0; i < count; ++i)
        {
            const size_t k = (size_t) i;
            const double hz = midiToHz (notes[k]);
            const double w0 = kTwoPi * hz / fs, N = fs / hz;
            const double decay = clampd (s.decay, 0.2, 30.0);
            const double per0 = std::pow (10.0, -3.0 / (hz * decay));
            const double perHf = std::pow (10.0, -3.0 / (hz * decay * hfRatio));
            // same idea as WaveguideString::lossPole, solved by bisection
            const double wr = kTwoPi * std::min (2000.0, fs * 0.45) / fs;
            double p = 0.0;
            if (wr > w0 * 1.2)
            {
                const double want = perHf / per0, c0 = std::cos (w0), cr = std::cos (wr);
                auto ratio = [c0, cr] (double q) { return std::sqrt ((1 - 2 * q * c0 + q * q) / (1 - 2 * q * cr + q * q)); };
                double lo = 0, hi = 0.995;
                if (ratio (hi) >= want) p = hi;
                else { for (int it = 0; it < 30; ++it) { const double mid = 0.5 * (lo + hi); if (ratio (mid) > want) lo = mid; else hi = mid; } p = 0.5 * (lo + hi); }
            }
            const double g = std::min (per0 / OnePoleLP::magnitude (1 - p, p, w0), 0.99995);
            loss[k].b0 = g * (1 - p);
            loss[k].p = p;
            delay[k] = std::max (N - OnePoleLP::phaseDelay (p, w0), 4.0);
        }
        // drive and output: louder banks get less of each so Amount means the same at any count
        drive = 0.02;
        outGain = 2.5 * clampd (s.amount, 0, 1) / std::sqrt ((double) count);
    }

    // x: the bridge signal. Returns the bank's contribution to the body input.
    double process (double x)
    {
        // DC-free drive (a bowed string pushes the bridge sideways a little)
        const double hp = hpCoef * (hpY1 + x - hpX1);
        hpX1 = x; hpY1 = std::fabs (hp) < 1e-15 ? 0.0 : hp;
        const double in = hp * drive;
        double out = 0;
        for (int i = 0; i < count; ++i)
        {
            const size_t k = (size_t) i;
            const double y = loss[k].process (lines[k].tap (delay[k]));
            lines[k].write (y + in + kTiny);
            out += y;
        }
        return out * outGain;
    }

    int stringCount() const { return count; }
    int note (int i) const { return notes[(size_t) i]; }

private:
    double fs = 48000, hpCoef = 0.99, hpX1 = 0, hpY1 = 0, drive = 0.02, outGain = 0.5;
    int count = 8;
    std::array<Delay, kMaxSympathetic> lines;
    std::array<OnePoleLP, kMaxSympathetic> loss {};
    std::array<double, kMaxSympathetic> delay {};
    std::array<int, kMaxSympathetic> notes {};
};
} // namespace nostrings
