#pragma once
// ============================================================================
//  No Strings DSP engine.
//
//  Each voice is one waveguide string (WaveguideString.h) played by one of
//  nine excitations (excite/): arco, pizzicato, Bartok, col legno battuto and
//  tratto, spiccato, natural harmonics, mallet and EBow. The excitation is
//  captured at note-on, so switching it (or a keyswitch, later) affects the
//  next notes, not ringing ones.
//
//  After the strings (all voices summed at the bridge):
//    Bridge (bridge hill) -> [+ sympathetic strings, driven by the bridge]
//    -> ModalBody (8 profiles, stereo modes) -> Radiation (air, proximity,
//    width) -> AmpStage (electric bodies only) -> master
//  The body also reaches back into each string: its strongest modes load the
//  string's bridge end (wolf note, body-coloured decay), open strings of the
//  selected body ring longer than stopped notes, and the arpeggione's frets
//  keep stopped notes bright.
//
//  Bow strokes start like a player's: the bow is pressed first, then drawn
//  with a smooth (raised-cosine) acceleration. That "Guettler-safe" onset
//  lets the string lock into Helmholtz motion within a few periods.
//
//  Control rate: every kChunk samples the voice targets are recomputed; the
//  string glides its delay lengths per sample, envelopes run per sample.
//  No allocation after prepare(), no locks, no framework dependency.
// ============================================================================
#include <array>
#include <vector>
#include "dsp/Common.h"
#include "dsp/Filters.h"
#include "WaveguideString.h"
#include "excite/Bow.h"
#include "excite/Exciters.h"
#include "Materials.h"
#include "Sympathetic.h"
#include "Reverb.h"
#include "body/BodyProfiles.h"
#include "body/Bridge.h"
#include "body/ModalBody.h"
#include "body/Radiation.h"
#include "body/AmpStage.h"

