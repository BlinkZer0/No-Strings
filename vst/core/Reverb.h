#pragma once
// ============================================================================
//  Reverb for the Output section: an 8-line feedback delay network with a
//  Householder mixing matrix, per-line damping, and a short predelay. Sized
//  like a small hall. Stereo in (summed to the lines with alternating signs),
//  stereo out (odd / even lines). Skipped entirely while the send is zero and
//  the tail has died away.
// ============================================================================
#include <array>
#include "dsp/Common.h"
#include "dsp/Delay.h"
#include "dsp/Filters.h"

namespace nostrings
{
class Reverb
{
public:
    static constexpr int kLines = 8;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        static const double ms[kLines] = { 29.7, 37.1, 41.1, 43.7, 53.3, 61.3, 67.9, 73.1 };
        for (int i = 0; i < kLines; ++i)
        {
            len[(size_t) i] = ms[i] * 1e-3 * fs;
            lines[(size_t) i].allocate ((int) len[(size_t) i] + 8);
            damp[(size_t) i].p = std::exp (-kTwoPi * 4500.0 / fs);
            damp[(size_t) i].b0 = 1.0 - damp[(size_t) i].p;
        }
        pre.allocate ((int) (0.012 * fs) + 8);
        preLen = 0.012 * fs;
        setDecay (2.4);
        clear();
    }

    void clear()
    {
        for (auto& l : lines) l.clear();
        for (auto& d : damp) d.reset();
        pre.clear();
        energy = 0;
    }

    void setDecay (double seconds)
    {
        decaySeconds = seconds;
        for (int i = 0; i < kLines; ++i)
            gain[(size_t) i] = std::pow (10.0, -3.0 * (len[(size_t) i] / fs) / std::max (seconds, 0.1));
    }

    bool idle() const { return energy < 1e-10; }

    // in: dry L/R; adds the wet signal (already scaled by `send`) to l and r
    void process (double& l, double& r, double send)
    {
        const double in = pre.tap (preLen);
        pre.write (0.5 * (l + r));
        std::array<double, kLines> out;
        double sum = 0;
        for (int i = 0; i < kLines; ++i)
        {
            const size_t k = (size_t) i;
            out[k] = damp[k].process (lines[k].tap (len[k])) * gain[k];
            sum += out[k];
        }
        // Householder: y_i = x_i - (2/N) * sum
        const double h = sum * (2.0 / kLines);
        double wl = 0, wr = 0, e = 0;
        for (int i = 0; i < kLines; ++i)
        {
            const size_t k = (size_t) i;
            const double fb = out[k] - h;
            lines[k].write (fb + in * (i % 2 == 0 ? 1.0 : -1.0) + kTiny);
            (i % 2 == 0 ? wl : wr) += out[k];
            e += fb * fb;
        }
        energy = e;
        l += wl * send * 0.35;
        r += wr * send * 0.35;
    }

    double decay() const { return decaySeconds; }

private:
    double fs = 48000, preLen = 100, decaySeconds = 2.4, energy = 0;
    std::array<Delay, kLines> lines;
    std::array<OnePoleLP, kLines> damp {};
    std::array<double, kLines> len {}, gain {};
    Delay pre;
};
} // namespace nostrings
