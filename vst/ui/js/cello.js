// ============================================================================
//  Cello graphics (SVG): the fixed logo in the top-left corner, and the
//  picture of the selected body in the BODY panel. Everything is drawn from
//  a few paths so each body gets its own shape, finish and string count.
// ============================================================================
'use strict';

// Classic violin-family outline, upright, in a 200 x 330 box (x centred on 100).
const CELLO_OUTLINE =
  'M100 70 C140 68 168 95 168 135 C168 155 158 162 142 165 C136 182 136 198 142 212 ' +
  'C165 218 180 240 180 265 C180 305 150 330 100 330 C50 330 20 305 20 265 C20 240 35 218 58 212 ' +
  'C64 198 64 182 58 165 C42 162 32 155 32 135 C32 95 60 68 100 70 Z';

// Guitar-shaped outline (the arpeggione): a waist instead of C-bouts.
const GUITAR_OUTLINE =
  'M100 72 C148 72 172 100 170 140 C168 160 152 170 150 184 C152 204 186 216 186 256 ' +
  'C186 306 150 330 100 330 C50 330 14 306 14 256 C14 216 48 204 50 184 C48 170 32 160 30 140 C28 100 52 72 100 72 Z';

// Narrow solid body (electric): a stick with a small wing.
const SLAB_OUTLINE = 'M84 60 L116 60 L122 150 L138 162 L130 300 L70 300 L62 162 L78 150 Z';

function fHoles(x1, x2, y0, h, cls) {
  const hole = (x, flip) =>
    `<path class="${cls}" d="M${x} ${y0} c${flip ? 10 : -10} ${h * 0.22} ${flip ? 10 : -10} ${h * 0.7} 0 ${h}"/>` +
    `<circle class="${cls}" cx="${x}" cy="${y0}" r="3.2"/><circle class="${cls}" cx="${x}" cy="${y0 + h}" r="3.2"/>`;
  return hole(x1, false) + hole(x2, true);
}

function strings(n, yTop, yBottom, spreadTop, spreadBottom, cls) {
  let s = '';
  for (let i = 0; i < n; i++) {
    const t = n === 1 ? 0.5 : i / (n - 1);
    const xt = 100 - spreadTop / 2 + spreadTop * t, xb = 100 - spreadBottom / 2 + spreadBottom * t;
    s += `<line class="${cls}" x1="${xt.toFixed(1)}" y1="${yTop}" x2="${xb.toFixed(1)}" y2="${yBottom}" style="stroke-width:${(0.7 + (n - 1 - i) * 0.35).toFixed(2)}"/>`;
  }
  return s;
}

// The small fixed logo (brass line art). Does not change with the body.
function celloLogoSvg() {
  return `
    <svg class="logo-svg" viewBox="0 0 200 340" aria-label="No Strings cello logo" role="img">
      <path class="logo-body" d="${CELLO_OUTLINE}"/>
      <path class="logo-line" d="M92 76 L92 12 M108 76 L108 12"/>
      <path class="logo-line" d="M100 10 c-14 -2 -16 16 -4 16 c8 0 8 -10 2 -10"/>
      ${fHoles(70, 130, 200, 62, 'logo-line')}
      <path class="logo-line" d="M80 240 L120 240 L118 250 L82 250 Z"/>
      ${strings(4, 20, 290, 12, 14, 'logo-string')}
      <path class="logo-line" d="M93 272 L107 272 L104 318 L96 318 Z"/>
    </svg>`;
}

// The selected body, for the BODY panel.
//  info: { name, strings[], fretted, electric, shape: 'cello'|'guitar'|'slab'|'frame' }
function bodyPictureSvg(info) {
  const n = info.strings.length;
  const name = info.name.toLowerCase();
  const scale = name.includes('piccolo') ? 0.86 : 1;
  const finish = name.includes('carbon') ? 'carbon' : name.includes('baroque') ? 'baroque'
               : name.includes('arpeggione') ? 'arpeggione' : info.electric ? 'electric' : 'varnish';
  let inner = '';
  const ox = (200 - 200 * scale) / 2, oy = 330 - 330 * scale;
  inner += `<g transform="translate(${ox} ${oy}) scale(${scale})">`;

  if (info.shape === 'frame') {
    // skeleton / silent cello: the silhouette as an open frame, a centre spine and a chest rest
    inner += `<path class="bp-frame" d="${CELLO_OUTLINE}"/>`;
    inner += `<rect class="bp-spine" x="86" y="60" width="28" height="275" rx="6"/>`;
    inner += `<path class="bp-frame" d="M30 150 C60 160 140 160 170 150"/>`;
    inner += `<rect class="bp-frame" x="116" y="210" width="44" height="10" rx="4"/>`;
  } else if (info.shape === 'slab') {
    inner += `<path class="bp-body bp-${finish}" d="${SLAB_OUTLINE}"/>`;
    inner += `<circle class="bp-knob" cx="122" cy="276" r="6"/><circle class="bp-knob" cx="122" cy="292" r="4.5"/>`;
  } else {
    const outline = info.shape === 'guitar' ? GUITAR_OUTLINE : CELLO_OUTLINE;
    inner += `<path class="bp-body bp-${finish}" d="${outline}"/>`;
    inner += `<path class="bp-purfling" d="${outline}" transform="translate(100 200) scale(0.955) translate(-100 -200)"/>`;
    inner += fHoles(70, 130, 198, 64, 'bp-fhole');
  }

  // neck, fingerboard, pegbox, scroll
  inner += `<path class="bp-neck" d="M91 12 L109 12 L111 80 L89 80 Z"/>`;
  inner += `<path class="bp-fingerboard" d="M90 0 L110 0 L113 ${info.shape === 'slab' ? 150 : 190} L87 ${info.shape === 'slab' ? 150 : 190} Z"/>`;
  if (info.fretted) for (let f = 1; f <= 12; f++) {
    const y = 14 + 150 * (1 - Math.pow(2, -f / 12)) * 1.9;
    inner += `<line class="bp-fret" x1="88" y1="${y.toFixed(1)}" x2="112" y2="${y.toFixed(1)}"/>`;
  }
  inner += `<path class="bp-scroll" d="M100 6 c-15 -4 -18 16 -5 16 c9 0 9 -11 2 -11"/>`;
  // bridge, tailpiece, strings
  const bridgeY = info.shape === 'slab' ? 235 : 242;
  inner += `<path class="bp-bridge" d="M79 ${bridgeY - 4} L121 ${bridgeY - 4} L119 ${bridgeY + 9} L81 ${bridgeY + 9} Z"/>`;
  inner += `<path class="bp-tail" d="M92 ${bridgeY + 32} L108 ${bridgeY + 32} L105 ${bridgeY + 78} L95 ${bridgeY + 78} Z"/>`;
  inner += strings(n, 14, bridgeY + 34, 4 + n * 2.2, 6 + n * 2.4, 'bp-string');
  if (!info.electric) inner += `<rect class="bp-endpin" x="98.5" y="330" width="3" height="10"/>`;
  inner += '</g>';

  return `<svg class="body-svg" viewBox="-10 -4 220 348" role="img" aria-label="${info.name} cello">${inner}</svg>`;
}