namespace nostrings
{
enum VoiceMode   { modePoly, modeMono, modeLegato, kNumVoiceModes };
enum VibSource   { vibAuto, vibModWheel, vibAftertouch, vibAutoAftertouch, kNumVibSources };
enum SwellSource { swellOff, swellModWheel, swellBreath, swellExpression, swellAftertouch, kNumSwellSources };
inline const char* voiceModeName (int m)   { static const char* n[] = { "Poly", "Mono", "Legato" }; return n[m >= 0 && m < kNumVoiceModes ? m : 0]; }
inline const char* vibSourceName (int m)   { static const char* n[] = { "Auto (delayed)", "Mod Wheel", "Aftertouch", "Auto + Aftertouch" }; return n[m >= 0 && m < kNumVibSources ? m : 0]; }
inline const char* swellSourceName (int m) { static const char* n[] = { "Off", "Mod Wheel (CC1)", "Breath (CC2)", "Expression (CC11)", "Aftertouch" }; return n[m >= 0 && m < kNumSwellSources ? m : 0]; }

constexpr int    kNumVoices = 16;
constexpr int    kChunk     = 32;
constexpr double kVoiceGain = 0.35;

struct EngineParams
{
    int    excitation  = excArco;
    // bow / contact (0..1 user ranges)
    double bowPressure = 0.5;  // 0 surface sound .. playable window .. 1 crunch
    double bowSpeed    = 0.5;  // 0.05 .. 0.8 m/s
    double bowPosition = 0.5;  // 0 sul tasto .. 0.5 normale .. 1 sul ponticello (pluck / strike point too)
    int    harmonic    = 2;    // natural harmonic number, 2..5 (touch at 1/n of the string)
    double malletHardness = 0.5;
    int    ebowMode    = 0;    // 0 fundamental, 1 harmonic
    // strings (Materials.h)
    int    material    = matSynthetic;
    double tension     = 0.5;  // 0 slack .. 0.5 normal .. 1 tight
    double age         = 0.25; // 0 fresh .. 0.25 nominal .. 1 old
    double torsion     = 1.0;  // torsional coupling multiplier (on top of the material's)
    double releaseTime = 0.5;  // s, T60 after the key is released (finger lifts / damps)
    // body
    int    body        = bodyModern;
    double ampDrive    = 0.25, ampBass = 0.5, ampMid = 0.5, ampTreble = 0.5;
    bool   ampCab      = true;
    double width       = 1.0;  // 0 mono .. 1 natural .. 2 wide
    // sympathetic strings (Sympathetic.h)
    SympSettings symp;
    // playing
    int    voiceMode    = 0;     // VoiceMode
    double portaTime    = 0.12;  // s for a one-octave fingered slide (scales with the interval)
    double vibRate      = 5.5;   // Hz
    double vibDepth     = 18.0;  // cents (peak)
    double vibDelay     = 0.35;  // s before Auto vibrato fades in
    int    vibSource    = 0;     // VibSource
    int    swellSource  = 1;     // SwellSource (default: mod wheel)
    int    keyswitchBase = 12;   // MIDI note of the first keyswitch (C0); -1 = off
    double releaseNoise = 0.3;   // 0..1
    double bowNoise     = 0.3;   // 0..1
    double bendRange    = 2.0;   // semitones
    // output
    double reverbSend   = 0.2;
    double reverbDecay  = 2.4;   // s
    double presetGain   = 1.0;   // the preset's own level
    double master      = 0.8;
};

constexpr double kBodyGain = 3.8; // overall level after the body (see bodyProfile().outputDb)

// ---- user ranges -> physical values (shared with tests) ----------------------
inline double bowBeta (double position)    { return contactBeta (excArco, position); }
inline double bowVelocity (double speed)   { return 0.05 * std::pow (2.0, 4.0 * clampd (speed, 0, 1)); }
// Tension modulation: pitch rise = kGlide x material/tension glide x string energy
// (mean square of the travelling waves), capped at kGlideMax.
constexpr double kGlide = 0.012, kGlideMax = 0.03;
constexpr double kNoiseInject = 0.08; // release / bow-change noise level at the contact (velocity units)

// Bow force from the Pressure knob. The playable (Helmholtz) window was measured
// with NoStringsRender's Schelleng map:
//   upper edge  ~ 0.8 x Schelleng's Fmax = 1.6 vb / beta
//   lower edge  ~ 3.4 vb beta^-1.54 hz^-0.59   (capped at 0.75 x the upper edge)
// The knob spans the window in log-force with one extra octave past each edge,
// so 0 gives surface sound / multi-slip and 1 gives raucous crunch.
inline double bowForce (double pressure, double vb, double beta, double hz)
{
    const double hi = std::log2 (1.6 / beta);
    const double lo = std::min (std::log2 (3.4 * std::pow (beta, -1.54) * std::pow (std::max (hz, 20.0), -0.59)), hi - 0.415);
    const double width = hi - lo;
    return vb * std::pow (2.0, 0.5 * (lo + hi) + (clampd (pressure, 0, 1) - 0.5) * (width + 2.0));
}

// Output level per excitation (set by rendering each at mf, see NoStringsRender).
inline double excitationGain (int e)
{
    //                          Arco Pizz Bartok Battuto Tratto Spicc Harm  Mallet EBow
    static const double g[] = { 1.0, 1.5, 0.75,  0.85,   1.4,   2.2,  1.6,  0.7,   11.0 };
    return e >= 0 && e < kNumExcitations ? g[e] : 1.0;
}

struct Voice
{
    WaveguideString string;
    BowFriction bow;
    PluckExciter pluck;
    HammerExciter hammer;
    EBowExciter ebow;
    Rng rng;
    int note = -1, exc = excArco, harmonic = 2;
    bool active = false, held = false;
    double velocity = 0;              // 0..1
    double stroke = 1, strokeInc = 0; // bow: 0..1 progress through the acceleration
    double force = 0, forceTarget = 0, forceStep = 0;  // bow: linear press / lift
    double bounce = 1, bounceInc = 0; // spiccato: 0..1 progress through the bow's contact
    double dir = 1;                   // bow direction (spiccato alternates)
    double accent = 0, accentCoef = 0;
    double noise = 0;                 // low-passed rosin / wood noise
    double click = 0, clickCoef = 0, clickLp = 0, clickHp = 0; // col legno: the stick's own knock
    double level = 0;                 // peak follower, decides when the voice is free
    double energy = 0, energyCoef = 0.01; // mean square of the waves at the bridge (tension modulation)
    // playing
    int stringIdx = 0;                // which string of the body plays this note
    bool sustained = false;           // released while the sustain pedal is down
    double pitch = 60, glideFrom = 60, glideTo = 60, glideT = 0, glideTime = 0; // semitones; fingered slide
    double time = 0;                  // s since the note (or slide) started: vibrato delay
    double vibPhase = 0, vibRateJit = 1, vibDepthJit = 1, vibCents = 0;
    double bowChange = 1, bowChangeInc = 0;   // 0..1 through a bow reversal (1 = none)
    double burst = 0, burstCoef = 0, burstLp = 0, burstHp = 0, burstLpCoef = 0.3, burstHpCoef = 0.1; // release / bow-change noise
    uint32_t age = 0;
};

class Engine
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        uint32_t seed = 0x1234567u;
        for (auto& v : voices)
        {
            v.string.prepare (fs);
            v.rng.seed (seed += 0x9E3779B9u);
            v.ebow.rng.seed (seed ^ 0x5bd1e995u);
            v.active = false;
        }
        dc.setup (20.0, fs);
        dc.reset();
        dcR.setup (20.0, fs);
        dcR.reset();
        configureBody (true);
        symp.prepare (fs);
        symp.configure (params.symp, bodyProfile (params.body));
        reverb.prepare (fs);
        reverb.setDecay (params.reverbDecay);
        levelDecay = std::exp (-1.0 / (0.05 * fs));
        noiseCoef = smoothCoef (1.0 / (kTwoPi * 2000.0), fs);
        woodNoiseCoef = smoothCoef (1.0 / (kTwoPi * 6000.0), fs);
        clickLpCoef = smoothCoef (1.0 / (kTwoPi * 4000.0), fs);
        clickHpCoef = smoothCoef (1.0 / (kTwoPi * 1200.0), fs);
        mix.assign (kChunk, 0.0);
    }

    void setParams (const EngineParams& p)
    {
        if (p.excitation != params.excitation) ksExcitation = -1;
        if (p.voiceMode != params.voiceMode) monoVoice = -1;
        const bool bodyChanged = p.body != params.body;
        const bool sympChanged = p.symp != params.symp;
        const bool sympTurnedOn = p.symp.on && ! params.symp.on;
        const bool ampChanged = p.ampDrive != params.ampDrive || p.ampBass != params.ampBass || p.ampMid != params.ampMid
                             || p.ampTreble != params.ampTreble || p.ampCab != params.ampCab;
        params = p;
        params.body = std::min (std::max (params.body, 0), kNumBodies - 1);
        resp = stringResponse (params.material, params.tension, params.age);
        if (bodyChanged) configureBody (true);
        else if (ampChanged) configureAmp();
        if (sympChanged || bodyChanged) symp.configure (params.symp, bodyProfile (params.body));
        if (sympTurnedOn) symp.clear();   // nothing stale rings out
        if (params.reverbDecay != reverb.decay()) reverb.setDecay (params.reverbDecay);
    }

    // ---- MIDI-level input --------------------------------------------------------
    void noteOn (int note, int vel)
    {
        note = std::min (std::max (note, 0), 127);
        if (params.keyswitchBase >= 0 && note >= params.keyswitchBase && note < params.keyswitchBase + kNumExcitations)
        {
            ksExcitation = note - params.keyswitchBase;      // articulation for the next notes; silent
            return;
        }
        const bool overlap = heldCount > 0;
        pushKey (note);
        const int exc = currentExcitation();

        if (params.voiceMode == modePoly)
        {
            Voice* v = findVoice (note);
            startVoice (*v, note, vel, exc, v->active && v->note == note && v->exc == exc ? startRebow : startFresh);
            return;
        }

        // Mono / Legato: one note at a time. Same string: the finger moves (slide when
        // legato, otherwise a bow change at the new pitch). Other string: cross to it.
        Voice* cur = monoVoice >= 0 && voices[(size_t) monoVoice].active ? &voices[(size_t) monoVoice] : nullptr;
        const int str = stringFor (note);
        if (cur != nullptr && cur->held && cur->exc == exc && params.voiceMode == modeLegato && overlap)
        {
            if (cur->stringIdx == str) slideVoice (*cur, note);
            else
            {
                releaseVoice (*cur, false);
                Voice* nv = findVoice (note, cur);
                startVoice (*nv, note, vel, exc, startCross, cur->dir);
                monoVoice = (int) (nv - voices.data());
            }
            return;
        }
        if (cur != nullptr && cur->stringIdx == str && cur->exc == exc)
        {
            startVoice (*cur, note, vel, exc, startRebow);
            return;
        }
        if (cur != nullptr) releaseVoice (*cur, true);
        Voice* nv = findVoice (note, cur);
        startVoice (*nv, note, vel, exc, startFresh);
        monoVoice = (int) (nv - voices.data());
    }

    void noteOff (int note)
    {
        note = std::min (std::max (note, 0), 127);
        if (params.keyswitchBase >= 0 && note >= params.keyswitchBase && note < params.keyswitchBase + kNumExcitations) return;
        removeKey (note);

        if (params.voiceMode == modePoly)
        {
            for (auto& v : voices)
                if (v.active && v.held && v.note == note)
                {
                    if (sustainPedal) v.sustained = true;
                    else releaseVoice (v, true);
                }
            return;
        }

        Voice* cur = monoVoice >= 0 && voices[(size_t) monoVoice].active ? &voices[(size_t) monoVoice] : nullptr;
        if (cur == nullptr || ! cur->held || cur->note != note) return;
        if (heldCount > 0)
        {
            // back to the last key still held: slide / cross in Legato, re-bow in Mono
            const int back = heldKeys[(size_t) heldCount - 1];
            const int exc = currentExcitation();
            if (params.voiceMode == modeLegato && cur->exc == exc && stringFor (back) == cur->stringIdx) slideVoice (*cur, back);
            else if (params.voiceMode == modeLegato && cur->exc == exc)
            {
                releaseVoice (*cur, false);
                Voice* nv = findVoice (back, cur);
                startVoice (*nv, back, (int) (cur->velocity * 127), exc, startCross, cur->dir);
                monoVoice = (int) (nv - voices.data());
            }
            else startVoice (*cur, back, (int) (cur->velocity * 127), exc, cur->stringIdx == stringFor (back) ? startRebow : startFresh);
            return;
        }
        if (sustainPedal) cur->sustained = true;
        else releaseVoice (*cur, true);
    }

    void allNotesOff()
    {
        heldCount = 0;
        for (auto& v : voices) if (v.active && v.held) releaseVoice (v, false);
    }

    void panic()
    {
        heldCount = 0;
        monoVoice = -1;
        for (auto& v : voices) { v.active = v.held = v.sustained = false; v.string.reset(); v.bow.reset(); }
        dc.reset(); dcR.reset(); bridge.reset(); body.reset(); radiation.reset(); amp.reset(); symp.clear(); reverb.clear();
    }

    void setSustain (bool down)
    {
        sustainPedal = down;
        if (! down)
            for (auto& v : voices)
                if (v.active && v.sustained) { v.sustained = false; if (! isKeyHeld (v.note)) releaseVoice (v, true); }
    }
    void setModWheel (double v)   { modWheel = clampd (v, 0, 1); }
    void setAftertouch (double v) { aftertouch = clampd (v, 0, 1); }
    void setBreath (double v)     { breath = clampd (v, 0, 1); }
    void setExpression (double v) { expression = clampd (v, 0, 1); }
    void setPitchBend (double v)  { bend = clampd (v, -1, 1); }       // -1..1, scaled by bendRange

    // The articulation used by the next note: the last keyswitch pressed, until the
    // Excitation parameter itself changes.
    int currentExcitation() const
    {
        return std::min (std::max (ksExcitation >= 0 ? ksExcitation : params.excitation, 0), kNumExcitations - 1);
    }

    // Writes numSamples into L and R.
    void render (float* L, float* R, int numSamples)
    {
        int pos = 0;
        while (pos < numSamples)
        {
            const int n = std::min (kChunk, numSamples - pos);
            std::fill (mix.begin(), mix.begin() + n, 0.0);
            // controllers glide over ~15 ms so wheel / pedal moves never step
            const double k = 1.0 - std::exp (-n / (0.015 * fs));
            modWheelS += (modWheel - modWheelS) * k;
            aftertouchS += (aftertouch - aftertouchS) * k;
            bendS += (bend - bendS) * k;
            double swellTarget = 0.0;
            switch (params.swellSource)
            {
                case swellModWheel:   swellTarget = modWheel; break;
                case swellBreath:     swellTarget = breath; break;
                case swellExpression: swellTarget = expression; break;
                case swellAftertouch: swellTarget = aftertouch; break;
                default: break;
            }
            swell += (swellTarget - swell) * k;
            int count = 0;
            for (auto& v : voices)
                if (v.active) { ++count; updateControl (v, n); configureVoice (v, false); renderVoice (v, n); }
            active = count;

            const BodyProfile& bp = bodyProfile (params.body);
            const double g = params.master * params.presetGain * kBodyGain * dbToGain (bp.outputDb);
            const bool electric = bp.electric;
            const bool sympOn = params.symp.on;
            if (sympOn) sympSamples += n;
            const bool reverbOn = params.reverbSend > 0.0 || ! reverb.idle();
            for (int i = 0; i < n; ++i)
            {
                double l, r;
                const double atBridge = bridge.process (mix[(size_t) i]);
                body.process (sympOn ? atBridge + symp.process (atBridge) : atBridge, l, r);
                radiation.process (l, r, params.width);
                if (electric)
                {
                    // piezo -> DI / amp (mono), keeping the frame's small stereo spread
                    const double m = 0.5 * (l + r), side = 0.5 * (l - r);
                    const double a = amp.process (m);
                    l = a + side; r = a - side;
                }
                l = dc.process (l) * g;
                r = dcR.process (r) * g;
                if (reverbOn) reverb.process (l, r, params.reverbSend);
                const float yl = (float) std::tanh (l), yr = (float) std::tanh (r);
                L[pos + i] = yl;
                R[pos + i] = yr;
                peak = std::max (peak, std::max (std::fabs (yl), std::fabs (yr)));
            }
            pos += n;
        }
    }

    int activeVoices() const { return active; }
    bool sustainDown() const { return sustainPedal; }
    double modWheelValue() const { return modWheel; }
    float takePeak() { const float p = peak; peak = 0; return p; }

    // ---- debug / tests ----
    int slipCount() const { int s = 0; for (auto& v : voices) s += v.bow.slipCount; return s; }
    void resetSlipCount() { for (auto& v : voices) v.bow.slipCount = 0; }
    int nanResets = 0;
    long long sympSamples = 0; // samples the sympathetic bank actually ran (0 while it is off)
    double monoPitch() const { return monoVoice >= 0 ? voices[(size_t) monoVoice].pitch : -1.0; } // current (sliding) pitch
    int heldNotes() const { return heldCount; }

