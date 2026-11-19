// Stage 5: sympathetic strings. Renders off/on comparisons, every tuning mode,
// count / decay / brightness sweeps and the ring after release, and measures
// the halo (matching vs non-matching notes), the strings' tuning, level,
// zero CPU when off, and stability.
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

Take notes (const EngineParams& p, const std::vector<int>& ns, double step, double hold, double tail = 1.5, int vel = 100)
{
    return render (48000, ns.size() * step + tail, p, [ns, step, hold, vel] (Engine& e, EngineParams&, double t)
    {
        for (size_t k = 0; k < ns.size(); ++k)
        {
            if (hit (t, k * step)) e.noteOn (ns[k], vel);
            if (hit (t, k * step + hold)) e.noteOff (ns[k]);
        }
    });
}

double rmsDb (const std::vector<float>& x, double a, double b, int sr = 48000)
{
    double s = 0;
    const size_t i0 = (size_t) (a * sr), i1 = std::min (x.size(), (size_t) (b * sr));
    for (size_t i = i0; i < i1; ++i) s += (double) x[i] * x[i];
    return 10 * std::log10 (s / (double) (i1 - i0) + 1e-20);
}

EngineParams sympOn (int mode = sympOpenStrings, int count = 8)
{
    EngineParams p;
    p.symp.on = true; p.symp.mode = mode; p.symp.count = count; p.symp.amount = 0.6;
    return p;
}

std::string tuningList (const EngineParams& p)
{
    int n[kMaxSympathetic];
    sympatheticNotes (p.symp, bodyProfile (p.body), n, p.symp.count);
    std::string s;
    for (int i = 0; i < p.symp.count; ++i) { s += noteName (n[i]); s += ' '; }
    return s;
}
} // namespace

