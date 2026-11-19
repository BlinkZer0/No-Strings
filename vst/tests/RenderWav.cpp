// ============================================================================
//  NoStringsRender: renders test phrases to WAV and measures the DSP core.
//  Core only (no JUCE), so it rebuilds in seconds.
//
//    NoStringsRender [--out <dir>] [--stage N] [--only <substring>] [--no-wav]
//
//  Writes <out>/stageN/*.wav and prints a measurement report per stage.
// ============================================================================
#include "RenderCommon.h"

namespace nostrings::test
{
void runStage1 (Context&);
void runStage2 (Context&);
void runStage3 (Context&);
void runStage4 (Context&);
void runStage5 (Context&);
void runStage6 (Context&);
}

int main (int argc, char** argv)
{
    using namespace nostrings::test;
    Context c;
    int stage = 0; // 0 = all
    for (int i = 1; i < argc; ++i)
    {
        if (! std::strcmp (argv[i], "--out") && i + 1 < argc) c.outDir = argv[++i];
        else if (! std::strcmp (argv[i], "--only") && i + 1 < argc) c.only = argv[++i];
        else if (! std::strcmp (argv[i], "--stage") && i + 1 < argc) stage = std::atoi (argv[++i]);
        else if (! std::strcmp (argv[i], "--no-wav")) c.wav = false;
    }
    if (stage == 0 || stage == 1) runStage1 (c);
    if (stage == 0 || stage == 2) runStage2 (c);
    if (stage == 0 || stage == 3) runStage3 (c);
    if (stage == 0 || stage == 4) runStage4 (c);
    if (stage == 0 || stage == 5) runStage5 (c);
    if (stage == 0 || stage == 6) runStage6 (c);
    std::printf ("\n%s\n", c.ok ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return c.ok ? 0 : 1;
}
