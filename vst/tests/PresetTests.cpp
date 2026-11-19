// ============================================================================
//  NoStringsTests: console tests for the preset system (no audio device or
//  window needed), plus the level matcher for the factory bank.
//
//    NoStringsTests                      run the tests
//    NoStringsTests --loudness           measure every factory preset, print "index  dB" lines
//    NoStringsTests --loudness <json>    ...and write the levels into factory_presets.json
//    NoStringsTests --parity <scenarios.json>   render the parity scenarios with the C++ engine (JSON out)
//
//  Build + run:  cmake --build <build> --config Release --target NoStringsTests
// ============================================================================
#include <juce_events/juce_events.h>
#include "PresetManager.h"
#include "Combine.h"

using namespace nostrings;

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

namespace
{
std::array<float, kNumParams> valuesOf (const PresetData& p)
{
    std::array<float, kNumParams> v {};
    for (int i = 0; i < kNumParams; ++i)
        v[(size_t) i] = std::isnan (p.values[(size_t) i]) ? paramTable()[(size_t) i].def : p.values[(size_t) i];
    return v;
}

// Loudest 100 ms window (RMS, dBFS) of a short phrase on the preset, with its level at 0 dB
// and the master at its default: what the preset will sound like once level-matched.
double measureLoudness (const PresetData& preset)
{
    constexpr int sr = 48000, block = 32;
    auto v = valuesOf (preset);
    v[pPresetLevel] = 0;
    v[pReverbSend] = 0;
    Engine e;
    e.prepare (sr);
    e.setParams (makeEngineParams ([&v] (int i) { return v[(size_t) i]; }));
    const int exc = (int) std::lround (v[pExcitation]);
    const bool sustained = exc == excArco || exc == excColLegnoTratto || exc == excHarmonic || exc == excEBow;
    const int notes[] = { exc == excHarmonic ? 69 : 50, exc == excHarmonic ? 76 : 57 };
    std::vector<float> L (block), R (block);
    double best = 0, acc = 0;
    int cnt = 0;
    const int total = sr * 4;
    for (int pos = 0; pos < total; pos += block)
    {
        if (pos == 0) e.noteOn (notes[0], 100);
        if (pos == (int) (1.2 * sr)) e.noteOn (notes[1], 100);
        if (pos == (int) (sustained ? 2.6 * sr : 1.0 * sr)) e.noteOff (notes[0]);
        if (pos == (int) (sustained ? 2.6 * sr : 2.2 * sr)) e.noteOff (notes[1]);
        e.render (L.data(), R.data(), block);
        for (int i = 0; i < block; ++i) acc += 0.5 * ((double) L[(size_t) i] * L[(size_t) i] + (double) R[(size_t) i] * R[(size_t) i]);
        cnt += block;
        if (cnt >= sr / 10) { best = std::max (best, acc / cnt); acc = 0; cnt = 0; }
    }
    return 10.0 * std::log10 (best + 1e-20);
}

// DSP parity: each scenario is rendered in 128-sample blocks at 48 kHz; the result is
// the RMS of 8 equal windows per channel plus the peak, which run-parity.ps1 compares
// with the JS engine's figures.
int runParity (const juce::File& scenarioFile)
{
    const auto json = juce::JSON::parse (scenarioFile);
    if (! json.isArray()) { std::printf ("cannot read %s\n", scenarioFile.getFullPathName().toRawUTF8()); return 2; }
    PresetManager pm;
    juce::Array<juce::var> results;
    constexpr int sr = 48000, block = 128, windows = 8;
    for (const auto& sc : *json.getArray())
    {
        std::array<float, kNumParams> values {};
        for (int i = 0; i < kNumParams; ++i) values[(size_t) i] = paramTable()[(size_t) i].def;
        if (sc.hasProperty ("preset"))
        {
            const PresetData* found = nullptr;
            for (const auto& p : pm.factory()) if (p.category + "/" + p.name == sc["preset"].toString()) found = &p;
            if (found == nullptr) { std::printf ("unknown preset %s\n", sc["preset"].toString().toRawUTF8()); return 2; }
            for (int i = 0; i < kNumParams; ++i) if (i != pMaster && ! std::isnan (found->values[(size_t) i])) values[(size_t) i] = found->values[(size_t) i];
        }
        if (auto* obj = sc["params"].getDynamicObject())
            for (const auto& prop : obj->getProperties())
            {
                const int i = paramIndex (prop.name.toString().toStdString());
                if (i < 0) { std::printf ("unknown param %s\n", prop.name.toString().toRawUTF8()); return 2; }
                values[(size_t) i] = (float) prop.value;
            }
        const int blocks = (int) sc["blocks"];
        const int perWindow = blocks * block / windows;
        std::array<double, windows> sumL {}, sumR {};
        double peak = 0;
        Engine engine;
        engine.prepare (sr);
        std::array<float, block> L {}, R {};
        for (int b = 0; b < blocks; ++b)
        {
            engine.setParams (makeEngineParams ([&values] (int i) { return values[(size_t) i]; }));
            if (auto* evs = sc["events"].getArray())
                for (const auto& ev : *evs)
                {
                    if ((int) ev[0] != b) continue;
                    const auto type = ev[1].toString();
                    if (type == "on")              engine.noteOn ((int) ev[2], (int) ev[3]);
                    else if (type == "off")        engine.noteOff ((int) ev[2]);
                    else if (type == "wheel")      engine.setModWheel ((double) ev[2]);
                    else if (type == "sustain")    engine.setSustain ((int) ev[2] != 0);
                    else if (type == "bend")       engine.setPitchBend ((double) ev[2]);
                    else if (type == "aftertouch") engine.setAftertouch ((double) ev[2]);
                    else if (type == "param")      values[(size_t) paramIndex (ev[2].toString().toStdString())] = (float) ev[3];
                }
            engine.render (L.data(), R.data(), block);
            for (int i = 0; i < block; ++i)
            {
                const int w = std::min (windows - 1, (b * block + i) / perWindow);
                sumL[(size_t) w] += (double) L[(size_t) i] * L[(size_t) i];
                sumR[(size_t) w] += (double) R[(size_t) i] * R[(size_t) i];
                peak = std::max (peak, (double) std::max (std::abs (L[(size_t) i]), std::abs (R[(size_t) i])));
            }
        }
        juce::Array<juce::var> rl, rr;
        for (int w = 0; w < windows; ++w) { rl.add (std::sqrt (sumL[(size_t) w] / perWindow)); rr.add (std::sqrt (sumR[(size_t) w] / perWindow)); }
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", sc["name"]);
        o->setProperty ("rmsL", rl);
        o->setProperty ("rmsR", rr);
        o->setProperty ("peak", peak);
        results.add (juce::var (o));
    }
    std::printf ("%s\n", juce::JSON::toString (juce::var (results), true).toRawUTF8());
    return 0;
}

int runLoudness (const juce::File& jsonToUpdate)
{
    PresetManager pm;
    const auto& f = pm.factory();
    constexpr double targetDb = -16.0;    // loudest 100 ms window at master 0.8
    std::vector<double> levels;
    for (size_t i = 0; i < f.size(); ++i)
    {
        const double loud = measureLoudness (f[i]);
        const double level = juce::jlimit (-18.0, 6.0, std::round ((targetDb - loud) * 2.0) / 2.0);
        levels.push_back (level);
        std::printf ("%2d  %-26s %-13s measured %6.1f dBFS -> level %+5.1f dB\n", (int) i, f[i].name.toRawUTF8(),
                     f[i].category.toRawUTF8(), loud, level);
    }
    if (jsonToUpdate != juce::File())
    {
        // every preset line carries "presetLevel": <n> ; replace it in order
        auto text = jsonToUpdate.loadFileAsString();
        juce::StringArray lines;
        lines.addLines (text);
        size_t k = 0;
        for (auto& line : lines)
        {
            const int at = line.indexOf ("\"presetLevel\":");
            if (at < 0) continue;
            if (k >= levels.size()) { std::printf ("more presetLevel entries than presets\n"); return 1; }
            const int end = line.indexOfAnyOf (",}", at);
            line = line.substring (0, at) + "\"presetLevel\": " + juce::String (levels[k++], 1) + line.substring (end);
        }
        if (k != levels.size()) { std::printf ("found %d presetLevel entries for %d presets\n", (int) k, (int) levels.size()); return 1; }
        jsonToUpdate.replaceWithText (lines.joinIntoString ("\n") + "\n");
        std::printf ("updated %s\n", jsonToUpdate.getFullPathName().toRawUTF8());
    }
    return 0;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init; // keeps JUCE leak checks quiet

    if (argc >= 3 && juce::String (argv[1]) == "--parity")
        return runParity (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[2])));
    if (argc >= 2 && juce::String (argv[1]) == "--loudness")
        return runLoudness (argc >= 3 ? juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[2])) : juce::File());

    PresetManager pm;
    const auto& f = pm.factory();
    std::printf ("factory presets: %d, parameters: %d\n", (int) f.size(), kNumParams);
    CHECK (f.size() == 64);
    CHECK ((int) paramTable().size() == kNumParams);

    // 8 categories x 8, every body 8 times, every value in range, master never stored
    std::map<juce::String, int> perCategory;
    std::array<int, kNumBodies> perBody {};
    for (const auto& p : f)
    {
        ++perCategory[p.category];
        CHECK (! std::isnan (p.values[pBody]) && ! std::isnan (p.values[pExcitation]) && ! std::isnan (p.values[pMaterial]));
        if (! std::isnan (p.values[pBody])) ++perBody[(size_t) p.values[pBody]];
        CHECK (std::isnan (p.values[pMaster]));
        for (int i = 0; i < kNumParams; ++i)
        {
            const float v = p.values[(size_t) i];
            const auto& d = paramTable()[(size_t) i];
            if (! std::isnan (v)) CHECK (v >= d.min && v <= d.max);
        }
    }
    CHECK (perCategory.size() == 8);
    for (const auto& [cat, n] : perCategory) { std::printf ("  %-13s %d\n", cat.toRawUTF8(), n); CHECK (n == 8); }
    for (int b = 0; b < kNumBodies; ++b) { std::printf ("  body %d %-18s %d\n", b, bodyProfile (b).name, perBody[(size_t) b]); CHECK (perBody[(size_t) b] == 8); }
    CHECK (f[0].name == "Concert Cello" && f[0].category == "Classical");
    CHECK (f[8].category == "Baroque" && f[8].values[pMaterial] == (float) matPlainGut);
    CHECK (f[17].name == "EBow Infinite" && f[17].values[pExcitation] == (float) excEBow && bodyProfile ((int) f[17].values[pBody]).electric);

    // names are unique
    juce::StringArray names;
    for (const auto& p : f) { CHECK (! names.contains (p.name)); names.add (p.name); }

    // save -> load round trip
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nostrings-test.nspreset");
    juce::String err;
    for (const size_t src : { (size_t) 1, (size_t) 10, (size_t) 55 })
    {
        PresetData out = f[src];
        out.name = "Round Trip";
        CHECK (PresetManager::writeFile (tmp, out, err));
        PresetData in;
        CHECK (PresetManager::readFile (tmp, in, err));
        CHECK (in.name == "Round Trip");
        for (int i = 0; i < kNumParams; ++i)
        {
            if (i == pMaster) continue;
            const float a = out.values[(size_t) i], b = in.values[(size_t) i];
            if (std::isnan (a)) CHECK (std::isnan (b)); // unset stays unset
            else CHECK (std::abs (b - a) < 1e-4f * std::max (1.0f, std::abs (a)));
        }
    }

    // E-Piano-style file with only some params: the rest stay NaN (-> defaults), values are clamped
    tmp.replaceWithText (R"({"format":"No Strings Preset","version":1,"name":"Partial","category":"User","params":{"body":3,"tension":9,"master":0.1}})");
    PresetData partial;
    CHECK (PresetManager::readFile (tmp, partial, err));
    CHECK (partial.values[pBody] == 3.0f);
    CHECK (partial.values[pTension] == 1.0f);          // clamped to range
    CHECK (std::isnan (partial.values[pMaster]));      // never read
    CHECK (std::isnan (partial.values[pExcitation]));

    // garbage is rejected with a message
    tmp.replaceWithText ("not json");
    PresetData bad;
    CHECK (! PresetManager::readFile (tmp, bad, err) && err.isNotEmpty());
    tmp.replaceWithText (R"({"name":"Empty","params":{}})");
    CHECK (! PresetManager::readFile (tmp, bad, err) && err.isNotEmpty());
    tmp.deleteFile();

    // Combine stays within range and musical limits, and names itself
    {
        Rng rng;
        rng.seed (42);
        for (int trial = 0; trial < 200; ++trial)
        {
            std::array<float, kNumParams> v {};
            for (int i = 0; i < kNumParams; ++i) v[(size_t) i] = paramTable()[(size_t) i].def;
            const auto name = combineRandom (v, rng);
            CHECK (! name.empty());
            for (int i = 0; i < kNumParams; ++i)
            {
                const auto& d = paramTable()[(size_t) i];
                CHECK (v[(size_t) i] >= d.min && v[(size_t) i] <= d.max);
            }
            CHECK (v[pBowPressure] >= 0.35f && v[pBowPressure] <= 0.65f);
            CHECK (v[pMaster] == paramTable()[pMaster].def);
            if (! bodyProfile ((int) v[pBody]).electric) CHECK (v[pAmpDrive] == paramTable()[pAmpDrive].def);
        }
    }

    std::printf ("user preset folder: %s\n", PresetManager::userFolder().getFullPathName().toRawUTF8());
    std::printf (failures == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
