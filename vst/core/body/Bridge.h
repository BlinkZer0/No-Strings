#pragma once
// ============================================================================
//  Bridge stage: the bridge's own resonance (the "bridge hill", a broad lift
//  around 2 kHz on a cello) and its high roll-off, applied to the summed
//  string forces before they drive the body. Swappable on its own.
// ============================================================================
#include "../dsp/Biquad.h"
#include "BodyProfiles.h"

namespace nostrings
{
class Bridge
{
public:
    void configure (const BodyProfile& b, double fs)
    {
        hill = Biquad::peaking (b.hillHz, b.hillQ, b.hillDb, fs);
        roll = Biquad::lowpass (b.bridgeLpHz, 0.707, fs);
    }
    void reset() { hill.reset(); roll.reset(); }
    double process (double x) { return roll.process (hill.process (x)); }

private:
    Biquad hill, roll;
};
} // namespace nostrings
