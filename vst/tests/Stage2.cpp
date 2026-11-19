// Stage 2: excitations. Renders a phrase per excitation plus focused A/B
// takes, and measures pitch, onset, decay and level for each one.
#include "RenderCommon.h"

namespace nostrings::test
{
namespace
{
// 10 ms RMS frames in dB
std::vector<double> envelopeDb (const std::vector<float>& x, int sr)
{
    const int hop = sr / 100;
    std::vector<double> env;
    for (size_t i = 0; i + (size_t) hop <= x.size(); i += (size_t) hop)
    {
        double s = 0;
        for (int k = 0; k < hop; ++k) s += (double) x[i + (size_t) k] * x[i + (size_t) k];
        env.push_back (10.0 * std::log10 (s / hop + 1e-20));
    }
    return env;
}

struct ExcStats
{
    double cents = 0, onsetMs = 0, t60 = 0, loud = 0, peakDb = 0, wobbleDb = 0;
    int nan = 0;
};

// One note: on at 0, off at `holdFor`. Measures over a 3.5 s render.
ExcStats measureExcitation (const EngineParams& p, int note, double holdFor, int vel = 100, int sr = 48000)
{
    Take t = render (sr, 3.5, p, [note, holdFor, vel] (Engine& e, EngineParams&, double time)
    {
        if (hit (time, 0.0)) e.noteOn (note, vel);
        if (hit (time, holdFor)) e.noteOff (note);
    });
    ExcStats st;
    st.nan = t.nanResets;
    const auto env = envelopeDb (t.L, sr);
    size_t peakAt = 0;
    for (size_t i = 0; i < env.size(); ++i) if (env[i] > env[peakAt]) peakAt = i;
    st.peakDb = env[peakAt];
    for (size_t i = 0; i < env.size(); ++i) if (env[i] > st.peakDb - 6.0) { st.onsetMs = i * 10.0; break; }

    // loudness: mean power over the first 0.5 s
    double s = 0;
    for (size_t i = 0; i < std::min<size_t> (50, env.size()); ++i) s += std::pow (10.0, env[i] / 10.0);
    st.loud = 10.0 * std::log10 (s / 50.0 + 1e-20);

    // T60 from the slope between -10 and -30 dB below the peak (while still held)
    const size_t holdEnd = std::min (env.size(), (size_t) (holdFor * 100));
    size_t a = 0, b = 0;
    for (size_t i = peakAt; i < holdEnd; ++i) { if (! a && env[i] < st.peakDb - 10) a = i; if (! b && env[i] < st.peakDb - 30) { b = i; break; } }
    if (a && b > a) st.t60 = 60.0 / ((env[a] - env[b]) / ((b - a) * 0.01));
    else st.t60 = -1; // sustained: did not fall 30 dB while held

    // sustain steadiness: spread of a 50 ms envelope over 1.0 .. holdFor-0.1 s
    // (frames must be longer than a low note's period, or they measure the waveform)
    if (holdFor > 1.3)
    {
        double lo = 1e9, hi = -1e9;
        for (size_t i = 100; i + 5 <= (size_t) ((holdFor - 0.1) * 100) && i + 5 <= env.size(); i += 5)
        {
            double pw = 0;
            for (size_t k = 0; k < 5; ++k) pw += std::pow (10.0, env[i + k] / 10.0);
            const double db = 10.0 * std::log10 (pw / 5.0 + 1e-20);
            lo = std::min (lo, db); hi = std::max (hi, db);
        }
        st.wobbleDb = hi - lo;
    }

    const double hz = midiToHz (note);
    const size_t at = (size_t) ((st.onsetMs / 1000.0 + 0.15) * sr);
    st.cents = centsBetween (finePitch (t.L, at, (size_t) (0.25 * sr), sr, hz), hz);
    return st;
}

// A phrase of notes for one excitation
Take phrase (const EngineParams& p, const std::vector<int>& notes, double step, double hold, int vel = 100)
{
    const double len = notes.size() * step + 2.0;
    return render (48000, len, p, [notes, step, hold, vel] (Engine& e, EngineParams&, double t)
    {
        for (size_t k = 0; k < notes.size(); ++k)
        {
            if (hit (t, k * step)) e.noteOn (notes[k], vel);
            if (hit (t, k * step + hold)) e.noteOff (notes[k]);
        }
    });
}
} // namespace

void runStage2 (Context& c)
{
    gMeasure = false;
    auto save = [&c] (const std::string& name, const Take& t) { saveTake (c, "stage2", name, t); };
    std::printf ("\n== Stage 2 renders: excitations (C2 G2 D3 A3 E4 A4 per excitation)\n");

    const std::vector<int> scale { 36, 43, 50, 57, 64, 69 };
    for (int e = 0; e < kNumExcitations; ++e)
    {
        EngineParams p;
        p.excitation = e;
        const bool sustained = e == excArco || e == excColLegnoTratto || e == excHarmonic;
        const bool ebow = e == excEBow;
        char name[64];
        std::snprintf (name, sizeof name, "%02d_%s", e + 1, excitationName (e));
        for (char* ch = name; *ch; ++ch) if (*ch == ' ') *ch = '_';
        if (e == excHarmonic)
            save (name, phrase (p, { 69, 76, 81, 74, 79, 86 }, 1.6, 1.2));   // sounding pitches, harmonic 2
        else if (ebow)
            save (name, phrase (p, { 36, 50, 57, 69 }, 3.5, 3.0));
        else
            save (name, phrase (p, scale, sustained ? 1.6 : 1.2, sustained ? 1.2 : 0.9));
    }

    {
        EngineParams p; p.excitation = excPizzicato;
        Take a = phrase (p, { 50, 50 }, 1.5, 1.2);
        p.excitation = excBartok;
        Take b = phrase (p, { 50, 50 }, 1.5, 1.2);
        a.L.insert (a.L.end(), b.L.begin(), b.L.end()); a.R.insert (a.R.end(), b.R.begin(), b.R.end());
        save ("10_pizz_then_bartok_D3", a);
    }
    {
        Take all; all.sr = 48000;
        for (const double hard : { 0.0, 0.5, 1.0 })
        {
            EngineParams p; p.excitation = excMallet; p.malletHardness = hard;
            Take t = phrase (p, { 57, 57 }, 1.0, 0.8, 110);
            all.L.insert (all.L.end(), t.L.begin(), t.L.end()); all.R.insert (all.R.end(), t.R.begin(), t.R.end());
        }
        save ("11_mallet_hardness_soft_mid_hard_A3", all);
    }
    {
        Take all; all.sr = 48000;
        for (int n = 2; n <= 5; ++n)
        {
            EngineParams p; p.excitation = excHarmonic; p.harmonic = n;
            const int sounding = 57 + (int) std::lround (12.0 * std::log2 ((double) n)); // on the A3 string
            Take t = phrase (p, { sounding }, 2.0, 1.6);
            all.L.insert (all.L.end(), t.L.begin(), t.L.end()); all.R.insert (all.R.end(), t.R.begin(), t.R.end());
        }
        save ("12_harmonics_2_to_5_on_A3_string", all);
    }
    {
        EngineParams p; p.excitation = excSpiccato;
        std::vector<int> run;
        for (int r = 0; r < 4; ++r) for (const int n : { 50, 52, 54, 55, 57, 55, 54, 52 }) run.push_back (n);
        save ("13_spiccato_run_8_per_second", phrase (p, run, 0.125, 0.1, 96));
    }
    {
        EngineParams p; p.excitation = excEBow; p.ebowMode = 0;
        Take a = phrase (p, { 50 }, 4.0, 3.5);
        p.ebowMode = 1;
        Take b = phrase (p, { 50 }, 4.0, 3.5);
        a.L.insert (a.L.end(), b.L.begin(), b.L.end()); a.R.insert (a.R.end(), b.R.begin(), b.R.end());
        save ("14_ebow_fundamental_then_harmonic_D3", a);
    }
    {
        Take all; all.sr = 48000;
        for (const int e : { excPizzicato, excMallet, excColLegnoBattuto })
        {
            EngineParams p; p.excitation = e;
            for (const int vel : { 30, 70, 110, 127 })
            {
                Take t = phrase (p, { 43 }, 0.9, 0.7, vel);
                all.L.insert (all.L.end(), t.L.begin(), t.L.end()); all.R.insert (all.R.end(), t.R.begin(), t.R.end());
            }
        }
        save ("15_velocity_30_70_110_127_pizz_mallet_battuto_G2", all);
    }

    gMeasure = true;
    // ------------------------------------------------------------ measurements
    std::printf ("\n== Excitations at C2 / D3 / A4 (vel 100, 48 kHz)\n");
    std::printf ("  %-18s %-6s %7s %7s %7s %7s %7s %7s\n", "excitation", "note", "cents", "onset", "T60 s", "loud", "peak", "wobble");
    for (int e = 0; e < kNumExcitations; ++e)
    {
        EngineParams p;
        p.excitation = e;
        const bool sustained = e == excArco || e == excColLegnoTratto || e == excHarmonic || e == excEBow;
        for (const int note : { 36, 50, 69 })
        {
            const auto st = measureExcitation (p, note, sustained ? 3.0 : 2.5);
            if (st.nan) c.ok = false;
            char t60[16];
            if (st.t60 < 0) std::snprintf (t60, sizeof t60, "sust"); else std::snprintf (t60, sizeof t60, "%.2f", st.t60);
            std::printf ("  %-18s %-6s %+7.1f %5.0fms %7s %5.1fdB %5.1fdB %5.1fdB\n", excitationName (e), noteName (note),
                         st.cents, st.onsetMs, t60, st.loud, st.peakDb, st.wobbleDb);
        }
    }

    // Stability: every excitation at 44.1 / 96 kHz, extreme velocity, extreme position
    std::printf ("\n== Stability sweep (all excitations x 44.1/48/96 kHz x vel 1/127 x position 0/1 x C1..C7)\n");
    int runs = 0, bad = 0;
    for (int e = 0; e < kNumExcitations; ++e)
        for (const int sr : { 44100, 48000, 96000 })
            for (const int vel : { 1, 127 })
                for (const double pos : { 0.0, 1.0 })
                {
                    EngineParams p; p.excitation = e; p.bowPosition = pos; p.bowPressure = vel > 64 ? 1.0 : 0.0; p.harmonic = 5;
                    Take t = render (sr, 1.5, p, [vel] (Engine& eng, EngineParams&, double time)
                    {
                        if (hit (time, 0.0)) for (int n = 24; n <= 96; n += 12) eng.noteOn (n, vel);
                        if (hit (time, 1.0)) eng.allNotesOff();
                    });
                    ++runs;
                    bool finite = t.nanResets == 0;
                    for (float s : t.L) if (! std::isfinite (s)) finite = false;
                    if (! finite) { ++bad; std::printf ("  FAIL: %s sr %d vel %d pos %.0f\n", excitationName (e), sr, vel, pos); }
                }
    std::printf ("  %d runs, %d with NaN/inf\n", runs, bad);
    if (bad) c.ok = false;

    std::printf ("\n== CPU, 16 voices per excitation, 48 kHz (10 s of audio)\n");
    for (int e = 0; e < kNumExcitations; ++e)
    {
        EngineParams p; p.excitation = e;
        const auto t0 = std::chrono::steady_clock::now();
        render (48000, 10.0, p, [] (Engine& eng, EngineParams&, double t)
        {
            if (hit (t, 0.0)) for (int k = 0; k < 16; ++k) eng.noteOn (36 + k * 3, 100);
        });
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  %-18s %.1f%% of one core\n", excitationName (e), secs * 10.0);
    }
}
} // namespace nostrings::test
