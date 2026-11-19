// Stage 3: bodies. Renders every body bowed and plucked, A/B takes (same note
// on all bodies, wolf search, amp drive, open vs stopped vs fretted), and
// measures level, brightness, stereo, tuning, the wolf, CPU and stability.
#include "RenderCommon.h"

namespace nostrings::test
{
namespace
{
std::string slug (const char* s)
{
    std::string out;
    for (const char* c = s; *c; ++c) out += (*c == ' ' || *c == '-') ? '_' : *c;
    return out;
}

void append (Take& all, const Take& t)
{
    all.L.insert (all.L.end(), t.L.begin(), t.L.end());
    all.R.insert (all.R.end(), t.R.begin(), t.R.end());
}

Take notes (const EngineParams& p, const std::vector<int>& ns, double step, double hold, int vel = 100, int sr = 48000)
{
    return render (sr, ns.size() * step + 1.5, p, [ns, step, hold, vel] (Engine& e, EngineParams&, double t)
    {
        for (size_t k = 0; k < ns.size(); ++k)
        {
            if (hit (t, k * step)) e.noteOn (ns[k], vel);
            if (hit (t, k * step + hold)) e.noteOff (ns[k]);
        }
    });
}

struct BodyStats { double loud = 0, bright = 0, corr = 0, cents = 0; };

// One note: loudness (dB, over [a, b) s), RMS frequency ("brightness"), L/R correlation, pitch.
BodyStats measure (const EngineParams& p, int note, double a, double b, double hold)
{
    const int sr = 48000;
    Take t = render (sr, b + 0.2, p, [note, hold] (Engine& e, EngineParams&, double time)
    {
        if (hit (time, 0.0)) e.noteOn (note, 100);
        if (hit (time, hold)) e.noteOff (note);
    });
    double e2 = 0, d2 = 0, lr = 0, ll = 0, rr = 0;
    for (size_t i = (size_t) (a * sr) + 1; i < (size_t) (b * sr) && i < t.L.size(); ++i)
    {
        const double m = 0.5 * (t.L[i] + t.R[i]), mp = 0.5 * (t.L[i - 1] + t.R[i - 1]);
        e2 += m * m; d2 += (m - mp) * (m - mp);
        lr += (double) t.L[i] * t.R[i]; ll += (double) t.L[i] * t.L[i]; rr += (double) t.R[i] * t.R[i];
    }
    const double n = (b - a) * sr;
    BodyStats st;
    st.loud = 10.0 * std::log10 (e2 / n + 1e-20);
    st.bright = sr / kTwoPi * std::sqrt (d2 / (e2 + 1e-20));
    st.corr = lr / std::sqrt (ll * rr + 1e-30);
    const double hz = midiToHz (note);
    // pitch from 0.15 s in, past the attack (body modes ring at their own frequencies there)
    st.cents = centsBetween (finePitch (t.L, (size_t) ((a + 0.15) * sr), (size_t) (0.3 * sr), sr, hz), hz);
    return st;
}
} // namespace

void runStage3 (Context& c)
{
    gMeasure = false;
    auto save = [&c] (const std::string& name, const Take& t) { saveTake (c, "stage3", name, t); };
    std::printf ("\n== Stage 3 renders: bodies\n");

    for (int b = 0; b < kNumBodies; ++b)
    {
        const BodyProfile& bp = bodyProfile (b);
        std::vector<int> phrase (bp.strings, bp.strings + bp.numStrings);  // open strings, then up the range
        for (const int n : { 64, 69, 74 }) if (n <= bp.rangeHi) phrase.push_back (n);
        char name[96];
        EngineParams p; p.body = b;
        std::snprintf (name, sizeof name, "%02d_%s_arco", b + 1, slug (bp.name).c_str());
        save (name, notes (p, phrase, 1.6, 1.25));
        p.excitation = excPizzicato;
        std::snprintf (name, sizeof name, "%02d_%s_pizz", b + 1, slug (bp.name).c_str());
        save (name, notes (p, phrase, 1.0, 0.9));
    }
    {
        Take all; all.sr = 48000;
        for (int b = 0; b < kNumBodies; ++b) { EngineParams p; p.body = b; append (all, notes (p, { 50 }, 2.2, 1.8)); }
        save ("20_same_D3_on_all_8_bodies", all);
    }
    {
        Take all; all.sr = 48000;
        std::vector<int> chrom;
        for (int n = 50; n <= 57; ++n) chrom.push_back (n);
        for (const int b : { bodyModern, bodyCarbon }) { EngineParams p; p.body = b; append (all, notes (p, chrom, 1.4, 1.2)); }
        save ("21_wolf_search_D3_to_A3_modern_then_carbon", all);
    }
    {
        Take all; all.sr = 48000;
        for (const double drive : { 0.0, 0.5, 1.0 })
            for (const bool cab : { true, false })
            {
                EngineParams p; p.body = bodyElectricSolid; p.ampDrive = drive; p.ampCab = cab;
                append (all, notes (p, { 45, 52, 57 }, 0.9, 0.8));
            }
        save ("22_electric_drive_0_50_100_each_cab_on_then_off", all);
    }
    {
        Take all; all.sr = 48000;
        for (const int b : { bodyModern, bodyArpeggione })
        {
            EngineParams p; p.body = b; p.excitation = excPizzicato;
            append (all, notes (p, { 50, 51, 52, 53 }, 1.2, 1.1));   // D3 open on both; the rest stopped / fretted
        }
        save ("23_pizz_open_then_stopped_modern_then_fretted_arpeggione", all);
    }

    gMeasure = true;
    // ------------------------------------------------------------ measurements
    std::printf ("\n== Bodies: D3 arco (0.6-1.6 s) and pizz (0-0.5 s), 48 kHz\n");
    std::printf ("  %-18s %6s %8s %8s %6s %7s   %8s %8s %7s\n", "body", "modes", "arco dB", "bright", "corr", "cents", "pizz dB", "bright", "cents");
    for (int b = 0; b < kNumBodies; ++b)
    {
        EngineParams p; p.body = b;
        ModalBody mb; mb.configure (bodyProfile (b), 48000);
        const auto a = measure (p, 50, 0.6, 1.6, 1.8);
        p.excitation = excPizzicato;
        const auto z = measure (p, 50, 0.0, 0.5, 1.0);
        std::printf ("  %-18s %6d %7.1f %7.0fHz %6.2f %+7.1f   %7.1f %7.0fHz %+7.1f\n", bodyProfile (b).name, mb.modeCount(),
                     a.loud, a.bright, a.corr, a.cents, z.loud, z.bright, z.cents);
    }

    std::printf ("\n== Wolf map: bowed C3..C4, slips per period / 50 ms level wobble (dB) after 1 s\n  note ");
    const int wolfBodies[] = { bodyModern, bodyBaroque, bodyCarbon };
    for (const int b : wolfBodies) std::printf ("  %-18s", bodyProfile (b).name);
    std::printf ("\n");
    for (int note = 48; note <= 60; ++note)
    {
        std::printf ("  %-4s", noteName (note));
        for (const int b : wolfBodies)
        {
            EngineParams p; p.body = b; p.vibDepth = 0;
            const int sr = 48000;
            Engine e; e.prepare (sr); e.setParams (p); e.noteOn (note, 100);
            std::vector<float> L ((size_t) (2.5 * sr)), R (L.size());
            for (int pos = 0; pos < (int) L.size(); pos += kChunk)
            {
                if (pos == sr) e.resetSlipCount();
                e.render (L.data() + pos, R.data() + pos, std::min (kChunk, (int) L.size() - pos));
            }
            const double slips = e.slipCount() / (midiToHz (note) * 1.5);
            double lo = 1e9, hi = -1e9;
            for (int w = sr; w + sr / 20 <= (int) L.size(); w += sr / 20)
            {
                double s = 0;
                for (int k = 0; k < sr / 20; ++k) s += (double) L[(size_t) (w + k)] * L[(size_t) (w + k)];
                const double db = 10 * std::log10 (s / (sr / 20) + 1e-20);
                lo = std::min (lo, db); hi = std::max (hi, db);
            }
            std::printf ("  %4.2f / %4.1f dB      ", slips, hi - lo);
        }
        std::printf ("\n");
    }

    std::printf ("\n== Stability: 8 bodies x 9 excitations x 44.1/96 kHz, six notes C1..C6 at vel 127\n");
    int runs = 0, bad = 0;
    for (int b = 0; b < kNumBodies; ++b)
        for (int ex = 0; ex < kNumExcitations; ++ex)
            for (const int sr : { 44100, 96000 })
            {
                EngineParams p; p.body = b; p.excitation = ex; p.ampDrive = 1.0;
                Take t = render (sr, 1.2, p, [] (Engine& e, EngineParams&, double time)
                {
                    if (hit (time, 0.0)) for (int n = 24; n <= 84; n += 12) e.noteOn (n, 127);
                    if (hit (time, 0.8)) e.allNotesOff();
                });
                ++runs;
                bool fine = t.nanResets == 0;
                for (size_t i = 0; i < t.L.size(); ++i) if (! std::isfinite (t.L[i]) || ! std::isfinite (t.R[i])) fine = false;
                if (! fine) { ++bad; std::printf ("  FAIL: %s / %s @ %d\n", bodyProfile (b).name, excitationName (ex), sr); }
            }
    std::printf ("  %d runs, %d with NaN/inf\n", runs, bad);
    if (bad) c.ok = false;

    std::printf ("\n== CPU with the body chain, 16 voices arco, 10 s\n");
    for (const int b : { bodyModern, bodyCarbon, bodyElectricSolid })
        for (const int sr : { 48000, 96000 })
        {
            EngineParams p; p.body = b;
            const auto t0 = std::chrono::steady_clock::now();
            render (sr, 10.0, p, [] (Engine& e, EngineParams&, double t)
            {
                if (hit (t, 0.0)) for (int k = 0; k < 16; ++k) e.noteOn (36 + k * 3, 100);
            });
            const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            std::printf ("  %-18s %6d Hz  %.1f%% of one core\n", bodyProfile (b).name, sr, secs * 10.0);
        }
}
} // namespace nostrings::test
