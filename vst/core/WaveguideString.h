#pragma once
// ============================================================================
//  Digital-waveguide string, split at the excitation point.
//
//         nut/finger                contact (bow)                 bridge
//   -1 x loss x dispersion  <-- neck line -->  (*)  <-- bridge line -->  -1
//
//  Each line holds the round trip of its segment (velocity waves). Frequency-
//  dependent loss (one-pole) and stiffness dispersion (allpass chain) are
//  lumped at the nut end, so the bridge segment stays a pure delay and can be
//  very short (sul ponticello). Tuning is exact at the fundamental: the
//  phase delay of the lumped filters at f0 is subtracted from the loop.
//
//  Optional touch point (natural harmonics): a finger lightly touching the
//  string at a fraction `touch` of its length from the nut. The neck segment
//  is then split in three lines:
//      contact --aOut--> touch --nutLine (round trip)--> touch --aIn--> contact
//  and the touch junction is a damper: it absorbs a share touchK of the
//  string velocity there, so only modes with a node at the touch point ring.
//
//  Per sample:  vh = incoming();  f = exciter(vh, ...);  out = commit(f);
//  out is the velocity wave arriving at the bridge, i.e. the bridge force
//  (up to the impedance factor) that later stages feed into the body.
// ============================================================================
#include <array>
#include "dsp/Common.h"
#include "dsp/Delay.h"
#include "dsp/Filters.h"
#include "dsp/Biquad.h"

namespace nostrings
{
constexpr int kDispersionStages = 4;
constexpr double kHfRefHz = 2000.0; // reference frequency for the high-partial decay time

struct StringSetup
{
    double hz = 110.0;      // fundamental of the vibrating length
    double beta = 0.1;      // contact point as a fraction of the vibrating length, from the bridge
    double decay = 6.0;     // T60 of the fundamental, seconds
    double hfDecay = 1.0;   // T60 of partials around kHfRefHz, seconds (shorter = darker)
    double inharm = 2.25e-5;// inharmonicity coefficient B: partial k sits at k f0 sqrt(1 + B k^2)
    double touch = 0.0;     // light finger touch, fraction of the length from the nut (0 = none)
    double touchDamp = 0.6; // share of the string velocity the finger absorbs (0..1)
    // bridge <-> body coupling: the body's strongest modes as an admittance at the bridge
    double couplingHz[2] { 0, 0 }, couplingQ[2] { 1, 1 }, couplingC[2] { 0, 0 };
    // Tune out the coupling's phase at f0? Right for free (plucked / struck) strings,
    // whose pitch is set by the loop phase at f0. A bowed string's period is set by the
    // Helmholtz corner, a broadband pulse that barely sees a narrow body mode, so
    // compensating would make bowed notes sharp.
    bool compensateCoupling = true;
};

class WaveguideString
{
public:
    void prepare (double sampleRate, double lowestHz = 7.5)
    {
        fs = sampleRate;
        const int maxLen = (int) std::ceil (fs / lowestHz) + 8;
        neck.allocate (maxLen);
        bridge.allocate (maxLen);
        aOut.allocate (maxLen / 2 + 8);
        aIn.allocate (maxLen / 2 + 8);
        smooth = smoothCoef (0.004, fs);
        reset();
    }

    void reset()
    {
        neck.clear(); bridge.clear(); aOut.clear(); aIn.clear();
        loss.reset();
        for (auto& ap : disp) ap.reset();
        for (auto& bp : couple) bp.reset();
        nIn = bIn = bRaw = 0;
    }

