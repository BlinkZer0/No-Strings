// Stage 4: string material, tension and age. Renders each material bowed and
// plucked, tension and age sweeps, and measures inharmonicity, decay,
// brightness, bow response (playable window, settle time), tuning and the
// tension-modulation pitch glide.
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

Take notes (const EngineParams& p, const std::vector<int>& ns, double step, double hold, int vel = 100)
{
    return render (48000, ns.size() * step + 1.0, p, [ns, step, hold, vel] (Engine& e, EngineParams&, double t)
    {
        for (size_t k = 0; k < ns.size(); ++k)
        {
            if (hit (t, k * step)) e.noteOn (ns[k], vel);
            if (hit (t, k * step + hold)) e.noteOff (ns[k]);
        }
    });
}

Take one (const EngineParams& p, int note, double seconds, double hold, int vel = 100, int sr = 48000)
{
    return render (sr, seconds, p, [note, hold, vel] (Engine& e, EngineParams&, double t)
    {
        if (hit (t, 0.0)) e.noteOn (note, vel);
        if (hit (t, hold)) e.noteOff (note);
    });
}

double rmsDb (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0;
    for (size_t i = a; i < b && i < x.size(); ++i) s += (double) x[i] * x[i];
    return 10 * std::log10 (s / (double) (b - a) + 1e-20);
}

// RMS frequency of the mono sum over [a, b): a simple brightness figure
double brightness (const Take& t, size_t a, size_t b)
{
    double e2 = 0, d2 = 0;
    for (size_t i = a + 1; i < b && i < t.L.size(); ++i)
    {
        const double m = 0.5 * (t.L[i] + t.R[i]), mp = 0.5 * (t.L[i - 1] + t.R[i - 1]);
        e2 += m * m; d2 += (m - mp) * (m - mp);
    }
    return t.sr / kTwoPi * std::sqrt (d2 / (e2 + 1e-20));
}

// T60 from the level drop between two windows
double t60 (const Take& t, double a, double b)
{
    const int sr = t.sr, w = sr / 20;
    const double la = rmsDb (t.L, (size_t) (a * sr), (size_t) (a * sr) + (size_t) w);
    const double lb = rmsDb (t.L, (size_t) (b * sr), (size_t) (b * sr) + (size_t) w);
    return la > lb ? 60.0 * (b - a) / (la - lb) : 99.0;
}

// Inharmonicity of the bare string (no bow, no body, no pluck-point nulls):
// cents of partial k above k x the measured fundamental (open string). Impulse at
// beta 0.013, so the first comb null is far above the partials measured.
double partialCents (const EngineParams& p, int note, int k)
{
    const int sr = 48000;
    const StringResponse r = stringResponse (p.material, p.tension, p.age);
    WaveguideString s;
    s.prepare (sr);
    StringSetup st;
    st.hz = midiToHz (note); st.beta = 0.013; st.decay = r.decay; st.hfDecay = r.decay * r.hfRatio; st.inharm = 1e-3 * r.stiffness * r.stiffness;
    s.configure (st, true);
    std::vector<float> y ((size_t) sr);
    for (size_t i = 0; i < y.size(); ++i) { s.incoming(); y[i] = (float) s.commit (i < 2 ? 1.0 : 0.0); }
    const double f1 = finePitch (y, 2400, 19200, sr, st.hz, 20);
    const double fk = finePitch (y, 2400, 19200, sr, st.hz * k, 40);
    return centsBetween (fk, f1 * k);
}
} // namespace