private:
    enum StartKind { startFresh, startRebow, startCross };

    // Starts (or re-plays) a note on a voice.
    //  fresh: a new stroke / pluck / strike on a silent or different-articulation string
    //  rebow: the string is still sounding: bowed -> the bow reverses (speed passes
    //         through zero, the string restarts: the real bow-change transient);
    //         plucked / struck -> played again on top of the ringing
    //  cross: legato onto another string: the bow is already moving, no new attack
    void startVoice (Voice& v, int note, int vel, int exc, StartKind kind, double crossDir = 1.0)
    {
        if (kind == startRebow && (! v.active || v.exc != exc)) kind = startFresh;
        const bool wasSounding = v.active;
        if (kind != startRebow)
        {
            if (! wasSounding || kind == startCross) v.string.reset();
            v.bow.reset(); v.pluck = PluckExciter(); v.hammer = HammerExciter();
            v.ebow.active = false; v.ebow.drive = v.ebow.env = 0; v.ebow.focus.reset();
            v.force = 0; v.stroke = 0; v.bounce = 1; v.click = 0; v.bowChange = 1; v.burst = 0;
            if (! wasSounding || kind == startCross) { v.energy = 0; v.level = 0; }
        }
        v.note = note;
        v.exc = exc;
        v.stringIdx = stringFor (note);
        v.harmonic = std::min (std::max (params.harmonic, 2), 5);
        v.active = v.held = true;
        v.sustained = false;
        v.velocity = clampd (vel / 127.0, 0.0, 1.0);
        v.age = ++ageCounter;
        v.time = 0;
        // a new finger position: jump there (a re-bow at another pitch glides 15 ms so it does not click)
        v.glideFrom = kind == startRebow ? v.pitch : (double) note;
        v.glideTo = note;
        v.glideT = 0;
        v.glideTime = kind == startRebow ? 0.015 : 0.0;
        v.pitch = v.glideFrom;
        if (kind != startRebow)
        {
            v.vibPhase = kTwoPi * v.rng.uni();
            v.vibRateJit = 1.0 + 0.06 * v.rng.bi();
            v.vibDepthJit = 1.0 + 0.15 * v.rng.bi();
        }
        configureVoice (v, kind != startRebow);

        const double vel01 = v.velocity;
        const double hz = midiToHz (note);
        switch (exc)
        {
            case excArco: case excHarmonic: case excColLegnoTratto:
            {
                if (exc == excColLegnoTratto) { v.bow.muS = 0.45; v.bow.muD = 0.28; v.bow.v0 = 0.05; }
                else                          { v.bow.muS = resp.muS; v.bow.muD = resp.muD; v.bow.v0 = resp.v0; }
                v.accentCoef = std::exp (-1.0 / (0.06 * fs));
                v.forceTarget = 1.0;
                v.forceStep = 1.0 / (0.005 * fs);
                if (kind == startCross)
                {
                    // legato string crossing: the bow arrives already moving, same direction
                    v.force = 1.0; v.stroke = 1.0; v.dir = crossDir; v.accent = 0;
                }
                else if (kind == startRebow && v.force > 0.5)
                {
                    // bow change: reverse through zero over 30..60 ms, a little bite, hair noise
                    v.bowChange = 0;
                    v.bowChangeInc = 1.0 / (lerp (0.06, 0.03, vel01) * fs);
                    v.accent = 0.15 * vel01;
                    triggerBurst (v, params.bowNoise * lerp (0.5, 1.0, vel01), 0.04, 1500, 5000);
                }
                else
                {
                    // press first (5 ms), then draw: harder = quicker acceleration and a little bite.
                    // Each new stroke goes the other way (down-bow, up-bow, ...).
                    const double pressTime = 0.005, drawTime = lerp (0.16, 0.035, vel01) * resp.drawScale;
                    bowDir = -bowDir;
                    v.dir = bowDir;
                    v.stroke = -pressTime / drawTime;
                    v.strokeInc = 1.0 / (drawTime * fs);
                    v.accent = (exc == excArco ? 0.25 : 0.1) * vel01;
                }
                break;
            }
            case excSpiccato:
                // the bow lands already moving, touches for 35..75 ms, lifts; direction alternates
                bowDir = -bowDir;
                v.dir = bowDir;
                v.bounce = 0;
                v.bounceInc = 1.0 / (lerp (0.075, 0.035, vel01) * fs);
                v.bow.muS = resp.muS; v.bow.muD = resp.muD; v.bow.v0 = resp.v0;
                break;
            case excPizzicato: case excBartok:
            {
                const bool bartok = exc == excBartok;
                const double mm = (bartok ? lerp (4.0, 9.0, vel01) : lerp (0.8, 4.0, vel01)) * std::pow (65.0 / std::max (hz, 40.0), 0.9) * resp.pluckScale;   // smaller swing up high keeps peaks even; capped below 40 Hz
                const double release = bartok ? 0.0006 : lerp (0.004, 0.0008, vel01);
                v.pluck.start (v.string, mm * 1e-3, release, fs, bartok);
                break;
            }
            case excMallet:
            {
                const double hard = clampd (params.malletHardness, 0, 1);
                v.hammer.start (lerp (0.3, 2.5, vel01), 0.0012, 0.4 * std::pow (10.0, 1.6 * hard), lerp (2.8, 2.0, hard), fs);
                break;
            }
            case excColLegnoBattuto:
                v.hammer.start (lerp (0.4, 2.2, vel01), 0.0015, 40.0, 1.5, fs, lerp (0.03, 0.018, vel01), 0.35);
                v.click = lerp (0.05, 0.25, vel01);
                v.clickCoef = std::exp (-1.0 / (0.004 * fs));
                break;
            case excEBow:
                v.ebow.setup (hz, fs, params.ebowMode == 1);
                v.ebow.start (kind == startCross ? 0.05 : lerp (1.2, 0.25, vel01), lerp (0.04, 0.15, vel01), fs);
                break;
            default: break;
        }
    }

    // Legato on the same string: the finger slides, the bow keeps going. Slide time
    // grows with the interval (portaTime is a one-octave shift) along an S-shaped
    // path (accelerate, then settle) like a real shift.
    void slideVoice (Voice& v, int note)
    {
        const double interval = std::fabs (note - v.pitch);
        v.glideFrom = v.pitch;
        v.glideTo = note;
        v.glideT = 0;
        v.glideTime = params.portaTime * std::pow (interval / 12.0, 0.6);
        v.note = note;
        v.time = 0;  // vibrato settles again after the shift
        v.age = ++ageCounter;
    }

    // Lift the bow / stop the EBow / let the note ring with release damping, plus the
    // release noise (bow hair leaving the string, or a finger damping a pluck).
    void releaseVoice (Voice& v, bool noise)
    {
        if (! v.active || ! v.held) return;
        v.held = false;
        v.sustained = false;
        if (isBowed (v.exc)) { v.forceTarget = 0.0; v.forceStep = 1.0 / (0.015 * fs); } // lift the bow
        if (v.exc == excEBow) v.ebow.stop();
        if (v.exc == excBartok) v.pluck.stopColliding();
        if (noise && params.releaseNoise > 0)
        {
            if (isBowed (v.exc)) triggerBurst (v, params.releaseNoise * 0.7, 0.05, 1800, 6000);
            else                 triggerBurst (v, params.releaseNoise, 0.03, 120, 900);   // fingertip thump
        }
        configureVoice (v, false);             // release damping
    }

    void triggerBurst (Voice& v, double amount, double seconds, double hpHz, double lpHz)
    {
        v.burst = std::max (v.burst, amount);
        v.burstCoef = std::exp (-1.0 / (seconds * fs));
        v.burstLpCoef = smoothCoef (1.0 / (kTwoPi * lpHz), fs);
        v.burstHpCoef = smoothCoef (1.0 / (kTwoPi * hpHz), fs);
    }

    // Which string of the selected body plays a note: the highest open string at or
    // below it (lowest position, as a player would by default).
    int stringFor (int note) const
    {
        const BodyProfile& bp = bodyProfile (params.body);
        int idx = 0;
        for (int k = 0; k < bp.numStrings; ++k) if (bp.strings[k] <= note) idx = k;
        return idx;
    }

    void pushKey (int note)
    {
        removeKey (note);
        if (heldCount < (int) heldKeys.size()) heldKeys[(size_t) heldCount++] = note;
    }
    void removeKey (int note)
    {
        int w = 0;
        for (int r = 0; r < heldCount; ++r) if (heldKeys[(size_t) r] != note) heldKeys[(size_t) w++] = heldKeys[(size_t) r];
        heldCount = w;
    }
    bool isKeyHeld (int note) const
    {
        for (int r = 0; r < heldCount; ++r) if (heldKeys[(size_t) r] == note) return true;
        return false;
    }

    Voice* findVoice (int note, const Voice* avoid = nullptr)
    {
        if (params.voiceMode == modePoly)
            for (auto& v : voices) if (v.active && v.note == note) return &v;   // replay the same string
        for (auto& v : voices) if (! v.active && &v != avoid) return &v;
        Voice* best = nullptr;                                               // oldest released, then oldest
        for (auto& v : voices) if (&v != avoid && ! v.held && (best == nullptr || v.age < best->age)) best = &v;
        if (best != nullptr) return best;
        for (auto& v : voices) if (&v != avoid && (best == nullptr || v.age < best->age)) best = &v;
        return best;
    }

    // Per-chunk control: slide progress, vibrato.
    void updateControl (Voice& v, int n)
    {
        const double dt = n / fs;
        v.time += dt;
        if (v.glideT < v.glideTime)
        {
            v.glideT += dt;
            const double x = std::min (1.0, v.glideT / v.glideTime);
            v.pitch = v.glideFrom + (v.glideTo - v.glideFrom) * x * x * (3.0 - 2.0 * x);
        }
        else v.pitch = v.glideTo;

        double amount = 0.0;
        const double x = clampd ((v.time - params.vibDelay) / 0.4, 0.0, 1.0);
        const double autoRamp = v.held ? x * x * (3.0 - 2.0 * x) : 0.0;
        switch (params.vibSource)
        {
            case vibModWheel:       amount = modWheelS; break;
            case vibAftertouch:     amount = aftertouchS; break;
            case vibAutoAftertouch: amount = std::max (autoRamp, aftertouchS); break;
            default:                amount = autoRamp; break;
        }
        v.vibPhase += kTwoPi * params.vibRate * v.vibRateJit * dt;
        if (v.vibPhase > kTwoPi) v.vibPhase -= kTwoPi;
        v.vibCents = params.vibDepth * v.vibDepthJit * amount * std::sin (v.vibPhase);
    }


    // Stick-slip with torsion flattens a bowed string (more on long strings). Measured at
    // default settings with NoStringsRender: -7.4 cents at C2, rising ~0.235 cents per
    // semitone. Compensating it keeps a normally bowed note in tune; harder pressure
    // still flattens it a little, as on a real cello.
    static double bowTuningCents (double note) { return clampd (7.4 - 0.235 * (note - 36), -12.0, 12.0); }

    // Natural harmonics: the string's fundamental is the played note / n.
    static double fundamentalNote (const Voice& v)
    {
        return v.exc == excHarmonic ? v.pitch - 12.0 * std::log2 ((double) v.harmonic) : v.pitch;
    }
    static double soundingHz (const Voice& v) { return midiToHz (v.note); }

    double ringDecay (const Voice& v) const
    {
        switch (v.exc)
        {
            case excPizzicato: case excBartok: case excSpiccato: case excColLegnoBattuto: return resp.decay * 0.45;
            case excMallet: return resp.decay * 0.8;
            default: return resp.decay;
        }
    }

    void configureVoice (Voice& v, bool snap)
    {
        StringSetup s;
        const double fund = fundamentalNote (v);
        // the bow-flattening compensation is measured for normal arco only
        s.hz = midiToHz (fund + (v.exc == excArco ? bowTuningCents (fund) * 0.01 : 0.0)
                         + v.vibCents * 0.01 + bendS * params.bendRange);
        s.beta = contactBeta (v.exc, params.bowPosition);
        // open strings of this body ring freely; stopped notes lose some decay and top
        // to the fingertip (unless a fret stops them, as on the arpeggione)
        const BodyProfile& bp = bodyProfile (params.body);
        bool open = v.exc == excHarmonic;
        if (std::fabs (v.pitch - v.note) < 0.01)
            for (int k = 0; k < bp.numStrings; ++k) open = open || bp.strings[k] == v.note;
        const double ring = ringDecay (v) * (open ? 1.0 : bp.stoppedDecay);
        s.decay = v.held ? ring : std::min (ring, params.releaseTime);
        s.hfDecay = s.decay * resp.hfRatio * (open ? 1.0 : bp.stoppedHf / bp.stoppedDecay);
        for (int k = 0; k < 2; ++k)
        {
            s.couplingHz[k] = bp.coupling[k].hz;
            s.couplingQ[k] = bp.coupling[k].q;
            s.couplingC[k] = bp.coupling[k].gain;
        }
        // free-ringing notes are tuned for the coupling; sustained bowing is not (see StringSetup)
        s.compensateCoupling = v.exc != excArco && v.exc != excColLegnoTratto && v.exc != excHarmonic;
        // inharmonicity: B = 1e-3 x stiffness^2 for the open string (synthetic ~2e-5,
        // steel ~1e-4), growing with the square of how far up it is stopped (B ~ 1/L^2)
        const int openNote = bp.strings[std::min (v.stringIdx, bp.numStrings - 1)];
        const double stop = std::pow (2.0, std::max (0.0, fund - openNote) / 12.0);
        s.inharm = std::min (2e-3, 1e-3 * resp.stiffness * resp.stiffness * stop * stop);
        // tension modulation: a big swing stretches the string and raises the pitch, which
        // falls back as it decays. Only for free-ringing notes: under a bow or an EBow the
        // swing is steady, so it would just be a fixed offset (players tune that out).
        if (! isBowed (v.exc) && v.exc != excEBow)
            s.hz *= 1.0 + std::min (kGlideMax, kGlide * resp.glide * v.energy);
        v.energyCoef = smoothCoef (1.5 / s.hz, fs);
        s.touch = v.exc == excHarmonic ? 1.0 / v.harmonic : 0.0;
        s.touchDamp = 0.85;
        v.string.configure (s, snap);
        v.bow.rho = resp.rho * params.torsion;
        v.bow.setup (s.hz, fs);
    }

    void renderVoice (Voice& v, int n)
    {
        const double fundHz = midiToHz (fundamentalNote (v));
        const double beta = contactBeta (v.exc, params.bowPosition);
        // swell (mod wheel by default) leans in: more bow speed and pressure; velocity adds pressure
        const double vbBase = bowVelocity (params.bowSpeed) * lerp (0.6, 1.0, v.velocity) * (1.0 + 0.6 * swell);
        const double pressure = clampd (params.bowPressure + 0.15 * (v.velocity - 0.75) + 0.3 * swell, 0.0, 1.0);
        // A touched string vibrates as n segments: the bow sits at n x beta of its segment
        // and plays in that segment's window (at the sounding pitch), with a lighter arm.
        const double fbBase = v.exc == excHarmonic
            ? bowForce (pressure, vbBase, std::min (beta * v.harmonic, 0.45), midiToHz (v.note)) * 0.7 * resp.forceScale
            : bowForce (pressure, vbBase, beta, fundHz) * (v.exc == excColLegnoTratto ? 1.0 : resp.forceScale);
        const double outGain = kVoiceGain * excitationGain (v.exc) * (isBowed (v.exc) ? std::pow (beta, 0.7) / 0.2 : 1.0);
        const bool tratto = v.exc == excColLegnoTratto;
        const double nCoef = tratto ? woodNoiseCoef : noiseCoef;
        const double nAmt = tratto ? 0.35 : 0.08;

        for (int i = 0; i < n; ++i)
        {
            const double vh = v.string.incoming();
            double f = 0.0, extra = 0.0;

            switch (v.exc)
            {
                case excArco: case excHarmonic: case excColLegnoTratto: case excSpiccato:
                {
                    double vb, fb;
                    v.noise += (v.rng.bi() - v.noise) * nCoef;
                    if (v.exc == excSpiccato)
                    {
                        double env = 0.0;
                        if (v.bounce < 1.0) { v.bounce = std::min (1.0, v.bounce + v.bounceInc); env = std::sin (kPi * v.bounce); }
                        vb = vbBase * v.dir;
                        fb = fbBase * env * (1.0 + nAmt * v.noise);
                    }
                    else
                    {
                        if (v.force < v.forceTarget) v.force = std::min (v.forceTarget, v.force + v.forceStep);
                        else if (v.force > v.forceTarget) v.force = std::max (v.forceTarget, v.force - v.forceStep);
                        if (v.stroke < 1.0) v.stroke = std::min (1.0, v.stroke + v.strokeInc);
                        const double sp = v.stroke <= 0.0 ? 0.0 : 0.5 - 0.5 * std::cos (kPi * v.stroke);
                        v.accent *= v.accentCoef;
                        // bow change: speed runs through zero to the other direction
                        double turn = 1.0;
                        if (v.bowChange < 1.0)
                        {
                            v.bowChange = std::min (1.0, v.bowChange + v.bowChangeInc);
                            turn = std::cos (kPi * v.bowChange);
                            if (v.bowChange >= 1.0) { v.dir = -v.dir; turn = 1.0; }
                        }
                        vb = vbBase * sp * v.dir * turn;
                        fb = fbBase * v.force * (1.0 + v.accent) * (1.0 + nAmt * v.noise) * (0.75 + 0.25 * std::fabs (turn));
                    }
                    f = v.bow.solve (vh, vb, fb);
                    break;
                }
                case excPizzicato: case excBartok:
                    f = v.pluck.process (vh);
                    break;
                case excMallet:
                    f = v.hammer.process (vh);
                    break;
                case excColLegnoBattuto:
                {
                    const bool wasActive = v.hammer.active;
                    f = v.hammer.process (vh);
                    if (v.hammer.active && ! wasActive) v.click = 0.4 * v.click + lerp (0.03, 0.1, v.velocity); // knock again on the bounce
                    if (v.click > 1e-5)
                    {
                        // the stick's own knock: band-passed noise, ~4 ms
                        const double nz = v.rng.bi() * v.click;
                        v.clickLp += (nz - v.clickLp) * clickLpCoef;
                        v.clickHp += (v.clickLp - v.clickHp) * clickHpCoef;
                        extra = v.clickLp - v.clickHp;
                        v.click *= v.clickCoef;
                    }
                    break;
                }
                case excEBow:
                    f = v.ebow.process (vh);
                    break;
                default: break;
            }

            if (v.burst > 1e-5)
            {
                // release / bow-change noise, band-passed and injected at the contact
                v.burstLp += (v.rng.bi() * v.burst - v.burstLp) * v.burstLpCoef;
                v.burstHp += (v.burstLp - v.burstHp) * v.burstHpCoef;
                f += (v.burstLp - v.burstHp) * kNoiseInject;
                v.burst *= v.burstCoef;
            }

            const double wave = v.string.commit (f);
            const double y = wave * outGain + extra * kVoiceGain;
            v.energy += (wave * wave - v.energy) * v.energyCoef;
            if (! std::isfinite (y)) { v.active = false; v.string.reset(); v.bow.reset(); ++nanResets; return; }
            mix[(size_t) i] += y;
            v.level = std::max (std::fabs (y), v.level * levelDecay);
        }

        // free the voice once it is silent and nothing is still driving it
        bool driving = false;
        switch (v.exc)
        {
            case excArco: case excHarmonic: case excColLegnoTratto: driving = v.held || v.force > 0.0; break;
            case excSpiccato: driving = v.bounce < 1.0; break;
            case excPizzicato: case excBartok: driving = v.pluck.ramp < 1.0; break;
            case excMallet: case excColLegnoBattuto: driving = v.hammer.busy() || v.click > 1e-5; break;
            case excEBow: driving = v.ebow.busy(); break;
            default: break;
        }
        // (never before the first wave has made a round trip: on a very long string a strike
        // takes several ms to reach the bridge, so the output is still silent at first)
        if (! driving && v.level < 1e-5 && v.burst <= 1e-5 && v.time * fundHz > 1.5) { v.active = false; v.held = false; }
    }

    void configureBody (bool resetState)
    {
        const BodyProfile& bp = bodyProfile (params.body);
        bridge.configure (bp, fs);
        body.configure (bp, fs);
        radiation.configure (bp, fs);
        configureAmp();
        if (resetState) { bridge.reset(); body.reset(); radiation.reset(); amp.reset(); }
    }

    void configureAmp()
    {
        AmpSettings a;
        a.drive = params.ampDrive; a.bass = params.ampBass; a.mid = params.ampMid; a.treble = params.ampTreble; a.cab = params.ampCab;
        amp.configure (a, fs);
    }

    double fs = 48000.0;
    EngineParams params;
    StringResponse resp = stringResponse (matSynthetic, 0.5, 0.25);
    Bridge bridge;
    ModalBody body;
    Radiation radiation;
    AmpStage amp;
    SympatheticBank symp;
    Reverb reverb;
    DcBlocker dcR;
    float peak = 0;
    std::array<Voice, kNumVoices> voices;
    std::vector<double> mix;
    DcBlocker dc;
    double levelDecay = 0.999, noiseCoef = 0.1, woodNoiseCoef = 0.3, clickLpCoef = 0.3, clickHpCoef = 0.1;
    double bowDir = 1.0;                 // alternates with every new stroke
    int ksExcitation = -1;               // articulation picked by a keyswitch (-1 = the parameter)
    std::array<int, 32> heldKeys {};     // key stack (last-note priority for Mono / Legato)
    int heldCount = 0;
    int monoVoice = -1;
    bool sustainPedal = false;
    double modWheel = 0, aftertouch = 0, breath = 0, expression = 0, bend = 0;
    double modWheelS = 0, aftertouchS = 0, bendS = 0, swell = 0;
    uint32_t ageCounter = 0;
    int active = 0;
};
} // namespace nostrings
