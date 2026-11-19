#include "PluginEditor.h"
#include "BinaryData.h"

using namespace nostrings;

namespace
{
const juce::Colour kBackground { 0xff1a110a };

const char* mimeForExtension (const juce::String& ext)
{
    if (ext == "html") return "text/html";
    if (ext == "js")   return "text/javascript";
    if (ext == "css")  return "text/css";
    if (ext == "svg")  return "image/svg+xml";
    if (ext == "json") return "application/json";
    if (ext == "png")  return "image/png";
    return "application/octet-stream";
}

std::vector<std::byte> toBytes (const void* data, size_t size)
{
    const auto* p = static_cast<const std::byte*> (data);
    return { p, p + size };
}

juce::var makeObject (std::initializer_list<std::pair<const char*, juce::var>> props)
{
    auto* obj = new juce::DynamicObject();
    for (const auto& [k, v] : props) obj->setProperty (k, v);
    return juce::var (obj);
}
} // namespace

//==============================================================================
NoStringsEditor::NoStringsEditor (NoStringsProcessor& p)
    : AudioProcessorEditor (p), processor (p), browser (makeBrowserOptions())
{
    lastSent.fill (std::numeric_limits<float>::quiet_NaN());

    addAndMakeVisible (browser);
    browser.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    setResizable (true, true);
    setResizeLimits (720, 560, 2600, 2200);
    setSize (1180, 900);
    startTimerHz (30);
}

NoStringsEditor::~NoStringsEditor()
{
    stopTimer();
    for (int i = 0; i < kNumParams; ++i)
        if (inGesture[(size_t) i]) processor.param (i)->endChangeGesture();
}

void NoStringsEditor::paint (juce::Graphics& g) { g.fillAll (kBackground); }
void NoStringsEditor::resized() { browser.setBounds (getLocalBounds()); }

//==============================================================================
juce::WebBrowserComponent::Options NoStringsEditor::makeBrowserOptions()
{
    using Opts = juce::WebBrowserComponent::Options;
    const auto userData = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("NoStringsVST-WebView2");

    return Opts{}
        .withBackend (Opts::Backend::webview2)
        .withWinWebView2Options (Opts::WinWebView2{}
                                     .withUserDataFolder (userData)
                                     .withBackgroundColour (kBackground)
                                     .withStatusBarDisabled())
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([this] (const juce::String& url) { return getResource (url); })
        .withEventListener ("uiReady",  [this] (const juce::var&) { onUiReady(); })
        .withEventListener ("setParam", [this] (const juce::var& e) { onSetParam (e); })
        .withEventListener ("gesture",  [this] (const juce::var& e) { onGesture (e); })
        .withEventListener ("noteOn", [this] (const juce::var& e)
        {
            const int note = juce::jlimit (0, 127, (int) e["note"]);
            const float vel = juce::jlimit (1, 127, (int) e["vel"]) / 127.0f;
            processor.keyboardState.noteOn (1, note, vel);
        })
        .withEventListener ("noteOff", [this] (const juce::var& e)
        {
            processor.keyboardState.noteOff (1, juce::jlimit (0, 127, (int) e["note"]), 0.0f);
        })
        .withEventListener ("panic", [this] (const juce::var&)
        {
            processor.keyboardState.allNotesOff (0);
            processor.requestPanic();
        })
        .withEventListener ("modWheel", [this] (const juce::var& e) { processor.setUiModWheel ((float) e["value"]); })
        .withEventListener ("paramMenu", [this] (const juce::var& e) { showParamMenu (e); })
        .withEventListener ("loadPreset", [this] (const juce::var& e)
        {
            juce::String error;
            if (! processor.loadPresetById (e["id"].toString(), error)) sendMessage (error);
            sendPreset();
        })
        .withEventListener ("savePreset", [this] (const juce::var& e)
        {
            const auto name = e["name"].toString().trim();
            bool exists = false;
            juce::String error;
            const bool ok = processor.saveUserPreset (name, (bool) e["overwrite"], exists, error);
            browser.emitEventIfBrowserIsVisible ("saveResult", makeObject ({
                { "ok", ok }, { "exists", exists }, { "name", name }, { "error", error } }));
            if (ok) { sendPresetList(); sendPreset(); }
        })
        .withEventListener ("deletePreset", [this] (const juce::var& e)
        {
            juce::String error;
            if (processor.deleteUserPreset (e["id"].toString(), error)) sendMessage ("Preset moved to the Recycle Bin");
            else sendMessage (error);
            sendPresetList();
            sendPreset();
        })
        .withEventListener ("loadPresetFile", [this] (const juce::var&) { chooseAndLoadPresetFile(); })
        .withEventListener ("openPresetFolder", [] (const juce::var&)
        {
            const auto folder = PresetManager::userFolder();
            folder.createDirectory();
            folder.startAsProcess();
        })
        .withEventListener ("combine", [this] (const juce::var&)
        {
            sendMessage ("Combined: " + processor.combine());
            sendPreset();
        });
}

