#pragma once
// ============================================================================
//  Bow-string friction junction (stick-slip) with torsional damping.
//
//  Units: velocities in m/s; forces normalised so the string's transverse
//  impedance seen from the bow (2Z, both halves in parallel) is 1. The
//  velocity of the string surface under the bow is
//      v = vh + f + rho * T(f)
//  vh: velocity the incoming transverse waves give on their own
//  f : friction force = velocity injected into both transverse halves
//  T : torsional (rolling) response. Torsional waves are fast and heavily
//      damped, and they reflect from the ends, so they cannot absorb a steady
//      push: T is a one-pole high-pass above roughly the torsional
//      fundamental. rho = transverse / torsional impedance ratio.
//  Torsion is what lets a bowed string settle into Helmholtz motion within a
//  few periods instead of wandering between multi-slip states.
//
//  Friction coefficient (hyperbolic curve):
//      mu(u) = muD + (muS - muD) * v0 / (v0 + |u|),   u = slip velocity
//  Because T(f) = c + a f is linear in this sample's f, the junction reduces
//  to the plain problem with vh' = vh + rho c and an effective admittance
//  k = 1 + rho a:  stick while |vb - vh'| <= muS Fb k, otherwise solve
//      u^2 + u (v0 + Fb k muD - |w|) + v0 (Fb k muS - |w|) = 0,  w = vb - vh'
//  In the hysteresis zone a slipping string keeps slipping on the larger
//  root (Friedlander's rule); a sticking string stays stuck.
// ============================================================================
#include "../dsp/Common.h"

namespace nostrings
{
struct BowFriction
{
    double muS = 0.8, muD = 0.3, v0 = 0.1;
    double rho = 1.0;          // torsional coupling (0 = no torsion)
    double torsA = 0.9;        // torsional high-pass pole
    double torsHp = 0, torsF1 = 0;
    bool slipping = false;
    int stuckFor = 0;          // samples since the last slip
    int slipCount = 0;         // debug: slips that follow a real stick phase (>= 4 samples), so
                               // ~1 per period = clean Helmholtz motion; rosin chatter isn't counted

    void reset() { slipping = false; stuckFor = 0; slipCount = 0; torsHp = torsF1 = 0; }

    // Torsional fundamental sits several times above the transverse one.
    void setup (double stringHz, double sampleRate, double torsionalRatio = 6.0)
    {
        torsA = std::exp (-kTwoPi * std::min (stringHz * torsionalRatio, sampleRate * 0.4) / sampleRate);
    }

    // vh: incoming velocity at the contact, vb: bow velocity, fb: normalised bow force.
    // Returns the velocity injected into both transverse string halves.
    double solve (double vh, double vb, double fb)
    {
        const double c = torsA * (torsHp - torsF1);   // T(f) = c + torsA * f
        const double k = 1.0 + rho * torsA;
        double f = 0.0;

        if (fb <= 1e-9)
        {
            slipping = true;
        }
        else
        {
            const double w = vb - (vh + rho * c);
            const double aw = std::fabs (w);
            const double sgn = w >= 0 ? 1.0 : -1.0;
            const double fk = fb * k;
            bool slip = aw > muS * fk;
            double u = 0.0;

            if (slip || slipping)
            {
                const double b = v0 + fk * muD - aw;
                const double cc = v0 * (fk * muS - aw);
                const double disc = b * b - 4.0 * cc;
                if (disc >= 0.0)
                {
                    u = 0.5 * (-b + std::sqrt (disc));
                    slip = u > 0.0;
                }
                else
                {
                    slip = false; // only reachable in the hysteresis zone
                }
            }

            if (slip && ! slipping && stuckFor >= 4) ++slipCount;
            stuckFor = slip ? 0 : stuckFor + 1;
            slipping = slip;
            f = (slip ? sgn * (aw - u) : w) / k; // stick: surface velocity = vb exactly
        }

        torsHp = c + torsA * f;
        if (std::fabs (torsHp) < 1e-15) torsHp = 0;
        torsF1 = f;
        return f;
    }
};
} // namespace nostrings
