#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Combine.h"

using namespace nostrings;

NoStringsProcessor::NoStringsProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "NoStrings", createParameterLayout())
{
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& id = paramTable()[(size_t) i].id;
        params[(size_t) i] = apvts.getParameter (id);
        raw[(size_t) i] = apvts.getRawParameterValue (id);
    }
    combineRng.seed ((uint32_t) juce::Time::getMillisecondCounter() ^ 0xC0FFEEu);
    setCurrentProgram (0); // a fresh instance starts on the first factory preset
}

NoStringsProcessor::~NoStringsProcessor() = default;

bool NoStringsProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void NoStringsProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);  // allocates the delay lines (message thread)
    engine.setParams (makeEngineParams ([this] (int i) { return paramValue (i); }));
    scratchR.assign ((size_t) std::max (samplesPerBlock, 1) * 2, 0.0f);
    keyboardState.reset();
}

void NoStringsProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
        engine.noteOn (m.getNoteNumber(), m.getVelocity());
    else if (m.isNoteOff())
        engine.noteOff (m.getNoteNumber());
    else if (m.isAllSoundOff())
        engine.panic();
    else if (m.isAllNotesOff())
        engine.allNotesOff();
    else if (m.isPitchWheel())
        engine.setPitchBend ((m.getPitchWheelValue() - 8192) / 8192.0);
    else if (m.isChannelPressure())
        engine.setAftertouch (m.getChannelPressureValue() / 127.0);
    else if (m.isAftertouch())
        engine.setAftertouch (m.getAfterTouchValue() / 127.0);   // poly aftertouch drives the same (channel-wide) control
    else if (m.isController())
    {
        const double v = m.getControllerValue() / 127.0;
        switch (m.getControllerNumber())
        {
            case 1:  engine.setModWheel (v); break;
            case 2:  engine.setBreath (v); break;
            case 11: engine.setExpression (v); break;
            case 64: engine.setSustain (m.getControllerValue() >= 64); break;
            default: break;
        }
    }
}

void NoStringsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, numSamples);

    // UI keyboard notes in; every sounding note (host + UI) is reflected back for the display
    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    engine.setParams (makeEngineParams ([this] (int i) { return paramValue (i); }));
    if (panicRequested.exchange (false)) engine.panic();
    if (const float w = uiModWheel.exchange (-1.0f); w >= 0.0f) engine.setModWheel (w);

    float* L = buffer.getWritePointer (0);
    float* R = nullptr;
    if (buffer.getNumChannels() > 1) R = buffer.getWritePointer (1);
    else
    {
        if ((int) scratchR.size() < numSamples) scratchR.resize ((size_t) numSamples); // mono host, rare
        R = scratchR.data();
    }

    // MIDI events land at their exact sample
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, numSamples, meta.samplePosition);
        if (at > pos) { engine.render (L + pos, R + pos, at - pos); pos = at; }
        handleMidi (meta.getMessage());
    }
    if (pos < numSamples) engine.render (L + pos, R + pos, numSamples - pos);

    statVoices.store (engine.activeVoices());
    statSustain.store (engine.sustainDown());
    statModWheel.store ((float) engine.modWheelValue());
    statExcitation.store (engine.currentExcitation());
    const float pk = engine.takePeak();
    if (pk > statPeak.load()) statPeak.store (pk);
}

