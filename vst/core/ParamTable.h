#pragma once
// ============================================================================
//  The parameter table: one list that the plugin (Source/Params.h), the
//  console tools and the generated UI table (ui/js/params.js, written by
//  NoStringsGenParams) all read, so they can never drift apart.
//
//  New parameters are only ever APPENDED (indices and IDs of existing ones
//  never change) and default to a neutral value, so older projects and
//  presets load and sound as before.
// ============================================================================
#include <string>
#include <vector>
#include "Engine.h"

namespace nostrings
{
enum ParamIndex
{
    pExcitation, pBowPressure, pBowSpeed, pBowPosition, pHarmonic, pMalletHardness, pEbowMode,
    pMaterial, pTension, pAge, pReleaseTime,
    pBody, pAmpDrive, pAmpBass, pAmpMid, pAmpTreble, pAmpCab, pWidth,
    pSympOn, pSympAmount, pSympCount, pSympMode, pSympKey, pSympScale, pSympDecay, pSympBright,
    pSympNote1, pSympNoteLast = pSympNote1 + kMaxSympathetic - 1,
    pVoiceMode, pPortaTime, pVibRate, pVibDepth, pVibDelay, pVibSource, pSwellSource, pKeyswitchOctave,
    pReleaseNoise, pBowNoise, pBendRange,
    pMaster,
    pReverbSend, pReverbDecay,
    pPresetLevel,
    kNumParams
};

enum class ParamKind { Float, Choice, Int };

struct ParamInfo
{
    std::string id, name, group;
    ParamKind kind;
    float min, max, def;
    std::string unit;      // "", "s", "Hz", "ct", "st", "dB", "%"
    bool logSkew = false;
    bool noPreset = false; // not stored in presets (Master)
    std::vector<std::string> choices; // Choice only
};

inline std::vector<std::string> namesOf (const char* (*name) (int), int count)
{
    std::vector<std::string> v;
    for (int i = 0; i < count; ++i) v.push_back (name (i));
    return v;
}

inline const std::vector<ParamInfo>& paramTable()
{
    static const std::vector<ParamInfo> table = []
    {
        std::vector<ParamInfo> t;
        auto f = [&t] (const char* id, const char* name, const char* group, float min, float max, float def, const char* unit, bool log = false)
        { t.push_back ({ id, name, group, ParamKind::Float, min, max, def, unit, log, false, {} }); };
        auto c = [&t] (const char* id, const char* name, const char* group, std::vector<std::string> choices, int def)
        { t.push_back ({ id, name, group, ParamKind::Choice, 0, (float) choices.size() - 1, (float) def, "", false, false, std::move (choices) }); };
        auto i = [&t] (const char* id, const char* name, const char* group, int min, int max, int def, const char* unit = "")
        { t.push_back ({ id, name, group, ParamKind::Int, (float) min, (float) max, (float) def, unit, false, false, {} }); };

        std::vector<std::string> bodies, materials, keys;
        for (int b = 0; b < kNumBodies; ++b) bodies.push_back (bodyProfile (b).name);
        for (int m = 0; m < kNumMaterials; ++m) materials.push_back (materialProfile (m).name);
        for (int k = 0; k < 12; ++k) keys.push_back (keyName (k));

        c ("excitation",     "Excitation",      "excite", namesOf (excitationName, kNumExcitations), excArco);
        f ("bowPressure",    "Pressure",        "excite", 0, 1, 0.5f, "%");
        f ("bowSpeed",       "Speed",           "excite", 0, 1, 0.5f, "%");
        f ("bowPosition",    "Position",        "excite", 0, 1, 0.5f, "%");
        i ("harmonic",       "Harmonic",        "excite", 2, 5, 2);
        f ("malletHardness", "Hardness",        "excite", 0, 1, 0.5f, "%");
        c ("ebowMode",       "EBow Mode",       "excite", { "Fundamental", "Harmonic" }, 0);
        c ("material",       "Material",        "strings", materials, matSynthetic);
        f ("tension",        "Tension",         "strings", 0, 1, 0.5f, "%");
        f ("age",            "String Age",      "strings", 0, 1, 0.25f, "%");
        f ("releaseTime",    "Release",         "strings", 0.05f, 5, 0.5f, "s", true);
        c ("body",           "Body",            "body", bodies, bodyModern);
        f ("ampDrive",       "Drive",           "amp", 0, 1, 0.25f, "%");
        f ("ampBass",        "Bass",            "amp", 0, 1, 0.5f, "%");
        f ("ampMid",         "Mid",             "amp", 0, 1, 0.5f, "%");
        f ("ampTreble",      "Treble",          "amp", 0, 1, 0.5f, "%");
        c ("ampCab",         "Cab",             "amp", { "Off", "On" }, 1);
        f ("width",          "Width",           "output", 0, 2, 1, "%");
        c ("sympOn",         "Sympathetic",     "symp", { "Off", "On" }, 0);
        f ("sympAmount",     "Amount",          "symp", 0, 1, 0.5f, "%");
        i ("sympCount",      "Strings",         "symp", 4, kMaxSympathetic, 8);
        c ("sympMode",       "Tuning",          "symp", namesOf (sympModeName, kNumSympModes), sympOpenStrings);
        c ("sympKey",        "Key",             "symp", keys, 2);
        c ("sympScale",      "Scale",           "symp", namesOf (scaleName, kNumScales), scaleMajor);
        f ("sympDecay",      "Decay",           "symp", 0.2f, 30, 6, "s", true);
        f ("sympBright",     "Brightness",      "symp", 0, 1, 0.5f, "%");
        static const int customDefaults[kMaxSympathetic] = { 38, 45, 50, 57, 62, 69, 74, 81, 43, 55, 67, 79 };
        for (int k = 0; k < kMaxSympathetic; ++k)
        {
            static std::string ids[kMaxSympathetic], names[kMaxSympathetic];
            ids[k] = "sympNote" + std::to_string (k + 1);
            names[k] = "String " + std::to_string (k + 1);
            i (ids[k].c_str(), names[k].c_str(), "symp", 24, 108, customDefaults[k], "note");
        }
        c ("voiceMode",      "Voice Mode",      "play", namesOf (voiceModeName, kNumVoiceModes), modePoly);
        f ("portaTime",      "Portamento",      "play", 0, 1, 0.12f, "s");
        f ("vibRate",        "Vib Rate",        "play", 2, 9, 5.5f, "Hz");
        f ("vibDepth",       "Vib Depth",       "play", 0, 60, 18, "ct");
        f ("vibDelay",       "Vib Delay",       "play", 0, 2, 0.35f, "s");
        c ("vibSource",      "Vibrato",         "play", namesOf (vibSourceName, kNumVibSources), vibAuto);
        c ("swellSource",    "Swell",           "play", namesOf (swellSourceName, kNumSwellSources), swellModWheel);
        c ("keyswitchOctave","Keyswitches",     "play", { "Off", "C-1", "C0", "C1", "C2" }, 2);
        f ("releaseNoise",   "Release Noise",   "play", 0, 1, 0.3f, "%");
        f ("bowNoise",       "Bow Noise",       "play", 0, 1, 0.3f, "%");
        i ("bendRange",      "Bend Range",      "play", 0, 12, 2, "st");
        f ("master",         "Master",          "output", 0, 1, 0.8f, "%");
        t.back().noPreset = true;
        f ("reverbSend",     "Reverb",          "output", 0, 1, 0.2f, "%");
        f ("reverbDecay",    "Rev Decay",       "output", 0.5f, 8, 2.4f, "s", true);
        f ("presetLevel",    "Level",           "output", -18, 6, 0, "dB");   // the preset's own level (level-matched factory presets)
        return t;
    }();
    return table;
}

inline int paramIndex (const std::string& id)
{
    const auto& t = paramTable();
    for (size_t i = 0; i < t.size(); ++i) if (t[i].id == id) return (int) i;
    return -1;
}

// raw parameter values (indexed by ParamIndex) -> engine snapshot
template <typename GetValue> // float (int paramIndex)
EngineParams makeEngineParams (GetValue&& get)
{
    auto choice = [&get] (int index, int maxIndex) { return std::min (std::max ((int) std::lround (get (index)), 0), maxIndex); };
    EngineParams p;
    p.excitation     = choice (pExcitation, kNumExcitations - 1);
    p.bowPressure    = get (pBowPressure);
    p.bowSpeed       = get (pBowSpeed);
    p.bowPosition    = get (pBowPosition);
    p.harmonic       = std::min (std::max ((int) std::lround (get (pHarmonic)), 2), 5);
    p.malletHardness = get (pMalletHardness);
    p.ebowMode       = choice (pEbowMode, 1);
    p.material       = choice (pMaterial, kNumMaterials - 1);
    p.tension        = get (pTension);
    p.age            = get (pAge);
    p.releaseTime    = get (pReleaseTime);
    p.body           = choice (pBody, kNumBodies - 1);
    p.ampDrive       = get (pAmpDrive);
    p.ampBass        = get (pAmpBass);
    p.ampMid         = get (pAmpMid);
    p.ampTreble      = get (pAmpTreble);
    p.ampCab         = choice (pAmpCab, 1) == 1;
    p.width          = get (pWidth);
    p.symp.on         = choice (pSympOn, 1) == 1;
    p.symp.amount     = get (pSympAmount);
    p.symp.count      = std::min (std::max ((int) std::lround (get (pSympCount)), 4), kMaxSympathetic);
    p.symp.mode       = choice (pSympMode, kNumSympModes - 1);
    p.symp.key        = choice (pSympKey, 11);
    p.symp.scale      = choice (pSympScale, kNumScales - 1);
    p.symp.decay      = get (pSympDecay);
    p.symp.brightness = get (pSympBright);
    for (int i = 0; i < kMaxSympathetic; ++i) p.symp.custom[i] = std::min (std::max ((int) std::lround (get (pSympNote1 + i)), 24), 108);
    p.voiceMode     = choice (pVoiceMode, kNumVoiceModes - 1);
    p.portaTime     = get (pPortaTime);
    p.vibRate       = get (pVibRate);
    p.vibDepth      = get (pVibDepth);
    p.vibDelay      = get (pVibDelay);
    p.vibSource     = choice (pVibSource, kNumVibSources - 1);
    p.swellSource   = choice (pSwellSource, kNumSwellSources - 1);
    const int ks    = choice (pKeyswitchOctave, 4);
    p.keyswitchBase = ks == 0 ? -1 : (ks - 1) * 12;   // C-1 = MIDI 0, C0 = 12, ...
    p.releaseNoise  = get (pReleaseNoise);
    p.bowNoise      = get (pBowNoise);
    p.bendRange     = get (pBendRange);
    p.master        = get (pMaster);
    p.reverbSend    = get (pReverbSend);
    p.reverbDecay   = get (pReverbDecay);
    p.presetGain    = dbToGain (get (pPresetLevel));
    return p;
}
} // namespace nostrings
