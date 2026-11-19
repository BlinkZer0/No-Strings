#pragma once
// ============================================================================
//  Presets
//   - 64 factory presets (8 per body, 8 categories), compiled in from
//     presets/factory_presets.json; also exposed to the host as programs
//   - user presets: *.nspreset JSON files in Documents\No Strings\Presets
//     (shared by every DAW / instance; sub-folders become categories,
//      the folder is re-scanned automatically)
//  Same file format as E-Piano's .epreset: { format, version, name, category,
//  params: { id: value } }. Anything a file leaves out loads at its default.
// ============================================================================
#include <juce_core/juce_core.h>
#include <vector>
#include "ParamTable.h"

namespace nostrings
{
struct PresetData
{
    juce::String name, category;
    std::array<float, kNumParams> values; // NaN = not in preset -> parameter default

    PresetData() { values.fill (NAN); }
};

struct PresetEntry
{
    juce::String id;        // "user:<relative/path.nspreset>"
    juce::String name, category;
    juce::File file;
};

class PresetManager
{
public:
    PresetManager();

    static juce::File userFolder();
    static constexpr const char* fileExtension = ".nspreset";

    // Factory (immutable, any thread)
    const std::vector<PresetData>& factory() const { return factoryPresets; }

    // User library (message thread only)
    bool rescanIfChanged();                     // true if the list changed
    const std::vector<PresetEntry>& userPresets() const { return user; }
    const PresetEntry* findUser (const juce::String& id) const;
    juce::File fileForName (const juce::String& name) const;
    juce::String idForFile (const juce::File& f) const;  // "" if not inside the user folder
    juce::String categoryForFile (const juce::File& f) const;

    static bool readFile (const juce::File&, PresetData&, juce::String& error);
    static bool writeFile (const juce::File&, const PresetData&, juce::String& error);
    static bool parseFactory (const juce::String& json, std::vector<PresetData>& out, juce::String& error);

private:
    std::vector<PresetData> factoryPresets;
    std::vector<PresetEntry> user;
    juce::String signature;
    bool scanned = false;
};
} // namespace nostrings