void NoStringsEditor::showParamMenu (const juce::var& e)
{
    const int i = paramIndex (e["id"].toString().toStdString());
    if (i < 0) return;
    auto* p = processor.param (i);
    const juce::Point<int> pos ((int) e["x"], (int) e["y"]);

    if (auto* host = getHostContext())
        if (auto menu = host->getContextMenuForParameter (p))
        {
            menu->showNativeMenu (pos);
            return;
        }

    juce::PopupMenu m;
    m.addSectionHeader (p->getName (64) + ": " + p->getCurrentValueAsText());
    m.addItem (1, "Reset to default");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                .withTargetScreenArea (localAreaToGlobal (juce::Rectangle<int> (pos.x, pos.y, 1, 1))),
                     [safeThis = juce::Component::SafePointer<NoStringsEditor> (this), i] (int result)
                     {
                         if (safeThis == nullptr || result != 1) return;
                         auto* param = safeThis->processor.param (i);
                         param->beginChangeGesture();
                         param->setValueNotifyingHost (param->getDefaultValue());
                         param->endChangeGesture();
                     });
}

void NoStringsEditor::sendMessage (const juce::String& text)
{
    browser.emitEventIfBrowserIsVisible ("message", makeObject ({ { "text", text } }));
}

void NoStringsEditor::chooseAndLoadPresetFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load No Strings preset", PresetManager::userFolder(),
                                                        juce::String ("*") + PresetManager::fileExtension);
    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync (chooserFlags, [safeThis = juce::Component::SafePointer<NoStringsEditor> (this)] (const juce::FileChooser& fc)
    {
        if (safeThis == nullptr) return;
        const auto file = fc.getResult();
        if (file == juce::File()) return; // cancelled
        juce::String error;
        if (safeThis->processor.loadPresetFile (file, error)) safeThis->sendMessage ("Loaded " + file.getFileName());
        else safeThis->sendMessage (error);
        safeThis->sendPreset();
    });
}

void NoStringsEditor::sendPresetList()
{
    auto& pm = processor.presetManager;
    pm.rescanIfChanged();

    juce::Array<juce::var> list;
    const auto& factory = pm.factory();
    for (size_t i = 0; i < factory.size(); ++i)
        list.add (makeObject ({ { "id", "factory:" + juce::String ((int) i) }, { "name", factory[i].name },
                                { "category", factory[i].category }, { "factory", true } }));
    for (const auto& e : pm.userPresets())
        list.add (makeObject ({ { "id", e.id }, { "name", e.name }, { "category", e.category }, { "factory", false } }));

    browser.emitEventIfBrowserIsVisible ("presetList", makeObject ({
        { "presets", list }, { "folder", PresetManager::userFolder().getFullPathName() } }));
}

void NoStringsEditor::sendPreset()
{
    const auto info = processor.currentPreset();
    lastPresetVersion = processor.presetVersion.load();
    browser.emitEventIfBrowserIsVisible ("preset", makeObject ({
        { "id", info.id }, { "name", info.name }, { "category", info.category } }));
}