void runStage4 (Context& c)
{
    gMeasure = false;
    auto save = [&c] (const std::string& name, const Take& t) { saveTake (c, "stage4", name, t); };
    std::printf ("\n== Stage 4 renders: string material, tension, age\n");

    for (int m = 0; m < kNumMaterials; ++m)
    {
        EngineParams p; p.material = m;
        Take all = notes (p, { 36, 50, 57, 64 }, 1.6, 1.25);
        p.excitation = excPizzicato;
        append (all, notes (p, { 36, 50, 57, 64 }, 1.2, 1.1));
        std::string name = std::to_string (m + 1);
        name = (m < 9 ? "0" : "") + name + "_" + materialProfile (m).name + "_arco_then_pizz";
        for (auto& ch : name) if (ch == ' ' || ch == '-') ch = '_';
        save (name, all);
    }
    {
        Take all; all.sr = 48000;
        for (const double t : { 0.0, 0.5, 1.0 })
        {
            EngineParams p; p.material = matPlainGut; p.tension = t; p.excitation = excPizzicato;
            append (all, notes (p, { 43, 43 }, 1.4, 1.3, 127));
        }
        save ("10_tension_slack_normal_tight_gut_pizz_ff_G2", all);
    }
    {
        Take all; all.sr = 48000;
        for (const double t : { 0.0, 0.5, 1.0 })
        {
            EngineParams p; p.tension = t;
            append (all, notes (p, { 50, 55 }, 1.4, 1.2));
        }
        save ("11_tension_slack_normal_tight_arco_D3_G3", all);
    }
    {
        Take all; all.sr = 48000;
        for (const double a : { 0.0, 0.25, 0.6, 1.0 })
        {
            EngineParams p; p.age = a; p.material = matSteel;
            append (all, notes (p, { 57 }, 1.6, 1.3));
            p.excitation = excPizzicato;
            append (all, notes (p, { 57 }, 1.3, 1.2));
        }
        save ("12_age_fresh_nominal_worn_old_steel_A3", all);
    }
    {
        Take all; all.sr = 48000;
        for (const int m : { matPlainGut, matSteel })
        {
            EngineParams p; p.body = bodyBaroque; p.material = m;
            append (all, notes (p, { 43, 50, 55, 62 }, 1.3, 1.1));
        }
        save ("13_baroque_cello_gut_then_steel", all);
    }

    gMeasure = true;
    // ------------------------------------------------------------ measurements
    std::printf ("\n== Materials at D3 (normal tension, nominal age)\n");
    std::printf ("  %-15s %8s %8s %8s %8s %8s %8s %8s %8s\n", "material", "p8 cents", "p16", "pizz T60", "arco brt", "arco ct", "settle", "window", "arco dB");
    for (int m = 0; m < kNumMaterials; ++m)
    {
        EngineParams p; p.material = m;
        EngineParams pz = p; pz.excitation = excPizzicato;
        const Take pluck = one (pz, 50, 3.0, 2.9);
        const double p8 = partialCents (p, 50, 8), p16 = partialCents (p, 50, 16);
        const auto st = measureNote (50, 48000, p);
        const Take arco = one (p, 50, 2.0, 1.9);
        // playable window: share of Helmholtz cells over pressure .25..75 x position 0..1
        int good = 0, cells = 0;
        for (double pos = 0.0; pos <= 1.001; pos += 0.25)
            for (double pr = 0.25; pr <= 0.751; pr += 0.125)
            {
                EngineParams q = p; q.bowPosition = pos; q.bowPressure = pr;
                ++cells; if (std::fabs (measureNote (50, 48000, q).helmholtz - 1.0) < 0.05) ++good;
            }
        std::printf ("  %-15s %+8.1f %+8.1f %7.2fs %6.0fHz %+8.1f %7.2fs %5d/%-2d %7.1f\n", materialProfile (m).name, p8, p16,
                     t60 (pluck, 0.3, 1.3), brightness (arco, 48000, 90000),
                     st.cents, st.settle, good, cells, rmsDb (arco.L, 48000, 90000));
    }

    std::printf ("\n== Tension (plain gut, G2): pizz ff pitch glide, arco settle and tuning, inharmonicity\n");
    std::printf ("  %-8s %10s %10s %10s %8s %8s %8s\n", "tension", "pitch@40ms", "@300ms", "@1.2s", "settle", "arco ct", "p16 ct");
    for (const double t : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        EngineParams p; p.material = matPlainGut; p.tension = t;
        EngineParams pz = p; pz.excitation = excPizzicato;
        const Take pluck = one (pz, 43, 2.0, 1.9, 127);
        const double f0 = midiToHz (43);
        auto at = [&] (double s) { return centsBetween (finePitch (pluck.L, (size_t) (s * 48000), 9600, 48000, f0, 80), f0); };
        const auto st = measureNote (43, 48000, p);
        std::printf ("  %-8.2f %+10.1f %+10.1f %+10.1f %7.2fs %+8.1f %+8.1f\n", t, at (0.04), at (0.3), at (1.2), st.settle, st.cents,
                     partialCents (p, 43, 16));
    }

    std::printf ("\n== Age (steel, A3): pizz T60 / brightness, inharmonicity, arco level\n");
    for (const double a : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        EngineParams p; p.material = matSteel; p.age = a;
        EngineParams pz = p; pz.excitation = excPizzicato;
        const Take pluck = one (pz, 57, 3.0, 2.9);
        const Take arco = one (p, 57, 2.0, 1.9);
        std::printf ("  age %.2f   T60 %5.2fs   bright %5.0fHz   p8 %+6.1f ct   arco %6.1f dB, bright %5.0fHz\n", a, t60 (pluck, 0.3, 1.3),
                     brightness (pluck, 2400, 24000), partialCents (p, 57, 8), rmsDb (arco.L, 48000, 90000),
                     brightness (arco, 48000, 90000));
    }

    std::printf ("\n== Stability: 6 materials x tension 0/1 x age 0/1 x 9 excitations x 44.1/96 kHz, C1..C6 vel 127\n");
    int runs = 0, bad = 0;
    for (int m = 0; m < kNumMaterials; ++m)
        for (const double t : { 0.0, 1.0 })
            for (const double a : { 0.0, 1.0 })
                for (int ex = 0; ex < kNumExcitations; ++ex)
                    for (const int sr : { 44100, 96000 })
                    {
                        EngineParams p; p.material = m; p.tension = t; p.age = a; p.excitation = ex; p.bowPressure = 1.0;
                        Take tk = render (sr, 1.0, p, [] (Engine& e, EngineParams&, double time)
                        {
                            if (hit (time, 0.0)) for (int n = 24; n <= 84; n += 12) e.noteOn (n, 127);
                            if (hit (time, 0.7)) e.allNotesOff();
                        });
                        ++runs;
                        bool fine = tk.nanResets == 0;
                        for (size_t i = 0; i < tk.L.size(); ++i) if (! std::isfinite (tk.L[i])) { fine = false; break; }
                        if (! fine) { ++bad; std::printf ("  FAIL: %s t%.0f a%.0f %s @ %d\n", materialProfile (m).name, t, a, excitationName (ex), sr); }
                    }
    std::printf ("  %d runs, %d with NaN/inf\n", runs, bad);
    if (bad) c.ok = false;
}
} // namespace nostrings::test
