#pragma once
// ============================================================================
//  Excitations: how the string is played. Each one acts on the string at the
//  contact point through the same interface: given vh (the velocity the
//  incoming waves give there), return the velocity to inject.
//
//  Bowed family (Arco, Col legno tratto, Spiccato, Harmonics) use BowFriction
//  (Bow.h) with different envelopes and friction constants; the engine drives
//  those. The rest live here.
// ============================================================================
#include "../dsp/Common.h"
#include "../WaveguideString.h"
#include "../dsp/Biquad.h"

namespace nostrings
{
enum Excitation
{
    excArco, excPizzicato, excBartok, excColLegnoBattuto, excColLegnoTratto,
    excSpiccato, excHarmonic, excMallet, excEBow, kNumExcitations
};

inline const char* excitationName (int e)
{
    static const char* names[] = { "Arco", "Pizzicato", "Bartok", "Col legno battuto", "Col legno tratto",
                                   "Spiccato", "Harmonics", "Mallet", "EBow" };
    return e >= 0 && e < kNumExcitations ? names[e] : "?";
}

inline bool isBowed (int e) { return e == excArco || e == excColLegnoTratto || e == excSpiccato || e == excHarmonic; }

// Contact point (fraction of the vibrating length from the bridge) for the
// Position control 0..1. Each technique has its own sensible span; 0.5 is
// where a player would normally do it.
inline double contactBeta (int exc, double position)
{
    const double p = clampd (position, 0.0, 1.0);
    auto span = [p] (double tasto, double ponticello) { return std::exp (lerp (std::log (tasto), std::log (ponticello), p)); };
    switch (exc)
    {
        case excPizzicato: case excBartok: return span (0.45, 0.10);  // plucked over the end of the fingerboard
        case excMallet:                    return span (0.30, 0.04);
        case excEBow:                      return span (0.35, 0.08);
        default:                           return span (0.235, 0.03); // bow (tasto end kept off beta = 1/4, which favours double slips)
    }
}

// ---------------------------------------------------------------------------
// Pluck (pizzicato, Bartok). The finger holds the string in a static triangle
// (set directly into the waveguide, so there is no latency), then lets go over
// releaseTime: a short release = narrow fingertip = brighter. Bartok adds a
// one-sided collision with the fingerboard `gap` below the rest position.
// ---------------------------------------------------------------------------
struct PluckExciter
{
    double hold = 0, ramp = 1, rampInc = 0;
    bool collide = false;
    double gap = 0, xs = 0, leak = 1, fs = 48000;

    void start (WaveguideString& s, double displacement, double releaseTime, double sampleRate, bool bartok)
    {
        fs = sampleRate;
        const double stillHeld = ramp < 1.0 ? hold * (0.5 + 0.5 * std::cos (kPi * ramp)) : 0.0;
        hold = s.addStaticDisplacement (displacement) + stillHeld;
        ramp = 0.0;
        rampInc = 1.0 / std::max (1.0, releaseTime * fs);
        xs += displacement;
        leak = 1.0 - kTwoPi * 4.0 / fs;
        collide = bartok;
        gap = 0.45 * displacement;
    }

    bool busy() const { return ramp < 1.0 || collide; }
    void stopColliding() { collide = false; }

    double process (double vh)
    {
        double f = 0.0;
        if (ramp < 1.0)
        {
            ramp = std::min (1.0, ramp + rampInc);
            f = hold * (0.5 + 0.5 * std::cos (kPi * ramp));
        }
        if (collide)
        {
            // fingerboard: the string can't go below -gap
            const double next = xs + (vh + f) / fs;
            if (next < -gap) f = (-gap - xs) * fs - vh;
            xs = (xs + (vh + f) / fs) * leak;
        }
        return f;
    }
};

// ---------------------------------------------------------------------------
// Hammer (mallet, col legno battuto). A mass on a nonlinear spring:
//     F = kRef * (c / 1e-4 m)^p   while compressed (c > 0)
// F is in the string's normalised units (velocity injected), the mass in
// seconds (mass / 2Z). Felt: soft, p ~ 2.5. Wood: hard, small, p ~ 1.5.
// An optional bounce strikes again after bounceDelay at bounceRatio x speed.
// ---------------------------------------------------------------------------
struct HammerExciter
{
    bool active = false;
    double xh = 0, vham = 0, xs = 0, mass = 0.004, kRef = 1.75, power = 2.5;
    double leak = 1, fs = 48000;
    int samples = 0, bounceIn = -1;
    double bounceVel = 0;

