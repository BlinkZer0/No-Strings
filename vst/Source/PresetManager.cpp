#include "PresetManager.h"
#include "BinaryData.h"

namespace nostrings
{
namespace
{
float clampParam (int index, float v)
{
    const auto& d = paramTable()[(size_t) index];
    return juce::jlimit (d.min, d.max, v);
}
} // namespace

PresetManager::PresetManager()
{
    juce::String error;
    parseFactory (juce::String::fromUTF8 (BinaryData::factory_presets_json, BinaryData::factory_presets_jsonSize), factoryPresets, error);
    jassert (error.isEmpty());
    if (factoryPresets.empty()) // never leave the host with zero programs
    {
        PresetData init;
        init.name = "Init";
        init.category = "Factory";
        factoryPresets.push_back (init);
    }
}

//==============================================================================
// Factory file format:
//   { "format": "No Strings Factory Presets", "version": 1,
//     "presets": [ { "name": "...", "category": "...", "params": { "<paramId>": value, ... } }, ... ] }
// Anything not given loads at its default.
bool PresetManager::parseFactory (const juce::String& text, std::vector<PresetData>& out, juce::String& error)
{
    juce::var json;
    if (juce::JSON::parse (text, json).failed() || ! json.isObject()) { error = "factory_presets.json is not valid JSON"; return false; }
    const auto* list = json["presets"].getArray();
    if (list == nullptr) { error = "factory_presets.json has no presets"; return false; }
    for (const auto& p : *list)
    {
        PresetData d;
        d.name = p["name"].toString();
        d.category = p["category"].toString();
        const auto params = p["params"];
        if (d.name.isEmpty() || ! params.isObject()) { error = "a factory preset is missing its name or params"; return false; }
        for (int i = 0; i < kNumParams; ++i)
        {
            const juce::Identifier id (paramTable()[(size_t) i].id);
            if (params.hasProperty (id)) d.values[(size_t) i] = clampParam (i, (float) params[id]);
        }
        out.push_back (std::move (d));
    }
    return true;
}

//==============================================================================
juce::File PresetManager::userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("No Strings")
        .getChildFile ("Presets");
}

juce::String PresetManager::idForFile (const juce::File& f) const
{
    const auto root = userFolder();
    if (! f.isAChildOf (root)) return {};
    return "user:" + f.getRelativePathFrom (root).replaceCharacter ('\\', '/');
}

juce::String PresetManager::categoryForFile (const juce::File& f) const
{
    const auto root = userFolder();
    const auto parent = f.getParentDirectory();
    if (parent == root || ! f.isAChildOf (root)) return "User";
    return parent.getRelativePathFrom (root).replaceCharacter ('\\', '/');
}

bool PresetManager::rescanIfChanged()
{
    const auto root = userFolder();
    root.createDirectory();

    const auto files = root.findChildFiles (juce::File::findFiles, true, juce::String ("*") + fileExtension);

    juce::String sig;
    for (const auto& f : files)
        sig << f.getFullPathName() << '|' << f.getSize() << '|' << f.getLastModificationTime().toMilliseconds() << ';';

    if (scanned && sig == signature) return false;
    scanned = true;
    signature = sig;

    user.clear();
    for (const auto& f : files)
        user.push_back ({ idForFile (f), f.getFileNameWithoutExtension(), categoryForFile (f), f });

    std::sort (user.begin(), user.end(), [] (const PresetEntry& a, const PresetEntry& b)
    {
        if (a.category != b.category) return a.category.compareNatural (b.category) < 0;
        return a.name.compareNatural (b.name) < 0;
    });
    return true;
}

const PresetEntry* PresetManager::findUser (const juce::String& id) const
{
    for (const auto& e : user)
        if (e.id == id) return &e;
    return nullptr;
}

juce::File PresetManager::fileForName (const juce::String& name) const
{
    return userFolder().getChildFile (juce::File::createLegalFileName (name.trim()) + fileExtension);
}

//==============================================================================
// User file format:
//   { "format": "No Strings Preset", "version": 1, "name": "...", "category": "...",
//     "params": { "<paramId>": value, ... } }        (master is never stored)
bool PresetManager::readFile (const juce::File& f, PresetData& d, juce::String& error)
{
    juce::var json;
    if (! f.existsAsFile() || juce::JSON::parse (f.loadFileAsString(), json).failed() || ! json.isObject())
    {
        error = "Not a valid preset file: " + f.getFileName();
        return false;
    }
    const auto params = json["params"];
    int found = 0;
    if (params.isObject())
    {
        for (int i = 0; i < kNumParams; ++i)
        {
            if (i == pMaster) continue;
            const juce::Identifier id (paramTable()[(size_t) i].id);
            if (params.hasProperty (id))
            {
                d.values[(size_t) i] = clampParam (i, (float) params[id]);
                ++found;
            }
        }
    }
    if (found == 0)
    {
        error = "No No Strings settings found in " + f.getFileName();
        return false;
    }
    d.name = json["name"].toString().trim();
    if (d.name.isEmpty()) d.name = f.getFileNameWithoutExtension();
    d.category = json["category"].toString();
    return true;
}

bool PresetManager::writeFile (const juce::File& f, const PresetData& d, juce::String& error)
{
    auto* params = new juce::DynamicObject();
    for (int i = 0; i < kNumParams; ++i)
    {
        if (i == pMaster || std::isnan (d.values[(size_t) i])) continue;
        // round to 6 significant digits so the files stay readable
        const double v = juce::String (d.values[(size_t) i], 6).getDoubleValue();
        params->setProperty (juce::String (paramTable()[(size_t) i].id), v);
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "No Strings Preset");
    root->setProperty ("version", 1);
    root->setProperty ("name", d.name);
    root->setProperty ("category", d.category);
    root->setProperty ("params", juce::var (params));

    if (! f.getParentDirectory().createDirectory() || ! f.replaceWithText (juce::JSON::toString (juce::var (root))))
    {
        error = "Couldn't write " + f.getFullPathName();
        return false;
    }
    return true;
}
} // namespace nostrings
