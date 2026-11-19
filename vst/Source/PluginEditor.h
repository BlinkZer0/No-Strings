#pragma once
#include <bitset>
#include "PluginProcessor.h"

// Hosts the HTML/SVG UI (vst/ui, compiled in as BinaryData) in a WebView2 and
// bridges it to the processor:
//   UI -> C++ : uiReady, setParam{id,value}, gesture{id,begin}, noteOn{note,vel},
//               noteOff{note}, panic, modWheel{value}, paramMenu{id,x,y},
//               loadPreset{id}, savePreset{name,overwrite}, deletePreset{id},
//               loadPresetFile, openPresetFolder, combine
//   C++ -> UI : params{id:value,...}, notes{on[],off[]}, stats{voices,peak,sustain,
//               modWheel,excitation}, config{...}, presetList{presets[],folder},
//               preset{id,name,category}, saveResult{...}, message{text}
class NoStringsEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NoStringsEditor (NoStringsProcessor&);
    ~NoStringsEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    juce::WebBrowserComponent::Options makeBrowserOptions();
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    void onUiReady();
    void onSetParam (const juce::var&);
    void onGesture (const juce::var&);
    void sendConfig();
    void sendPresetList();
    void sendPreset();
    void sendMessage (const juce::String& text);
    void showParamMenu (const juce::var&);
    void chooseAndLoadPresetFile();

    NoStringsProcessor& processor;

    std::array<float, nostrings::kNumParams> lastSent {};
    std::array<bool, nostrings::kNumParams> inGesture {};
    std::bitset<128> lastNotes;
    int lastPresetVersion = -1;
    int ticks = 0;
    bool uiReady = false;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::WebBrowserComponent browser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoStringsEditor)
};
