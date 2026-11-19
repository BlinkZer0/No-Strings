#pragma once
// ============================================================================
//  Amp / DI stage for the electric bodies:
//    drive (tanh with first-order anti-derivative anti-aliasing)
//    -> tone stack (bass shelf 120 Hz, mid peak 700 Hz, treble shelf 3 kHz, +-12 dB)
//    -> cab on/off (high-pass, low thump, upper-mid dip, speaker roll-off)
//  Drive 0 is a clean DI. Output is level-compensated so drive mostly changes
//  the character, not the volume.
// ============================================================================
#include "../dsp/Biquad.h"

namespace nostrings
{
struct AmpSettings { double drive = 0.25, bass = 0.5, mid = 0.5, treble = 0.5; bool cab = true; };

class AmpStage
{
public:
    void configure (const AmpSettings& s, double fs)
    {
        auto toDb = [] (double v) { return (clampd (v, 0, 1) - 0.5) * 24.0; };
        bass = Biquad::lowShelf (120.0, toDb (s.bass), fs);
        mid = Biquad::peaking (700.0, 0.8, toDb (s.mid), fs);
        treble = Biquad::highShelf (3000.0, toDb (s.treble), fs);
        cab[0] = Biquad::highpass (75.0, 0.7, fs);
        cab[1] = Biquad::peaking (110.0, 1.0, 3.0, fs);
        cab[2] = Biquad::peaking (2000.0, 1.0, -4.0, fs);
        cab[3] = Biquad::lowpass (4800.0, 0.9, fs);
        cab[4] = Biquad::lowpass (5500.0, 0.6, fs);
        cabOn = s.cab;
        const double d = clampd (s.drive, 0, 1);
        inGain = dbToGain (36.0 * d);
        outGain = 1.0 / (inGain > 1.0 ? std::pow (inGain, 0.55) : 1.0);   // a saturated signal is louder than its peak suggests
        clean = d < 1e-3;
    }

    void reset() { bass.reset(); mid.reset(); treble.reset(); for (auto& c : cab) c.reset(); x1 = 0; }

    double process (double x)
    {
        double y = x;
        if (! clean)
        {
            const double v = x * inGain;
            const double dx = v - x1;
            // ADAA: (F(v) - F(v1)) / (v - v1), F = log cosh
            y = std::fabs (dx) > 1e-6 ? (logCosh (v) - logCosh (x1)) / dx : std::tanh (0.5 * (v + x1));
            x1 = v;
            y *= outGain;
        }
        y = treble.process (mid.process (bass.process (y)));
        if (cabOn) for (auto& c : cab) y = c.process (y);
        return y;
    }

private:
    static double logCosh (double x)
    {
        const double a = std::fabs (x);
        return a > 20.0 ? a - 0.6931471805599453 : std::log (std::cosh (x));
    }

    Biquad bass, mid, treble, cab[5];
    bool cabOn = true, clean = false;
    double inGain = 1, outGain = 1, x1 = 0;
};
} // namespace nostrings