void runStage5 (Context& c)
{
    gMeasure = false;
    auto save = [&c] (const std::string& name, const Take& t) { saveTake (c, "stage5", name, t); };
    std::printf ("\n== Stage 5 renders: sympathetic strings\n");

    const std::vector<int> phrase { 38, 45, 50, 54, 57, 62 };   // D2 A2 D3 F#3 A3 D4
    {
        EngineParams off; Take all = notes (off, phrase, 1.0, 0.8);
        append (all, notes (sympOn(), phrase, 1.0, 0.8));
        save ("01_off_then_on_open_strings_arco", all);
    }
    {
        EngineParams p = sympOn (sympScale, 12); p.symp.key = 2; p.symp.scale = scaleMajor;
        EngineParams off; Take all = notes (off, { 50, 52, 54, 55, 57, 59, 61, 62 }, 0.6, 0.5);
        append (all, notes (p, { 50, 52, 54, 55, 57, 59, 61, 62 }, 0.6, 0.5, 2.5));
        save ("02_D_major_scale_off_then_on_12_strings_D_major", all);
    }
    {
        EngineParams p = sympOn (sympScale, 6); p.symp.key = 2; p.symp.scale = scaleDrone; p.symp.decay = 12; p.symp.amount = 0.8;
        p.body = bodyBaroque; p.material = matPlainGut;
        save ("03_baroque_gut_with_D_drone", notes (p, { 50, 54, 57, 62, 61, 59, 57, 50 }, 0.8, 0.7, 3.0));
    }
    {
        Take all; all.sr = 48000;
        for (const int n : { 4, 8, 12 }) append (all, notes (sympOn (sympOpenStrings, n), { 50, 57 }, 0.8, 0.6, 1.4));
        save ("04_count_4_8_12_open_strings", all);
    }
    {
        Take all; all.sr = 48000;
        for (const double d : { 1.5, 6.0, 20.0 }) { auto p = sympOn(); p.symp.decay = d; append (all, notes (p, { 50 }, 1.0, 0.6, 2.5)); }
        save ("05_decay_1_5_6_20s_D3", all);
    }
    {
        Take all; all.sr = 48000;
        for (const double b : { 0.0, 0.5, 1.0 }) { auto p = sympOn(); p.symp.brightness = b; append (all, notes (p, { 50 }, 1.0, 0.6, 2.0)); }
        save ("06_brightness_dark_mid_bright_D3", all);
    }
    {
        auto p = sympOn (sympOpenStrings, 12); p.excitation = excPizzicato; p.symp.decay = 10;
        save ("07_pizz_staccato_then_listen_to_the_halo", notes (p, { 50, 57, 62, 55 }, 0.35, 0.15, 4.0));
    }
    {
        auto p = sympOn (sympCustom, 8);
        const int custom[] = { 36, 43, 48, 52, 55, 60, 64, 67 };   // C major arpeggio
        for (int i = 0; i < 8; ++i) p.symp.custom[i] = custom[i];
        p.excitation = excPizzicato;
        save ("08_custom_C_major_arpeggio_tuning_pizz", notes (p, { 48, 52, 55, 60, 49, 51 }, 0.7, 0.3, 3.0));
    }

    gMeasure = true;
    // ------------------------------------------------------------ measurements
    std::printf ("\n== Tunings\n");
    {
        EngineParams p = sympOn (sympOpenStrings, 8);
        std::printf ("  Open strings, modern, 8:     %s\n", tuningList (p).c_str());
        p.body = bodyArpeggione; p.symp.count = 12;
        std::printf ("  Open strings, arpeggione,12: %s\n", tuningList (p).c_str());
        p = sympOn (sympScale, 12); p.symp.key = 2; p.symp.scale = scaleMajor;
        std::printf ("  Scale D major, 12:           %s\n", tuningList (p).c_str());
        p.symp.scale = scaleDrone; p.symp.count = 6;
        std::printf ("  Scale D drone, 6:            %s\n", tuningList (p).c_str());
    }

    std::printf ("\n== Halo: tail level 0.4-1.4 s after release (dB), sympathetic on vs off, open strings x8\n");
    std::printf ("  %-26s %8s %8s %8s\n", "note", "off", "on", "halo");
    for (const int note : { 50, 51, 57, 58, 43, 44 })
    {
        for (const int exc : { excArco, excPizzicato })
        {
            EngineParams off; off.excitation = exc;
            EngineParams on = sympOn(); on.excitation = exc;
            const Take a = notes (off, { note }, 1.0, 0.8, 1.6), b = notes (on, { note }, 1.0, 0.8, 1.6);
            const double la = rmsDb (a.L, 1.2, 2.2), lb = rmsDb (b.L, 1.2, 2.2);
            char label[64];
            std::snprintf (label, sizeof label, "%s %s", noteName (note), exc == excArco ? "arco" : "pizz");
            std::printf ("  %-26s %7.1f %8.1f %+7.1f dB %s\n", label, la, lb, lb - la,
                         (note == 50 || note == 57 || note == 43) ? "(matches a string)" : "");
        }
    }

    std::printf ("\n== Sympathetic tuning: pitch of the ring after a matching pizz note is damped\n");
    for (const int note : { 50, 57, 43 })
    {
        EngineParams p = sympOn(); p.excitation = excPizzicato; p.symp.decay = 12; p.releaseTime = 0.08;
        const Take t = notes (p, { note }, 0.3, 0.3, 3.0);
        // open strings mode: the bank has a string in unison with each open string
        const double hz = midiToHz (note);
        const double est = finePitch (t.L, (size_t) (1.3 * 48000), 48000, 48000, hz, 30);
        std::printf ("  %s -> sympathetic ring: %+.2f cents\n", noteName (note), centsBetween (est, hz));
    }

    std::printf ("\n== Level while playing (arco phrase, 0.2-6 s), off vs on at amount 0.6\n");
    {
        EngineParams off; const Take a = notes (off, phrase, 1.0, 0.8);
        const Take b = notes (sympOn(), phrase, 1.0, 0.8);
        std::printf ("  off %.1f dB, on %.1f dB\n", rmsDb (a.L, 0.2, 6.0), rmsDb (b.L, 0.2, 6.0));
    }

    std::printf ("\n== CPU and zero-cost when off (16 voices arco, 48 kHz, 10 s)\n");
    for (const int mode : { -1, 4, 8, 12 })
    {
        EngineParams p;
        if (mode > 0) p = sympOn (sympOpenStrings, mode);
        long long ran = 0;
        const auto t0 = std::chrono::steady_clock::now();
        {
            ScopedNoDenormals guard;
            Engine e; e.prepare (48000); e.setParams (p);
            for (int k = 0; k < 16; ++k) e.noteOn (36 + k * 3, 100);
            std::vector<float> L (kChunk), R (kChunk);
            for (int pos = 0; pos < 480000; pos += kChunk) e.render (L.data(), R.data(), kChunk);
            ran = e.sympSamples;
        }
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  %-12s %.1f%% of one core, bank ran %lld samples\n", mode < 0 ? "off" : (std::to_string (mode) + " strings").c_str(), secs * 10.0, ran);
        if (mode < 0 && ran != 0) { std::printf ("  FAIL: the bank ran while off\n"); c.ok = false; }
    }

    std::printf ("\n== Stability: 3 modes x 7 scales x count 4/12 x decay 0.2/30 x brightness 0/1 x 44.1/96 kHz, pizz+arco chords\n");
    int runs = 0, bad = 0;
    for (int mode = 0; mode < kNumSympModes; ++mode)
        for (int sc = 0; sc < kNumScales; ++sc)
        {
            if (mode != sympScale && sc > 0) continue;
            for (const int n : { 4, 12 })
                for (const double d : { 0.2, 30.0 })
                    for (const double br : { 0.0, 1.0 })
                        for (const int sr : { 44100, 96000 })
                        {
                            EngineParams p = sympOn (mode, n); p.symp.scale = sc; p.symp.decay = d; p.symp.brightness = br; p.symp.amount = 1.0;
                            Take t = render (sr, 1.5, p, [] (Engine& e, EngineParams& q, double time)
                            {
                                if (hit (time, 0.0)) for (int k = 24; k <= 96; k += 7) e.noteOn (k, 127);
                                if (hit (time, 0.6)) { e.allNotesOff(); q.excitation = excPizzicato; }
                                if (hit (time, 0.7)) for (int k = 24; k <= 96; k += 5) e.noteOn (k, 127);
                            });
                            ++runs;
                            bool fine = t.nanResets == 0;
                            for (float s : t.L) if (! std::isfinite (s) || std::fabs (s) > 1.0f) { fine = false; break; }
                            if (! fine) { ++bad; std::printf ("  FAIL: mode %d scale %d n %d decay %.1f bright %.0f @ %d\n", mode, sc, n, d, br, sr); }
                        }
        }
    std::printf ("  %d runs, %d with NaN/inf\n", runs, bad);
    if (bad) c.ok = false;
}
} // namespace nostrings::test