    void start (double velocity, double massSec, double stiffness, double p, double sampleRate,
                double bounceDelay = 0.0, double bounceRatio = 0.0)
    {
        fs = sampleRate;
        mass = massSec; kRef = stiffness; power = p;
        leak = 1.0 - kTwoPi * 4.0 / fs;
        launch (velocity);
        bounceIn = bounceDelay > 0 ? (int) (bounceDelay * fs) : -1;
        bounceVel = velocity * bounceRatio;
    }

    bool busy() const { return active || bounceIn >= 0; }

    // Implicit contact: this sample's force sets where both the hammer and the string
    // end up, and the compression there sets the force. Compression falls linearly
    // as F rises, so F = kRef (c(F) * 1e4)^p has exactly one root: found by
    // bisection. (Explicit integration overshoots on hard, light hammers and the
    // contact chatters, which comes out as clicks.)
    double process (double vh)
    {
        if (! active && bounceIn >= 0 && --bounceIn < 0) launch (bounceVel);
        if (! active)
        {
            xs = (xs + vh / fs) * leak;
            return 0.0;
        }
        const double a = 1.0 / fs + 1.0 / (mass * fs * fs);         // dc / dF over one sample
        const double cFree = (xh + vham / fs) - (xs + vh / fs);      // compression if F = 0
        double F = 0.0;
        if (cFree > 0.0)
        {
            double lo = 0.0, hi = cFree / a;
            for (int it = 0; it < 28; ++it)
            {
                const double mid = 0.5 * (lo + hi);
                if (mid > kRef * std::pow ((cFree - a * mid) * 1e4, power)) hi = mid; else lo = mid;
            }
            F = 0.5 * (lo + hi);
        }
        vham -= F / (mass * fs);
        xh += vham / fs;
        xs = (xs + (vh + F) / fs) * leak;
        // done once it has left the string and is moving away (or has been around 20 ms)
        if (++samples > 8 && F == 0.0 && (vham <= 0.0 || samples > (int) (0.02 * fs))) active = false;
        return F;
    }

private:
    void launch (double velocity) { active = velocity > 0; xh = xs; vham = velocity; samples = 0; }
};

// ---------------------------------------------------------------------------
// EBow: an electromagnetic driver. The pickup coil senses the string velocity
// at the contact point and the driver coil pushes back in phase (negative
// damping), filtered and saturated so the string swells to a steady level and
// sustains forever. The feedback is band-passed around the partial it should
// lock to: the fundamental, or (harmonic mode) the octave above. A band-pass
// has zero phase at its centre, so the driver doesn't pull the pitch.
// ---------------------------------------------------------------------------
//
// Level control (the real EBow's job is done by its circuit): the drive gain
// follows how far the string is from a target level that swells up over the
// attack time. Below target it pushes (up to maxGain), above it backs off and
// even brakes, so the swell is the same speed on every string and the sustain
// sits at a steady level.
struct EBowExciter
{
    bool active = false, harmonic = false;
    double target = 0, targetEnd = 0, targetStep = 0;
    Biquad focus;
    double env = 0, envCoef = 0.99, drive = 0, driveCoef = 0.01;
    static constexpr double maxGain = 0.3;
    Rng rng;

    void setup (double hz, double fs, bool harmonicMode)
    {
        harmonic = harmonicMode;
        focus = Biquad::bandpass (std::min (hz * (harmonicMode ? 2.0 : 1.0), fs * 0.4), harmonicMode ? 3.0 : 1.5, fs);
        envCoef = std::exp (-1.0 / (2.0 * fs / hz));      // peak follower over ~2 periods
        driveCoef = smoothCoef (std::max (0.01, 3.0 / hz), fs); // slower than the loop delay, or it hunts
    }

    void start (double swellTime, double level, double fs)
    {
        if (! active) target = 0;
        active = true;
        targetEnd = level;
        targetStep = level / std::max (1.0, swellTime * fs);
    }

    void stop() { active = false; }
    bool busy() const { return active || drive > 1e-6; }

    double process (double vh)
    {
        const double x = focus.process (vh);
        env = std::max (std::fabs (x), env * envCoef);

        double want = 0.0;
        if (active)
        {
            target = std::min (targetEnd, target + targetStep);
            want = target > 0 ? maxGain * clampd ((target - env) / target, -1.0, 1.0) : 0.0;
        }
        drive += (want - drive) * driveCoef;
        if (! active && std::fabs (drive) < 1e-6) { drive = 0; return 0.0; }
        // the driver can't push harder than the magnet allows; a tiny hum seeds a silent string
        return drive * targetEnd * std::tanh (x / std::max (targetEnd, 1e-3)) + (active ? 1e-4 * rng.bi() : 0.0);
    }
};
} // namespace nostrings
