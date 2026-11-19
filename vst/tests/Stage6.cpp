// Stage 6: playability. Renders voice modes, slides, string crossings,
// vibrato, swells, keyswitches, bow-change and release noise, the full MIDI
// range, pitch bend, aftertouch and the sustain pedal; measures slide timing,
// legato vs re-attack, vibrato rate / depth / onset, keyswitch behaviour, the
// full range, pedal handling, a random-MIDI torture test and CPU.
#include "RenderCommon.h"

namespace nostrings::test
{
namespace
{
void append (Take& all, const Take& t)
{
    all.L.insert (all.L.end(), t.L.begin(), t.L.end());
    all.R.insert (all.R.end(), t.R.begin(), t.R.end());
}

struct Ev { double t; int note; bool on; int vel = 100; };

Take play (const EngineParams& p, const std::vector<Ev>& evs, double seconds,
           const std::function<void (Engine&, double)>& ctl = nullptr, int sr = 48000)
{
    return render (sr, seconds, p, [evs, ctl] (Engine& e, EngineParams&, double t)
    {
        for (const auto& ev : evs)
            if (hit (t, ev.t)) { if (ev.on) e.noteOn (ev.note, ev.vel); else e.noteOff (ev.note); }
        if (ctl) ctl (e, t);
    });
}

// overlapping (legato fingering) phrase: each note starts `overlap` s before the previous ends
std::vector<Ev> legatoPhrase (const std::vector<int>& ns, double step, double overlap, double start = 0.1)
{
    std::vector<Ev> ev;
    for (size_t k = 0; k < ns.size(); ++k)
    {
        ev.push_back ({ start + k * step, ns[k], true });
        ev.push_back ({ start + (k + 1) * step + (k + 1 < ns.size() ? overlap : 0.0), ns[k], false });
    }
    return ev;
}

double rmsDb (const std::vector<float>& x, double a, double b, int sr = 48000)
{
    double s = 0;
    const size_t i0 = (size_t) std::max (0.0, a * sr), i1 = std::min (x.size(), (size_t) (b * sr));
    for (size_t i = i0; i < i1; ++i) s += (double) x[i] * x[i];
    return 10 * std::log10 (s / (double) std::max<size_t> (1, i1 - i0) + 1e-20);
}

// deepest 10 ms dip within +-60 ms of time t, relative to the level 150..60 ms before it
double dipAround (const std::vector<float>& x, double t)
{
    const double before = rmsDb (x, t - 0.15, t - 0.06);
    double low = 1e9;
    for (double a = t - 0.03; a < t + 0.08; a += 0.005) low = std::min (low, rmsDb (x, a, a + 0.01));
    return low - before;
}
} // namespace

void runStage6 (Context& c)
{
    gMeasure = false;
    auto save = [&c] (const std::string& name, const Take& t) { saveTake (c, "stage6", name, t); };
    std::printf ("\n== Stage 6 renders: playability\n");

    const std::vector<int> line { 50, 52, 54, 55, 57, 59, 57, 55, 54, 52, 50 };   // D3 .. B3 .. D3 (crosses to the A string)
    {
        Take all; all.sr = 48000;
        for (const int mode : { modePoly, modeMono, modeLegato })
        {
            EngineParams p; p.voiceMode = mode;
            append (all, play (p, legatoPhrase (line, 0.45, 0.04), line.size() * 0.45 + 1.2));
        }
        save ("01_same_phrase_poly_then_mono_then_legato", all);
    }
    {
        EngineParams p; p.voiceMode = modeLegato; p.portaTime = 0.25;
        std::vector<Ev> ev;
        double t = 0.1;
        for (const int target : { 59, 62, 69 })   // A3 -> B3 (2), D4 (5), A4 (12), all on the A string
        {
            ev.push_back ({ t, 57, true }); ev.push_back ({ t + 0.8, target, true });
            ev.push_back ({ t + 0.85, 57, false }); ev.push_back ({ t + 2.0, target, false });
            t += 2.6;
        }
        save ("02_legato_slides_2_5_12_semitones_on_the_A_string", play (p, ev, t + 0.5));
    }
    {
        EngineParams p; p.voiceMode = modeLegato;
        const std::vector<int> cross { 50, 57, 50, 43, 36, 43, 50, 57 };
        save ("03_legato_string_crossings", play (p, legatoPhrase (cross, 0.6, 0.05), cross.size() * 0.6 + 1.2));
    }
    {
        Take all; all.sr = 48000;
        EngineParams p;                                   // Auto: fades in after 0.35 s
        append (all, play (p, { { 0.1, 50, true }, { 3.0, 50, false } }, 3.6));
        p.vibRate = 4.0; p.vibDepth = 10;
        append (all, play (p, { { 0.1, 50, true }, { 3.0, 50, false } }, 3.6));
        p.vibRate = 7.0; p.vibDepth = 35; p.vibDelay = 0.0;
        append (all, play (p, { { 0.1, 50, true }, { 3.0, 50, false } }, 3.6));
        save ("04_vibrato_auto_then_slow_narrow_then_fast_wide_no_delay", all);
    }
    {
        EngineParams p;
        save ("05_mod_wheel_swell_G2", play (p, { { 0.1, 43, true, 70 }, { 7.0, 43, false } }, 7.6,
            [] (Engine& e, double t) { e.setModWheel (t < 1.0 ? 0.0 : t < 4.0 ? (t - 1.0) / 3.0 : std::max (0.0, 1.0 - (t - 4.0) / 2.5)); }));
    }
    {
        // keyswitches C0.. pick articulations; each one is followed by the same three notes
        EngineParams p;
        std::vector<Ev> ev;
        double t = 0.1;
        for (const int art : { excArco, excPizzicato, excSpiccato, excColLegnoBattuto, excHarmonic, excMallet, excBartok, excColLegnoTratto, excEBow })
        {
            ev.push_back ({ t, 12 + art, true }); ev.push_back ({ t + 0.05, 12 + art, false });
            for (int k = 0; k < 3; ++k)
            {
                const int n = art == excHarmonic ? 69 + k * 5 : 50 + k * 4;
                ev.push_back ({ t + 0.1 + k * 0.45, n, true }); ev.push_back ({ t + 0.1 + k * 0.45 + 0.38, n, false });
            }
            t += 1.8;
        }
        save ("06_keyswitches_arco_pizz_spicc_battuto_harm_mallet_bartok_tratto_ebow", play (p, ev, t + 0.8));
    }
    {
        Take all; all.sr = 48000;
        for (const double lvl : { 0.0, 0.5, 1.0 })
        {
            EngineParams p; p.bowNoise = lvl; p.releaseNoise = 0;
            std::vector<Ev> ev;
            for (int k = 0; k < 4; ++k) ev.push_back ({ 0.1 + k * 0.6, 50, true });   // re-bow the sounding string
            ev.push_back ({ 2.7, 50, false });
            append (all, play (p, ev, 3.2));
        }
        save ("07_bow_changes_noise_0_50_100", all);
    }
    {
        Take all; all.sr = 48000;
        for (const double lvl : { 0.0, 0.5, 1.0 })
        {
            EngineParams p; p.releaseNoise = lvl;
            append (all, play (p, { { 0.1, 50, true }, { 0.7, 50, false }, { 1.2, 57, true }, { 1.8, 57, false } }, 2.4));
            p.excitation = excPizzicato;
            append (all, play (p, { { 0.1, 50, true }, { 0.35, 50, false }, { 0.7, 57, true }, { 0.95, 57, false } }, 1.5));
        }
        save ("08_release_noise_0_50_100_arco_then_pizz", all);
    }
    {
        EngineParams p; p.excitation = excPizzicato; p.keyswitchBase = -1;
        std::vector<Ev> ev;
        for (int n = 0; n < 128; ++n) { ev.push_back ({ 0.05 + n * 0.08, n, true }); ev.push_back ({ 0.05 + n * 0.08 + 0.07, n, false }); }
        save ("09_full_midi_range_0_to_127_pizz", play (p, ev, 128 * 0.08 + 1.0));
    }
    {
        EngineParams p;
        save ("10_pitch_bend_up_down_A3", play (p, { { 0.1, 57, true }, { 4.0, 57, false } }, 4.5,
            [] (Engine& e, double t) { e.setPitchBend (t < 1.0 ? 0.0 : t < 2.0 ? (t - 1.0) : t < 3.0 ? 1.0 - 2.0 * (t - 2.0) : -1.0 + (t - 3.0)); }));
    }
    {
        EngineParams p; p.vibSource = vibAftertouch;
        save ("11_aftertouch_vibrato_D3", play (p, { { 0.1, 50, true }, { 4.0, 50, false } }, 4.5,
            [] (Engine& e, double t) { e.setAftertouch (t < 1.0 ? 0.0 : std::min (1.0, (t - 1.0) / 1.5)); }));
    }
    {
        EngineParams p; p.excitation = excPizzicato;
        save ("12_sustain_pedal_pizz_chord_rings_until_pedal_up", play (p,
            { { 0.1, 43, true }, { 0.15, 43, false }, { 0.3, 50, true }, { 0.35, 50, false }, { 0.5, 57, true }, { 0.55, 57, false } }, 4.0,
            [] (Engine& e, double t) { if (hit (t, 0.0)) e.setSustain (true); if (hit (t, 2.5)) e.setSustain (false); }));
    }

    // ------------------------------------------------------------ measurements
    gMeasure = true;
    std::printf ("\n== Fingered portamento (Legato, A string, porta 0.25 s per octave): 10-90%% slide time\n");
    for (const int interval : { 1, 2, 5, 7, 12 })
    {
        EngineParams p; p.voiceMode = modeLegato; p.portaTime = 0.25; p.vibDepth = 0;
        ScopedNoDenormals guard;
        Engine e; e.prepare (48000); e.setParams (p);
        e.noteOn (57, 100);
        std::vector<float> L (kChunk), R (kChunk);
        double t10 = -1, t90 = -1;
        const int target = 57 + interval;
        for (int pos = 0; pos < 48000 * 2; pos += kChunk)
        {
            const double t = (double) pos / 48000;
            if (pos == 24000) e.noteOn (target, 100);
            if (pos == 24000 + 2400) e.noteOff (57);
            e.render (L.data(), R.data(), kChunk);
            const double frac = (e.monoPitch() - 57) / interval;
            if (t10 < 0 && frac >= 0.1) t10 = t;
            if (t90 < 0 && frac >= 0.9) t90 = t;
        }
        const double expect = 0.25 * std::pow (interval / 12.0, 0.6);
        std::printf ("  %2d semitones: %5.0f ms  (set: %3.0f ms total, S-curve 10-90%% = %3.0f ms)\n", interval,
                     (t90 - t10) * 1000, expect * 1000, expect * 1000 * 0.67);
    }

    std::printf ("\n== Legato vs re-attack: level dip at each note change (dB), D3 E3 F#3 G3 (same string)\n");
    for (const int mode : { modePoly, modeMono, modeLegato })
    {
        EngineParams p; p.voiceMode = mode; p.vibDepth = 0;
        const std::vector<int> ns { 50, 52, 54, 55 };
        const Take t = play (p, legatoPhrase (ns, 0.6, 0.04), 3.2);
        std::printf ("  %-7s", voiceModeName (mode));
        for (int k = 1; k < 4; ++k) std::printf (" %+6.1f", dipAround (t.L, 0.1 + k * 0.6));
        std::printf ("\n");
    }

    std::printf ("\n== Vibrato (Auto): measured from the audio pitch track, A4 arco (40 ms windows)\n");
    gMeasure = false;   // this one needs the vibrato
    {
        EngineParams p;
        p.vibDepth = 18; p.vibRate = 5.5; p.vibDelay = 0.35;
        Take t = render (48000, 3.0, p, [] (Engine& e, EngineParams&, double time) { if (hit (time, 0.0)) e.noteOn (69, 110); });
        const double hz = midiToHz (69);
        std::vector<double> track;
        for (double a = 0.0; a + 0.04 < 3.0; a += 0.01)
            track.push_back (centsBetween (finePitch (t.L, (size_t) (a * 48000), 1920, 48000, hz, 60), hz));
        auto span = [&] (double a, double b)
        {
            double lo = 1e9, hi = -1e9;
            for (size_t i = (size_t) (a * 100); i < (size_t) (b * 100) && i < track.size(); ++i) { lo = std::min (lo, track[i]); hi = std::max (hi, track[i]); }
            return hi - lo;
        };
        int crossings = 0;
        double mean = 0;
        for (size_t i = 150; i < 280 && i < track.size(); ++i) mean += track[i];
        mean /= 130;
        for (size_t i = 151; i < 280 && i < track.size(); ++i) if ((track[i - 1] - mean) * (track[i] - mean) < 0) ++crossings;
        std::printf ("  peak-to-peak 0.0-0.3 s: %4.1f cents (before onset)\n", span (0.0, 0.3));
        std::printf ("  peak-to-peak 1.5-2.8 s: %4.1f cents (set 2 x 18 = 36, +-15%% per voice)\n", span (1.5, 2.8));
        std::printf ("  rate 1.5-2.8 s: %.2f Hz (set 5.5, +-6%% per voice)\n", crossings / 2.0 / 1.3);
    }
    gMeasure = true;

    std::printf ("\n== Keyswitches (C0..G#0 by default)\n");
    {
        EngineParams p;
        Engine e; e.prepare (48000); e.setParams (p);
        std::vector<float> L (4800), R (4800);
        bool silent = true, switched = true;
        for (int k = 0; k < kNumExcitations; ++k)
        {
            e.noteOn (12 + k, 100);
            e.render (L.data(), R.data(), 4800);
            for (float s : L) if (std::fabs (s) > 1e-6f) silent = false;
            e.noteOff (12 + k);
            if (e.currentExcitation() != k) switched = false;
        }
        p.excitation = excMallet;  // moving the parameter takes over again
        e.setParams (p);
        std::printf ("  keyswitch notes silent: %s, each selects its articulation: %s, parameter takes over: %s\n",
                     silent ? "yes" : "NO", switched ? "yes" : "NO", e.currentExcitation() == excMallet ? "yes" : "NO");
        if (! silent || ! switched || e.currentExcitation() != excMallet) c.ok = false;
    }

    std::printf ("\n== Full MIDI range (keyswitches off): notes 0..127 that sound and stay finite\n");
    for (const int exc : { excArco, excPizzicato, excMallet })
    {
        int good = 0;
        for (int n = 0; n < 128; ++n)
        {
            EngineParams p; p.excitation = exc; p.keyswitchBase = -1;
            const Take t = play (p, { { 0.0, n, true }, { 0.3, n, false } }, 0.4);
            float pk = 0; bool fin = true;
            for (float s : t.L) { if (! std::isfinite (s)) fin = false; pk = std::max (pk, std::fabs (s)); }
            if (fin && pk > 1e-4f) ++good;
        }
        std::printf ("  %-10s %3d / 128\n", excitationName (exc), good);
        if (good < 128) c.ok = false;
    }

    std::printf ("\n== Sustain pedal: pizz note level 1.0 s after key-up, pedal down vs up\n");
    {
        EngineParams p; p.excitation = excPizzicato; p.vibDepth = 0;
        const Take a = play (p, { { 0.1, 50, true }, { 0.2, 50, false } }, 1.6);
        const Take b = play (p, { { 0.1, 50, true }, { 0.2, 50, false } }, 1.6, [] (Engine& e, double t) { if (hit (t, 0.0)) e.setSustain (true); });
        std::printf ("  pedal up %.1f dB, pedal down %.1f dB\n", rmsDb (a.L, 1.1, 1.3), rmsDb (b.L, 1.1, 1.3));
    }

    std::printf ("\n== Torture: 20 s of random MIDI per voice mode (notes, velocities, wheel, aftertouch, bend, pedal, keyswitches), 44.1/96 kHz\n");
    int bad = 0, runs = 0;
    for (const int mode : { modePoly, modeMono, modeLegato })
        for (const int sr : { 44100, 96000 })
        {
            EngineParams p; p.voiceMode = mode; p.symp.on = true; p.body = (mode * 3) % kNumBodies;
            Rng rng; rng.seed (77u + (uint32_t) mode * 13u + (uint32_t) sr);
            Take t = render (sr, 20.0, p, [&rng] (Engine& e, EngineParams&, double)
            {
                const double r = rng.uni();
                if (r < 0.06) e.noteOn (12 + (int) (rng.uni() * 100), 1 + (int) (rng.uni() * 126));
                else if (r < 0.11) e.noteOff (12 + (int) (rng.uni() * 100));
                else if (r < 0.13) e.setModWheel (rng.uni());
                else if (r < 0.15) e.setAftertouch (rng.uni());
                else if (r < 0.17) e.setPitchBend (rng.bi());
                else if (r < 0.175) e.setSustain (rng.uni() < 0.5);
                else if (r < 0.176) e.allNotesOff();
            });
            ++runs;
            bool fine = t.nanResets == 0;
            for (float s : t.L) if (! std::isfinite (s) || std::fabs (s) > 1.0f) { fine = false; break; }
            if (! fine) { ++bad; std::printf ("  FAIL: %s @ %d\n", voiceModeName (mode), sr); }
        }
    std::printf ("  %d runs, %d with NaN/inf\n", runs, bad);
    if (bad) c.ok = false;

    std::printf ("\n== CPU, 16 voices arco with vibrato and the swell moving, 48 kHz\n");
    {
        EngineParams p; p.vibDepth = 18; p.vibDelay = 0;
        const auto t0 = std::chrono::steady_clock::now();
        {
            ScopedNoDenormals guard;
            Engine e; e.prepare (48000); e.setParams (p);
            for (int k = 0; k < 16; ++k) e.noteOn (36 + k * 3, 100);
            std::vector<float> L (kChunk), R (kChunk);
            for (int pos = 0; pos < 480000; pos += kChunk) { e.setModWheel (0.5 + 0.5 * std::sin (pos * 1e-4)); e.render (L.data(), R.data(), kChunk); }
        }
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  %.1f%% of one core\n", secs * 10.0);
    }
}
} // namespace nostrings::test
