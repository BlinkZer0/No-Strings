// ============================================================================
//  No Strings DSP engine: JavaScript twin of vst/core (Engine.h and friends).
//  Kept line-for-line equivalent (same control rate, same RNG, same maths,
//  same denormal flushes) and checked against the C++ build by
//  vst/tests/run-parity.ps1.
//
//  This whole function is stringified and loaded as an AudioWorklet module
//  (or run on the main thread as a ScriptProcessor fallback), so it must not
//  reference anything outside itself. The body and material tables come in
//  through processorOptions.tables (generated into params.js from the C++
//  tables by NoStringsGenParams).
// ============================================================================
function nostringsWorkletModule() {
  'use strict';

  // ------------------------------------------------------------- Common ---
  const PI = 3.141592653589793, TWO_PI = 6.283185307179586, TINY = 1e-18;
  const clampd = (x, lo, hi) => (x < lo ? lo : (x > hi ? hi : x));
  const lerp = (a, b, t) => a + (b - a) * t;
  const dbToGain = (db) => Math.pow(10, db / 20);
  const midiToHz = (note) => 440 * Math.pow(2, (note - 69) / 12);
  const smoothCoef = (seconds, sr) => 1 - Math.exp(-1 / (Math.max(seconds, 1e-5) * sr));

  // Deterministic RNG: same sequence as nostrings::Rng (C++)
  class Rng {
    constructor() { this.s = 0x2545F491; }
    seed(v) { this.s = v >>> 0; }
    next() { this.s = (Math.imul(this.s, 1664525) + 1013904223) >>> 0; return this.s; }
    uni() { return (this.next() >>> 8) * (1 / 16777216); }
    bi() { return this.uni() * 2 - 1; }
  }

  // -------------------------------------------------------------- Delay ---
  class Delay {
    constructor() { this.buf = new Float64Array(16); this.mask = 15; this.w = 0; }
    allocate(maxDelay) {
      let n = 16;
      while (n < maxDelay + 8) n <<= 1;
      this.buf = new Float64Array(n);
      this.mask = n - 1;
      this.w = 0;
    }
    clear() { this.buf.fill(0); this.w = 0; }
    write(x) { this.buf[this.w] = x; this.w = (this.w + 1) & this.mask; }
    tapInt(d) { return this.buf[(this.w - d) & this.mask]; }
    tap(d) {
      const i = Math.trunc(d);
      const t = d - i;
      const xm1 = this.tapInt(i - 1), x0 = this.tapInt(i), x1 = this.tapInt(i + 1), x2 = this.tapInt(i + 2);
      const tp1 = t + 1, tm1 = t - 1, tm2 = t - 2;
      const hm1 = -t * tm1 * tm2 * (1 / 6);
      const h0 = tp1 * tm1 * tm2 * 0.5;
      const h1 = -tp1 * t * tm2 * 0.5;
      const h2 = tp1 * t * tm1 * (1 / 6);
      return hm1 * xm1 + h0 * x0 + h1 * x1 + h2 * x2;
    }
  }

  // ------------------------------------------------------------ Filters ---
  class OnePoleLP {
    constructor() { this.b0 = 1; this.p = 0; this.y1 = 0; }
    reset() { this.y1 = 0; }
    process(x) { this.y1 = this.b0 * x + this.p * this.y1; return this.y1; }
    static magnitude(b0, p, w) { return b0 / Math.sqrt(1 - 2 * p * Math.cos(w) + p * p); }
    static phaseDelay(p, w) { return Math.atan2(p * Math.sin(w), 1 - p * Math.cos(w)) / w; }
  }

  class Allpass1 {
    constructor() { this.a = 0; this.x1 = 0; this.y1 = 0; }
    reset() { this.x1 = 0; this.y1 = 0; }
    process(x) { const y = this.a * x + this.x1 - this.a * this.y1; this.x1 = x; this.y1 = y; return y; }
    static phaseDelay(a, w) {
      let ph = Math.atan2(-Math.sin(w), a + Math.cos(w)) - Math.atan2(-a * Math.sin(w), 1 + a * Math.cos(w));
      while (ph > 0) ph -= TWO_PI;
      while (ph < -TWO_PI) ph += TWO_PI;
      return -ph / w;
    }
  }

  class DcBlocker {
    constructor() { this.r = 0.999; this.x1 = 0; this.y1 = 0; }
    setup(hz, sr) { this.r = 1 - TWO_PI * hz / sr; }
    reset() { this.x1 = 0; this.y1 = 0; }
    process(x) {
      let y = x - this.x1 + this.r * this.y1;
      if (Math.abs(y) < 1e-15) y = 0;
      this.x1 = x; this.y1 = y;
      return y;
    }
  }

  // ------------------------------------------------------------- Biquad ---
  class Biquad {
    constructor() { this.b0 = 1; this.b1 = 0; this.b2 = 0; this.a1 = 0; this.a2 = 0; this.z1 = 0; this.z2 = 0; }
    reset() { this.z1 = 0; this.z2 = 0; }
    process(x) {
      const y = this.b0 * x + this.z1;
      this.z1 = this.b1 * x - this.a1 * y + this.z2;
      this.z2 = this.b2 * x - this.a2 * y;
      if (Math.abs(this.z1) < 1e-15) this.z1 = 0;
      if (Math.abs(this.z2) < 1e-15) this.z2 = 0;
      return y;
    }
    copyCoefs(d) { this.b0 = d.b0; this.b1 = d.b1; this.b2 = d.b2; this.a1 = d.a1; this.a2 = d.a2; }
    // returns [re, im] of H at angular frequency w
    response(w) {
      const c1 = Math.cos(w), s1 = Math.sin(w), c2 = Math.cos(2 * w), s2 = Math.sin(2 * w);
      const nr = this.b0 + this.b1 * c1 + this.b2 * c2, ni = -(this.b1 * s1 + this.b2 * s2);
      const dr = 1 + this.a1 * c1 + this.a2 * c2, di = -(this.a1 * s1 + this.a2 * s2);
      const den = dr * dr + di * di;
      return [(nr * dr + ni * di) / den, (ni * dr - nr * di) / den];
    }
    static make(b0, b1, b2, a0, a1, a2) {
      const q = new Biquad();
      q.b0 = b0 / a0; q.b1 = b1 / a0; q.b2 = b2 / a0; q.a1 = a1 / a0; q.a2 = a2 / a0;
      return q;
    }
    static wOf(hz, fs) { return TWO_PI * clampd(hz, 1, fs * 0.49) / fs; }
    static lowpass(hz, q, fs) {
      const w = Biquad.wOf(hz, fs), c = Math.cos(w), al = Math.sin(w) / (2 * q);
      return Biquad.make((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    static highpass(hz, q, fs) {
      const w = Biquad.wOf(hz, fs), c = Math.cos(w), al = Math.sin(w) / (2 * q);
      return Biquad.make((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    static bandpass(hz, q, fs) {
      const w = Biquad.wOf(hz, fs), c = Math.cos(w), al = Math.sin(w) / (2 * q);
      return Biquad.make(al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    static peaking(hz, q, db, fs) {
      const A = Math.pow(10, db / 40), w = Biquad.wOf(hz, fs), c = Math.cos(w), al = Math.sin(w) / (2 * q);
      return Biquad.make(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    static lowShelf(hz, db, fs) {
      const A = Math.pow(10, db / 40), w = Biquad.wOf(hz, fs), c = Math.cos(w), s = Math.sin(w);
      const al = s / 2 * Math.sqrt(2), sq = 2 * Math.sqrt(A) * al;
      return Biquad.make(A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
                         (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }
    static highShelf(hz, db, fs) {
      const A = Math.pow(10, db / 40), w = Biquad.wOf(hz, fs), c = Math.cos(w), s = Math.sin(w);
      const al = s / 2 * Math.sqrt(2), sq = 2 * Math.sqrt(A) * al;
      return Biquad.make(A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                         (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
  }

  // ---------------------------------------------------- WaveguideString ---
  const DISPERSION_STAGES = 4, HF_REF_HZ = 2000;

  function newStringSetup() {
    return { hz: 110, beta: 0.1, decay: 6, hfDecay: 1, inharm: 2.25e-5, touch: 0, touchDamp: 0.6,
             couplingHz: [0, 0], couplingQ: [1, 1], couplingC: [0, 0], compensateCoupling: true };
  }

  class WaveguideString {
    constructor() {
      this.fs = 48000; this.smooth = 0.01;
      this.neck = new Delay(); this.bridge = new Delay(); this.aOut = new Delay(); this.aIn = new Delay();
      this.loss = new OnePoleLP();
      this.disp = [new Allpass1(), new Allpass1(), new Allpass1(), new Allpass1()];
      this.dN = 100; this.dB = 10; this.dNT = 100; this.dBT = 10;
      this.touchOn = false;
      this.dA = 40; this.dAT = 40; this.dT = 20; this.dTT = 20; this.touchK = 0.6;
      this.nIn = 0; this.bIn = 0; this.bRaw = 0;
      this.cacheHz = -1; this.cacheB = -1; this.cacheP = -1; this.cacheA = 0;
      this.poleHz = -1; this.poleDecay = -1; this.poleHf = -1; this.pole = 0;
      this.designedHz = [-1, -1]; this.designedQ = [-1, -1];
      this.couple = [new Biquad(), new Biquad()];
      this.couplingC = [0, 0];
      this.coupled = 0;
    }

    prepare(sampleRate, lowestHz = 7.5) {
      this.fs = sampleRate;
      const maxLen = Math.ceil(this.fs / lowestHz) + 8;
      this.neck.allocate(maxLen);
      this.bridge.allocate(maxLen);
      this.aOut.allocate(Math.trunc(maxLen / 2) + 8);
      this.aIn.allocate(Math.trunc(maxLen / 2) + 8);
      this.smooth = smoothCoef(0.004, this.fs);
      this.reset();
    }

    reset() {
      this.neck.clear(); this.bridge.clear(); this.aOut.clear(); this.aIn.clear();
      this.loss.reset();
      for (const ap of this.disp) ap.reset();
      for (const bp of this.couple) bp.reset();
      this.nIn = this.bIn = this.bRaw = 0;
    }

    configure(s, snap) {
      const fs = this.fs;
      const hz = clampd(s.hz, 8, fs * 0.2);
      const w0 = TWO_PI * hz / fs;
      const N = fs / hz;

      const perPeriod = Math.pow(10, -3 / (hz * Math.max(s.decay, 0.01)));
      if (!(Math.abs(hz - this.poleHz) < hz * 0.003 && s.decay === this.poleDecay && s.hfDecay === this.poleHf)) {
        this.poleHz = hz; this.poleDecay = s.decay; this.poleHf = s.hfDecay;
        this.pole = WaveguideString.lossPole(perPeriod, Math.pow(10, -3 / (hz * clampd(s.hfDecay, 0.005, s.decay))), w0,
                                             TWO_PI * Math.min(HF_REF_HZ, fs * 0.45) / fs);
      }
      const p = this.pole;
      const unit = OnePoleLP.magnitude(1 - p, p, w0);
      const g = Math.min(perPeriod / unit, 0.99995);
      this.loss.b0 = g * (1 - p);
      this.loss.p = p;

      let yRe = 0, yIm = 0;
      this.coupled = 0;
      for (let k = 0; k < 2; k++) {
        this.couplingC[k] = clampd(s.couplingC[k], 0, 0.5);
        if (this.couplingC[k] <= 0 || s.couplingHz[k] <= 0) { this.couplingC[k] = 0; continue; }
        if (s.couplingHz[k] !== this.designedHz[k] || s.couplingQ[k] !== this.designedQ[k]) {
          this.couple[k].copyCoefs(Biquad.bandpass(s.couplingHz[k], s.couplingQ[k], fs));
          this.designedHz[k] = s.couplingHz[k]; this.designedQ[k] = s.couplingQ[k];
        }
        const [re, im] = this.couple[k].response(w0);
        yRe += this.couplingC[k] * re; yIm += this.couplingC[k] * im;
        this.coupled++;
      }
      const bridgeDelay = s.compensateCoupling ? -Math.atan2(-2 * yIm, 1 - 2 * yRe) / w0 : 0;

      let a = this.dispersionCoef(hz, N, w0, p, clampd(s.inharm, 0, 0.01));
      let filt = 0;
      for (let tries = 0; tries < 8; tries++) {
        filt = bridgeDelay + OnePoleLP.phaseDelay(p, w0) + DISPERSION_STAGES * Allpass1.phaseDelay(a, w0);
        if (N - filt >= 6 || a === 0) break;
        a *= 0.5;
        if (a > -0.01) a = 0;
      }
      for (const ap of this.disp) ap.a = a;

      const loop = Math.max(N - filt, 4.5);
      this.dBT = clampd(s.beta * loop, 2, loop - 2.5);
      this.dNT = loop - this.dBT;

      const wantTouch = s.touch > 0 && this.dNT >= 8;
      if (wantTouch !== this.touchOn) { this.reset(); snap = true; this.touchOn = wantTouch; }
      if (this.touchOn) {
        this.dTT = clampd(s.touch * N - filt, 2, this.dNT - 4);
        this.dAT = 0.5 * (this.dNT - this.dTT);
        this.touchK = clampd(s.touchDamp, 0, 1);
      }
      if (snap) { this.dB = this.dBT; this.dN = this.dNT; this.dT = this.dTT; this.dA = this.dAT; }
    }

    incoming() {
      if (!this.touchOn) {
        this.nIn = -this.nutReturn(this.neck.tap(this.dN));
      } else {
        this.nIn = this.aIn.tap(this.dA);
        const fromContact = this.aOut.tap(this.dA);
        const fromNut = -this.nutReturn(this.neck.tap(this.dT));
        const ft = -(fromContact + fromNut) * this.touchK;
        this.aIn.write(fromNut + ft + TINY);
        this.neck.write(fromContact + ft + TINY);
      }
      this.bRaw = this.bridge.tap(this.dB);
      this.bIn = -this.bRaw;
      if (this.coupled > 0)
        for (let k = 0; k < 2; k++)
          if (this.couplingC[k] > 0) this.bIn += 2 * this.couplingC[k] * this.couple[k].process(this.bRaw);
      return this.nIn + this.bIn;
    }

    commit(injected) {
      if (!this.touchOn) this.neck.write(this.bIn + injected + TINY);
      else this.aOut.write(this.bIn + injected + TINY);
      this.bridge.write(this.nIn + injected + TINY);
      this.dB += (this.dBT - this.dB) * this.smooth;
      this.dN += (this.dNT - this.dN) * this.smooth;
      this.dA += (this.dAT - this.dA) * this.smooth;
      this.dT += (this.dTT - this.dT) * this.smooth;
      return this.bRaw;
    }

    addStaticDisplacement(height) {
      const vb = height * this.fs / this.dB;
      const vn = height * this.fs / this.dN;
      WaveguideString.addToLine(this.bridge, Math.trunc(this.dB) + 4, vb);
      if (!this.touchOn) {
        WaveguideString.addToLine(this.neck, Math.trunc(this.dN) + 4, vn);
      } else {
        WaveguideString.addToLine(this.aOut, Math.trunc(this.dA) + 4, vn);
        WaveguideString.addToLine(this.neck, Math.trunc(this.dT) + 4, vn);
        WaveguideString.addToLine(this.aIn, Math.trunc(this.dA) + 4, -vn);
      }
      this.loss.y1 += vn * this.loss.b0 / (1 - this.loss.p);
      const dcGain = this.loss.b0 / (1 - this.loss.p);
      for (const ap of this.disp) { ap.x1 += vn * dcGain; ap.y1 += vn * dcGain; }
      return this.touchOn ? vb + vn : vb + vn * dcGain;
    }

    dispersionCoef(hz, N, w0, lossP, B) {
      if (Math.abs(hz - this.cacheHz) < hz * 0.003 && Math.abs(B - this.cacheB) <= B * 0.01 && Math.abs(lossP - this.cacheP) < 1e-3) return this.cacheA;
      this.cacheHz = hz; this.cacheB = B; this.cacheP = lossP;
      const kr = clampd(Math.floor(Math.min(3000, this.fs * 0.2) / hz), 2, 60);
      const wr = w0 * kr;
      const need = N * (1 / Math.sqrt(1 + B) - 1 / Math.sqrt(1 + B * kr * kr))
                 - (OnePoleLP.phaseDelay(lossP, w0) - OnePoleLP.phaseDelay(lossP, wr));
      if (B <= 0 || need <= 0 || kr * hz >= this.fs * 0.45) return (this.cacheA = 0);
      const spread = (a) => DISPERSION_STAGES * (Allpass1.phaseDelay(a, w0) - Allpass1.phaseDelay(a, wr));
      let lo = -0.98, hi = 0;
      if (spread(lo) <= need) return (this.cacheA = lo);
      for (let it = 0; it < 40; it++) {
        const mid = 0.5 * (lo + hi);
        if (spread(mid) > need) lo = mid; else hi = mid;
      }
      return (this.cacheA = 0.5 * (lo + hi));
    }

    static lossPole(per0, perHf, w0, wr) {
      if (wr <= w0 * 1.2) return 0;
      const want = clampd(perHf / per0, 1e-9, 1);
      const c0 = Math.cos(w0), cr = Math.cos(wr);
      const ratio = (p) => Math.sqrt((1 - 2 * p * c0 + p * p) / (1 - 2 * p * cr + p * p));
      let lo = 0, hi = 0.995;
      if (ratio(hi) >= want) return hi;
      for (let it = 0; it < 30; it++) {
        const mid = 0.5 * (lo + hi);
        if (ratio(mid) > want) lo = mid; else hi = mid;
      }
      return 0.5 * (lo + hi);
    }

    nutReturn(x) {
      x = this.loss.process(x);
      for (const ap of this.disp) x = ap.process(x);
      return x;
    }

    static addToLine(d, count, v) {
      for (let k = 1; k <= count; k++) d.buf[(d.w - k) & d.mask] += v;
    }
  }

  // ---------------------------------------------------------------- Bow ---
  class BowFriction {
    constructor() {
      this.muS = 0.8; this.muD = 0.3; this.v0 = 0.1;
      this.rho = 1; this.torsA = 0.9; this.torsHp = 0; this.torsF1 = 0;
      this.slipping = false; this.stuckFor = 0; this.slipCount = 0;
    }
    reset() { this.slipping = false; this.stuckFor = 0; this.slipCount = 0; this.torsHp = 0; this.torsF1 = 0; }
    setup(stringHz, sampleRate, torsionalRatio = 6) {
      this.torsA = Math.exp(-TWO_PI * Math.min(stringHz * torsionalRatio, sampleRate * 0.4) / sampleRate);
    }
    solve(vh, vb, fb) {
      const c = this.torsA * (this.torsHp - this.torsF1);
      const k = 1 + this.rho * this.torsA;
      let f = 0;
      if (fb <= 1e-9) {
        this.slipping = true;
      } else {
        const w = vb - (vh + this.rho * c);
        const aw = Math.abs(w);
        const sgn = w >= 0 ? 1 : -1;
        const fk = fb * k;
        let slip = aw > this.muS * fk;
        let u = 0;
        if (slip || this.slipping) {
          const b = this.v0 + fk * this.muD - aw;
          const cc = this.v0 * (fk * this.muS - aw);
          const disc = b * b - 4 * cc;
          if (disc >= 0) {
            u = 0.5 * (-b + Math.sqrt(disc));
            slip = u > 0;
          } else {
            slip = false;
          }
        }
        if (slip && !this.slipping && this.stuckFor >= 4) this.slipCount++;
        this.stuckFor = slip ? 0 : this.stuckFor + 1;
        this.slipping = slip;
        f = (slip ? sgn * (aw - u) : w) / k;
      }
      this.torsHp = c + this.torsA * f;
      if (Math.abs(this.torsHp) < 1e-15) this.torsHp = 0;
      this.torsF1 = f;
      return f;
    }
  }

  // ----------------------------------------------------------- Exciters ---
  const EXC_ARCO = 0, EXC_PIZZ = 1, EXC_BARTOK = 2, EXC_BATTUTO = 3, EXC_TRATTO = 4, EXC_SPICCATO = 5,
        EXC_HARMONIC = 6, EXC_MALLET = 7, EXC_EBOW = 8, NUM_EXCITATIONS = 9;
  const isBowed = (e) => e === EXC_ARCO || e === EXC_TRATTO || e === EXC_SPICCATO || e === EXC_HARMONIC;

  function contactBeta(exc, position) {
    const p = clampd(position, 0, 1);
    const span = (tasto, ponticello) => Math.exp(lerp(Math.log(tasto), Math.log(ponticello), p));
    switch (exc) {
      case EXC_PIZZ: case EXC_BARTOK: return span(0.45, 0.10);
      case EXC_MALLET: return span(0.30, 0.04);
      case EXC_EBOW: return span(0.35, 0.08);
      default: return span(0.235, 0.03);
    }
  }

  class PluckExciter {
    constructor() { this.hold = 0; this.ramp = 1; this.rampInc = 0; this.collide = false; this.gap = 0; this.xs = 0; this.leak = 1; this.fs = 48000; }
    start(s, displacement, releaseTime, sampleRate, bartok) {
      this.fs = sampleRate;
      const stillHeld = this.ramp < 1 ? this.hold * (0.5 + 0.5 * Math.cos(PI * this.ramp)) : 0;
      this.hold = s.addStaticDisplacement(displacement) + stillHeld;
      this.ramp = 0;
      this.rampInc = 1 / Math.max(1, releaseTime * this.fs);
      this.xs += displacement;
      this.leak = 1 - TWO_PI * 4 / this.fs;
      this.collide = bartok;
      this.gap = 0.45 * displacement;
    }
    busy() { return this.ramp < 1 || this.collide; }
    stopColliding() { this.collide = false; }
    process(vh) {
      let f = 0;
      if (this.ramp < 1) {
        this.ramp = Math.min(1, this.ramp + this.rampInc);
        f = this.hold * (0.5 + 0.5 * Math.cos(PI * this.ramp));
      }
      if (this.collide) {
        const next = this.xs + (vh + f) / this.fs;
        if (next < -this.gap) f = (-this.gap - this.xs) * this.fs - vh;
        this.xs = (this.xs + (vh + f) / this.fs) * this.leak;
      }
      return f;
    }
  }

  class HammerExciter {
    constructor() {
      this.active = false; this.xh = 0; this.vham = 0; this.xs = 0; this.mass = 0.004; this.kRef = 1.75; this.power = 2.5;
      this.leak = 1; this.fs = 48000; this.samples = 0; this.bounceIn = -1; this.bounceVel = 0;
    }
    start(velocity, massSec, stiffness, p, sampleRate, bounceDelay = 0, bounceRatio = 0) {
      this.fs = sampleRate;
      this.mass = massSec; this.kRef = stiffness; this.power = p;
      this.leak = 1 - TWO_PI * 4 / this.fs;
      this.launch(velocity);
      this.bounceIn = bounceDelay > 0 ? Math.trunc(bounceDelay * this.fs) : -1;
      this.bounceVel = velocity * bounceRatio;
    }
    busy() { return this.active || this.bounceIn >= 0; }
    process(vh) {
      if (!this.active && this.bounceIn >= 0 && --this.bounceIn < 0) this.launch(this.bounceVel);
      if (!this.active) {
        this.xs = (this.xs + vh / this.fs) * this.leak;
        return 0;
      }
      const fs = this.fs;
      const a = 1 / fs + 1 / (this.mass * fs * fs);
      const cFree = (this.xh + this.vham / fs) - (this.xs + vh / fs);
      let F = 0;
      if (cFree > 0) {
        let lo = 0, hi = cFree / a;
        for (let it = 0; it < 28; it++) {
          const mid = 0.5 * (lo + hi);
          if (mid > this.kRef * Math.pow((cFree - a * mid) * 1e4, this.power)) hi = mid; else lo = mid;
        }
        F = 0.5 * (lo + hi);
      }
      this.vham -= F / (this.mass * fs);
      this.xh += this.vham / fs;
      this.xs = (this.xs + (vh + F) / fs) * this.leak;
      if (++this.samples > 8 && F === 0 && (this.vham <= 0 || this.samples > Math.trunc(0.02 * fs))) this.active = false;
      return F;
    }
    launch(velocity) { this.active = velocity > 0; this.xh = this.xs; this.vham = velocity; this.samples = 0; }
  }

  class EBowExciter {
    constructor() {
      this.active = false; this.harmonic = false;
      this.target = 0; this.targetEnd = 0; this.targetStep = 0;
      this.focus = new Biquad();
      this.env = 0; this.envCoef = 0.99; this.drive = 0; this.driveCoef = 0.01;
      this.rng = new Rng();
    }
    setup(hz, fs, harmonicMode) {
      this.harmonic = harmonicMode;
      this.focus = Biquad.bandpass(Math.min(hz * (harmonicMode ? 2 : 1), fs * 0.4), harmonicMode ? 3 : 1.5, fs);
      this.envCoef = Math.exp(-1 / (2 * fs / hz));
      this.driveCoef = smoothCoef(Math.max(0.01, 3 / hz), fs);
    }
    start(swellTime, level, fs) {
      if (!this.active) this.target = 0;
      this.active = true;
      this.targetEnd = level;
      this.targetStep = level / Math.max(1, swellTime * fs);
    }
    stop() { this.active = false; }
    busy() { return this.active || this.drive > 1e-6; }
    process(vh) {
      const MAX_GAIN = 0.3;
      const x = this.focus.process(vh);
      this.env = Math.max(Math.abs(x), this.env * this.envCoef);
      let want = 0;
      if (this.active) {
        this.target = Math.min(this.targetEnd, this.target + this.targetStep);
        want = this.target > 0 ? MAX_GAIN * clampd((this.target - this.env) / this.target, -1, 1) : 0;
      }
      this.drive += (want - this.drive) * this.driveCoef;
      if (!this.active && Math.abs(this.drive) < 1e-6) { this.drive = 0; return 0; }
      return this.drive * this.targetEnd * Math.tanh(x / Math.max(this.targetEnd, 1e-3)) + (this.active ? 1e-4 * this.rng.bi() : 0);
    }
  }

  // ---------------------------------------------------------- Materials ---
  function stringResponse(materials, material, tension, age) {
    const m = materials[material >= 0 && material < materials.length ? material : 2];
    const t = clampd(tension, 0, 1), a = clampd(age, 0, 1);
    return {
      decay: m.decay * lerp(0.8, 1.2, t) / lerp(0.8, 1.2, 0.5) * Math.pow(2, (0.25 - a) * 0.8),
      hfRatio: m.hfRatio * lerp(0.8, 1.25, t) / lerp(0.8, 1.25, 0.5) * Math.pow(2, (0.25 - a) * 3.5),
      stiffness: clampd(m.stiffness * (0.5 / Math.max(0.25, lerp(0.25, 0.75, t))) + 0.12 * (a - 0.25), 0, 1),
      muS: m.muS, muD: m.muD, v0: m.v0, rho: m.rho,
      glide: m.glide * Math.pow(4, 1 - 2 * t),
      drawScale: Math.pow(2, 1 - 2 * t) * 0.5 + 0.5,
      pluckScale: lerp(1.3, 0.8, t) / lerp(1.3, 0.8, 0.5),
      forceScale: 0.5 / Math.max(0.1, m.muS - m.muD),
    };
  }

  // -------------------------------------------------------- Sympathetic ---
  const MAX_SYMPATHETIC = 12;
  const SYMP_SCALE = 0, SYMP_OPEN = 1;
  const SCALE_DEGREES = [[0, 2, 4, 5, 7, 9, 11], [0, 2, 3, 5, 7, 8, 10], [0, 2, 3, 5, 7, 9, 10], [0, 2, 4, 5, 7, 9, 10],
                         [0, 2, 4, 7, 9], [0, 2, 3, 5, 7, 8, 11], [0, 7]];

  function sympatheticNotes(s, body, out, count) {
    const deg = SCALE_DEGREES[s.scale >= 0 && s.scale < SCALE_DEGREES.length ? s.scale : 0];
    const len = deg.length;
    for (let i = 0; i < count; i++) {
      let n;
      switch (s.mode) {
        case SYMP_SCALE: n = 48 + ((s.key % 12) + 12) % 12 + deg[i % len] + 12 * Math.floor(i / len); break;
        case SYMP_OPEN: n = body.strings[i % body.strings.length] + 12 * Math.floor(i / body.strings.length); break;
        default: n = s.custom[i]; break;
      }
      out[i] = Math.min(Math.max(n, 24), 108);
    }
  }

  function sympEquals(a, b) {
    if (a.on !== b.on || a.amount !== b.amount || a.count !== b.count || a.mode !== b.mode || a.key !== b.key || a.scale !== b.scale
        || a.decay !== b.decay || a.brightness !== b.brightness) return false;
    for (let i = 0; i < MAX_SYMPATHETIC; i++) if (a.custom[i] !== b.custom[i]) return false;
    return true;
  }

  class SympatheticBank {
    constructor() {
      this.fs = 48000; this.hpCoef = 0.99; this.hpX1 = 0; this.hpY1 = 0; this.drive = 0.02; this.outGain = 0.5;
      this.count = 8;
      this.lines = []; this.loss = [];
      for (let i = 0; i < MAX_SYMPATHETIC; i++) { this.lines.push(new Delay()); this.loss.push(new OnePoleLP()); }
      this.delay = new Float64Array(MAX_SYMPATHETIC);
      this.notes = new Int32Array(MAX_SYMPATHETIC);
    }
    prepare(sampleRate) {
      this.fs = sampleRate;
      for (const l of this.lines) l.allocate(Math.ceil(this.fs / 30) + 8);
      this.hpCoef = Math.exp(-TWO_PI * 40 / this.fs);
      this.clear();
    }
    clear() {
      for (const l of this.lines) l.clear();
      for (const f of this.loss) f.reset();
      this.hpX1 = 0; this.hpY1 = 0;
    }
    configure(s, body) {
      const fs = this.fs;
      const previous = this.count;
      this.count = Math.min(Math.max(s.count, 4), MAX_SYMPATHETIC);
      for (let i = previous; i < this.count; i++) { this.lines[i].clear(); this.loss[i].reset(); }
      sympatheticNotes(s, body, this.notes, this.count);
      const hfRatio = lerp(0.03, 0.4, clampd(s.brightness, 0, 1));
      for (let k = 0; k < this.count; k++) {
        const hz = midiToHz(this.notes[k]);
        const w0 = TWO_PI * hz / fs, N = fs / hz;
        const decay = clampd(s.decay, 0.2, 30);
        const per0 = Math.pow(10, -3 / (hz * decay));
        const perHf = Math.pow(10, -3 / (hz * decay * hfRatio));
        const wr = TWO_PI * Math.min(2000, fs * 0.45) / fs;
        let p = 0;
        if (wr > w0 * 1.2) {
          const want = perHf / per0, c0 = Math.cos(w0), cr = Math.cos(wr);
          const ratio = (q) => Math.sqrt((1 - 2 * q * c0 + q * q) / (1 - 2 * q * cr + q * q));
          let lo = 0, hi = 0.995;
          if (ratio(hi) >= want) p = hi;
          else { for (let it = 0; it < 30; it++) { const mid = 0.5 * (lo + hi); if (ratio(mid) > want) lo = mid; else hi = mid; } p = 0.5 * (lo + hi); }
        }
        const g = Math.min(per0 / OnePoleLP.magnitude(1 - p, p, w0), 0.99995);
        this.loss[k].b0 = g * (1 - p);
        this.loss[k].p = p;
        this.delay[k] = Math.max(N - OnePoleLP.phaseDelay(p, w0), 4);
      }
      this.drive = 0.02;
      this.outGain = 2.5 * clampd(s.amount, 0, 1) / Math.sqrt(this.count);
    }
    process(x) {
      const hp = this.hpCoef * (this.hpY1 + x - this.hpX1);
      this.hpX1 = x; this.hpY1 = Math.abs(hp) < 1e-15 ? 0 : hp;
      const inp = hp * this.drive;
      let out = 0;
      for (let k = 0; k < this.count; k++) {
        const y = this.loss[k].process(this.lines[k].tap(this.delay[k]));
        this.lines[k].write(y + inp + TINY);
        out += y;
      }
      return out * this.outGain;
    }
  }

  // ------------------------------------------------------------- Reverb ---
  class Reverb {
    constructor() {
      this.LINES = 8;
      this.fs = 48000; this.preLen = 100; this.decaySeconds = 2.4; this.energy = 0;
      this.lines = []; this.damp = [];
      for (let i = 0; i < 8; i++) { this.lines.push(new Delay()); this.damp.push(new OnePoleLP()); }
      this.len = new Float64Array(8); this.gain = new Float64Array(8);
      this.pre = new Delay();
      this.out = new Float64Array(8);
    }
    prepare(sampleRate) {
      this.fs = sampleRate;
      const ms = [29.7, 37.1, 41.1, 43.7, 53.3, 61.3, 67.9, 73.1];
      for (let i = 0; i < 8; i++) {
        this.len[i] = ms[i] * 1e-3 * this.fs;
        this.lines[i].allocate(Math.trunc(this.len[i]) + 8);
        this.damp[i].p = Math.exp(-TWO_PI * 4500 / this.fs);
        this.damp[i].b0 = 1 - this.damp[i].p;
      }
      this.pre.allocate(Math.trunc(0.012 * this.fs) + 8);
      this.preLen = 0.012 * this.fs;
      this.setDecay(2.4);
      this.clear();
    }
    clear() {
      for (const l of this.lines) l.clear();
      for (const d of this.damp) d.reset();
      this.pre.clear();
      this.energy = 0;
    }
    setDecay(seconds) {
      this.decaySeconds = seconds;
      for (let i = 0; i < 8; i++) this.gain[i] = Math.pow(10, -3 * (this.len[i] / this.fs) / Math.max(seconds, 0.1));
    }
    idle() { return this.energy < 1e-10; }
    // returns [l, r] with the wet signal added
    process(l, r, send) {
      const inp = this.pre.tap(this.preLen);
      this.pre.write(0.5 * (l + r));
      const out = this.out;
      let sum = 0;
      for (let i = 0; i < 8; i++) {
        out[i] = this.damp[i].process(this.lines[i].tap(this.len[i])) * this.gain[i];
        sum += out[i];
      }
      const h = sum * (2 / 8);
      let wl = 0, wr = 0, e = 0;
      for (let i = 0; i < 8; i++) {
        const fb = out[i] - h;
        this.lines[i].write(fb + inp * (i % 2 === 0 ? 1 : -1) + TINY);
        if (i % 2 === 0) wl += out[i]; else wr += out[i];
        e += fb * fb;
      }
      this.energy = e;
      return [l + wl * send * 0.35, r + wr * send * 0.35];
    }
    decay() { return this.decaySeconds; }
  }

  // ------------------------------------------------------------- Bodies ---
  class Bridge {
    constructor() { this.hill = new Biquad(); this.roll = new Biquad(); }
    configure(b, fs) { this.hill = Biquad.peaking(b.hillHz, b.hillQ, b.hillDb, fs); this.roll = Biquad.lowpass(b.bridgeLpHz, 0.707, fs); }
    reset() { this.hill.reset(); this.roll.reset(); }
    process(x) { return this.roll.process(this.hill.process(x)); }
  }

  const MAX_MODES = 64;
  class ModalBody {
    constructor() {
      this.count = 0;
      this.b = new Float64Array(MAX_MODES); this.c1 = new Float64Array(MAX_MODES); this.c2 = new Float64Array(MAX_MODES);
      this.wl = new Float64Array(MAX_MODES); this.wr = new Float64Array(MAX_MODES);
      this.y1 = new Float64Array(MAX_MODES); this.y2 = new Float64Array(MAX_MODES);
      this.x1 = 0; this.x2 = 0; this.direct = 0; this.modeScale = 1;
      this.outL = 0; this.outR = 0;
    }
    configure(prof, fs) {
      this.count = 0;
      for (const m of prof.low) if (m[2] > 0 && m[0] > 0) this.add(m[0], m[1], m[2], 0, fs);
      const rng = new Rng();
      rng.seed((0xB0D1 + Math.trunc(prof.cloudLoHz * 7 + prof.cloudHiHz)) >>> 0);
      const lo = Math.log(prof.cloudLoHz), hi = Math.log(prof.cloudHiHz);
      for (let k = 0; k < prof.cloudCount && this.count < MAX_MODES; k++) {
        const u = Math.sqrt((k + 0.5 + 0.4 * rng.bi()) / prof.cloudCount);
        const hz = Math.exp(lerp(lo, hi, clampd(u, 0, 1)));
        const q = lerp(prof.cloudQLo, prof.cloudQHi, rng.uni());
        const gain = dbToGain(ModalBody.envelopeDb(prof, hz)) * (0.45 + 0.55 * rng.uni());
        const tilt = (rng.uni() < 0.5 ? -1 : 1) * (0.3 + 0.7 * rng.uni());
        this.add(hz, q, gain, tilt * prof.spread, fs);
      }
      this.direct = prof.direct;
      this.modeScale = this.count > 0 ? 3 / Math.sqrt(this.count) : 0;
    }
    reset() { for (let i = 0; i < this.count; i++) { this.y1[i] = 0; this.y2[i] = 0; } this.x1 = 0; this.x2 = 0; }
    process(x) {
      let l = 0, r = 0;
      const dx = x - this.x2;
      const b = this.b, c1 = this.c1, c2 = this.c2, y1 = this.y1, y2 = this.y2, wl = this.wl, wr = this.wr;
      for (let k = 0; k < this.count; k++) {
        let y = b[k] * dx + c1[k] * y1[k] - c2[k] * y2[k];
        if (Math.abs(y) < 1e-15) y = 0;
        y2[k] = y1[k]; y1[k] = y;
        l += y * wl[k];
        r += y * wr[k];
      }
      this.x2 = this.x1; this.x1 = x;
      this.outL = l * this.modeScale + x * this.direct;
      this.outR = r * this.modeScale + x * this.direct;
    }
    static envelopeDb(b, hz) {
      const e = b.env;
      if (hz <= e[0][0]) return e[0][1];
      for (let i = 1; i < 7; i++)
        if (hz <= e[i][0]) {
          const t = Math.log(hz / e[i - 1][0]) / Math.log(e[i][0] / e[i - 1][0]);
          return lerp(e[i - 1][1], e[i][1], t);
        }
      return e[6][1];
    }
    add(hz, q, gain, tilt, fs) {
      if (this.count >= MAX_MODES || hz >= fs * 0.45) return;
      const k = this.count++;
      const w = TWO_PI * hz / fs;
      const r = Math.exp(-0.5 * w / q);
      this.c1[k] = 2 * r * Math.cos(w);
      this.c2[k] = r * r;
      this.b[k] = gain * (1 - r * r) * 0.5;
      this.wl[k] = 1 + tilt;
      this.wr[k] = 1 - tilt;
      this.y1[k] = 0; this.y2[k] = 0;
    }
  }

  class Radiation {
    constructor() { this.airL = new Biquad(); this.airR = new Biquad(); this.proxL = new Biquad(); this.proxR = new Biquad(); this.l = 0; this.r = 0; }
    configure(b, fs) {
      this.airL = Biquad.lowpass(b.airLpHz, 0.6, fs); this.airR = Biquad.lowpass(b.airLpHz, 0.6, fs);
      this.proxL = Biquad.lowShelf(150, b.proximityDb, fs); this.proxR = Biquad.lowShelf(150, b.proximityDb, fs);
    }
    reset() { this.airL.reset(); this.airR.reset(); this.proxL.reset(); this.proxR.reset(); }
    process(l, r, width) {
      l = this.proxL.process(this.airL.process(l));
      r = this.proxR.process(this.airR.process(r));
      const mid = 0.5 * (l + r), side = 0.5 * (l - r) * width;
      this.l = mid + side;
      this.r = mid - side;
    }
  }

  class AmpStage {
    constructor() {
      this.bass = new Biquad(); this.mid = new Biquad(); this.treble = new Biquad();
      this.cab = [new Biquad(), new Biquad(), new Biquad(), new Biquad(), new Biquad()];
      this.cabOn = true; this.clean = false; this.inGain = 1; this.outGain = 1; this.x1 = 0;
    }
    configure(s, fs) {
      const toDb = (v) => (clampd(v, 0, 1) - 0.5) * 24;
      this.bass = Biquad.lowShelf(120, toDb(s.bass), fs);
      this.mid = Biquad.peaking(700, 0.8, toDb(s.mid), fs);
      this.treble = Biquad.highShelf(3000, toDb(s.treble), fs);
      this.cab[0] = Biquad.highpass(75, 0.7, fs);
      this.cab[1] = Biquad.peaking(110, 1, 3, fs);
      this.cab[2] = Biquad.peaking(2000, 1, -4, fs);
      this.cab[3] = Biquad.lowpass(4800, 0.9, fs);
      this.cab[4] = Biquad.lowpass(5500, 0.6, fs);
      this.cabOn = s.cab;
      const d = clampd(s.drive, 0, 1);
      this.inGain = dbToGain(36 * d);
      this.outGain = 1 / (this.inGain > 1 ? Math.pow(this.inGain, 0.55) : 1);
      this.clean = d < 1e-3;
    }
    reset() { this.bass.reset(); this.mid.reset(); this.treble.reset(); for (const c of this.cab) c.reset(); this.x1 = 0; }
    process(x) {
      let y = x;
      if (!this.clean) {
        const v = x * this.inGain;
        const dx = v - this.x1;
        y = Math.abs(dx) > 1e-6 ? (AmpStage.logCosh(v) - AmpStage.logCosh(this.x1)) / dx : Math.tanh(0.5 * (v + this.x1));
        this.x1 = v;
        y *= this.outGain;
      }
      y = this.treble.process(this.mid.process(this.bass.process(y)));
      if (this.cabOn) for (const c of this.cab) y = c.process(y);
      return y;
    }
    static logCosh(x) {
      const a = Math.abs(x);
      return a > 20 ? a - 0.6931471805599453 : Math.log(Math.cosh(x));
    }
  }

  // ------------------------------------------------------------- Engine ---
  const NUM_VOICES = 16, CHUNK = 32, VOICE_GAIN = 0.35, BODY_GAIN = 3.8;
  const GLIDE = 0.012, GLIDE_MAX = 0.03, NOISE_INJECT = 0.08;
  const MODE_POLY = 0, MODE_MONO = 1, MODE_LEGATO = 2;
  const VIB_AUTO = 0, VIB_WHEEL = 1, VIB_AFTERTOUCH = 2, VIB_AUTO_AT = 3;
  const SWELL_WHEEL = 1, SWELL_BREATH = 2, SWELL_EXPR = 3, SWELL_AT = 4;
  const START_FRESH = 0, START_REBOW = 1, START_CROSS = 2;
  const EXC_GAIN = [1.0, 1.5, 0.75, 0.85, 1.4, 2.2, 1.6, 0.7, 11.0];

  const bowVelocity = (speed) => 0.05 * Math.pow(2, 4 * clampd(speed, 0, 1));
  function bowForce(pressure, vb, beta, hz) {
    const hi = Math.log2(1.6 / beta);
    const lo = Math.min(Math.log2(3.4 * Math.pow(beta, -1.54) * Math.pow(Math.max(hz, 20), -0.59)), hi - 0.415);
    const width = hi - lo;
    return vb * Math.pow(2, 0.5 * (lo + hi) + (clampd(pressure, 0, 1) - 0.5) * (width + 2));
  }
  const bowTuningCents = (note) => clampd(7.4 - 0.235 * (note - 36), -12, 12);

  function newEngineParams() {
    return {
      excitation: EXC_ARCO, bowPressure: 0.5, bowSpeed: 0.5, bowPosition: 0.5, harmonic: 2, malletHardness: 0.5, ebowMode: 0,
      material: 2, tension: 0.5, age: 0.25, torsion: 1, releaseTime: 0.5,
      body: 0, ampDrive: 0.25, ampBass: 0.5, ampMid: 0.5, ampTreble: 0.5, ampCab: true, width: 1,
      symp: { on: false, amount: 0.5, count: 8, mode: 1, key: 2, scale: 0, decay: 6, brightness: 0.5,
              custom: [38, 45, 50, 57, 62, 69, 74, 81, 43, 55, 67, 79] },
      voiceMode: 0, portaTime: 0.12, vibRate: 5.5, vibDepth: 18, vibDelay: 0.35, vibSource: 0, swellSource: 1,
      keyswitchBase: 12, releaseNoise: 0.3, bowNoise: 0.3, bendRange: 2,
      reverbSend: 0.2, reverbDecay: 2.4, presetGain: 1, master: 0.8,
    };
  }

  // raw parameter values (by id) -> engine params (mirror of ParamTable.h makeEngineParams)
  function makeEngineParams(v, p) {
    const choice = (id, maxIndex) => Math.min(Math.max(Math.round(v[id]), 0), maxIndex);
    p.excitation = choice('excitation', NUM_EXCITATIONS - 1);
    p.bowPressure = v.bowPressure; p.bowSpeed = v.bowSpeed; p.bowPosition = v.bowPosition;
    p.harmonic = Math.min(Math.max(Math.round(v.harmonic), 2), 5);
    p.malletHardness = v.malletHardness;
    p.ebowMode = choice('ebowMode', 1);
    p.material = choice('material', 5);
    p.tension = v.tension; p.age = v.age; p.releaseTime = v.releaseTime;
    p.body = choice('body', 7);
    p.ampDrive = v.ampDrive; p.ampBass = v.ampBass; p.ampMid = v.ampMid; p.ampTreble = v.ampTreble;
    p.ampCab = choice('ampCab', 1) === 1;
    p.width = v.width;
    const s = p.symp;
    s.on = choice('sympOn', 1) === 1;
    s.amount = v.sympAmount;
    s.count = Math.min(Math.max(Math.round(v.sympCount), 4), MAX_SYMPATHETIC);
    s.mode = choice('sympMode', 2);
    s.key = choice('sympKey', 11);
    s.scale = choice('sympScale', 6);
    s.decay = v.sympDecay;
    s.brightness = v.sympBright;
    for (let i = 0; i < MAX_SYMPATHETIC; i++) s.custom[i] = Math.min(Math.max(Math.round(v[`sympNote${i + 1}`]), 24), 108);
    p.voiceMode = choice('voiceMode', 2);
    p.portaTime = v.portaTime; p.vibRate = v.vibRate; p.vibDepth = v.vibDepth; p.vibDelay = v.vibDelay;
    p.vibSource = choice('vibSource', 3);
    p.swellSource = choice('swellSource', 4);
    const ks = choice('keyswitchOctave', 4);
    p.keyswitchBase = ks === 0 ? -1 : (ks - 1) * 12;
    p.releaseNoise = v.releaseNoise; p.bowNoise = v.bowNoise; p.bendRange = v.bendRange;
    p.master = v.master;
    p.reverbSend = v.reverbSend; p.reverbDecay = v.reverbDecay;
    p.presetGain = dbToGain(v.presetLevel);
    return p;
  }

  function copyParams(src) {
    const d = Object.assign({}, src);
    d.symp = Object.assign({}, src.symp, { custom: src.symp.custom.slice() });
    return d;
  }

  function newVoice() {
    return {
      string: new WaveguideString(), bow: new BowFriction(), pluck: new PluckExciter(), hammer: new HammerExciter(), ebow: new EBowExciter(),
      rng: new Rng(), note: -1, exc: EXC_ARCO, harmonic: 2, active: false, held: false, velocity: 0,
      stroke: 1, strokeInc: 0, force: 0, forceTarget: 0, forceStep: 0, bounce: 1, bounceInc: 0, dir: 1,
      accent: 0, accentCoef: 0, noise: 0, click: 0, clickCoef: 0, clickLp: 0, clickHp: 0, level: 0, energy: 0, energyCoef: 0.01,
      stringIdx: 0, sustained: false, pitch: 60, glideFrom: 60, glideTo: 60, glideT: 0, glideTime: 0, time: 0,
      vibPhase: 0, vibRateJit: 1, vibDepthJit: 1, vibCents: 0, bowChange: 1, bowChangeInc: 0,
      burst: 0, burstCoef: 0, burstLp: 0, burstHp: 0, burstLpCoef: 0.3, burstHpCoef: 0.1, age: 0,
    };
  }

  class Engine {
    constructor(sampleRate, tables) {
      this.bodies = tables.bodies; this.materials = tables.materials;
      this.fs = sampleRate;
      this.params = newEngineParams();
      this.resp = stringResponse(this.materials, 2, 0.5, 0.25);
      this.bridge = new Bridge(); this.body = new ModalBody(); this.radiation = new Radiation(); this.amp = new AmpStage();
      this.symp = new SympatheticBank(); this.reverb = new Reverb(); this.dc = new DcBlocker(); this.dcR = new DcBlocker();
      this.peak = 0;
      this.voices = [];
      for (let i = 0; i < NUM_VOICES; i++) this.voices.push(newVoice());
      this.mix = new Float64Array(CHUNK);
      this.levelDecay = 0.999; this.noiseCoef = 0.1; this.woodNoiseCoef = 0.3; this.clickLpCoef = 0.3; this.clickHpCoef = 0.1;
      this.bowDir = 1; this.ksExcitation = -1;
      this.heldKeys = new Int32Array(32); this.heldCount = 0; this.monoVoice = -1;
      this.sustainPedal = false;
      this.modWheel = 0; this.aftertouch = 0; this.breath = 0; this.expression = 0; this.bend = 0;
      this.modWheelS = 0; this.aftertouchS = 0; this.bendS = 0; this.swell = 0;
      this.ageCounter = 0; this.active = 0; this.nanResets = 0;
      this.setup = newStringSetup();
      this.prepare(sampleRate);
    }

    bodyProfile(b) { return this.bodies[b >= 0 && b < this.bodies.length ? b : 0]; }

    prepare(sampleRate) {
      const fs = this.fs = sampleRate;
      let seed = 0x1234567;
      for (const v of this.voices) {
        v.string.prepare(fs);
        seed = (seed + 0x9E3779B9) >>> 0;
        v.rng.seed(seed);
        v.ebow.rng.seed((seed ^ 0x5bd1e995) >>> 0);
        v.active = false;
      }
      this.dc.setup(20, fs); this.dc.reset();
      this.dcR.setup(20, fs); this.dcR.reset();
      this.configureBody(true);
      this.symp.prepare(fs);
      this.symp.configure(this.params.symp, this.bodyProfile(this.params.body));
      this.reverb.prepare(fs);
      this.reverb.setDecay(this.params.reverbDecay);
      this.levelDecay = Math.exp(-1 / (0.05 * fs));
      this.noiseCoef = smoothCoef(1 / (TWO_PI * 2000), fs);
      this.woodNoiseCoef = smoothCoef(1 / (TWO_PI * 6000), fs);
      this.clickLpCoef = smoothCoef(1 / (TWO_PI * 4000), fs);
      this.clickHpCoef = smoothCoef(1 / (TWO_PI * 1200), fs);
      this.mix.fill(0);
    }

    setParams(p) {
      const old = this.params;
      if (p.excitation !== old.excitation) this.ksExcitation = -1;
      if (p.voiceMode !== old.voiceMode) this.monoVoice = -1;
      const bodyChanged = p.body !== old.body;
      const sympChanged = !sympEquals(p.symp, old.symp);
      const sympTurnedOn = p.symp.on && !old.symp.on;
      const ampChanged = p.ampDrive !== old.ampDrive || p.ampBass !== old.ampBass || p.ampMid !== old.ampMid
                      || p.ampTreble !== old.ampTreble || p.ampCab !== old.ampCab;
      this.params = copyParams(p);
      this.params.body = Math.min(Math.max(this.params.body, 0), this.bodies.length - 1);
      this.resp = stringResponse(this.materials, this.params.material, this.params.tension, this.params.age);
      if (bodyChanged) this.configureBody(true);
      else if (ampChanged) this.configureAmp();
      if (sympChanged || bodyChanged) this.symp.configure(this.params.symp, this.bodyProfile(this.params.body));
      if (sympTurnedOn) this.symp.clear();
      if (this.params.reverbDecay !== this.reverb.decay()) this.reverb.setDecay(this.params.reverbDecay);
    }

    // ---- MIDI-level input
    noteOn(note, vel) {
      note = Math.min(Math.max(note, 0), 127);
      const P = this.params;
      if (P.keyswitchBase >= 0 && note >= P.keyswitchBase && note < P.keyswitchBase + NUM_EXCITATIONS) {
        this.ksExcitation = note - P.keyswitchBase;
        return;
      }
      const overlap = this.heldCount > 0;
      this.pushKey(note);
      const exc = this.currentExcitation();

      if (P.voiceMode === MODE_POLY) {
        const v = this.findVoice(note);
        this.startVoice(v, note, vel, exc, v.active && v.note === note && v.exc === exc ? START_REBOW : START_FRESH);
        return;
      }

      const cur = this.monoVoice >= 0 && this.voices[this.monoVoice].active ? this.voices[this.monoVoice] : null;
      const str = this.stringFor(note);
      if (cur !== null && cur.held && cur.exc === exc && P.voiceMode === MODE_LEGATO && overlap) {
        if (cur.stringIdx === str) this.slideVoice(cur, note);
        else {
          this.releaseVoice(cur, false);
          const nv = this.findVoice(note, cur);
          this.startVoice(nv, note, vel, exc, START_CROSS, cur.dir);
          this.monoVoice = this.voices.indexOf(nv);
        }
        return;
      }
      if (cur !== null && cur.stringIdx === str && cur.exc === exc) {
        this.startVoice(cur, note, vel, exc, START_REBOW);
        return;
      }
      if (cur !== null) this.releaseVoice(cur, true);
      const nv = this.findVoice(note, cur);
      this.startVoice(nv, note, vel, exc, START_FRESH);
      this.monoVoice = this.voices.indexOf(nv);
    }

    noteOff(note) {
      note = Math.min(Math.max(note, 0), 127);
      const P = this.params;
      if (P.keyswitchBase >= 0 && note >= P.keyswitchBase && note < P.keyswitchBase + NUM_EXCITATIONS) return;
      this.removeKey(note);

      if (P.voiceMode === MODE_POLY) {
        for (const v of this.voices)
          if (v.active && v.held && v.note === note) {
            if (this.sustainPedal) v.sustained = true;
            else this.releaseVoice(v, true);
          }
        return;
      }

      const cur = this.monoVoice >= 0 && this.voices[this.monoVoice].active ? this.voices[this.monoVoice] : null;
      if (cur === null || !cur.held || cur.note !== note) return;
      if (this.heldCount > 0) {
        const back = this.heldKeys[this.heldCount - 1];
        const exc = this.currentExcitation();
        if (P.voiceMode === MODE_LEGATO && cur.exc === exc && this.stringFor(back) === cur.stringIdx) this.slideVoice(cur, back);
        else if (P.voiceMode === MODE_LEGATO && cur.exc === exc) {
          this.releaseVoice(cur, false);
          const nv = this.findVoice(back, cur);
          this.startVoice(nv, back, Math.trunc(cur.velocity * 127), exc, START_CROSS, cur.dir);
          this.monoVoice = this.voices.indexOf(nv);
        } else this.startVoice(cur, back, Math.trunc(cur.velocity * 127), exc, cur.stringIdx === this.stringFor(back) ? START_REBOW : START_FRESH);
        return;
      }
      if (this.sustainPedal) cur.sustained = true;
      else this.releaseVoice(cur, true);
    }

    allNotesOff() {
      this.heldCount = 0;
      for (const v of this.voices) if (v.active && v.held) this.releaseVoice(v, false);
    }

    panic() {
      this.heldCount = 0;
      this.monoVoice = -1;
      for (const v of this.voices) { v.active = false; v.held = false; v.sustained = false; v.string.reset(); v.bow.reset(); }
      this.dc.reset(); this.dcR.reset(); this.bridge.reset(); this.body.reset(); this.radiation.reset(); this.amp.reset(); this.symp.clear(); this.reverb.clear();
    }

    setSustain(down) {
      this.sustainPedal = down;
      if (!down)
        for (const v of this.voices)
          if (v.active && v.sustained) { v.sustained = false; if (!this.isKeyHeld(v.note)) this.releaseVoice(v, true); }
    }
    setModWheel(v) { this.modWheel = clampd(v, 0, 1); }
    setAftertouch(v) { this.aftertouch = clampd(v, 0, 1); }
    setBreath(v) { this.breath = clampd(v, 0, 1); }
    setExpression(v) { this.expression = clampd(v, 0, 1); }
    setPitchBend(v) { this.bend = clampd(v, -1, 1); }

    currentExcitation() {
      return Math.min(Math.max(this.ksExcitation >= 0 ? this.ksExcitation : this.params.excitation, 0), NUM_EXCITATIONS - 1);
    }

    // Writes numSamples into L (and R if present) from `offset`.
    render(L, R, offset, numSamples) {
      const P = this.params, fs = this.fs, mix = this.mix;
      let pos = 0;
      while (pos < numSamples) {
        const n = Math.min(CHUNK, numSamples - pos);
        for (let i = 0; i < n; i++) mix[i] = 0;
        const k = 1 - Math.exp(-n / (0.015 * fs));
        this.modWheelS += (this.modWheel - this.modWheelS) * k;
        this.aftertouchS += (this.aftertouch - this.aftertouchS) * k;
        this.bendS += (this.bend - this.bendS) * k;
        let swellTarget = 0;
        switch (P.swellSource) {
          case SWELL_WHEEL: swellTarget = this.modWheel; break;
          case SWELL_BREATH: swellTarget = this.breath; break;
          case SWELL_EXPR: swellTarget = this.expression; break;
          case SWELL_AT: swellTarget = this.aftertouch; break;
          default: break;
        }
        this.swell += (swellTarget - this.swell) * k;
        let count = 0;
        for (const v of this.voices)
          if (v.active) { count++; this.updateControl(v, n); this.configureVoice(v, false); this.renderVoice(v, n); }
        this.active = count;

        const bp = this.bodyProfile(P.body);
        const g = P.master * P.presetGain * BODY_GAIN * dbToGain(bp.outputDb);
        const electric = bp.electric;
        const sympOn = P.symp.on;
        const reverbOn = P.reverbSend > 0 || !this.reverb.idle();
        for (let i = 0; i < n; i++) {
          const atBridge = this.bridge.process(mix[i]);
          this.body.process(sympOn ? atBridge + this.symp.process(atBridge) : atBridge);
          this.radiation.process(this.body.outL, this.body.outR, P.width);
          let l = this.radiation.l, r = this.radiation.r;
          if (electric) {
            const m = 0.5 * (l + r), side = 0.5 * (l - r);
            const a = this.amp.process(m);
            l = a + side; r = a - side;
          }
          l = this.dc.process(l) * g;
          r = this.dcR.process(r) * g;
          if (reverbOn) { const w = this.reverb.process(l, r, P.reverbSend); l = w[0]; r = w[1]; }
          const yl = Math.fround(Math.tanh(l)), yr = Math.fround(Math.tanh(r));
          L[offset + pos + i] = yl;
          if (R) R[offset + pos + i] = yr;
          const a1 = Math.abs(yl), a2 = Math.abs(yr);
          if (a1 > this.peak) this.peak = a1;
          if (a2 > this.peak) this.peak = a2;
        }
        pos += n;
      }
    }

    // ---- voices
    startVoice(v, note, vel, exc, kind, crossDir = 1) {
      const P = this.params, fs = this.fs, resp = this.resp;
      if (kind === START_REBOW && (!v.active || v.exc !== exc)) kind = START_FRESH;
      const wasSounding = v.active;
      if (kind !== START_REBOW) {
        if (!wasSounding || kind === START_CROSS) v.string.reset();
        v.bow.reset(); v.pluck = new PluckExciter(); v.hammer = new HammerExciter();
        v.ebow.active = false; v.ebow.drive = 0; v.ebow.env = 0; v.ebow.focus.reset();
        v.force = 0; v.stroke = 0; v.bounce = 1; v.click = 0; v.bowChange = 1; v.burst = 0;
        if (!wasSounding || kind === START_CROSS) { v.energy = 0; v.level = 0; }
      }
      v.note = note;
      v.exc = exc;
      v.stringIdx = this.stringFor(note);
      v.harmonic = Math.min(Math.max(P.harmonic, 2), 5);
      v.active = true; v.held = true;
      v.sustained = false;
      v.velocity = clampd(vel / 127, 0, 1);
      v.age = ++this.ageCounter;
      v.time = 0;
      v.glideFrom = kind === START_REBOW ? v.pitch : note;
      v.glideTo = note;
      v.glideT = 0;
      v.glideTime = kind === START_REBOW ? 0.015 : 0;
      v.pitch = v.glideFrom;
      if (kind !== START_REBOW) {
        v.vibPhase = TWO_PI * v.rng.uni();
        v.vibRateJit = 1 + 0.06 * v.rng.bi();
        v.vibDepthJit = 1 + 0.15 * v.rng.bi();
      }
      this.configureVoice(v, kind !== START_REBOW);

      const vel01 = v.velocity;
      const hz = midiToHz(note);
      switch (exc) {
        case EXC_ARCO: case EXC_HARMONIC: case EXC_TRATTO: {
          if (exc === EXC_TRATTO) { v.bow.muS = 0.45; v.bow.muD = 0.28; v.bow.v0 = 0.05; }
          else { v.bow.muS = resp.muS; v.bow.muD = resp.muD; v.bow.v0 = resp.v0; }
          v.accentCoef = Math.exp(-1 / (0.06 * fs));
          v.forceTarget = 1;
          v.forceStep = 1 / (0.005 * fs);
          if (kind === START_CROSS) {
            v.force = 1; v.stroke = 1; v.dir = crossDir; v.accent = 0;
          } else if (kind === START_REBOW && v.force > 0.5) {
            v.bowChange = 0;
            v.bowChangeInc = 1 / (lerp(0.06, 0.03, vel01) * fs);
            v.accent = 0.15 * vel01;
            this.triggerBurst(v, P.bowNoise * lerp(0.5, 1, vel01), 0.04, 1500, 5000);
          } else {
            const pressTime = 0.005, drawTime = lerp(0.16, 0.035, vel01) * resp.drawScale;
            this.bowDir = -this.bowDir;
            v.dir = this.bowDir;
            v.stroke = -pressTime / drawTime;
            v.strokeInc = 1 / (drawTime * fs);
            v.accent = (exc === EXC_ARCO ? 0.25 : 0.1) * vel01;
          }
          break;
        }
        case EXC_SPICCATO:
          this.bowDir = -this.bowDir;
          v.dir = this.bowDir;
          v.bounce = 0;
          v.bounceInc = 1 / (lerp(0.075, 0.035, vel01) * fs);
          v.bow.muS = resp.muS; v.bow.muD = resp.muD; v.bow.v0 = resp.v0;
          break;
        case EXC_PIZZ: case EXC_BARTOK: {
          const bartok = exc === EXC_BARTOK;
          const mm = (bartok ? lerp(4, 9, vel01) : lerp(0.8, 4, vel01)) * Math.pow(65 / Math.max(hz, 40), 0.9) * resp.pluckScale;
          const release = bartok ? 0.0006 : lerp(0.004, 0.0008, vel01);
          v.pluck.start(v.string, mm * 1e-3, release, fs, bartok);
          break;
        }
        case EXC_MALLET: {
          const hard = clampd(P.malletHardness, 0, 1);
          v.hammer.start(lerp(0.3, 2.5, vel01), 0.0012, 0.4 * Math.pow(10, 1.6 * hard), lerp(2.8, 2.0, hard), fs);
          break;
        }
        case EXC_BATTUTO:
          v.hammer.start(lerp(0.4, 2.2, vel01), 0.0015, 40, 1.5, fs, lerp(0.03, 0.018, vel01), 0.35);
          v.click = lerp(0.05, 0.25, vel01);
          v.clickCoef = Math.exp(-1 / (0.004 * fs));
          break;
        case EXC_EBOW:
          v.ebow.setup(hz, fs, P.ebowMode === 1);
          v.ebow.start(kind === START_CROSS ? 0.05 : lerp(1.2, 0.25, vel01), lerp(0.04, 0.15, vel01), fs);
          break;
        default: break;
      }
    }

    slideVoice(v, note) {
      const interval = Math.abs(note - v.pitch);
      v.glideFrom = v.pitch;
      v.glideTo = note;
      v.glideT = 0;
      v.glideTime = this.params.portaTime * Math.pow(interval / 12, 0.6);
      v.note = note;
      v.time = 0;
      v.age = ++this.ageCounter;
    }

    releaseVoice(v, noise) {
      if (!v.active || !v.held) return;
      const P = this.params, fs = this.fs;
      v.held = false;
      v.sustained = false;
      if (isBowed(v.exc)) { v.forceTarget = 0; v.forceStep = 1 / (0.015 * fs); }
      if (v.exc === EXC_EBOW) v.ebow.stop();
      if (v.exc === EXC_BARTOK) v.pluck.stopColliding();
      if (noise && P.releaseNoise > 0) {
        if (isBowed(v.exc)) this.triggerBurst(v, P.releaseNoise * 0.7, 0.05, 1800, 6000);
        else this.triggerBurst(v, P.releaseNoise, 0.03, 120, 900);
      }
      this.configureVoice(v, false);
    }

    triggerBurst(v, amount, seconds, hpHz, lpHz) {
      const fs = this.fs;
      v.burst = Math.max(v.burst, amount);
      v.burstCoef = Math.exp(-1 / (seconds * fs));
      v.burstLpCoef = smoothCoef(1 / (TWO_PI * lpHz), fs);
      v.burstHpCoef = smoothCoef(1 / (TWO_PI * hpHz), fs);
    }

    stringFor(note) {
      const bp = this.bodyProfile(this.params.body);
      let idx = 0;
      for (let k = 0; k < bp.strings.length; k++) if (bp.strings[k] <= note) idx = k;
      return idx;
    }

    pushKey(note) {
      this.removeKey(note);
      if (this.heldCount < this.heldKeys.length) this.heldKeys[this.heldCount++] = note;
    }
    removeKey(note) {
      let w = 0;
      for (let r = 0; r < this.heldCount; r++) if (this.heldKeys[r] !== note) this.heldKeys[w++] = this.heldKeys[r];
      this.heldCount = w;
    }
    isKeyHeld(note) {
      for (let r = 0; r < this.heldCount; r++) if (this.heldKeys[r] === note) return true;
      return false;
    }

    findVoice(note, avoid = null) {
      if (this.params.voiceMode === MODE_POLY)
        for (const v of this.voices) if (v.active && v.note === note) return v;
      for (const v of this.voices) if (!v.active && v !== avoid) return v;
      let best = null;
      for (const v of this.voices) if (v !== avoid && !v.held && (best === null || v.age < best.age)) best = v;
      if (best !== null) return best;
      for (const v of this.voices) if (v !== avoid && (best === null || v.age < best.age)) best = v;
      return best;
    }

    updateControl(v, n) {
      const P = this.params;
      const dt = n / this.fs;
      v.time += dt;
      if (v.glideT < v.glideTime) {
        v.glideT += dt;
        const x = Math.min(1, v.glideT / v.glideTime);
        v.pitch = v.glideFrom + (v.glideTo - v.glideFrom) * x * x * (3 - 2 * x);
      } else v.pitch = v.glideTo;

      let amount = 0;
      const x = clampd((v.time - P.vibDelay) / 0.4, 0, 1);
      const autoRamp = v.held ? x * x * (3 - 2 * x) : 0;
      switch (P.vibSource) {
        case VIB_WHEEL: amount = this.modWheelS; break;
        case VIB_AFTERTOUCH: amount = this.aftertouchS; break;
        case VIB_AUTO_AT: amount = Math.max(autoRamp, this.aftertouchS); break;
        default: amount = autoRamp; break;
      }
      v.vibPhase += TWO_PI * P.vibRate * v.vibRateJit * dt;
      if (v.vibPhase > TWO_PI) v.vibPhase -= TWO_PI;
      v.vibCents = P.vibDepth * v.vibDepthJit * amount * Math.sin(v.vibPhase);
    }

    static fundamentalNote(v) { return v.exc === EXC_HARMONIC ? v.pitch - 12 * Math.log2(v.harmonic) : v.pitch; }

    ringDecay(v) {
      switch (v.exc) {
        case EXC_PIZZ: case EXC_BARTOK: case EXC_SPICCATO: case EXC_BATTUTO: return this.resp.decay * 0.45;
        case EXC_MALLET: return this.resp.decay * 0.8;
        default: return this.resp.decay;
      }
    }

    configureVoice(v, snap) {
      const P = this.params, resp = this.resp, s = this.setup;
      const fund = Engine.fundamentalNote(v);
      s.hz = midiToHz(fund + (v.exc === EXC_ARCO ? bowTuningCents(fund) * 0.01 : 0) + v.vibCents * 0.01 + this.bendS * P.bendRange);
      s.beta = contactBeta(v.exc, P.bowPosition);
      const bp = this.bodyProfile(P.body);
      let open = v.exc === EXC_HARMONIC;
      if (Math.abs(v.pitch - v.note) < 0.01)
        for (let k = 0; k < bp.strings.length; k++) open = open || bp.strings[k] === v.note;
      const ring = this.ringDecay(v) * (open ? 1 : bp.stoppedDecay);
      s.decay = v.held ? ring : Math.min(ring, P.releaseTime);
      s.hfDecay = s.decay * resp.hfRatio * (open ? 1 : bp.stoppedHf / bp.stoppedDecay);
      for (let k = 0; k < 2; k++) {
        s.couplingHz[k] = bp.coupling[k][0];
        s.couplingQ[k] = bp.coupling[k][1];
        s.couplingC[k] = bp.coupling[k][2];
      }
      s.compensateCoupling = v.exc !== EXC_ARCO && v.exc !== EXC_TRATTO && v.exc !== EXC_HARMONIC;
      const openNote = bp.strings[Math.min(v.stringIdx, bp.strings.length - 1)];
      const stop = Math.pow(2, Math.max(0, fund - openNote) / 12);
      s.inharm = Math.min(2e-3, 1e-3 * resp.stiffness * resp.stiffness * stop * stop);
      if (!isBowed(v.exc) && v.exc !== EXC_EBOW)
        s.hz *= 1 + Math.min(GLIDE_MAX, GLIDE * resp.glide * v.energy);
      v.energyCoef = smoothCoef(1.5 / s.hz, this.fs);
      s.touch = v.exc === EXC_HARMONIC ? 1 / v.harmonic : 0;
      s.touchDamp = 0.85;
      v.string.configure(s, snap);
      v.bow.rho = resp.rho * P.torsion;
      v.bow.setup(s.hz, this.fs);
    }

    renderVoice(v, n) {
      const P = this.params, resp = this.resp, mix = this.mix;
      const fundHz = midiToHz(Engine.fundamentalNote(v));
      const beta = contactBeta(v.exc, P.bowPosition);
      const vbBase = bowVelocity(P.bowSpeed) * lerp(0.6, 1, v.velocity) * (1 + 0.6 * this.swell);
      const pressure = clampd(P.bowPressure + 0.15 * (v.velocity - 0.75) + 0.3 * this.swell, 0, 1);
      const fbBase = v.exc === EXC_HARMONIC
        ? bowForce(pressure, vbBase, Math.min(beta * v.harmonic, 0.45), midiToHz(v.note)) * 0.7 * resp.forceScale
        : bowForce(pressure, vbBase, beta, fundHz) * (v.exc === EXC_TRATTO ? 1 : resp.forceScale);
      const outGain = VOICE_GAIN * EXC_GAIN[v.exc] * (isBowed(v.exc) ? Math.pow(beta, 0.7) / 0.2 : 1);
      const tratto = v.exc === EXC_TRATTO;
      const nCoef = tratto ? this.woodNoiseCoef : this.noiseCoef;
      const nAmt = tratto ? 0.35 : 0.08;

      for (let i = 0; i < n; i++) {
        const vh = v.string.incoming();
        let f = 0, extra = 0;

        switch (v.exc) {
          case EXC_ARCO: case EXC_HARMONIC: case EXC_TRATTO: case EXC_SPICCATO: {
            let vb, fb;
            v.noise += (v.rng.bi() - v.noise) * nCoef;
            if (v.exc === EXC_SPICCATO) {
              let env = 0;
              if (v.bounce < 1) { v.bounce = Math.min(1, v.bounce + v.bounceInc); env = Math.sin(PI * v.bounce); }
              vb = vbBase * v.dir;
              fb = fbBase * env * (1 + nAmt * v.noise);
            } else {
              if (v.force < v.forceTarget) v.force = Math.min(v.forceTarget, v.force + v.forceStep);
              else if (v.force > v.forceTarget) v.force = Math.max(v.forceTarget, v.force - v.forceStep);
              if (v.stroke < 1) v.stroke = Math.min(1, v.stroke + v.strokeInc);
              const sp = v.stroke <= 0 ? 0 : 0.5 - 0.5 * Math.cos(PI * v.stroke);
              v.accent *= v.accentCoef;
              let turn = 1;
              if (v.bowChange < 1) {
                v.bowChange = Math.min(1, v.bowChange + v.bowChangeInc);
                turn = Math.cos(PI * v.bowChange);
                if (v.bowChange >= 1) { v.dir = -v.dir; turn = 1; }
              }
              vb = vbBase * sp * v.dir * turn;
              fb = fbBase * v.force * (1 + v.accent) * (1 + nAmt * v.noise) * (0.75 + 0.25 * Math.abs(turn));
            }
            f = v.bow.solve(vh, vb, fb);
            break;
          }
          case EXC_PIZZ: case EXC_BARTOK:
            f = v.pluck.process(vh);
            break;
          case EXC_MALLET:
            f = v.hammer.process(vh);
            break;
          case EXC_BATTUTO: {
            const wasActive = v.hammer.active;
            f = v.hammer.process(vh);
            if (v.hammer.active && !wasActive) v.click = 0.4 * v.click + lerp(0.03, 0.1, v.velocity);
            if (v.click > 1e-5) {
              const nz = v.rng.bi() * v.click;
              v.clickLp += (nz - v.clickLp) * this.clickLpCoef;
              v.clickHp += (v.clickLp - v.clickHp) * this.clickHpCoef;
              extra = v.clickLp - v.clickHp;
              v.click *= v.clickCoef;
            }
            break;
          }
          case EXC_EBOW:
            f = v.ebow.process(vh);
            break;
          default: break;
        }

        if (v.burst > 1e-5) {
          v.burstLp += (v.rng.bi() * v.burst - v.burstLp) * v.burstLpCoef;
          v.burstHp += (v.burstLp - v.burstHp) * v.burstHpCoef;
          f += (v.burstLp - v.burstHp) * NOISE_INJECT;
          v.burst *= v.burstCoef;
        }

        const wave = v.string.commit(f);
        const y = wave * outGain + extra * VOICE_GAIN;
        v.energy += (wave * wave - v.energy) * v.energyCoef;
        if (!Number.isFinite(y)) { v.active = false; v.string.reset(); v.bow.reset(); this.nanResets++; return; }
        mix[i] += y;
        v.level = Math.max(Math.abs(y), v.level * this.levelDecay);
      }

      let driving = false;
      switch (v.exc) {
        case EXC_ARCO: case EXC_HARMONIC: case EXC_TRATTO: driving = v.held || v.force > 0; break;
        case EXC_SPICCATO: driving = v.bounce < 1; break;
        case EXC_PIZZ: case EXC_BARTOK: driving = v.pluck.ramp < 1; break;
        case EXC_MALLET: case EXC_BATTUTO: driving = v.hammer.busy() || v.click > 1e-5; break;
        case EXC_EBOW: driving = v.ebow.busy(); break;
        default: break;
      }
      if (!driving && v.level < 1e-5 && v.burst <= 1e-5 && v.time * fundHz > 1.5) { v.active = false; v.held = false; }
    }

    configureBody(resetState) {
      const bp = this.bodyProfile(this.params.body);
      this.bridge.configure(bp, this.fs);
      this.body.configure(bp, this.fs);
      this.radiation.configure(bp, this.fs);
      this.configureAmp();
      if (resetState) { this.bridge.reset(); this.body.reset(); this.radiation.reset(); this.amp.reset(); }
    }

    configureAmp() {
      const P = this.params;
      this.amp.configure({ drive: P.ampDrive, bass: P.ampBass, mid: P.ampMid, treble: P.ampTreble, cab: P.ampCab }, this.fs);
    }
  }

  // ---------------------------------------------------------- processor ---
  class CelloProcessor extends AudioWorkletProcessor {
    constructor(options) {
      super();
      const po = (options && options.processorOptions) || {};
      this.values = Object.assign({}, po.params || {});
      this.engine = new Engine(sampleRate, po.tables);
      this.P = newEngineParams();
      this.dirty = true;
      this.blockCount = 0;
      this.port.onmessage = (e) => this.onMessage(e.data);
    }

    onMessage(m) {
      const e = this.engine;
      switch (m.type) {
        case 'param': this.values[m.id] = m.value; this.dirty = true; break;
        case 'params': Object.assign(this.values, m.values); this.dirty = true; break;
        case 'noteOn': this.applyParams(); e.noteOn(m.note, m.vel); break;
        case 'noteOff': this.applyParams(); e.noteOff(m.note); break;
        case 'sustain': e.setSustain(!!m.on); break;
        case 'bend': e.setPitchBend(m.value); break;
        case 'modWheel': e.setModWheel(m.value); break;
        case 'aftertouch': e.setAftertouch(m.value); break;
        case 'breath': e.setBreath(m.value); break;
        case 'expression': e.setExpression(m.value); break;
        case 'allNotesOff': e.allNotesOff(); break;
        case 'panic': e.panic(); break;
      }
    }

    applyParams() {
      if (!this.dirty) return;
      this.engine.setParams(makeEngineParams(this.values, this.P));
      this.dirty = false;
    }

    process(inputs, outputs) {
      const out = outputs[0];
      const L = out[0];
      if (!L) return true;
      const R = out.length > 1 ? out[1] : null;
      this.applyParams();
      this.engine.render(L, R, 0, L.length);
      if (++this.blockCount >= 16) {
        const e = this.engine;
        this.port.postMessage({ type: 'stats', voices: e.active, peak: e.peak, sustain: e.sustainPedal,
                                modWheel: e.modWheel, excitation: e.currentExcitation() });
        this.blockCount = 0;
        e.peak = 0;
      }
      return true;
    }
  }

  registerProcessor('nostrings-cello', CelloProcessor);
}
