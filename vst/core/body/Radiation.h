#pragma once
// ============================================================================
//  Radiation / air: what happens between the body and the listener.
//  Air roll-off of the highest partials, close-mic proximity (bass lift on
//  the skeleton cello), and stereo width (mid/side). Swappable on its own.
// ============================================================================
#include "../dsp/Biquad.h"
#include "BodyProfiles.h"

namespace nostrings
{
class Radiation
{
public:
    void configure (const BodyProfile& b, double fs)
    {
        airL = airR = Biquad::lowpass (b.airLpHz, 0.6, fs);
        proxL = proxR = Biquad::lowShelf (150.0, b.proximityDb, fs);
    }
    void reset() { airL.reset(); airR.reset(); proxL.reset(); proxR.reset(); }

    void process (double& l, double& r, double width)
    {
        l = proxL.process (airL.process (l));
        r = proxR.process (airR.process (r));
        const double mid = 0.5 * (l + r), side = 0.5 * (l - r) * width;
        l = mid + side;
        r = mid - side;
    }

private:
    Biquad airL, airR, proxL, proxR;
};
} // namespace nostrings