    // Recomputes filters and target delay lengths. snap = jump straight to the
    // new lengths (new note on a silent string) instead of gliding.
    void configure (const StringSetup& s, bool snap)
    {
        const double hz = clampd (s.hz, 8.0, fs * 0.2);
        const double w0 = kTwoPi * hz / fs;
        const double N = fs / hz;

        // Loss filter. Real strings lose high partials at a rate set by their frequency in
        // Hz, but this filter acts once per period, so it is solved per note: the pole p
        // gives the right ratio between the per-period losses at kHfRefHz and at f0, then
        // the DC gain sets the fundamental's decay. (A fixed corner would eat into the
        // fundamental of high notes and make them die far too quickly.)
        const double perPeriod = std::pow (10.0, -3.0 / (hz * std::max (s.decay, 0.01)));
        // the pole hardly moves over a few cents: reuse it while vibrato / slides stay within 5 cents
        if (! (std::fabs (hz - poleHz) < hz * 0.003 && s.decay == poleDecay && s.hfDecay == poleHf))
        {
            poleHz = hz; poleDecay = s.decay; poleHf = s.hfDecay;
            pole = lossPole (perPeriod, std::pow (10.0, -3.0 / (hz * clampd (s.hfDecay, 0.005, s.decay))), w0,
                             kTwoPi * std::min (kHfRefHz, fs * 0.45) / fs);
        }
        const double p = pole;
        const double unit = OnePoleLP::magnitude (1.0 - p, p, w0);   // |H| at f0 with unity DC gain
        const double g = std::min (perPeriod / unit, 0.99995);       // DC gain < 1 keeps the loop passive
        loss.b0 = g * (1.0 - p);
        loss.p = p;

        // Bridge coupling. The bridge moves with admittance Y = sum c_k H_k (0 dB band-passes
        // at the body's strongest modes), so the reflection is -(1 - 2Y). Each H_k traces the
        // circle |H - 1/2| = 1/2, so with sum c_k <= 1 the reflection never exceeds 1: the
        // bridge can only take energy (wolf note, body-coloured decay), never add it.
        double yRe = 0, yIm = 0;
        coupled = 0;
        for (int k = 0; k < 2; ++k)
        {
            couplingC[k] = clampd (s.couplingC[k], 0.0, 0.5);
            if (couplingC[k] <= 0.0 || s.couplingHz[k] <= 0.0) { couplingC[k] = 0.0; continue; }
            if (s.couplingHz[k] != designedHz[k] || s.couplingQ[k] != designedQ[k])
            {
                const Biquad d = Biquad::bandpass (s.couplingHz[k], s.couplingQ[k], fs);
                couple[k].b0 = d.b0; couple[k].b1 = d.b1; couple[k].b2 = d.b2; couple[k].a1 = d.a1; couple[k].a2 = d.a2;
                designedHz[k] = s.couplingHz[k]; designedQ[k] = s.couplingQ[k];
            }
            double re, im;
            couple[k].response (w0, re, im);
            yRe += couplingC[k] * re; yIm += couplingC[k] * im;
            ++coupled;
        }
        const double bridgeDelay = s.compensateCoupling ? -std::atan2 (-2.0 * yIm, 1.0 - 2.0 * yRe) / w0 : 0.0;

        // dispersion: the allpass chain is fitted so a reference partial lands where B puts
        // it; shrink it on very short loops so the delay lines keep their minimum length
        double a = dispersionCoef (hz, N, w0, p, clampd (s.inharm, 0.0, 0.01));
        double filt = 0;
        for (int tries = 0; tries < 8; ++tries)
        {
            filt = bridgeDelay + OnePoleLP::phaseDelay (p, w0) + kDispersionStages * Allpass1::phaseDelay (a, w0);
            if (N - filt >= 6.0 || a == 0.0) break;
            a *= 0.5;
            if (a > -0.01) a = 0.0;
        }
        for (auto& ap : disp) ap.a = a;

        const double loop = std::max (N - filt, 4.5);
        dBT = clampd (s.beta * loop, 2.0, loop - 2.5);
        dNT = loop - dBT;

        // touch point: nut segment round trip = touch * full loop, the rest is split evenly between aOut / aIn
        const bool wantTouch = s.touch > 0.0 && dNT >= 8.0;
        if (wantTouch != touchOn) { reset(); snap = true; touchOn = wantTouch; }
        if (touchOn)
        {
            dTT = clampd (s.touch * N - filt, 2.0, dNT - 4.0);
            dAT = 0.5 * (dNT - dTT);
            touchK = clampd (s.touchDamp, 0.0, 1.0);
        }
        if (snap) { dB = dBT; dN = dNT; dT = dTT; dA = dAT; }
    }

    double incoming()
    {
        if (! touchOn)
        {
            nIn = -nutReturn (neck.tap (dN));
        }
        else
        {
            nIn = aIn.tap (dA);                                // arriving at the contact from the touch point
            const double fromContact = aOut.tap (dA);          // arriving at the touch point from the contact
            const double fromNut = -nutReturn (neck.tap (dT));
            const double ft = -(fromContact + fromNut) * touchK; // finger absorbs part of the motion
            aIn.write (fromNut + ft + kTiny);
            neck.write (fromContact + ft + kTiny);
        }
        bRaw = bridge.tap (dB);
        bIn = -bRaw;              // bridge reflection
        if (coupled > 0)
            for (int k = 0; k < 2; ++k)
                if (couplingC[k] > 0.0) bIn += 2.0 * couplingC[k] * couple[(size_t) k].process (bRaw);
        return nIn + bIn;
    }

    double commit (double injected)
    {
        if (! touchOn) neck.write (bIn + injected + kTiny);
        else           aOut.write (bIn + injected + kTiny);
        bridge.write (nIn + injected + kTiny);
        dB += (dBT - dB) * smooth;
        dN += (dNT - dN) * smooth;
        dA += (dAT - dA) * smooth;
        dT += (dTT - dT) * smooth;
        return bRaw;
    }

