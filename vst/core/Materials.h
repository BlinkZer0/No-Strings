#pragma once
// ============================================================================
//  String material, tension and age -> how the string behaves.
//
//  Material (per string set) sets:
//    decay      T60 of an open string's fundamental (internal friction: gut
//               loses most, steel least)
//    hfRatio    T60 of partials around 2 kHz as a share of decay (brightness)
//    stiffness  dispersion -> inharmonicity (solid steel stiffest; windings add
//               mass without much stiffness, so wound strings sit lower)
//    muS, muD, v0, rho   how the bow grabs: gut is grippy and soft-speaking,
//               steel slippery and quick; rho = torsional coupling
//    glide      tension modulation: how far the pitch rises with amplitude
//               (gut stretches most)
//
//  Tension (0 slack .. 0.5 normal .. 1 tight), at the same pitch:
//    inharmonicity ~ 1/T, tighter = more sustain and top, crisper bow
//    attack; slacker = wider pluck excursion and much more pitch glide.
//  Age (0 fresh .. 1 old; 0.25 = nominal): old strings are dull, shorter,
//    a little more out of true; fresh ones zing.
//
//  Synthetic at normal tension and age 0.25 reproduces stages 1-3 exactly.
// ============================================================================
#include "dsp/Common.h"

namespace nostrings
{
enum StringMaterial { matPlainGut, matWoundGut, matSynthetic, matSteel, matSilverWound, matTungstenWound, kNumMaterials };

struct MaterialProfile
{
    const char* name;
    double decay, hfRatio, stiffness;
    double muS, muD, v0, rho;
    double glide;
};

inline const MaterialProfile& materialProfile (int m)
{
    static const MaterialProfile p[kNumMaterials] = {
        //  name              decay hfRatio stiff   muS   muD   v0    rho   glide
        { "Plain Gut",        3.5,  0.07,   0.06,   0.90, 0.40, 0.12, 0.80, 1.5 },
        { "Wound Gut",        4.5,  0.10,   0.10,   0.85, 0.35, 0.11, 0.90, 1.2 },
        { "Synthetic",        6.0,  0.158,  0.15,   0.80, 0.30, 0.10, 1.00, 1.0 },
        { "Steel",            9.0,  0.26,   0.32,   0.70, 0.25, 0.07, 1.20, 0.5 },
        { "Silver-Wound",     7.0,  0.20,   0.18,   0.78, 0.30, 0.09, 1.10, 0.8 },
        { "Tungsten-Wound",   7.5,  0.17,   0.22,   0.75, 0.28, 0.08, 1.15, 0.7 },
    };
    return p[m >= 0 && m < kNumMaterials ? m : matSynthetic];
}

// Everything the engine needs from material x tension x age.
struct StringResponse
{
    double decay, hfRatio, stiffness;
    double muS, muD, v0, rho;
    double glide;        // pitch rise per unit of string energy (see Engine)
    double drawScale;    // bow acceleration time multiplier
    double pluckScale;   // pluck excursion multiplier
    double forceScale;   // keeps the Pressure knob centred on this material's window
};

inline StringResponse stringResponse (int material, double tension, double age)
{
    const MaterialProfile& m = materialProfile (material);
    const double t = clampd (tension, 0, 1), a = clampd (age, 0, 1);
    StringResponse r;
    r.decay     = m.decay * lerp (0.8, 1.2, t) / lerp (0.8, 1.2, 0.5) * std::pow (2.0, (0.25 - a) * 0.8);
    r.hfRatio   = m.hfRatio * lerp (0.8, 1.25, t) / lerp (0.8, 1.25, 0.5) * std::pow (2.0, (0.25 - a) * 3.5);
    r.stiffness = clampd (m.stiffness * (0.5 / std::max (0.25, lerp (0.25, 0.75, t))) + 0.12 * (a - 0.25), 0.0, 1.0);
    r.muS = m.muS; r.muD = m.muD; r.v0 = m.v0; r.rho = m.rho;
    r.glide     = m.glide * std::pow (4.0, 1.0 - 2.0 * t);   // x4 slack .. x0.25 tight
    r.drawScale = std::pow (2.0, 1.0 - 2.0 * t) * 0.5 + 0.5; // 1.5 slack .. 0.75 tight (1 at normal)
    r.pluckScale = lerp (1.3, 0.8, t) / lerp (1.3, 0.8, 0.5);
    r.forceScale = 0.5 / std::max (0.1, m.muS - m.muD);    // the window scales with 1 / (muS - muD)
    return r;
}
} // namespace nostrings
