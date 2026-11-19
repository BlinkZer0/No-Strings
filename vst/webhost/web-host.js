// ============================================================================
//  Web host: stands in for the plugin's C++ side when the UI runs in a
//  browser. It provides window.__JUCE__.backend with exactly the events the
//  JUCE editor uses (see PluginEditor.h), so vst/ui runs unchanged:
//    audio   Web Audio: AudioWorklet (or ScriptProcessor fallback on file://)
//            running dsp-worklet.js, the JS twin of the C++ core
//    MIDI    Web MIDI (all inputs or one): notes, sustain, mod wheel, breath,
//            expression, aftertouch, pitch bend
//    presets factory (FACTORY_JSON), this browser (localStorage), or a linked
//            folder on disk (File System Access API): the same .nspreset format
//    combine the same randomiser as the plugin (core/Combine.h)
// ============================================================================
(function () {
  'use strict';

  // ---------------------------------------------------------- the bridge ---
  const listeners = {};
  const handlers = {};
  const toUi = (ev, data) => queueMicrotask(() => (listeners[ev] || []).forEach(f => f(data)));
  window.__JUCE__ = {
    backend: {
      addEventListener(ev, fn) { (listeners[ev] = listeners[ev] || []).push(fn); },
      // handled synchronously so file pickers / permission prompts keep the click's user activation
      emitEvent(ev, data) { const h = handlers[ev]; if (h) h(data || {}); },
    },
  };
  const message = (text) => toUi('message', { text });
  const store = {
    get(k) { try { return localStorage.getItem(k); } catch (e) { return null; } },
    set(k, v) { try { localStorage.setItem(k, v); return true; } catch (e) { return false; } },
  };

  // ------------------------------------------------------ factory presets ---
  const FACTORY = FACTORY_JSON.presets.map((p, i) => ({ id: `factory:${i}`, name: p.name, category: p.category, factory: true, values: p.params }));

  // ---------------------------------------------------------------- state ---
  const values = defaultParamValues();
  let current = { id: '', name: '', category: '' };

  // Missing params -> default; master untouched (same as the plugin's applyPreset)
  function presetValues(pv) {
    const out = {};
    for (const d of PARAM_DEFS) {
      if (d.noPreset) continue;
      let v = Number(pv[d.id]);
      if (pv[d.id] === undefined || pv[d.id] === null || isNaN(v)) v = d.def;
      v = clamp(v, d.min, d.max);
      if (d.step) v = Math.round(v / d.step) * d.step;
      out[d.id] = v;
    }
    return out;
  }
  function applyPreset(pv, info) {
    const all = presetValues(pv);
    Object.assign(values, all);
    audio.post({ type: 'params', values: all });
    current = info;
    toUi('params', Object.assign({}, values));
    toUi('preset', current);
  }

  // start on the first factory preset, like a fresh plugin instance (before audio starts)
  Object.assign(values, presetValues(FACTORY[0].values));
  current = { id: FACTORY[0].id, name: FACTORY[0].name, category: FACTORY[0].category };
  // index.html#body=6,excitation=8 starts on those settings (previews, screenshots, links)
  if (location.hash.length > 1) {
    let any = false;
    for (const pair of location.hash.slice(1).split(',')) {
      const [id, v] = pair.split('=');
      const d = PARAM_BY_ID[id];
      if (d && v !== undefined && !isNaN(Number(v))) { values[id] = clamp(Number(v), d.min, d.max); any = true; }
    }
    if (any) current = { id: '', name: 'Custom', category: 'User' };
  }

  // ---------------------------------------------------------------- audio ---
  const TABLES = { bodies: BODY_PROFILES, materials: MATERIAL_PROFILES };
  const audio = {
    ctx: null, node: null, port: null, mode: '', error: '',
    post(m) { if (this.port) this.port.postMessage(m); },
    get running() { return !!this.ctx && this.ctx.state === 'running'; },

    async start() {
      const ctx = new (window.AudioContext || window.webkitAudioContext)({ latencyHint: 'interactive' });
      this.ctx = ctx;
      ctx.onstatechange = sendConfig;
      const src = `(${nostringsWorkletModule.toString()})();`;
      let built = null;
      if (ctx.audioWorklet && location.protocol !== 'file:') {
        try {
          const url = URL.createObjectURL(new Blob([src], { type: 'application/javascript' }));
          try { await ctx.audioWorklet.addModule(url); } finally { URL.revokeObjectURL(url); }
          const node = new AudioWorkletNode(ctx, 'nostrings-cello', {
            numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2],
            processorOptions: { params: values, tables: TABLES },
          });
          node.port.onmessage = (e) => onDspMessage(e.data);
          built = { node, port: node.port, mode: 'AudioWorklet' };
        } catch (e) { console.warn('AudioWorklet unavailable, using ScriptProcessor', e); }
      }
      if (!built) built = this.scriptProcessor(ctx, src);
      this.node = built.node; this.port = built.port; this.mode = built.mode;
      this.node.connect(ctx.destination);
      sendConfig();
    },

    // Chrome/Edge block worklet modules on file://: run the same DSP on the main thread
    scriptProcessor(ctx, src) {
      class AudioWorkletProcessorShim { constructor() { this.port = { postMessage: onDspMessage, onmessage: null }; } }
      let Processor = null;
      new Function('AudioWorkletProcessor', 'registerProcessor', 'sampleRate', src)(
        AudioWorkletProcessorShim, (name, cls) => { Processor = cls; }, ctx.sampleRate);
      const proc = new Processor({ processorOptions: { params: values, tables: TABLES } });
      const L128 = new Float32Array(128), R128 = new Float32Array(128);
      const node = ctx.createScriptProcessor(1024, 0, 2);
      node.onaudioprocess = (e) => {
        const L = e.outputBuffer.getChannelData(0), R = e.outputBuffer.getChannelData(1);
        for (let off = 0; off < L.length; off += 128) {
          proc.process([], [[L128, R128]]);
          L.set(L128, off); R.set(R128, off);
        }
      };
      return { node, port: { postMessage: (m) => proc.port.onmessage({ data: m }) }, mode: 'ScriptProcessor' };
    },
  };

  function onDspMessage(m) { if (m.type === 'stats') toUi('stats', m); }

  function sendConfig() {
    const c = audio.ctx;
    let text;
    if (audio.error) text = `Audio error: ${audio.error}`;
    else if (!audio.running) text = 'Click anywhere to enable audio';
    else text = `Web · ${audio.mode} · ${(c.sampleRate / 1000).toFixed(1)} kHz · ~${(((c.baseLatency || 0) + (c.outputLatency || 0)) * 1000).toFixed(1)} ms`;
    toUi('config', { wrapper: 'Web', sampleRate: c ? c.sampleRate : 0, blockSize: 128, audio: text, running: audio.running });
  }

  const started = audio.start().catch((e) => { audio.error = e.message || String(e); console.error(e); sendConfig(); });
  function unlockAudio() {
    if (audio.running || audio.error) return;
    started.then(() => audio.ctx && audio.ctx.resume()).then(sendConfig).catch(() => {});
  }
  document.addEventListener('pointerdown', unlockAudio, true);
  document.addEventListener('keydown', unlockAudio, true);

  // ---------------------------------------------------------------- notes ---
  const sounding = new Set();
  function noteOn(note, vel) {
    if (!audio.running) message('Audio is off: click anywhere on the page once to enable it');
    audio.post({ type: 'noteOn', note, vel });
    sounding.add(note);
    toUi('notes', { on: [note], off: [] });
  }
  function noteOff(note) {
    audio.post({ type: 'noteOff', note });
    if (sounding.delete(note)) toUi('notes', { on: [], off: [note] });
  }
  function allOff() {
    const off = [...sounding];
    sounding.clear();
    if (off.length) toUi('notes', { on: [], off });
  }

  // ----------------------------------------------------------------- MIDI ---
  const midi = { access: null, selected: store.get('nostrings.midiInput') || 'all' };
  function midiInputs() {
    return midi.access ? Array.from(midi.access.inputs.values()).map(i => ({ id: i.id, name: i.name, state: i.state })) : [];
  }
  function bindMidi() {
    if (!midi.access) return;
    for (const input of midi.access.inputs.values()) {
      const on = midi.selected === 'all' || midi.selected === input.id;
      input.onmidimessage = on ? (e) => onMidi(e.data) : null;
    }
  }
  function onMidi(d) {
    const st = d[0];
    if (st >= 0xf0) return;
    const cmd = st & 0xf0;
    if (cmd === 0x90 && d[2] > 0) noteOn(d[1], d[2]);
    else if (cmd === 0x80 || cmd === 0x90) noteOff(d[1]);
    else if (cmd === 0xb0) {
      const v = d[2] / 127;
      if (d[1] === 1) audio.post({ type: 'modWheel', value: v });
      else if (d[1] === 2) audio.post({ type: 'breath', value: v });
      else if (d[1] === 11) audio.post({ type: 'expression', value: v });
      else if (d[1] === 64) audio.post({ type: 'sustain', on: d[2] >= 64 });
      else if (d[1] === 120) { audio.post({ type: 'panic' }); allOff(); }
      else if (d[1] === 123) { audio.post({ type: 'allNotesOff' }); allOff(); }
    } else if (cmd === 0xd0) {
      audio.post({ type: 'aftertouch', value: d[1] / 127 });
    } else if (cmd === 0xa0) {
      audio.post({ type: 'aftertouch', value: d[2] / 127 });
    } else if (cmd === 0xe0) {
      audio.post({ type: 'bend', value: (((d[2] << 7) | d[1]) - 8192) / 8192 });
    }
  }
  // Native MIDI (the Android app): the app opens the phone's MIDI devices itself and
  // pushes the raw messages here, so a USB or Bluetooth keyboard works even where the
  // WebView has no Web MIDI. It also tells us the device names for the MIDI IN picker.
  const native = { devices: [] };
  window.nostringsNativeMidi = {
    message(bytes) { onMidi(bytes); },
    devices(list) {
      native.devices = Array.isArray(list) ? list : [];
      if (!midi.access) toUi('midiInputs', { inputs: native.devices.map((n, i) => ({ id: `native:${i}`, name: n, state: 'connected' })), selected: 'all' });
      message(native.devices.length ? `MIDI: ${native.devices.join(', ')}` : 'MIDI: no devices found (plug one in, it appears automatically)');
    },
  };
  const nativeMidiApp = !!(window.NoStringsAndroid && typeof window.NoStringsAndroid.midiReady === 'function');
  if (nativeMidiApp) queueMicrotask(() => window.NoStringsAndroid.midiReady());

  if (typeof navigator.requestMIDIAccess === 'function' && !nativeMidiApp) {
    navigator.requestMIDIAccess({ sysex: false }).then((access) => {
      midi.access = access;
      access.onstatechange = () => { bindMidi(); toUi('midiInputs', { inputs: midiInputs(), selected: midi.selected }); };
      bindMidi();
      toUi('midiInputs', { inputs: midiInputs(), selected: midi.selected });
      const n = midiInputs().length;
      message(n ? `MIDI ready: ${n} input(s): ${midiInputs().map(i => i.name).join(', ')}` : 'MIDI ready: no devices found (plug one in, it appears automatically)');
    }).catch((e) => {
      toUi('midiInputs', { inputs: [], selected: 'all', error: true });
      message(`MIDI unavailable: ${e.message || e}`);
    });
  } else if (!nativeMidiApp) {
    queueMicrotask(() => toUi('midiInputs', { inputs: [], selected: 'all', error: true }));
  }

  // -------------------------------------------------------------- combine ---
  // Mirror of core/Combine.h: a new excitation x body x material x sympathetic
  // combination within musical limits. Same RNG as the plugin.
  const rng = { s: (Date.now() ^ 0xC0FFEE) >>> 0,
    next() { this.s = (Math.imul(this.s, 1664525) + 1013904223) >>> 0; return this.s; },
    uni() { return (this.next() >>> 8) * (1 / 16777216); } };
  function combine() {
    const pick = (opts) => opts[Math.min(Math.trunc(rng.uni() * opts.length), opts.length - 1)];
    const range = (lo, hi) => lo + (hi - lo) * rng.uni();
    const v = Object.assign({}, values);
    const exc = pick([0, 0, 0, 0, 1, 1, 2, 3, 4, 5, 5, 6, 7, 7, 8, 8]);
    const body = pick([0, 1, 2, 3, 4, 5, 6, 7]);
    const electric = BODIES[body].electric;
    const material = electric ? pick([3, 3, 4, 5, 2]) : pick([0, 1, 2, 2, 3, 3, 4, 5]);
    v.excitation = exc; v.body = body; v.material = material;
    v.tension = range(0.3, 0.7); v.age = range(0, 0.6);
    v.bowPressure = range(0.35, 0.65); v.bowSpeed = range(0.35, 0.7); v.bowPosition = range(0.25, 0.75);
    v.harmonic = pick([2, 2, 3, 4]); v.malletHardness = range(0.2, 0.9); v.ebowMode = pick([0, 0, 0, 1]);
    v.releaseTime = exc === 1 || exc === 2 || exc === 7 ? range(0.4, 1.2) : range(0.2, 0.7);
    const symp = rng.uni() < 0.5;
    v.sympOn = symp ? 1 : 0;
    if (symp) {
      v.sympMode = pick([0, 0, 1, 1, 1]);
      v.sympCount = pick([4, 6, 8, 8, 10, 12]);
      v.sympAmount = range(0.3, 0.8); v.sympDecay = range(2, 15); v.sympBright = range(0.3, 0.8);
      v.sympKey = pick([0, 2, 4, 5, 7, 9, 10]);
      v.sympScale = pick([0, 1, 2, 3, 4, 6, 6]);
    }
    if (electric) {
      v.ampDrive = range(0, 0.75); v.ampBass = range(0.35, 0.65); v.ampMid = range(0.35, 0.65); v.ampTreble = range(0.35, 0.65);
      v.ampCab = rng.uni() < 0.7 ? 1 : 0;
    }
    v.vibDepth = exc === 0 || exc === 6 || exc === 8 ? range(8, 28) : 0;
    v.reverbSend = range(0.1, 0.45); v.width = range(0.8, 1.4); v.presetLevel = 0;
    const name = `${EXCITATIONS[exc]} · ${BODIES[body].name} · ${PARAM_BY_ID.material.choices[material]}`;
    applyPreset(v, { id: '', name, category: 'Combine' });
    message(`Combined: ${name}`);
  }

  // -------------------------------------------------------------- presets ---
  const PRESET_EXT = '.nspreset';
  const LS_KEY = 'nostrings.userPresets.v1';
  const local = {
    read() { try { return JSON.parse(store.get(LS_KEY)) || {}; } catch (e) { return {}; } },
    write(obj) { return store.set(LS_KEY, JSON.stringify(obj)); },
  };
  const localPresets = () => Object.keys(local.read()).sort((a, b) => a.localeCompare(b, undefined, { numeric: true }))
    .map(name => ({ id: `local:${name}`, name, category: 'User', factory: false }));

  const fsSupported = typeof window.showDirectoryPicker === 'function';
  const folder = { handle: null, granted: false, files: [], sig: '' };
  const idb = {
    open() {
      return new Promise((res, rej) => {
        const r = indexedDB.open('nostrings', 1);
        r.onupgradeneeded = () => r.result.createObjectStore('kv');
        r.onsuccess = () => res(r.result);
        r.onerror = () => rej(r.error);
      });
    },
    async get(key) {
      const db = await this.open();
      return new Promise((res) => { const r = db.transaction('kv').objectStore('kv').get(key); r.onsuccess = () => res(r.result); r.onerror = () => res(undefined); });
    },
    async set(key, val) {
      const db = await this.open();
      return new Promise((res) => { const tx = db.transaction('kv', 'readwrite'); tx.objectStore('kv').put(val, key); tx.oncomplete = () => res(true); tx.onerror = () => res(false); });
    },
  };

  async function scanFolder() {
    if (!folder.handle || !folder.granted) return false;
    const files = [];
    async function walk(dir, path) {
      for await (const [name, h] of dir.entries()) {
        const rel = path ? `${path}/${name}` : name;
        if (h.kind === 'directory') await walk(h, rel);
        else if (name.toLowerCase().endsWith(PRESET_EXT)) {
          const f = await h.getFile();
          files.push({ id: `user:${rel}`, name: name.slice(0, -PRESET_EXT.length), category: path || 'User',
                       factory: false, handle: h, parent: dir, fileName: name, stamp: `${f.size}:${f.lastModified}` });
        }
      }
    }
    try { await walk(folder.handle, ''); } catch (e) { folder.granted = false; return true; }
    files.sort((a, b) => a.category.localeCompare(b.category) || a.name.localeCompare(b.name, undefined, { numeric: true }));
    const sig = files.map(f => `${f.id}|${f.stamp}`).join(';');
    if (sig === folder.sig) return false;
    folder.sig = sig;
    folder.files = files;
    return true;
  }

  function allPresets() { return [...FACTORY, ...folder.files, ...localPresets()]; }
  function sendPresetList() {
    toUi('presetList', {
      presets: allPresets().map(({ id, name, category, factory }) => ({ id, name, category, factory })),
      folder: folder.granted ? `${folder.handle.name} (linked folder)` : 'this browser (click FOLDER to link a folder on disk)',
    });
  }

  async function linkFolder(handle, ask) {
    folder.handle = handle;
    folder.sig = '';
    let state = await handle.queryPermission({ mode: 'readwrite' });
    if (state !== 'granted' && ask) state = await handle.requestPermission({ mode: 'readwrite' });
    folder.granted = state === 'granted';
    await idb.set('presetFolder', handle);
    if (folder.granted) await scanFolder();
    sendPresetList();
    return folder.granted;
  }
  if (fsSupported && window.indexedDB) {
    idb.get('presetFolder').then(async (h) => {
      if (h && !(await linkFolder(h, false))) message(`Click FOLDER to reconnect the preset folder "${h.name}"`);
    }).catch(() => {});
  }
  setInterval(async () => { if (await scanFolder()) sendPresetList(); }, 2000); // autoload

  function parsePresetText(text, fallbackName) {
    let json;
    try { json = JSON.parse(text); } catch (e) { throw new Error(`Not a valid preset file: ${fallbackName}`); }
    const params = json && typeof json.params === 'object' ? json.params : null;
    if (!params || !PARAM_DEFS.some(d => !d.noPreset && d.id in params)) throw new Error(`No No Strings settings found in ${fallbackName}`);
    return { name: String(json.name || '').trim() || fallbackName, params };
  }
  function presetText(name, category) {
    const params = {};
    for (const d of PARAM_DEFS) if (!d.noPreset) params[d.id] = +Number(values[d.id]).toPrecision(6);
    return JSON.stringify({ format: 'No Strings Preset', version: 1, name, category, params }, null, 2);
  }
  const legalName = (s) => s.trim().replace(/[\\/:*?"<>|]/g, '_').slice(0, 60);

  async function loadPresetById(id) {
    const p = allPresets().find(x => x.id === id);
    if (!p) { message('Preset not found (was it moved or deleted?)'); toUi('preset', current); return; }
    try {
      const info = { id, name: p.name, category: p.category };
      if (p.factory) applyPreset(p.values, info);
      else {
        const text = id.startsWith('local:') ? local.read()[p.name] : await (await p.handle.getFile()).text();
        applyPreset(parsePresetText(text, p.name).params, info);
      }
    } catch (e) { message(e.message || String(e)); toUi('preset', current); }
  }

  async function savePreset(rawName, overwrite) {
    const name = legalName(rawName);
    const result = (r) => toUi('saveResult', Object.assign({ name }, r));
    if (!name) return result({ ok: false, error: 'Enter a name for the preset' });
    try {
      if (folder.granted) {
        const fileName = name + PRESET_EXT;
        if (!overwrite) {
          let exists = true;
          try { await folder.handle.getFileHandle(fileName); } catch (e) { exists = false; }
          if (exists) return result({ ok: false, exists: true });
        }
        const w = await (await folder.handle.getFileHandle(fileName, { create: true })).createWritable();
        await w.write(presetText(name, 'User'));
        await w.close();
        await scanFolder();
        current = { id: `user:${fileName}`, name, category: 'User' };
      } else {
        const all = local.read();
        if (!overwrite && all[name]) return result({ ok: false, exists: true });
        all[name] = presetText(name, 'User');
        if (!local.write(all)) throw new Error('browser storage is unavailable (private window?)');
        current = { id: `local:${name}`, name, category: 'User' };
      }
      sendPresetList();
      toUi('preset', current);
      result({ ok: true });
    } catch (e) { result({ ok: false, error: e.message || String(e) }); }
  }

  async function deletePreset(id) {
    const p = allPresets().find(x => x.id === id);
    try {
      if (!p || p.factory) throw new Error('Only user presets can be deleted');
      if (id.startsWith('local:')) { const all = local.read(); delete all[p.name]; local.write(all); }
      else { await p.parent.removeEntry(p.fileName); await scanFolder(); }
      if (current.id === id) current = { id: '', name: current.name, category: 'User' };
      message(`Deleted preset "${p.name}"`);
    } catch (e) { message(e.message || String(e)); }
    sendPresetList();
    toUi('preset', current);
  }

  const fileInput = document.createElement('input');
  fileInput.type = 'file';
  fileInput.accept = `${PRESET_EXT},application/json`;
  fileInput.style.display = 'none';
  document.addEventListener('DOMContentLoaded', () => document.body.appendChild(fileInput));
  fileInput.addEventListener('change', async () => {
    const f = fileInput.files && fileInput.files[0];
    fileInput.value = '';
    if (!f) return;
    try {
      const p = parsePresetText(await f.text(), f.name.replace(/\.[^.]+$/, ''));
      applyPreset(p.params, { id: `file:${f.name}`, name: p.name, category: 'File' });
      message(`Loaded ${f.name}`);
    } catch (e) { message(e.message || String(e)); }
  });

  async function folderButton() {
    if (!fsSupported) { message('Linking a preset folder needs Chrome or Edge: presets are saved in this browser instead.'); return; }
    try {
      if (folder.handle && !folder.granted) { if (await linkFolder(folder.handle, true)) message(`Reconnected "${folder.handle.name}"`); return; }
      const handle = await window.showDirectoryPicker({ id: 'nostrings-presets', mode: 'readwrite', startIn: 'documents' });
      await linkFolder(handle, true);
      message(`Preset folder linked: ${handle.name}: ${folder.files.length} preset(s). Link Documents\\No Strings\\Presets to share presets with the VST.`);
    } catch (e) { if (e && e.name !== 'AbortError') message(`Folder: ${e.message || e}`); }
  }

  // ------------------------------------------------- UI -> host events ---
  Object.assign(handlers, {
    uiReady() {
      sendConfig();
      sendPresetList();
      toUi('params', Object.assign({}, values));
      toUi('preset', current);
      toUi('stats', { voices: 0, peak: 0, sustain: false, modWheel: 0, excitation: Math.round(values.excitation) });
      if (midi.access) toUi('midiInputs', { inputs: midiInputs(), selected: midi.selected });
    },
    paramMenu() {}, // no host automation menu in a browser
    setParam({ id, value }) { if (id in values) { values[id] = value; audio.post({ type: 'param', id, value }); } },
    gesture() {},
    noteOn({ note, vel }) { noteOn(note, vel); },
    noteOff({ note }) { noteOff(note); },
    panic() { audio.post({ type: 'panic' }); allOff(); },
    modWheel({ value }) { audio.post({ type: 'modWheel', value }); },
    selectMidi({ id }) { midi.selected = id; store.set('nostrings.midiInput', id); bindMidi(); },
    loadPreset({ id }) { loadPresetById(id); },
    savePreset({ name, overwrite }) { savePreset(name, overwrite); },
    deletePreset({ id }) { deletePreset(id); },
    loadPresetFile() { fileInput.click(); },
    openPresetFolder() { folderButton(); },
    combine() { combine(); },
  });
})();