    // Puts the string into a static triangle with its apex (`height`, metres)
    // at the contact point, on top of whatever it is doing. A static shape is a
    // constant velocity wave on each side: V = disp * fs / (round-trip samples).
    // Holding it there needs a constant injection, which is returned: ramp that
    // down to zero to "let go" (the ramp time is the finger's width).
    double addStaticDisplacement (double height)
    {
        const double vb = height * fs / dB;
        const double vn = height * fs / dN;
        addToLine (bridge, (int) dB + 4, vb);
        if (! touchOn)
        {
            addToLine (neck, (int) dN + 4, vn);
        }
        else
        {
            addToLine (aOut, (int) dA + 4, vn);   // travelling towards the nut
            addToLine (neck, (int) dT + 4, vn);
            addToLine (aIn,  (int) dA + 4, -vn);  // travelling back towards the contact (not inverted on read)
        }
        // settle the nut-end filters on the new constant so they don't ring
        loss.y1 += vn * loss.b0 / (1.0 - loss.p);
        const double dcGain = loss.b0 / (1.0 - loss.p);
        for (auto& ap : disp) { ap.x1 += vn * dcGain; ap.y1 += vn * dcGain; }
        return touchOn ? vb + vn : vb + vn * dcGain;
    }

    double bridgeLoop() const { return dB; }

private:
    // Allpass coefficient (all kDispersionStages share it) so that partial kr, around
    // 3 kHz, sits at kr f0 sqrt(1 + B kr^2) relative to the fundamental. The loop delay
    // must fall from N at f0 to N / sqrt(1 + B kr^2) at partial kr; the loss filter
    // already supplies a little of that fall, the allpasses the rest. Cached: only
    // recomputed when the pitch, B or the loss pole change.
    double dispersionCoef (double hz, double N, double w0, double lossP, double B)
    {
        if (std::fabs (hz - cacheHz) < hz * 0.003 && std::fabs (B - cacheB) <= B * 0.01 && std::fabs (lossP - cacheP) < 1e-3) return cacheA;
        cacheHz = hz; cacheB = B; cacheP = lossP;
        const double kr = clampd (std::floor (std::min (3000.0, fs * 0.2) / hz), 2.0, 60.0);
        const double wr = w0 * kr;
        double need = N * (1.0 / std::sqrt (1.0 + B) - 1.0 / std::sqrt (1.0 + B * kr * kr))
                    - (OnePoleLP::phaseDelay (lossP, w0) - OnePoleLP::phaseDelay (lossP, wr));
        if (B <= 0.0 || need <= 0.0 || kr * hz >= fs * 0.45) return cacheA = 0.0;
        auto spread = [w0, wr] (double a) { return kDispersionStages * (Allpass1::phaseDelay (a, w0) - Allpass1::phaseDelay (a, wr)); };
        double lo = -0.98, hi = 0.0;          // spread grows as a -> -1
        if (spread (lo) <= need) return cacheA = lo;
        for (int it = 0; it < 40; ++it)
        {
            const double mid = 0.5 * (lo + hi);
            if (spread (mid) > need) lo = mid; else hi = mid;
        }
        return cacheA = 0.5 * (lo + hi);
    }

    // One-pole pole p such that |H(wr)| / |H(w0)| = perHf / per0 (both per-period gains).
    // That ratio falls from 1 at p = 0 towards sin(w0/2)/sin(wr/2) as p -> 1.
    static double lossPole (double per0, double perHf, double w0, double wr)
    {
        if (wr <= w0 * 1.2) return 0.0;
        const double want = clampd (perHf / per0, 1e-9, 1.0);
        const double c0 = std::cos (w0), cr = std::cos (wr);
        auto ratio = [c0, cr] (double p) { return std::sqrt ((1.0 - 2.0 * p * c0 + p * p) / (1.0 - 2.0 * p * cr + p * p)); };
        double lo = 0.0, hi = 0.995;
        if (ratio (hi) >= want) return hi;
        for (int it = 0; it < 30; ++it)
        {
            const double mid = 0.5 * (lo + hi);
            if (ratio (mid) > want) lo = mid; else hi = mid;
        }
        return 0.5 * (lo + hi);
    }

    double nutReturn (double x)
    {
        x = loss.process (x);
        for (auto& ap : disp) x = ap.process (x);
        return x;
    }

    static void addToLine (Delay& d, int count, double v)
    {
        for (int k = 1; k <= count; ++k) d.buf[(size_t) ((d.w - k) & d.mask)] += v;
    }

    double fs = 48000.0, smooth = 0.01;
    Delay neck, bridge, aOut, aIn;
    OnePoleLP loss;
    std::array<Allpass1, kDispersionStages> disp {};
    double dN = 100, dB = 10, dNT = 100, dBT = 10;
    bool touchOn = false;
    double dA = 40, dAT = 40, dT = 20, dTT = 20, touchK = 0.6;
    double nIn = 0, bIn = 0, bRaw = 0;
    double cacheHz = -1, cacheB = -1, cacheP = -1, cacheA = 0;
    double poleHz = -1, poleDecay = -1, poleHf = -1, pole = 0;
    double designedHz[2] { -1, -1 }, designedQ[2] { -1, -1 };
    std::array<Biquad, 2> couple {};
    double couplingC[2] { 0, 0 };
    int coupled = 0;
};
} // namespace nostrings
