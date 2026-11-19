#pragma once
// ============================================================================
//  Biquad (transposed direct form II) with RBJ-cookbook designs, plus the
//  two-pole modal resonator used by the body.
// ============================================================================
#include "Common.h"

namespace nostrings
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }
    double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        if (std::fabs (z1) < 1e-15) z1 = 0; // explicit flush (the JS build has no FTZ)
        if (std::fabs (z2) < 1e-15) z2 = 0;
        return y;
    }

    // |H| and phase at angular frequency w
    void response (double w, double& re, double& im) const
    {
        const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        const double dr = 1 + a1 * c1 + a2 * c2,  di = -(a1 * s1 + a2 * s2);
        const double den = dr * dr + di * di;
        re = (nr * dr + ni * di) / den;
        im = (ni * dr - nr * di) / den;
    }

    static Biquad identity() { return {}; }

    static Biquad make (double b0, double b1, double b2, double a0, double a1, double a2)
    {
        Biquad q;
        q.b0 = b0 / a0; q.b1 = b1 / a0; q.b2 = b2 / a0; q.a1 = a1 / a0; q.a2 = a2 / a0;
        return q;
    }

    static double wOf (double hz, double fs) { return kTwoPi * clampd (hz, 1.0, fs * 0.49) / fs; }

    static Biquad lowpass (double hz, double q, double fs)
    {
        const double w = wOf (hz, fs), c = std::cos (w), al = std::sin (w) / (2 * q);
        return make ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    static Biquad highpass (double hz, double q, double fs)
    {
        const double w = wOf (hz, fs), c = std::cos (w), al = std::sin (w) / (2 * q);
        return make ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    // band-pass with 0 dB peak: its response traces the circle |H - 1/2| = 1/2
    static Biquad bandpass (double hz, double q, double fs)
    {
        const double w = wOf (hz, fs), c = std::cos (w), al = std::sin (w) / (2 * q);
        return make (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    static Biquad peaking (double hz, double q, double db, double fs)
    {
        const double A = std::pow (10.0, db / 40.0), w = wOf (hz, fs), c = std::cos (w), al = std::sin (w) / (2 * q);
        return make (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    static Biquad lowShelf (double hz, double db, double fs)
    {
        const double A = std::pow (10.0, db / 40.0), w = wOf (hz, fs), c = std::cos (w), s = std::sin (w);
        const double al = s / 2 * std::sqrt (2.0), sq = 2 * std::sqrt (A) * al;
        return make (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
                     (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }
    static Biquad highShelf (double hz, double db, double fs)
    {
        const double A = std::pow (10.0, db / 40.0), w = wOf (hz, fs), c = std::cos (w), s = std::sin (w);
        const double al = s / 2 * std::sqrt (2.0), sq = 2 * std::sqrt (A) * al;
        return make (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                     (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
};
} // namespace nostrings