std::optional<juce::WebBrowserComponent::Resource> NoStringsEditor::getResource (const juce::String& url)
{
    auto path = url.upToFirstOccurrenceOf ("?", false, false).trimCharactersAtStart ("/");
    if (path.isEmpty()) path = "index.html";
    const auto fileName = path.fromLastOccurrenceOf ("/", false, false);
    const auto mime = mimeForExtension (fileName.fromLastOccurrenceOf (".", false, false).toLowerCase());

    // Dev mode: set NOSTRINGS_UI_DIR to the vst/ui folder to load the UI from disk
    // (edit HTML/CSS/JS and just reopen the editor, no rebuild needed).
    const auto devDir = juce::SystemStats::getEnvironmentVariable ("NOSTRINGS_UI_DIR", {});
    if (devDir.isNotEmpty())
    {
        const auto f = juce::File (devDir).getChildFile (path);
        juce::MemoryBlock mb;
        if (f.existsAsFile() && f.loadFileAsData (mb))
            return juce::WebBrowserComponent::Resource { toBytes (mb.getData(), mb.getSize()), mime };
    }

    // Embedded UI. BinaryData is keyed by file name (all UI file names are unique).
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        if (fileName == BinaryData::originalFilenames[i])
        {
            int size = 0;
            if (const auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size))
                return juce::WebBrowserComponent::Resource { toBytes (data, (size_t) size), mime };
        }
    }
    return std::nullopt;
}

//==============================================================================
void NoStringsEditor::onUiReady()
{
    uiReady = true;
    lastSent.fill (std::numeric_limits<float>::quiet_NaN()); // force a full resend
    lastNotes.reset();
    sendConfig();
    sendPresetList();
    sendPreset();
    timerCallback();
}

void NoStringsEditor::onSetParam (const juce::var& e)
{
    const int i = paramIndex (e["id"].toString().toStdString());
    if (i < 0) return;
    auto* p = processor.param (i);
    const float v = (float) (double) e["value"];
    const bool wrap = ! inGesture[(size_t) i];   // wheel / keys / double-click: one-shot gesture
    if (wrap) p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (v));
    if (wrap) p->endChangeGesture();
    lastSent[(size_t) i] = processor.paramValue (i); // don't echo it back to the UI
}

void NoStringsEditor::onGesture (const juce::var& e)
{
    const int i = paramIndex (e["id"].toString().toStdString());
    if (i < 0) return;
    const bool begin = (bool) e["begin"];
    if (begin == inGesture[(size_t) i]) return;
    inGesture[(size_t) i] = begin;
    if (begin) processor.param (i)->beginChangeGesture();
    else       processor.param (i)->endChangeGesture();
}

void NoStringsEditor::sendConfig()
{
    const auto wrapper = juce::String (juce::AudioProcessor::getWrapperTypeDescription (processor.wrapperType));
    browser.emitEventIfBrowserIsVisible ("config", makeObject ({
        { "sampleRate", processor.getSampleRate() },
        { "blockSize",  processor.getBlockSize() },
        { "wrapper",    wrapper },
        { "voices",     kNumVoices },
    }));
}

void NoStringsEditor::timerCallback()
{
    if (! uiReady) return;

    auto* changed = new juce::DynamicObject();
    for (int i = 0; i < kNumParams; ++i)
    {
        const float v = processor.paramValue (i);
        if (v != lastSent[(size_t) i]) // NaN != anything -> always sent after uiReady
        {
            changed->setProperty (juce::String (paramTable()[(size_t) i].id), v);
            lastSent[(size_t) i] = v;
        }
    }
    juce::var changedVar (changed);
    if (changed->getProperties().size() > 0)
        browser.emitEventIfBrowserIsVisible ("params", changedVar);

    juce::Array<juce::var> on, off;
    for (int n = 0; n < 128; ++n)
    {
        const bool down = processor.keyboardState.isNoteOnForChannels (0xffff, n);
        if (down != lastNotes[(size_t) n])
        {
            lastNotes[(size_t) n] = down;
            (down ? on : off).add (n);
        }
    }
    if (! on.isEmpty() || ! off.isEmpty())
        browser.emitEventIfBrowserIsVisible ("notes", makeObject ({ { "on", on }, { "off", off } }));

    // Preset changed outside the UI (host program change, project reload)
    if (processor.presetVersion.load() != lastPresetVersion)
        sendPreset();

    // Autoload: pick up preset files added / removed / edited in the preset folder (every ~2 s)
    if (++ticks % 60 == 0 && processor.presetManager.rescanIfChanged())
        sendPresetList();

    browser.emitEventIfBrowserIsVisible ("stats", makeObject ({
        { "voices",     processor.statVoices.load() },
        { "peak",       processor.statPeak.exchange (0.0f) },
        { "sustain",    processor.statSustain.load() },
        { "modWheel",   processor.statModWheel.load() },
        { "excitation", processor.statExcitation.load() },
    }));
}
