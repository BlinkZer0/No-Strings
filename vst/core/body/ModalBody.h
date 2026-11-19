#pragma once
// ============================================================================
//  Modal body: a bank of two-pole resonators driven by the bridge force.
//  Signature low modes come straight from the profile; the higher modes are
//  a seeded "cloud" (same every time for a given body) under the profile's
//  spectral envelope, getting denser with frequency like a real plate.
//
//  Each mode radiates to left and right with its own weights (low modes
//  equally, higher ones with a random tilt): that is the stereo image.
//  A "direct" share of the bridge signal bypasses the modes (a piezo pickup
//  sees the bridge directly).
//
//  Resonator: H(z) = g (1 - r^2)/2 (1 - z^-2) / (1 - 2 r cos(w) z^-1 + r^2 z^-2)
//  (peak gain ~ g, zeros at DC and Nyquist so it never passes DC).
// ============================================================================
#include <array>
#include "../dsp/Common.h"
#include "BodyProfiles.h"

namespace nostrings
{
constexpr int kMaxModes = 64;

class ModalBody
{
public:
    void configure (const BodyProfile& prof, double fs)
    {
        count = 0;
        for (const auto& m : prof.low)
            if (m.gain > 0 && m.hz > 0) add (m.hz, m.q, m.gain, 0.0, fs);

        Rng rng;
        rng.seed (0xB0D1u + (uint32_t) (prof.cloudLoHz * 7 + prof.cloudHiHz));
        const double lo = std::log (prof.cloudLoHz), hi = std::log (prof.cloudHiHz);
        for (int k = 0; k < prof.cloudCount && count < kMaxModes; ++k)
        {
            // denser towards the top: spacing shrinks with sqrt of the position
            const double u = std::sqrt ((k + 0.5 + 0.4 * rng.bi()) / prof.cloudCount);
            const double hz = std::exp (lerp (lo, hi, clampd (u, 0, 1)));
            const double q = lerp (prof.cloudQLo, prof.cloudQHi, rng.uni());
            const double gain = dbToGain (envelopeDb (prof, hz)) * (0.45 + 0.55 * rng.uni());
            const double tilt = (rng.uni() < 0.5 ? -1.0 : 1.0) * (0.3 + 0.7 * rng.uni());
            add (hz, q, gain, tilt * prof.spread, fs);
        }
        direct = prof.direct;
        // normalise roughly by mode count so dense bodies aren't louder
        modeScale = count > 0 ? 3.0 / std::sqrt ((double) count) : 0.0;
    }

    void reset() { for (int i = 0; i < count; ++i) y1[(size_t) i] = y2[(size_t) i] = 0; x1 = x2 = 0; }

    void process (double x, double& outL, double& outR)
    {
        double l = 0, r = 0;
        const double dx = x - x2;
        for (int i = 0; i < count; ++i)
        {
            const size_t k = (size_t) i;
            double y = b[k] * dx + c1[k] * y1[k] - c2[k] * y2[k];
            if (std::fabs (y) < 1e-15) y = 0;
            y2[k] = y1[k]; y1[k] = y;
            l += y * wl[k];
            r += y * wr[k];
        }
        x2 = x1; x1 = x;
        outL = l * modeScale + x * direct;
        outR = r * modeScale + x * direct;
    }

    int modeCount() const { return count; }

private:
    static double envelopeDb (const BodyProfile& b, double hz)
    {
        const auto& e = b.env;
        if (hz <= e[0].hz) return e[0].db;
        for (int i = 1; i < 7; ++i)
            if (hz <= e[i].hz)
            {
                const double t = std::log (hz / e[i - 1].hz) / std::log (e[i].hz / e[i - 1].hz);
                return lerp (e[i - 1].db, e[i].db, t);
            }
        return e[6].db;
    }

    void add (double hz, double q, double gain, double tilt, double fs)
    {
        if (count >= kMaxModes || hz >= fs * 0.45) return;
        const size_t k = (size_t) count++;
        const double w = kTwoPi * hz / fs;
        const double r = std::exp (-0.5 * w / q);   // bandwidth = f / Q
        c1[k] = 2 * r * std::cos (w);
        c2[k] = r * r;
        b[k] = gain * (1 - r * r) * 0.5;
        wl[k] = 1.0 + tilt;
        wr[k] = 1.0 - tilt;
        y1[k] = y2[k] = 0;
    }

    int count = 0;
    std::array<double, kMaxModes> b {}, c1 {}, c2 {}, wl {}, wr {}, y1 {}, y2 {};
    double x1 = 0, x2 = 0, direct = 0, modeScale = 1;
};
} // namespace nostrings
