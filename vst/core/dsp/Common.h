#pragma once
// ============================================================================
//  Shared math, RNG and denormal protection for the No Strings DSP core.
//
//  The core is plain C++17 with no framework dependency. Its JavaScript twin
//  (vst/webhost/dsp-worklet.js, built in the HTML stage) mirrors it line for
//  line, so everything here sticks to doubles and simple loops.
// ============================================================================
#include <algorithm>
#include <cmath>
#include <cstdint>

#if defined(_M_X64) || defined(_M_IX86) || defined(__SSE2__)
 #include <xmmintrin.h>
 #define NOSTRINGS_HAS_SSE 1
#endif

namespace nostrings
{
constexpr double kPi    = 3.141592653589793;
constexpr double kTwoPi = 6.283185307179586;
constexpr double kTiny  = 1e-18; // anti-denormal offset added inside feedback loops

inline double clampd (double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline double lerp (double a, double b, double t)     { return a + (b - a) * t; }
inline double dbToGain (double db)                    { return std::pow (10.0, db / 20.0); }
inline double midiToHz (double note)                  { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

// One-pole smoothing coefficient: reaches ~63% of a step in `seconds`.
inline double smoothCoef (double seconds, double sr)
{
    return 1.0 - std::exp (-1.0 / (std::max (seconds, 1e-5) * sr));
}

// Deterministic RNG (same LCG as E-Piano; identical sequence in JS via Math.imul).
struct Rng
{
    uint32_t s = 0x2545F491u;
    void seed (uint32_t v) { s = v; }
    uint32_t next() { s = s * 1664525u + 1013904223u; return s; }
    double uni() { return (double) (next() >> 8) * (1.0 / 16777216.0); } // [0, 1)
    double bi()  { return uni() * 2.0 - 1.0; }                            // [-1, 1)
};

// Sets flush-to-zero / denormals-are-zero for the scope (x86). The core also
// adds kTiny inside its feedback loops, which is what protects the JS build.
struct ScopedNoDenormals
{
#if NOSTRINGS_HAS_SSE
    unsigned int saved;
    ScopedNoDenormals() : saved (_mm_getcsr()) { _mm_setcsr (saved | 0x8040u); }
    ~ScopedNoDenormals() { _mm_setcsr (saved); }
#endif
};
} // namespace nostrings
