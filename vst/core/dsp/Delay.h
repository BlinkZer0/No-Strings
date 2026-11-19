#pragma once
// ============================================================================
//  Circular delay line with 4-point (3rd-order) Lagrange fractional reads.
//  Lagrange copes well with delay lengths that move every sample (vibrato,
//  portamento, bow position), which an allpass interpolator does not.
//
//  Convention: tap(d) is called BEFORE write() in a sample, and returns the
//  value written exactly d samples ago (d >= 2 for the 4-point read).
// ============================================================================
#include <vector>
#include "Common.h"

namespace nostrings
{
struct Delay
{
    std::vector<double> buf;
    int mask = 0;
    int w = 0;

    void allocate (int maxDelay)
    {
        int n = 16;
        while (n < maxDelay + 8) n <<= 1;
        buf.assign ((size_t) n, 0.0);
        mask = n - 1;
        w = 0;
    }

    void clear() { std::fill (buf.begin(), buf.end(), 0.0); w = 0; }
    int capacity() const { return mask - 4; }

    void write (double x) { buf[(size_t) w] = x; w = (w + 1) & mask; }

    double tapInt (int d) const { return buf[(size_t) ((w - d) & mask)]; }

    double tap (double d) const
    {
        const int i = (int) d;
        const double t = d - i;
        const double xm1 = tapInt (i - 1), x0 = tapInt (i), x1 = tapInt (i + 1), x2 = tapInt (i + 2);
        // Lagrange weights for points at -1, 0, 1, 2 evaluated at t
        const double tp1 = t + 1.0, tm1 = t - 1.0, tm2 = t - 2.0;
        const double hm1 = -t * tm1 * tm2 * (1.0 / 6.0);
        const double h0  =  tp1 * tm1 * tm2 * 0.5;
        const double h1  = -tp1 * t * tm2 * 0.5;
        const double h2  =  tp1 * t * tm1 * (1.0 / 6.0);
        return hm1 * xm1 + h0 * x0 + h1 * x1 + h2 * x2;
    }
};
} // namespace nostrings
