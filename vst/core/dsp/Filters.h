#pragma once
// ============================================================================
//  Small filters used inside the string loop and on the output, plus the
//  phase-delay formulas the string uses to stay in tune.
// ============================================================================
#include "Common.h"

namespace nostrings
{
// y = b0 x + p y1  (one-pole low-pass with DC gain b0 / (1 - p))
struct OnePoleLP
{
    double b0 = 1, p = 0, y1 = 0;
    void reset() { y1 = 0; }
    double process (double x) { y1 = b0 * x + p * y1; return y1; }

    static double magnitude (double b0, double p, double w)
    {
        return b0 / std::sqrt (1.0 - 2.0 * p * std::cos (w) + p * p);
    }
    // phase delay in samples at angular frequency w
    static double phaseDelay (double p, double w)
    {
        return std::atan2 (p * std::sin (w), 1.0 - p * std::cos (w)) / w;
    }
};

// First-order allpass  H(z) = (a + z^-1) / (1 + a z^-1).
// a < 0 delays low frequencies more than high ones: the dispersion of a stiff
// string, where higher partials travel faster and land sharp.
struct Allpass1
{
    double a = 0, x1 = 0, y1 = 0;
    void reset() { x1 = y1 = 0; }
    double process (double x) { const double y = a * x + x1 - a * y1; x1 = x; y1 = y; return y; }

    static double phaseDelay (double a, double w)
    {
        double ph = std::atan2 (-std::sin (w), a + std::cos (w)) - std::atan2 (-a * std::sin (w), 1.0 + a * std::cos (w));
        while (ph > 0) ph -= kTwoPi;          // phase runs 0 .. -pi
        while (ph < -kTwoPi) ph += kTwoPi;
        return -ph / w;
    }
};

struct DcBlocker
{
    double r = 0.999, x1 = 0, y1 = 0;
    void setup (double hz, double sr) { r = 1.0 - kTwoPi * hz / sr; }
    void reset() { x1 = y1 = 0; }
    double process (double x)
    {
        double y = x - x1 + r * y1;
        if (std::fabs (y) < 1e-15) y = 0; // explicit flush (the JS build has no FTZ)
        x1 = x; y1 = y;
        return y;
    }
};
} // namespace nostrings
