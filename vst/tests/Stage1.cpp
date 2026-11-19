// Stage 1: the bowed-string core (arco only): tuning, Helmholtz motion,
// Schelleng map, CPU and denormals.
#include "RenderCommon.h"

namespace nostrings::test
{
void runStage1 (Context& c)
{
    gMeasure = false;
    const EngineParams def;
    auto save = [&c] (const std::string& name, const Take& t) { saveTake (c, "stage1", name, t); };
    // ------------------------------------------------------------ WAV phrases
    std::printf ("\n== Stage 1 renders: bowed string core\n");

    save ("01_open_strings", render (48000, 12.0, def, [] (Engine& e, EngineParams&, double t)
    {
        static const int notes[] = { 36, 43, 50, 57 };          // C2 G2 D3 A3
        for (int k = 0; k < 4; ++k)
        {
            const double on = k * 3.0;
            if (hit (t, on)) e.noteOn (notes[k], 96);
            if (hit (t, on + 2.2)) e.noteOff (notes[k]);
        }
    }));

    save ("02_range_C2_to_A5", render (48000, 14.0, def, [] (Engine& e, EngineParams&, double t)
    {
        static const int notes[] = { 36, 40, 43, 47, 50, 53, 57, 60, 64, 67, 71, 74, 77, 81 };
        for (int j = 0; j < 14; ++j) if (hit (t, j * 1.0 + 0.0)) e.noteOn (notes[j], 90);
        for (int j = 0; j < 14; ++j) if (hit (t, j * 1.0 + 0.8)) e.noteOff (notes[j]);
    }));

    save ("03_pressure_sweep_D3", render (48000, 10.0, def, [] (Engine& e, EngineParams& p, double t)
    {
        if (hit (t, 0.0)) e.noteOn (50, 90);
        p.bowPressure = clampd ((t - 0.5) / 9.0, 0, 1);
        if (hit (t, 9.5)) e.noteOff (50);
    }));

    save ("04_position_sweep_G2", render (48000, 10.0, def, [] (Engine& e, EngineParams& p, double t)
    {
        if (hit (t, 0.0)) e.noteOn (43, 90);
        p.bowPosition = clampd ((t - 0.5) / 9.0, 0, 1);
        if (hit (t, 9.5)) e.noteOff (43);
    }));

    save ("05_speed_sweep_A3", render (48000, 10.0, def, [] (Engine& e, EngineParams& p, double t)
    {
        if (hit (t, 0.0)) e.noteOn (57, 90);
        p.bowSpeed = clampd ((t - 0.5) / 9.0, 0, 1);
        if (hit (t, 9.5)) e.noteOff (57);
    }));

    save ("06_velocity_D3", render (48000, 10.0, def, [] (Engine& e, EngineParams&, double t)
    {
        static const int vels[] = { 20, 50, 80, 110, 127 };
        for (int j = 0; j < 5; ++j) if (hit (t, j * 2.0 + 0.0)) e.noteOn (50, vels[j]);
        for (int j = 0; j < 5; ++j) if (hit (t, j * 2.0 + 1.4)) e.noteOff (50);
    }));

    save ("07_gut_synthetic_steel_C2", render (48000, 9.0, def, [] (Engine& e, EngineParams& p, double t)
    {
        static const int mats[] = { matPlainGut, matSynthetic, matSteel };
        const int k = (int) (t / 3.0);
        if (k < 3) p.material = mats[k];
        for (int j = 0; j < 3; ++j) if (hit (t, j * 3.0 + 0.0)) e.noteOn (36, 90);
        for (int j = 0; j < 3; ++j) if (hit (t, j * 3.0 + 2.0)) e.noteOff (36);
    }));

    for (const int sr : { 44100, 48000, 96000 })
    {
        char name[64];
        std::snprintf (name, sizeof name, "08_samplerate_%d", sr);
        save (name, render (sr, 6.0, def, [] (Engine& e, EngineParams&, double t)
        {
            static const int notes[] = { 36, 50, 69 };
            for (int j = 0; j < 3; ++j) if (hit (t, j * 2.0 + 0.0)) e.noteOn (notes[j], 96);
            for (int j = 0; j < 3; ++j) if (hit (t, j * 2.0 + 1.5)) e.noteOff (notes[j]);
        }));
    }

    gMeasure = true;
    // ------------------------------------------------------------ measurements
    std::printf ("\n== Tuning and Helmholtz motion (steady part of a held note, defaults)\n");
    std::printf ("  note    %10s %10s %10s   (cents / slips per period)\n", "44.1k", "48k", "96k");
    double worstCents = 0;
    for (const int note : { 24, 36, 43, 50, 57, 64, 69, 76, 81, 88, 96 })
    {
        std::printf ("  %-6s", noteName (note));
        for (const int sr : { 44100, 48000, 96000 })
        {
            const auto st = measureNote (note, sr, def);
            if (note >= 36 && note <= 81) worstCents = std::max (worstCents, std::fabs (st.cents));
            if (st.nan) c.ok = false;
            std::printf ("  %+5.1f/%4.2f/%4.2f", st.cents, st.helmholtz, st.settle);
        }
        std::printf ("\n");
    }
    std::printf ("  worst tuning error C2..A5: %.2f cents\n", worstCents);

    std::printf ("\n== Pitch vs pressure, D3 @ 48k (cents)\n  ");
    for (double pr = 0.25; pr <= 0.751; pr += 0.125)
    {
        EngineParams p = def;
        p.bowPressure = pr;
        std::printf (" p%.3f %+5.1f ", pr, measureNote (50, 48000, p).cents);
    }
    std::printf ("\n");

    std::printf ("\n== Schelleng map: slips per period, D3 @ 48k (rows = bow position, cols = pressure)\n");
    std::printf ("  pos\\pr ");
    for (double pr = 0.0; pr <= 1.001; pr += 0.125) std::printf (" %5.3f", pr);
    std::printf ("\n");
    for (double pos = 0.0; pos <= 1.001; pos += 0.25)
    {
        std::printf ("  %5.2f  ", pos);
        for (double pr = 0.0; pr <= 1.001; pr += 0.125)
        {
            EngineParams p = def;
            p.bowPosition = pos; p.bowPressure = pr;
            std::printf (" %5.2f", measureNote (50, 48000, p).helmholtz);
        }
        std::printf ("\n");
    }

    // CPU: 16 voices held for 10 s
    std::printf ("\n== CPU (16 voices held, 10 s of audio)\n");
    for (const int sr : { 44100, 48000, 96000 })
    {
        const auto t0 = std::chrono::steady_clock::now();
        const Take t = render (sr, 10.0, def, [] (Engine& e, EngineParams&, double t)
        {
            if (hit (t, 0.0)) for (int k = 0; k < 16; ++k) e.noteOn (36 + k * 3, 100);
        });
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  %6d Hz: %.2f s to render 10 s  ->  %.1f%% of one core\n", sr, secs, secs * 10.0);
        if (t.nanResets) c.ok = false;
    }

    // Denormals: release a note and keep rendering silence with FTZ OFF (as in a host that doesn't set it)
    std::printf ("\n== Denormal check (no FTZ, 30 s tail after release)\n");
    {
        Engine e;
        e.prepare (48000);
        e.setParams (def);
        e.noteOn (36, 100);
        std::vector<float> L (kChunk), R (kChunk);
        auto block = [&] (int count)
        {
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < count; ++i) e.render (L.data(), R.data(), kChunk);
            return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        };
        block (48000 * 2 / kChunk);
        e.noteOff (36);
        double worst = 0, first = block (48000 / kChunk);
        for (int s = 0; s < 29; ++s) worst = std::max (worst, block (48000 / kChunk));
        std::printf ("  first second after release %.4f s, slowest later second %.4f s, voices still active %d\n", first, worst, e.activeVoices());
        if (worst > first * 4 + 0.01) { std::printf ("  WARNING: tail got slower: possible denormals\n"); c.ok = false; }
    }
}
} // namespace nostrings::test
