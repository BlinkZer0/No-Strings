# No Strings — v1.0

A physically modelled cello. No samples: every sound comes from a digital-waveguide string driven by a stick-slip bow model, played nine ways through eight bodies.

No Strings comes in two builds that share the same sound engine, controls and presets:

| Build | Where | What it is |
|---|---|---|
| **VST3 plugin** | [`vst/`](vst/) | C++ / JUCE 8 instrument plugin for any VST3 DAW, plus a Standalone app |
| **Web build** | [`web/index.html`](https://blinkzer0.github.io/No-Strings/) | One self-contained HTML file that runs in Chrome or Edge with Web MIDI |

## What it can do

**Sound engine (physical model, no samples)**
- A digital-waveguide string per voice (16 voices): fractional delay lines split at the contact point, frequency-dependent loss solved per note, stiffness dispersion fitted to a physical inharmonicity coefficient, a light-touch point for natural harmonics, and a bridge end loaded by the body's strongest modes (the wolf note).
- A stick-slip bow with torsional damping. Strokes start the way a player's do (press, then draw), bow direction alternates, and re-bowing a sounding string reverses the bow through zero.
- **Nine excitations** on the same string: Arco, Pizzicato, Bartók pizzicato (fingerboard slap), Col legno battuto (bouncing wooden hammer plus the stick's knock), Col legno tratto, Spiccato, Natural harmonics (2nd to 5th), Mallet (felt hammer on a nonlinear spring, with a Hardness control), EBow (magnetic driver with level control, fundamental or harmonic mode).
- **Eight bodies**, each a data table of strings, tuning, range, signature low modes, a seeded cloud of higher modes, bridge coupling and radiation: Modern Concert, Baroque, Five-String, Carbon Fiber, Cello Piccolo, Arpeggione (fretted, six strings in guitar tuning), Electric Solid (piezo into an amp / DI stage: drive, tone stack, cab) and Electric Skeleton (close-miked frame). Open strings ring freer than stopped notes; frets keep stopped notes bright.
- **Six string sets**: plain gut, wound gut, synthetic, steel, silver-wound, tungsten-wound, each with its own decay, brightness, inharmonicity, bow grip and tension modulation; plus **Tension** (slack strings "boing" when plucked hard) and **String Age** (fresh and zingy to old and dull).
- **Sympathetic strings**: 4–12 strings tuned to a key and scale, to the body's open strings, or one by one, ringing along through the bridge, with Amount, Decay and Brightness; zero CPU when off.
- An 8-line reverb, stereo width and a soft clipper on the output. Stable at 44.1 / 48 / 96 kHz, no denormal spikes, about 3% of one core for 16 voices.

## Playing

| Control | What it does |
|---|---|
| **Voice Mode** | **Poly**; **Mono** (one note, every note re-bowed); **Legato** (overlapping notes on the same string *slide* without a new attack, notes on another string *cross* with the bow still moving) |
| **Portamento** | time of a one-octave fingered slide; smaller intervals are quicker (time x (interval/12)^0.6), S-shaped like a real shift |
| **Strings** | each note goes to the highest open string of the body at or below it; open strings ring freer than stopped notes |
| **Vibrato** | Rate, Depth (cents), Delay; Source: Auto (fades in after the delay), Mod Wheel, Aftertouch, or Auto + Aftertouch. Each voice drifts a little in rate and depth |
| **Swell** | Mod wheel by default (or breath CC2, expression CC11, aftertouch): leans into the string with more bow pressure *and* speed, for crescendos |
| **Velocity** | attack speed, bite and pressure |
| **Keyswitches** | C0..G#0 by default (octave selectable, or off): Arco, Pizzicato, Bartok, Col legno battuto, Col legno tratto, Spiccato, Harmonics, Mallet, EBow. Silent; they pick the articulation for the next notes until the Excitation control is moved |
| **Bow direction** | alternates on every new stroke; re-bowing a sounding string reverses the bow through zero (the real bow-change transient) |
| **Release / Bow Change Noise** | bow hair leaving the string or a fingertip damping a pluck; hair noise at bow changes (both injected into the string, so they ring through the body) |
| **MIDI** | all 128 notes, pitch bend (range 0..12 st), sustain pedal (CC64), mod wheel, breath, expression, channel and poly aftertouch |


Signal flow:
```
per voice:  exciter <-> waveguide string (bridge end loaded by the body's strongest modes: wolf note)
global:     sum -> Bridge (bridge hill) -> [+ sympathetic bank, driven by the bridge] -> ModalBody (signature modes + mode cloud, stereo)
                -> Radiation (air, proximity, width) -> AmpStage (electric bodies) -> master -> soft clip
```

## Build (Visual Studio on Windows)

Requirements: Visual Studio 2022 or 2026 with the **Desktop development with C++** workload, Git (to fetch JUCE), and the WebView2 runtime (built into Windows 11). CMake comes with Visual Studio.

```
vst\build.bat                     VST3 + Standalone (Release). Also regenerates ui\js\params.js from the C++ parameter table
vst\build.bat Debug
vst\install.bat                   copies "No Strings.vst3" to C:\Program Files\Common Files\VST3 (asks for admin)
vst\render.bat                    console renderer only: test WAVs + measurements (seconds to build, no JUCE involved)
vst\render.bat --stage 2          one stage only (default: all)
vst\render.bat --only pressure    render just the scenarios whose name contains "pressure"
vst\render.bat --no-wav           measurements only
```

- On the first build CMake downloads JUCE 8.0.17 (it reuses E-Piano's copy in `%LOCALAPPDATA%\EPianoVST\deps` when that is there).
- Build files go to `%LOCALAPPDATA%\NoStringsVST\build`, so OneDrive doesn't sync them:
  - `...\build\NoStringsVST_artefacts\Release\VST3\No Strings.vst3`
  - `...\build\NoStringsVST_artefacts\Release\Standalone\No Strings.exe` is the quickest way to test without a DAW. Use *Options → Audio/MIDI Settings* to pick the audio device and MIDI input.
- To work in the IDE, open `%LOCALAPPDATA%\NoStringsVST\build\NoStringsVST.slnx` after one build. Targets: **NoStringsVST_VST3**, **NoStringsVST_Standalone**, **NoStringsRender** (pass `--out <repo>\renders`), **GenParams**.
- WAVs from the renderer go to `renders\stage1\`, `renders\stage2\`, ... in this repo (ignored by git).

### Editing the UI without rebuilding
Set the environment variable `NOSTRINGS_UI_DIR` to this repo's `vst\ui` folder before starting the DAW or the Standalone. The plugin then loads the UI from disk: edit HTML/CSS/JS and just reopen the plugin window. Shift+right-click in the window opens DevTools. (`ui\js\params.js` is generated: change `core/ParamTable.h` and run `build.bat` or the GenParams target instead of editing it.)

### In the DAW
- Loads as an instrument (Instrument|Synth) with stereo out and MIDI in. Every parameter can be automated; a knob drag records as one automation gesture.
- Right-click any control for the host's own parameter menu (FL Studio: *Create automation clip*, *Link to controller*…). Hosts without one get a small fallback menu with *Reset to default*.
- The project saves every setting.

## What the renderer measures

| Check | Meaning |
|---|---|
| cents | tuning of a held, bowed note (steady part) |
| slips per period | 1.00 = clean Helmholtz motion (one stick-slip cycle per period); 2–3 = surface sound / multiple slips; 0.5 = period doubling ("crunch") |
| settle | seconds until the bow locks into Helmholtz motion for good |
| Schelleng map | slips per period over bow position × pressure: shows the playable window |
| CPU | 16 held voices at 44.1 / 48 / 96 kHz |
| Denormal check | a 30 s release tail rendered without flush-to-zero must not slow down |
| Excitation table | per excitation at C2 / D3 / A4: tuning, onset, decay (T60), loudness, peak, sustain steadiness |
| Stability sweep | every excitation at 44.1 / 48 / 96 kHz, velocity 1 and 127, both position extremes, C1..C7 at once: no NaN / inf |
| Body table | per body: level (arco and pizz), brightness (RMS frequency), stereo correlation, tuning |
| Wolf map | bowed C3..C4 on modern, baroque and carbon: slips per period and level beating per note |
| Materials table | per material: inharmonicity (partials 8 and 16, bare string), pizz decay, arco brightness / tuning / settle, playable-window cells, level |
| Sympathetic | tunings per mode, halo (tail level on vs off, matching vs non-matching notes), ring tuning, level, CPU and "bank ran 0 samples" when off |
| Playability | slide timing per interval, legato vs re-attack level dips, vibrato depth / rate from the audio, keyswitches silent and switching, all 128 notes sounding, sustain pedal, a 20 s random-MIDI torture test per voice mode, CPU with vibrato |
| Tension / age | pizz pitch glide (tension modulation), bow settle and tuning, inharmonicity; decay and brightness vs age |

## Layout

```
vst/
  CMakeLists.txt, render.bat
  core/                    DSP core: plain C++17, no framework (the web build gets a JS twin)
    dsp/Common.h           math, RNG, denormal guard
    dsp/Delay.h            delay line with Lagrange fractional reads
    dsp/Filters.h          one-pole, allpass, DC blocker (+ phase-delay formulas for tuning)
    WaveguideString.h      the string: delay lines split at the contact point, lumped loss + dispersion,
                           optional light-touch point (natural harmonics), static pluck shapes
    excite/Bow.h           stick-slip friction junction with torsional damping
    excite/Exciters.h      excitation list, contact points, pluck / Bartok, hammer (mallet, col legno), EBow
    dsp/Biquad.h           biquad + RBJ designs
    body/BodyProfiles.h    the eight bodies as data (strings, modes, coupling, radiation, levels)
    body/Bridge.h          bridge hill + roll-off
    body/ModalBody.h       modal resonator bank (signature modes + seeded cloud), stereo weights
    body/Radiation.h       air, close-mic proximity, width
    body/AmpStage.h        drive (ADAA), tone stack, cab: electric bodies
    Sympathetic.h          sympathetic string bank: tuning modes, per-note loss, zero cost when off
    Reverb.h               8-line FDN reverb for the Output section (skipped while silent)
    ParamTable.h           THE parameter table: ids, ranges, defaults, groups -> plugin + generated params.js
    Combine.h              the COMBINE randomiser (musical limits), shared with the web build
    Materials.h            string sets x tension x age -> decay, brightness, inharmonicity, bow grip, pitch glide
    Engine.h               voices, bow strokes, per-excitation envelopes, control mapping
  tests/                   NoStringsRender: RenderWav.cpp (main), RenderCommon.h, Stage1..6.cpp
                           GenParamsJs.cpp: writes ui/js/params.js from ParamTable.h
  Source/                  JUCE plugin: Params.h (APVTS layout), PluginProcessor.* (engine, MIDI, presets, state),
                           PluginEditor.* (WebView2 host + JS <-> C++ bridge), PresetManager.* (factory + user presets)
  presets/factory_presets.json   the 64 factory presets
  tests/PresetTests.cpp    NoStringsTests: preset tests + --loudness level matcher
  ui/                      the UI (HTML/CSS/SVG/JS), compiled into the plugin
    index.html, css/style.css   layout and the wood skin
    js/params.js           GENERATED parameter table + body / excitation facts
    js/app.js              UI wiring (shared with the web build)
    js/knob.js, controls.js, piano.js   SVG knob, toggle, segmented, dropdown, mod wheel, keyboard
    js/cello.js            the cello logo and the per-body picture (SVG)
    js/engine-juce.js      the bridge (JUCE native integration)
  webhost/
    dsp-worklet.js         JS twin of the C++ core (web audio engine), checked by run-parity.ps1
    web-host.js            provides the bridge in a browser: Web Audio, Web MIDI, presets, COMBINE
    build-web.ps1          assembles ../web/index.html and ../docs/index.html
  tests/parity_scenarios.json, run-parity.ps1   C++ vs JS DSP parity
  build.bat, install.bat, render.bat
android/                   Android app (Gradle): app/src/main/java/.../MainActivity.java (WebView + MidiManager bridge),
                           build-apk.bat; the web build is copied into app/src/main/assets at build time
web/index.html             web build (single file, generated: vst\webhost\build-web.ps1)
docs/index.html            the same file, served by GitHub Pages
start.bat, serve.ps1       optional local server for the web build
```

## Presets

- **Factory:** 64 presets in [vst/presets/factory_presets.json](vst/presets/factory_presets.json), 8 per category and 8 per body. Their levels were set by rendering each one (`NoStringsTests --loudness`), so switching presets keeps a steady volume. They also appear in the DAW's program menu.
- **COMBINE:** a new excitation × body × material × sympathetic combination within musical limits (bow pressure stays in the playable window, gut goes with the acoustic bodies, the amp only moves on the electric ones). The name tells you what it picked; SAVE keeps it.
- **User folder:** `Documents\No Strings\Presets`, shared by every DAW and every instance. Scanned on open and every 2 seconds, so new, renamed and deleted files show up on their own; subfolders become category tabs. **SAVE** writes `<name>.nspreset` and asks before replacing a file, **LOAD** opens any `.nspreset`, **FOLDER** opens the folder in Explorer, and the **×** on a user preset moves it to the Recycle Bin.
- **File format** (plain JSON, the same shape as E-Piano's `.epreset`). Any parameter a file leaves out loads at its default; Master is never stored:
  ```json
  { "format": "No Strings Preset", "version": 1, "name": "My Sound", "category": "User",
    "params": { "body": 1, "excitation": 0, "material": 0, "sympOn": 1, "sympMode": 0, "sympScale": 6, "presetLevel": -2 } }
  ```
- **Tests:** `cmake --build %LOCALAPPDATA%\NoStringsVST\build --config Release --target NoStringsTests`, then run `NoStringsTests.exe` from `...\build\NoStringsTests_artefacts\Release\` (factory bank counts, ranges, round trip, partial / bad files, Combine limits). `NoStringsTests.exe --loudness vst\presets\factory_presets.json` re-measures and rewrites the levels.

## Web build (`web/index.html`)

- **One file**: UI, DSP and all 64 presets are inside it. Open it directly in Chrome or Edge, or run `start.bat` to serve it from `http://localhost:8080`.
- It is **generated from the plugin's own UI** by `vst\webhost\build-web.ps1` (which also writes the GitHub Pages copy `docs/index.html`), so it has every feature of the plugin. Its JavaScript engine (`vst/webhost/dsp-worklet.js`) is a line-for-line twin of the C++ core and is checked against it by the parity test below.
- Audio switches on with your first click or key press. The bottom-right corner shows the audio path: **AudioWorklet** when served by `start.bat` (lowest latency), **ScriptProcessor** when opened straight from disk (same DSP, a bit more latency).
- **MIDI**: pick your keyboard under *MIDI IN*. Notes, sustain, mod wheel, breath, expression, aftertouch and pitch bend all work. The browser asks for MIDI permission the first time.
- **Presets**: **FOLDER** links a folder on disk (Chrome/Edge). Link `Documents\No Strings\Presets` to share presets with the plugin; linked folders autoload the same way the plugin's does, and after reopening the page one click on **FOLDER** reconnects it. With no linked folder, **SAVE** stores presets in this browser (the *User* tab). **LOAD** opens any `.nspreset` file. **COMBINE** works as in the plugin.

## Android app (`android/`)

The web build wrapped in a WebView, so it is the same instrument with the same presets. The page is served from an https asset origin, which gives it the **AudioWorklet** path (about 3 ms of audio latency on a current phone) and persistent browser-stored presets.

**MIDI in** arrives two ways, and the page uses whichever it gets: Web MIDI inside the WebView (granted by the app), and the phone's own MIDI service (`MidiManager`): every USB, Bluetooth or virtual MIDI device the system knows is opened by the app and its messages are pushed into the page, so a class-compliant USB keyboard on a USB-C adapter, or a Bluetooth MIDI controller paired in Android's settings, plays the instrument. Notes, velocity, sustain, mod wheel, breath, expression, aftertouch and pitch bend all come through. Devices plugged in while the app runs appear on their own.

```
android\build-apk.bat             regenerates web\index.html, builds the debug APK and installs it over adb
android\build-apk.bat noinstall   build only: android\app\build\outputs\apk\debug\app-debug.apk
```

- Needs the Android SDK (`%LOCALAPPDATA%\Android\Sdk` or `ANDROID_HOME`) and a JDK 17 or newer (`JAVA_HOME`; the script also finds the JDK that VS Code's Java extension installs). Gradle comes through the wrapper. Android Gradle plugin 9.5, Gradle 9.7, min SDK 26 (Android 8).
- The APK is self-signed (debug keystore), so the phone must allow installing from this source. `adb devices` must show the phone as `device` (not `unauthorized`: accept the USB debugging prompt on the phone).
- Landscape, full screen, keeps the screen on; the Back button sends the app to the background instead of stopping the sound.
- `WebView.setWebContentsDebuggingEnabled` is on: open `chrome://inspect` on the desktop to see the page's console.
- Presets saved in the app live in its browser storage (the *User* tab). The folder-linking FOLDER button is a desktop-Chrome feature and is not available in a WebView.

### Release builds and Google Play

```
android\build-release.bat                                  signed APK + App Bundle -> release\NoStrings-<version>.apk / .aab
powershell -ExecutionPolicy Bypass -File android\store\make-artwork.ps1   icon, feature graphic, screenshots -> release\store\
```

- **Signing:** `android\keystore\nostrings-upload.jks` (upload key, valid to 2054) and `android\keystore.properties` (its passwords) are *not* in git. **Back them up**: Play accepts updates only from this key. If `keystore.properties` is missing, release builds sign with the debug key, which installs fine but Play rejects.
- **`release\NoStrings-1.0.apk`** installs directly (`adb install`, or copy to the phone and open it; the phone must allow installs from that source).
- **`release\NoStrings-1.0.aab`** is what the Play Console takes. Before each upload bump `versionCode` (and `versionName`) in `android\app\build.gradle.kts`: Play refuses a bundle whose `versionCode` it has seen.
- **Play Console steps** (one time): create the app → *App integrity*: keep **Play App Signing** on and upload the bundle (Google signs the distributed APKs with its own key; your key is the upload key) → *Store listing*: name "No Strings", short and full description, the icon (`release\store\icon-512.png`), feature graphic (`feature-1024x500.png`) and at least two phone screenshots (`screenshot-*.png`, 2400×1080; retake on a real phone if you prefer) → *App content*: privacy policy (the app collects nothing; a one-line page stating that satisfies the form), ads = no, data safety = no data collected or shared, target audience 13+, content rating questionnaire (utility / music app) → *Release*: upload the `.aab` to internal testing first, then production.
- Requirements already met: targetSdk 36, 64-bit compliant (no native code), adaptive + round icons, no dangerous permissions, app bundle format.

### DSP parity test
`powershell -ExecutionPolicy Bypass -File vst\tests\run-parity.ps1` renders the 14 scenarios in [vst/tests/parity_scenarios.json](vst/tests/parity_scenarios.json) (every excitation, bodies, sympathetic strings, legato slides, vibrato, swells, keyswitches, sustain, pitch bend, a 16-voice flood) with both engines (`NoStringsTests --parity` and headless Edge) and compares RMS per window and peak. The two currently agree to about 1e-7, bowed notes included; the test's tolerance is 1e-3.

### Release
1. `vst\build.bat`, run `NoStringsTests.exe` and `vst\tests\run-parity.ps1`, play the Standalone and the plugin in a DAW.
2. `powershell -ExecutionPolicy Bypass -File vst\webhost\build-web.ps1` regenerates `web\index.html` and `docs\index.html` (the latter is what GitHub Pages serves).
3. Bump the version in `vst\CMakeLists.txt` (`project(NoStringsVST VERSION ...)`, shown in the web build's title) and in `vst\ui\index.html`'s header, commit, tag.

## Release history

**v1.0: first release**
- The complete instrument: bowed-string core, nine excitations, eight bodies, six string sets with tension and age, sympathetic strings, Poly / Mono / Legato playability with fingered portamento, vibrato, swells, keyswitches and release / bow-change noise.
- VST3 + Standalone (JUCE 8, WebView2 UI) with the wood-panel SVG UI and the 24-key keyboard; every parameter automatable, right-click for the host's automation menu.
- 64 level-matched factory presets in 8 categories (8 per body), `.nspreset` user presets in a shared folder with autoload, host programs, COMBINE.
- Single-file web build with the JavaScript twin of the engine (AudioWorklet or ScriptProcessor), Web MIDI, browser or linked-folder presets.
- **Automated checks:** the console renderer (`vst\render.bat`: tuning, Helmholtz motion, Schelleng map, per-excitation / body / material tables, stability sweeps, CPU, denormals), the preset tests (`NoStringsTests`), and C++ vs JS parity (`vst\tests\run-parity.ps1`, agreeing to about 1e-7).
- **Still to confirm by hand:** playing feel and the right-click automation menu in Minihost Modular / FL Studio, and Web MIDI with a real controller. Everything else was built and checked automatically.

**Known limitations**
- Windows only for now. The plugin code is portable JUCE, but no macOS build has been set up or tested.
- The web build relies on Chrome or Edge for Web MIDI and folder linking. Other browsers can play but only store presets in the browser.
- Col legno tratto on the lowest notes keeps a slightly unsteady pitch: wood barely grips the string, which is the character of the technique, but worth an ear.

**Ideas after 1.0**
- Keyswitch-selectable bow position presets (tasto / normale / ponticello) and per-string tuning / scordatura.
- Double stops on one bow stroke (two strings under the same bow envelope).
- A macOS (AU / VST3) build and an installer.

## Bridge events (UI <-> C++)

| UI → host | host → UI (30 Hz, only changes) |
|---|---|
| `uiReady`, `setParam {id,value}`, `gesture {id,begin}` | `params {id:value,…}` |
| `noteOn {note,vel}`, `noteOff {note}`, `panic`, `modWheel {value}` | `notes {on:[],off:[]}` (host MIDI + UI keys) |
| `paramMenu {id,x,y}` (right-click: the host's automation menu) | `stats {voices,peak,sustain,modWheel,excitation}`, `config {…}` |
| `loadPreset {id}`, `savePreset {name,overwrite}`, `deletePreset {id}`, `loadPresetFile`, `openPresetFolder`, `combine` | `presetList {presets,folder}`, `preset {id,name,category}`, `saveResult {ok,exists,name,error}`, `message {text}` |
