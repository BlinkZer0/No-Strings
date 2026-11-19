// ============================================================================
//  24-key SVG piano in ebony and boxwood: naturals are dark, sharps are pale.
//  Keyswitch keys (see setKeyswitches) are marked and labelled.
//  Click/touch to play, drag for glissando. Velocity = how far down the key
//  you click (lower = harder), like striking nearer the front edge.
// ============================================================================
'use strict';

const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const BLACK_PCS = new Set([1, 3, 6, 8, 10]);

function midiNoteName(n) { return NOTE_NAMES[n % 12] + (Math.floor(n / 12) - 1); }

class Piano {
  constructor(container, { numKeys = 24, baseNote = 48, onNoteOn, onNoteOff }) {
    this.numKeys = numKeys;
    this.base = baseNote; // must be a C
    this.onNoteOn = onNoteOn;
    this.onNoteOff = onNoteOff;
    this.active = new Set();     // sounding MIDI notes (from any source)
    this.ks = { lo: -1, hi: -1, labels: [] };   // keyswitch range + short labels
    this.pointers = new Map();   // pointerId -> note
    this.keys = [];
    this.build(container);
  }

  build(container) {
    const WW = 40, WH = 200, BW = 24, BH = 122, TOP = 10;
    const whites = [], blacks = [];
    let wi = 0;
    for (let i = 0; i < this.numKeys; i++) {
      if (BLACK_PCS.has(i % 12)) {
        blacks.push(`<rect class="bkey" data-idx="${i}" x="${wi * WW - BW / 2}" y="${TOP}" width="${BW}" height="${BH}" rx="3"/>`);
      } else {
        whites.push(`<rect class="wkey" data-idx="${i}" x="${wi * WW + 1}" y="${TOP}" width="${WW - 2}" height="${WH}" rx="4"/>`);
        if (i % 12 === 0) whites.push(`<text class="klabel" data-label="${i}" x="${wi * WW + WW / 2}" y="${TOP + WH - 12}"></text>`);
        wi++;
      }
    }
    const W = wi * WW;
    container.innerHTML = `
      <svg class="piano-svg" viewBox="0 0 ${W} ${TOP + WH + 2}" preserveAspectRatio="xMidYMid meet">
        ${whites.join('')}
        ${blacks.join('')}
        <rect class="felt" x="0" y="0" width="${W}" height="${TOP + 2}"/>
      </svg>`;
    this.svg = container.querySelector('svg');
    this.keys = Array.from({ length: this.numKeys }, (_, i) => this.svg.querySelector(`[data-idx="${i}"]`));
    this.labels = Array.from(this.svg.querySelectorAll('[data-label]'));
    this.bindPointer();
    this.render();
  }

  setBase(note) {
    this.base = note;
    this.render();
  }

  // Keyswitch keys: MIDI lo..lo+labels.length-1 (lo < 0 = none)
  setKeyswitches(lo, labels) {
    this.ks = { lo, hi: lo + labels.length - 1, labels };
    this.render();
  }

  setNote(note, on) {
    if (on) this.active.add(note); else this.active.delete(note);
    const i = note - this.base;
    if (i >= 0 && i < this.numKeys) this.keys[i].classList.toggle('down', on);
  }

  clear() {
    this.active.clear();
    this.render();
  }

  render() {
    this.keys.forEach((k, i) => {
      const n = this.base + i;
      k.classList.toggle('down', this.active.has(n));
      const isKs = n >= this.ks.lo && n <= this.ks.hi;
      k.classList.toggle('ks', isKs);
    });
    this.labels.forEach(t => { t.textContent = midiNoteName(this.base + Number(t.dataset.label)); });
    // keyswitch labels sit on the keys themselves
    this.svg.querySelectorAll('.kslabel').forEach(e => e.remove());
    if (this.ks.lo >= 0) {
      for (let i = 0; i < this.numKeys; i++) {
        const n = this.base + i;
        if (n < this.ks.lo || n > this.ks.hi) continue;
        const k = this.keys[i];
        const x = Number(k.getAttribute('x')) + Number(k.getAttribute('width')) / 2;
        const black = k.classList.contains('bkey');
        const t = document.createElementNS('http://www.w3.org/2000/svg', 'text');
        t.setAttribute('class', 'kslabel' + (black ? ' on-black' : ''));
        t.setAttribute('x', x);
        t.setAttribute('y', black ? 60 : 150);
        t.setAttribute('transform', `rotate(-90 ${x} ${black ? 60 : 150})`);
        t.textContent = this.ks.labels[n - this.ks.lo];
        this.svg.appendChild(t);
      }
    }
  }

  hit(x, y) {
    const t = document.elementFromPoint(x, y);
    if (!t || !this.svg.contains(t) || !t.dataset.idx) return null;
    const r = t.getBoundingClientRect();
    const depth = clamp((y - r.top) / r.height, 0, 1);
    return { note: this.base + Number(t.dataset.idx), vel: Math.round(30 + depth * 97) };
  }

  bindPointer() {
    const svg = this.svg;
    const press = (id, x, y) => {
      const h = this.hit(x, y);
      if (!h) return;
      this.pointers.set(id, h.note);
      this.onNoteOn(h.note, h.vel);
    };
    const lift = (id) => {
      const n = this.pointers.get(id);
      if (n !== undefined && n !== null) this.onNoteOff(n);
      this.pointers.delete(id);
    };
    svg.addEventListener('pointerdown', (e) => {
      e.preventDefault();
      svg.setPointerCapture(e.pointerId);
      press(e.pointerId, e.clientX, e.clientY);
    });
    svg.addEventListener('pointermove', (e) => {
      if (!this.pointers.has(e.pointerId)) return;
      const h = this.hit(e.clientX, e.clientY);
      const cur = this.pointers.get(e.pointerId);
      const next = h ? h.note : null;
      if (next === cur) return;
      if (cur !== null) this.onNoteOff(cur);
      this.pointers.set(e.pointerId, next);
      if (h) this.onNoteOn(h.note, h.vel);
    });
    svg.addEventListener('pointerup', (e) => lift(e.pointerId));
    svg.addEventListener('pointercancel', (e) => lift(e.pointerId));
  }
}
