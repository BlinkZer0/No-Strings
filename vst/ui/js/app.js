// ============================================================================
//  UI wiring: shared by the plugin and the web build.
//   - talks only to JuceEngine (engine-juce.js). In the plugin that is JUCE's
//     native bridge to C++; in the web build the web host provides the same
//     bridge on top of Web Audio + Web MIDI.
//   - the host owns all parameter state; automation, preset loads and MIDI
//     notes flow back in as events
//   - the whole panel scales to fit the window
// ============================================================================
(function () {
  'use strict';

  const $ = (sel) => document.querySelector(sel);
  function el(tag, cls, html) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (html !== undefined) e.innerHTML = html;
    return e;
  }
  const esc = (s) => String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

  // ---------------------------------------------------------------- state ---
  const values = defaultParamValues();      // replaced by the plugin's state on connect
  let presetList = [];                      // [{id, name, category, factory}]
  let presetFolder = '';
  let current = { id: '', name: 'Init', category: '' };
  let presetDirty = false;
  let activeCat = null;

  const engine = new JuceEngine();
  const knobs = {};
  const bound = {};             // id -> [controls with setValue()]
  const dragging = new Set();   // params being dragged in the UI
  const lastLocal = {};         // id -> time of last UI-side change

  // ---------------------------------------------------- parameter plumbing ---
  function applyValue(id, v) {
    values[id] = v;
    if (knobs[id]) knobs[id].setValue(v);
    (bound[id] || []).forEach(c => c.setValue(v));
  }

  // A change made in this UI
  function setParam(id, v) {
    lastLocal[id] = performance.now();
    applyValue(id, v);
    engine.setParam(id, v);
    if (!PARAM_BY_ID[id].noPreset && !presetDirty) { presetDirty = true; renderPresetName(); }
    refresh(PARAM_BY_ID[id].group);
  }

  function knob(container, id) {
    const def = PARAM_BY_ID[id];
    const k = new Knob(def, values[id], (v) => setParam(id, v), (begin) => {
      if (begin) { dragging.add(id); engine.beginGesture(id); }
      else { dragging.delete(id); engine.endGesture(id); }
    });
    k.el.dataset.param = id;
    knobs[id] = k;
    container.appendChild(k.el);
    return k;
  }

  // Bind a non-knob control (ToggleButton / Segmented / SelectBox) to a param
  function control(Cls, id, ...extra) {
    const c = new Cls(PARAM_BY_ID[id], values[id], (v) => setParam(id, v), ...extra);
    (bound[id] = bound[id] || []).push(c);
    c.el.dataset.param = id;
    return c;
  }

  // A labelled dropdown in a select grid
  function field(container, id, label) {
    const f = el('div', 'field');
    f.appendChild(el('span', 'ctl-label', esc(label || PARAM_BY_ID[id].name)));
    const c = control(SelectBox, id);
    f.appendChild(c.el);
    f.dataset.field = id;
    container.appendChild(f);
    return f;
  }

  // Changes coming from the plugin (automation, presets, state restore)
  engine.on('params', (obj) => {
    const now = performance.now();
    const groups = new Set();
    for (const id in obj) {
      if (!PARAM_BY_ID[id]) continue;
      // don't fight the user's hand: skip params being dragged or changed here in the last 300 ms
      if (dragging.has(id) || (lastLocal[id] !== undefined && now - lastLocal[id] < 300)) continue;
      applyValue(id, obj[id]);
      groups.add(PARAM_BY_ID[id].group);
    }
    groups.forEach(g => refresh(g));
  });

  // ------------------------------------------------------------ presets ----
  engine.on('presetList', ({ presets, folder }) => {
    presetList = presets || [];
    presetFolder = folder || '';
    if (!activeCat || !categories().includes(activeCat)) activeCat = current.category || (presetList[0] && presetList[0].category) || 'User';
    renderBrowser();
  });

  engine.on('preset', (p) => {
    const changed = p.id !== current.id || p.name !== current.name;
    current = p;
    if (changed) presetDirty = false;
    if (p.category && categories().includes(p.category)) activeCat = p.category;
    renderPresetName();
    renderBrowser();
  });

  engine.on('message', ({ text }) => logMidi(text));

  function categories() {
    const cats = [];
    for (const p of presetList) if (!cats.includes(p.category)) cats.push(p.category);
    if (!presetList.some(p => !p.factory) && !cats.includes('User')) cats.push('User');
    return cats;
  }

  function loadPreset(p) {
    presetDirty = false;
    current = { id: p.id, name: p.name, category: p.category };
    for (const id in lastLocal) delete lastLocal[id]; // let all incoming values through
    renderPresetName();
    renderBrowser();
    engine.loadPreset(p.id); // plugin applies the values and sends them back
  }

  function stepPreset(dir) {
    if (!presetList.length) return;
    const i = presetList.findIndex(p => p.id === current.id);
    const next = i < 0 ? 0 : (i + dir + presetList.length) % presetList.length;
    loadPreset(presetList[next]);
  }

  function renderPresetName() {
    $('#preset-name').textContent = current.name + (presetDirty ? ' *' : '');
    $('#preset-cat').textContent = (current.category || 'Preset').toUpperCase();
  }

  const tabsEl = $('#cat-tabs');
  const gridEl = $('#preset-grid');
  function renderBrowser() {
    const cats = categories();
    tabsEl.innerHTML = '';
    for (const c of cats) {
      const t = el('button', 'cat-tab' + (c === activeCat ? ' on' : ''), esc(c));
      t.type = 'button';
      t.setAttribute('role', 'tab');
      t.addEventListener('click', () => { activeCat = c; renderBrowser(); });
      tabsEl.appendChild(t);
    }
    gridEl.innerHTML = '';
    const shown = presetList.filter(p => p.category === activeCat);
    if (!shown.length) {
      gridEl.appendChild(el('div', 'preset-empty', 'No user presets yet: press SAVE to keep this sound, or COMBINE to find a new one.'));
      return;
    }
    for (const p of shown) {
      const b = el('button', 'preset-btn' + (p.id === current.id ? ' selected' : ''));
      b.type = 'button';
      b.innerHTML = `<span class="nm">${esc(p.name)}</span>` + (p.factory ? '' : '<span class="del" title="Delete this preset">×</span>');
      b.addEventListener('click', (e) => {
        if (e.target.classList.contains('del')) {
          confirmModal('Delete preset', `Move <b>${esc(p.name)}</b> to the Recycle Bin?`, () => engine.deletePreset(p.id));
          return;
        }
        loadPreset(p);
      });
      gridEl.appendChild(b);
    }
  }

  $('#preset-prev').addEventListener('click', () => stepPreset(-1));
  $('#preset-next').addEventListener('click', () => stepPreset(1));
  $('#preset-load').addEventListener('click', () => engine.loadPresetFile());
  $('#preset-folder').addEventListener('click', () => engine.openPresetFolder());
  $('#combine-btn').addEventListener('click', () => { for (const id in lastLocal) delete lastLocal[id]; engine.combine(); });
  $('#preset-save').addEventListener('click', () => {
    promptModal('Save preset', 'Saved to ' + (presetFolder ? `<b>${esc(presetFolder)}</b>` : 'the preset folder') + ' as a .nspreset file.',
      current.name.replace(/ \*$/, ''), (name) => engine.savePreset(name, false));
  });
  engine.on('saveResult', ({ ok, exists, name, error }) => {
    if (ok) { logMidi(`Saved "${name}"`); return; }
    if (exists) { confirmModal('Replace preset', `<b>${esc(name)}</b> already exists. Replace it?`, () => engine.savePreset(name, true)); return; }
    logMidi(error || 'Could not save the preset');
  });

  // --------------------------------------------------------------- modal ---
  const modal = $('#modal'), modalInput = $('#modal-input');
  let modalOk = null;
  function openModal(title, body, withInput, value, onOk) {
    $('#modal-title').textContent = title;
    $('#modal-body').innerHTML = body;
    modalInput.classList.toggle('hidden', !withInput);
    modalInput.value = value || '';
    modalOk = onOk;
    modal.classList.remove('hidden');
    if (withInput) { modalInput.focus(); modalInput.select(); } else $('#modal-ok').focus();
  }
  function closeModal() { modal.classList.add('hidden'); modalOk = null; }
  function promptModal(title, body, value, onOk) { openModal(title, body, true, value, () => { const v = modalInput.value.trim(); if (v) onOk(v); }); }
  function confirmModal(title, body, onOk) { openModal(title, body, false, '', onOk); }
  $('#modal-ok').addEventListener('click', () => { const f = modalOk; closeModal(); if (f) f(); });
  $('#modal-cancel').addEventListener('click', closeModal);
  modal.addEventListener('keydown', (e) => {
    e.stopPropagation();
    if (e.key === 'Enter') { const f = modalOk; closeModal(); if (f) f(); }
    if (e.key === 'Escape') closeModal();
  });

  // ------------------------------------------------------------- panels ----
  // Excitation
  $('#logo').innerHTML = celloLogoSvg();
  $('#excite-select').appendChild(control(SelectBox, 'excitation').el);
  ['bowPressure', 'bowSpeed', 'bowPosition'].forEach(id => knob($('#excite-knobs'), id));
  knob($('#excite-extra'), 'harmonic');
  knob($('#excite-extra'), 'malletHardness');
  const ebowField = field($('#excite-extra'), 'ebowMode', 'EBow');
  ebowField.style.flex = '1';

  // Body
  $('#body-select').appendChild(control(SelectBox, 'body').el);
  const BODY_DESC = [
    'Warm, full and even: the reference sound.',
    'Lighter build, sweeter and less projecting; made for gut.',
    'A modern cello with a high E string for the upper register.',
    'Bright, focused and very projecting; hardly any wolf.',
    'A smaller body: higher, more nasal, quick to speak.',
    'Fretted, guitar-shaped and bowed: six strings in guitar tuning.',
    'Solid body with a piezo pickup, through the amp / DI stage below.',
    'A silent frame: a hint of body, close-miked and intimate.',
  ];
  ['ampDrive', 'ampBass', 'ampMid', 'ampTreble'].forEach(id => knob($('#amp-knobs'), id));
  $('#amp-cab').appendChild(control(ToggleButton, 'ampCab', 'CAB').el);

  // Strings
  $('#strings-select').appendChild(control(SelectBox, 'material').el);
  ['tension', 'age', 'releaseTime'].forEach(id => knob($('#strings-knobs'), id));

  // Sympathetic
  $('#symp-power').appendChild(control(ToggleButton, 'sympOn', 'ON').el);
  ['sympAmount', 'sympCount', 'sympDecay', 'sympBright'].forEach(id => knob($('#symp-knobs'), id));
  field($('#symp-selects'), 'sympMode', 'Tuning');
  field($('#symp-selects'), 'sympKey', 'Key');
  field($('#symp-selects'), 'sympScale', 'Scale');
  // custom tuning: one note picker per string
  const noteGrid = $('#symp-custom');
  for (let i = 1; i <= MAX_SYMPATHETIC; i++) {
    const id = `sympNote${i}`, def = PARAM_BY_ID[id];
    const s = document.createElement('select');
    s.className = 'param-select';
    s.title = def.name;
    for (let n = def.min; n <= def.max; n++) s.appendChild(new Option(noteNameOf(n), String(n)));
    s.addEventListener('change', () => setParam(id, Number(s.value)));
    s.addEventListener('keydown', (e) => e.stopPropagation());
    s.dataset.param = id;
    const ctl = { el: s, setValue: (v) => { s.value = String(Math.round(v)); } };
    ctl.setValue(values[id]);
    (bound[id] = bound[id] || []).push(ctl);
    noteGrid.appendChild(s);
  }

  // Bow · Expression
  $('#voice-mode').appendChild(control(Segmented, 'voiceMode').el);
  ['portaTime', 'vibRate', 'vibDepth', 'vibDelay'].forEach(id => knob($('#play-knobs'), id));
  field($('#play-selects'), 'vibSource', 'Vibrato');
  field($('#play-selects'), 'swellSource', 'Swell');
  field($('#play-selects'), 'keyswitchOctave', 'Keyswitches');
  ['releaseNoise', 'bowNoise', 'bendRange'].forEach(id => knob($('#play-knobs2'), id));

  // Output
  ['reverbSend', 'reverbDecay', 'width', 'master'].forEach(id => knob($('#output-knobs'), id));
  $('#voice-max').textContent = NUM_VOICES;

  // ---- contextual refresh: show what matters for the chosen articulation / body / tuning
  const KS_LABELS = ['ARCO', 'PIZZ', 'BARTÓK', 'BATT.', 'TRATTO', 'SPICC.', 'HARM.', 'MALLET', 'EBOW'];
  let liveExcitation = -1;
  function refresh(group) {
    if (!group || group === 'excite') {
      const e = Math.round(values.excitation);
      const bowed = BOWED_EXCITATIONS.includes(e);
      knobs.harmonic.el.classList.toggle('hidden', e !== EXC_HARMONIC);
      knobs.malletHardness.el.classList.toggle('hidden', e !== EXC_MALLET);
      ebowField.classList.toggle('hidden', e !== EXC_EBOW);
      knobs.bowSpeed.el.classList.toggle('hidden', !bowed && e !== EXC_SPICCATO);
      knobs.bowPressure.el.classList.toggle('hidden', !bowed && e !== EXC_SPICCATO);
      const posLabel = bowed || e === EXC_SPICCATO ? 'Bow Position' : e === EXC_MALLET ? 'Strike Point' : e === EXC_EBOW ? 'Driver Pos' : 'Pluck Point';
      knobs.bowPosition.el.querySelector('.knob-label').textContent = posLabel;
      renderExciteNote();
    }
    if (!group || group === 'body' || group === 'play') {
      const b = BODIES[Math.round(values.body)];
      $('#body-picture').innerHTML = bodyPictureSvg(b);
      $('#body-info').innerHTML =
        `<div class="row"><span>Strings</span><span>${b.strings.map(noteNameOf).join(' ')}</span></div>` +
        `<div class="row"><span>Range</span><span>${noteNameOf(b.rangeLo)}–${noteNameOf(b.rangeHi)}</span></div>` +
        `<div>${b.fretted ? '<span class="tag">FRETTED</span>' : ''}${b.electric ? '<span class="tag">PIEZO</span>' : '<span class="tag">ACOUSTIC</span>'}</div>` +
        `<div class="desc">${esc(BODY_DESC[Math.round(values.body)] || '')}</div>`;
      $('#amp-strip').classList.toggle('show', !!b.electric);
      $('#body-note').textContent = b.electric ? 'amp / DI stage active' : '';
      // keyswitch keys on the keyboard
      const ks = Math.round(values.keyswitchOctave);
      piano.setKeyswitches(ks === 0 ? -1 : (ks - 1) * 12, KS_LABELS);
    }
    if (!group || group === 'symp' || group === 'body') {
      const mode = Math.round(values.sympMode);
      $('#panel-symp').classList.toggle('off', values.sympOn < 0.5);
      $('#symp-selects [data-field="sympKey"]').classList.toggle('hidden', mode !== SYMP_SCALE);
      $('#symp-selects [data-field="sympScale"]').classList.toggle('hidden', mode !== SYMP_SCALE);
      noteGrid.classList.toggle('show', mode === SYMP_CUSTOM);
      const notes = sympatheticNotes(values);
      $('#symp-tuning').innerHTML = `<b>${notes.length}</b> strings: ${notes.map(noteNameOf).join(' ')}`;
      for (let i = 1; i <= MAX_SYMPATHETIC; i++) noteGrid.children[i - 1].style.visibility = i <= Math.round(values.sympCount) ? '' : 'hidden';
    }
    if (!group || group === 'strings') {
      const m = PARAM_BY_ID.material.choices[Math.round(values.material)];
      $('#strings-note').textContent = m;
    }
  }

  function renderExciteNote() {
    const chosen = Math.round(values.excitation);
    const note = $('#excite-note');
    if (liveExcitation >= 0 && liveExcitation !== chosen) {
      note.textContent = `keyswitch: ${EXCITATIONS[liveExcitation]}`;
      note.classList.add('ks');
    } else {
      note.textContent = '';
      note.classList.remove('ks');
    }
  }

  // ------------------------------------------------------------ keybed ----
  const piano = new Piano($('#piano'), {
    numKeys: 24, baseNote: 36,
    onNoteOn: (n, v) => noteOn(n, v),
    onNoteOff: (n) => noteOff(n),
  });

  function noteOn(note, vel) { engine.noteOn(note, vel); piano.setNote(note, true); }
  function noteOff(note) { engine.noteOff(note); piano.setNote(note, false); }

  engine.on('notes', ({ on, off }) => {
    off.forEach(n => piano.setNote(n, false));
    on.forEach(n => piano.setNote(n, true));
    flashLed();
    if (on.length) logMidi(`NOTE ON   ${on.map(n => `${midiNoteName(n)} (${n})`).join('  ')}`);
    else if (off.length) logMidi(`NOTE OFF  ${off.map(n => `${midiNoteName(n)} (${n})`).join('  ')}`);
  });

  function setOctave(base) {
    base = clamp(base, 0, 96);
    for (const [k, n] of heldKeys) { noteOff(n); heldKeys.delete(k); }
    piano.setBase(base);
    $('#octave-readout').textContent = `${midiNoteName(base)}–${midiNoteName(base + 23)}`;
  }
  $('#oct-down').addEventListener('click', () => setOctave(piano.base - 12));
  $('#oct-up').addEventListener('click', () => setOctave(piano.base + 12));

  // Computer keyboard: two rows, A = C
  const KEYMAP = { a: 0, w: 1, s: 2, e: 3, d: 4, f: 5, t: 6, g: 7, y: 8, h: 9, u: 10, j: 11,
                   k: 12, o: 13, l: 14, p: 15, ';': 16, "'": 17 };
  const heldKeys = new Map();
  window.addEventListener('keydown', (e) => {
    if (e.repeat || e.ctrlKey || e.metaKey || e.altKey) return;
    if (/^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName) || !modal.classList.contains('hidden')) return;
    const key = e.key.toLowerCase();
    if (key === 'z') { setOctave(piano.base - 12); return; }
    if (key === 'x') { setOctave(piano.base + 12); return; }
    if (key in KEYMAP && !heldKeys.has(key)) {
      const note = piano.base + KEYMAP[key];
      heldKeys.set(key, note);
      noteOn(note, 100);
    }
  });
  window.addEventListener('keyup', (e) => {
    const key = e.key.toLowerCase();
    if (heldKeys.has(key)) { noteOff(heldKeys.get(key)); heldKeys.delete(key); }
  });
  window.addEventListener('blur', () => { for (const [, n] of heldKeys) noteOff(n); heldKeys.clear(); });

  $('#panic-btn').addEventListener('click', () => {
    engine.panic();
    piano.clear();
    heldKeys.clear();
    logMidi('PANIC: all voices stopped');
  });

  const wheel = new ModWheel((v) => engine.modWheel(v));
  $('#wheel-slot').replaceWith(wheel.el);

  // ------------------------------------------------------------- stats -----
  const meterFill = $('#meter-fill');
  const meterClip = document.querySelector('.meter-clip');
  const sustainLed = $('#sustain-led');
  const midiLed = $('#midi-led');
  let clipTimer = 0, ledTimer = 0;
  function flashLed() { midiLed.classList.add('on'); clearTimeout(ledTimer); ledTimer = setTimeout(() => midiLed.classList.remove('on'), 120); }
  engine.on('stats', ({ voices, peak, sustain, modWheel, excitation }) => {
    $('#voice-count').textContent = voices;
    sustainLed.classList.toggle('on', !!sustain);
    if (typeof modWheel === 'number') wheel.setValue(modWheel);
    if (typeof excitation === 'number' && excitation !== liveExcitation) { liveExcitation = excitation; renderExciteNote(); }
    const db = 20 * Math.log10(Math.max(peak, 1e-5));
    meterFill.style.width = `${clamp((db + 60) / 60, 0, 1) * 100}%`;
    if (peak > 0.95) {
      meterClip.classList.add('on');
      clearTimeout(clipTimer);
      clipTimer = setTimeout(() => meterClip.classList.remove('on'), 800);
    }
  });

  let connectedOnce = false;
  engine.on('config', ({ sampleRate, blockSize, wrapper, audio, running }) => {
    const web = wrapper === 'Web';
    document.body.classList.toggle('web', web);
    const sr = sampleRate > 0 ? `${(sampleRate / 1000).toFixed(1)} kHz · ${blockSize} smp` : 'not playing yet';
    $('#audio-info').textContent = audio || `${wrapper} · ${sr}`;
    $('#audio-led').classList.toggle('on', running !== false);
    if (!connectedOnce) logMidi('Connected: play your MIDI controller, the keys, or A–\' on the computer keyboard');
    connectedOnce = true;
    fit();
  });

  const midiSelect = $('#midi-select');
  engine.on('midiInputs', ({ inputs, selected, error }) => {
    midiSelect.innerHTML = '';
    if (error) { midiSelect.appendChild(new Option('MIDI unavailable', 'all')); midiSelect.disabled = true; return; }
    midiSelect.disabled = false;
    midiSelect.appendChild(new Option(inputs.length ? `All inputs (${inputs.length})` : 'No MIDI devices', 'all'));
    inputs.forEach(d => midiSelect.appendChild(new Option(d.name + (d.state === 'connected' ? '' : ' (off)'), d.id)));
    midiSelect.value = Array.from(midiSelect.options).some(o => o.value === selected) ? selected : 'all';
  });
  midiSelect.addEventListener('change', () => engine.selectMidi(midiSelect.value));

  const monitor = $('#midi-monitor');
  function logMidi(text) { monitor.textContent = text; }

  // --------------------------------------------------------- fit window ----
  const synth = $('.synth');
  const coarse = window.matchMedia ? window.matchMedia('(pointer: coarse)') : null;
  function fit() {
    const body = document.body, vw = window.innerWidth, vh = window.innerHeight;
    body.classList.remove('compact', 'portrait');
    synth.style.zoom = '';
    const w = synth.offsetWidth + 24, h = synth.offsetHeight + 24;
    const s = Math.min(vw / w, vh / h);
    const compact = !!(coarse && coarse.matches) && (vh > vw || s < 0.8 * (vw / w));
    const svg = $('.piano-svg');
    if (svg) svg.setAttribute('preserveAspectRatio', compact && vh <= vw ? 'none' : 'xMidYMid meet');
    if (!compact) { synth.style.transform = `translateX(-50%) scale(${s})`; return; }
    body.classList.add('compact');
    body.classList.toggle('portrait', vh > vw);
    synth.style.transform = '';
    synth.style.zoom = String(Math.min(1.4, vw / (synth.offsetWidth + 12)));
  }
  window.addEventListener('resize', fit);

  // Right-click on any control = the host's parameter menu. Shift+right-click keeps the browser menu (Inspect).
  document.addEventListener('contextmenu', (e) => {
    if (!engine.connected || e.shiftKey) return;
    e.preventDefault();
    const ctl = e.target.closest('[data-param]');
    if (ctl) engine.paramMenu(ctl.dataset.param, e.clientX, e.clientY);
  });

  // -------------------------------------------------------------- init -----
  // UI preview outside the plugin: open index.html#body=6,excitation=8,sympOn=1 to see those settings
  if (!engine.connected && location.hash.length > 1) {
    for (const pair of location.hash.slice(1).split(',')) {
      const [id, v] = pair.split('=');
      if (PARAM_BY_ID[id] && v !== undefined && !isNaN(Number(v))) values[id] = clamp(Number(v), PARAM_BY_ID[id].min, PARAM_BY_ID[id].max);
    }
    for (const id in values) applyValue(id, values[id]);
  }
  renderPresetName();
  renderBrowser();
  refresh();
  setOctave(36);
  fit();
  if (engine.connected) engine.uiReady();
  else logMidi('Not running inside the plugin: no audio engine connected (UI preview only)');
})();