//==============================================================================
void NoStringsProcessor::applyPreset (const PresetData& preset)
{
    for (int i = 0; i < kNumParams; ++i)
    {
        if (i == pMaster) continue;
        const float stored = preset.values[(size_t) i];
        const float v = std::isnan (stored) ? paramTable()[(size_t) i].def : stored;
        auto* p = params[(size_t) i];
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

void NoStringsProcessor::setPresetInfo (const juce::String& id, const juce::String& name, const juce::String& category)
{
    {
        const juce::ScopedLock sl (presetLock);
        presetInfo = { id, name, category };
    }
    ++presetVersion;
}

NoStringsProcessor::PresetInfo NoStringsProcessor::currentPreset() const
{
    const juce::ScopedLock sl (presetLock);
    return presetInfo;
}

void NoStringsProcessor::setCurrentProgram (int index)
{
    index = juce::jlimit (0, getNumPrograms() - 1, index);
    currentProgram = index;
    const auto& preset = presetManager.factory()[(size_t) index];
    applyPreset (preset);
    setPresetInfo ("factory:" + juce::String (index), preset.name, preset.category);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

const juce::String NoStringsProcessor::getProgramName (int index)
{
    if (index < 0 || index >= getNumPrograms()) return {};
    const auto& p = presetManager.factory()[(size_t) index];
    return p.category + " - " + p.name;
}

bool NoStringsProcessor::loadPresetById (const juce::String& id, juce::String& error)
{
    if (id.startsWith ("factory:"))
    {
        const int index = id.fromFirstOccurrenceOf (":", false, false).getIntValue();
        if (index < 0 || index >= getNumPrograms()) { error = "Unknown preset"; return false; }
        setCurrentProgram (index);
        return true;
    }

    presetManager.rescanIfChanged();
    if (const auto* entry = presetManager.findUser (id))
    {
        PresetData data;
        if (! PresetManager::readFile (entry->file, data, error)) return false;
        applyPreset (data);
        setPresetInfo (entry->id, entry->name, entry->category);
        return true;
    }
    error = "Preset not found (was it moved or deleted?)";
    return false;
}

bool NoStringsProcessor::loadPresetFile (const juce::File& file, juce::String& error)
{
    PresetData data;
    if (! PresetManager::readFile (file, data, error)) return false;
    applyPreset (data);
    const auto userId = presetManager.idForFile (file);
    if (userId.isNotEmpty())
        setPresetInfo (userId, file.getFileNameWithoutExtension(), presetManager.categoryForFile (file));
    else
        setPresetInfo ("file:" + file.getFullPathName(), data.name, "File");
    return true;
}

bool NoStringsProcessor::saveUserPreset (const juce::String& rawName, bool overwrite, bool& alreadyExists, juce::String& error)
{
    alreadyExists = false;
    const auto name = rawName.trim();
    if (name.isEmpty()) { error = "Enter a name for the preset"; return false; }

    const auto file = presetManager.fileForName (name);
    if (file.existsAsFile() && ! overwrite) { alreadyExists = true; return false; }

    PresetData data;
    data.name = name;
    data.category = "User";
    for (int i = 0; i < kNumParams; ++i)
        if (i != pMaster) data.values[(size_t) i] = paramValue (i);

    if (! PresetManager::writeFile (file, data, error)) return false;
    presetManager.rescanIfChanged();
    setPresetInfo (presetManager.idForFile (file), file.getFileNameWithoutExtension(), presetManager.categoryForFile (file));
    return true;
}

bool NoStringsProcessor::deleteUserPreset (const juce::String& id, juce::String& error)
{
    presetManager.rescanIfChanged();
    const auto* entry = presetManager.findUser (id);
    if (entry == nullptr) { error = "Only user presets can be deleted"; return false; }
    const auto file = entry->file;
    if (! file.moveToTrash() && ! file.deleteFile()) { error = "Couldn't delete " + file.getFullPathName(); return false; }
    presetManager.rescanIfChanged();
    if (currentPreset().id == id)                       // keep the sound, drop the link to the file
        setPresetInfo ({}, currentPreset().name, "User");
    return true;
}

juce::String NoStringsProcessor::combine()
{
    std::array<float, kNumParams> v {};
    for (int i = 0; i < kNumParams; ++i) v[(size_t) i] = paramValue (i);
    const auto name = juce::String::fromUTF8 (combineRandom (v, combineRng).c_str());
    PresetData data;
    for (int i = 0; i < kNumParams; ++i) data.values[(size_t) i] = v[(size_t) i];
    applyPreset (data);
    setPresetInfo ({}, name, "Combine");
    return name;
}

//==============================================================================
void NoStringsProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    const auto info = currentPreset();
    state.setProperty ("program", currentProgram.load(), nullptr);
    state.setProperty ("presetId", info.id, nullptr);
    state.setProperty ("presetName", info.name, nullptr);
    state.setProperty ("presetCategory", info.category, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void NoStringsProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) state.getProperty ("program", 0));
            const auto name = state.getProperty ("presetName").toString();
            if (name.isEmpty()) // project saved by an older build: fall back to its program
            {
                const auto& p = presetManager.factory()[(size_t) currentProgram.load()];
                setPresetInfo ("factory:" + juce::String (currentProgram.load()), p.name, p.category);
            }
            else
            {
                setPresetInfo (state.getProperty ("presetId").toString(), name, state.getProperty ("presetCategory").toString());
            }
            apvts.replaceState (state);
        }
    }
}

juce::AudioProcessorEditor* NoStringsProcessor::createEditor() { return new NoStringsEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NoStringsProcessor(); }
