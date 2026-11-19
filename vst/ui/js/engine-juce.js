// ============================================================================
//  JUCE bridge: the UI's connection to the C++ processor through JUCE 8's
//  native integration (window.__JUCE__.backend), which JUCE injects into the
//  page. The web build (stage 9) provides the same bridge on top of Web Audio.
// ============================================================================
'use strict';

class JuceEngine {
  constructor() {
    this.backend = (window.__JUCE__ && window.__JUCE__.backend) || null;
    this.connected = !!this.backend;
    this.ready = true;
    this.handlers = {};
    if (this.backend) {
      for (const ev of ['params', 'notes', 'stats', 'config', 'presetList', 'preset', 'saveResult', 'message', 'midiInputs']) {
        this.backend.addEventListener(ev, (data) => { if (this.handlers[ev]) this.handlers[ev](data); });
      }
    }
  }

  on(event, fn) { this.handlers[event] = fn; }
  emit(id, payload) { if (this.backend) this.backend.emitEvent(id, payload || {}); }

  start() { return Promise.resolve(); }
  uiReady() { this.emit('uiReady'); }

  // parameters
  setParam(id, value) { this.emit('setParam', { id, value }); }
  beginGesture(id) { this.emit('gesture', { id, begin: true }); }
  endGesture(id) { this.emit('gesture', { id, begin: false }); }

  // notes
  noteOn(note, vel) { this.emit('noteOn', { note, vel }); }
  noteOff(note) { this.emit('noteOff', { note }); }
  panic() { this.emit('panic'); }
  modWheel(value) { this.emit('modWheel', { value }); }

  // right-click on a control -> the host's parameter menu (automation, MIDI learn...)
  paramMenu(id, x, y) { this.emit('paramMenu', { id, x: Math.round(x), y: Math.round(y) }); }

  // web build only (the plugin takes MIDI from the host)
  selectMidi(id) { this.emit('selectMidi', { id }); }

  // presets
  loadPreset(id) { this.emit('loadPreset', { id }); }
  savePreset(name, overwrite) { this.emit('savePreset', { name, overwrite: !!overwrite }); }
  deletePreset(id) { this.emit('deletePreset', { id }); }
  loadPresetFile() { this.emit('loadPresetFile'); }
  openPresetFolder() { this.emit('openPresetFolder'); }
  combine() { this.emit('combine'); }
}
