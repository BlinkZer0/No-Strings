#pragma once
// ============================================================================
//  The eight bodies, as data. Each one sets:
//   - strings: count, open-string tuning, playable range, fretted or not
//   - bridge:  "bridge hill" resonance and high roll-off (Bridge.h)
//   - body:    signature low modes (A0 air, CBR, B1-, B1+ ...) plus a seeded
//              cloud of higher modes under a spectral envelope (ModalBody.h)
//   - coupling: the strongest modes as seen by the strings at the bridge
//              (the wolf note lives here)
//   - radiation: stereo spread, air roll-off, close-mic proximity (Radiation.h)
//   - electric bodies go through the amp / DI stage (AmpStage.h)
//
//  Low-mode frequencies follow published cello modal studies (A0 ~ 100 Hz,
//  B1 pair ~ 170..210 Hz, wolf near F3..F#3) and scale with body size.
// ============================================================================

namespace nostrings
{
enum BodyType
{
    bodyModern, bodyBaroque, bodyFiveString, bodyCarbon, bodyPiccolo, bodyArpeggione,
    bodyElectricSolid, bodyElectricSkeleton, kNumBodies
};

struct Mode { double hz, q, gain; };
struct EnvPoint { double hz, db; };

struct BodyProfile
{
    const char* name;
    int numStrings;
    int strings[6];          // open strings, MIDI notes, low to high
    int rangeLo, rangeHi;    // natural range (all MIDI notes still play)
    bool fretted, electric;
    // bridge
    double hillHz, hillDb, hillQ, bridgeLpHz;
    // body: signature low modes + seeded cloud
    Mode low[6];
    int cloudCount;
    double cloudLoHz, cloudHiHz, cloudQLo, cloudQHi;
    EnvPoint env[7];         // cloud spectral envelope (dB over log frequency)
    double direct;           // share of the bridge signal that bypasses the modes (piezo)
    // string <-> body coupling at the bridge (0 = rigid bridge)
    Mode coupling[2];        // gain = coupling strength c (sum must stay <= 1)
    // radiation
    double spread, airLpHz, proximityDb;
    // finger damping on stopped notes (share of decay / high decay kept)
    double stoppedDecay, stoppedHf;
    double outputDb;         // level match (set by rendering, see NoStringsRender stage 3)
};

inline const BodyProfile& bodyProfile (int b)
{
    static const BodyProfile p[kNumBodies] = {
        // 1. Modern concert cello: warm, full, the reference
        { "Modern Concert", 4, { 36, 43, 50, 57 }, 36, 81, false, false,
          2000, 4.0, 1.0, 7000,
          { { 98, 20, 0.9 }, { 145, 25, 0.4 }, { 172, 30, 1.0 }, { 204, 28, 1.1 }, { 265, 30, 0.6 }, { 330, 30, 0.5 } },
          48, 380, 7000, 25, 60,
          { { 400, -4 }, { 800, -2 }, { 1500, 0 }, { 2200, 1 }, { 3500, -5 }, { 6000, -14 }, { 8000, -20 } },
          0.05,
          { { 178, 35, 0.4 }, { 98, 20, 0.02 } },
          0.6, 14000, 0,
          0.65, 0.6, +0.0 },
        // 2. Baroque: lighter build, gut-voiced, sweeter, less projecting
        { "Baroque", 4, { 36, 43, 50, 57 }, 36, 77, false, false,
          1700, 2.0, 0.9, 5500,
          { { 95, 18, 0.9 }, { 140, 22, 0.5 }, { 165, 26, 1.0 }, { 195, 25, 1.0 }, { 255, 25, 0.7 }, { 320, 28, 0.5 } },
          44, 360, 6000, 20, 45,
          { { 400, -2 }, { 800, 0 }, { 1500, -1 }, { 2200, -3 }, { 3500, -9 }, { 6000, -20 }, { 8000, -26 } },
          0.04,
          { { 170, 30, 0.45 }, { 95, 18, 0.02 } },
          0.55, 11000, 0,
          0.6, 0.5, +0.8 },
        // 3. Five-string: adds a high E, same family as the modern cello
        { "Five-String", 5, { 36, 43, 50, 57, 64 }, 36, 88, false, false,
          2100, 4.0, 1.0, 7500,
          { { 96, 20, 0.9 }, { 142, 25, 0.4 }, { 170, 30, 1.0 }, { 200, 28, 1.1 }, { 262, 30, 0.6 }, { 325, 30, 0.5 } },
          50, 380, 7500, 25, 60,
          { { 400, -4 }, { 800, -2 }, { 1500, 0 }, { 2400, 2 }, { 3800, -4 }, { 6500, -13 }, { 9000, -20 } },
          0.05,
          { { 176, 35, 0.035 }, { 96, 20, 0.02 } },
          0.6, 14000, 0,
          0.65, 0.6, +1.8 },
        // 4. Carbon fibre: bright, focused, very projecting, almost no wolf
        { "Carbon Fiber", 4, { 36, 43, 50, 57 }, 36, 84, false, false,
          2400, 6.0, 1.2, 9000,
          { { 104, 30, 0.8 }, { 155, 35, 0.4 }, { 188, 40, 1.0 }, { 222, 40, 1.0 }, { 290, 45, 0.7 }, { 360, 45, 0.6 } },
          52, 400, 8000, 40, 90,
          { { 400, -3 }, { 800, -1 }, { 1500, 1 }, { 2500, 3 }, { 4000, 0 }, { 6500, -8 }, { 9000, -16 } },
          0.08,
          { { 190, 45, 0.015 }, { 104, 30, 0.01 } },
          0.5, 16000, 0,
          0.7, 0.65, +1.2 },
        // 5. Cello piccolo: smaller body, higher and more nasal
        { "Cello Piccolo", 5, { 36, 43, 50, 57, 64 }, 36, 91, false, false,
          2600, 5.0, 1.4, 8000,
          { { 125, 22, 0.6 }, { 185, 26, 0.4 }, { 222, 30, 0.9 }, { 262, 28, 1.0 }, { 335, 30, 0.8 }, { 420, 30, 0.7 } },
          44, 450, 7500, 25, 55,
          { { 500, -5 }, { 900, -1 }, { 1300, 4 }, { 1800, 1 }, { 2700, 2 }, { 4500, -6 }, { 7000, -16 } },
          0.05,
          { { 230, 35, 0.03 }, { 125, 22, 0.015 } },
          0.55, 13000, 0,
          0.65, 0.6, +2.6 },
        // 6. Arpeggione: fretted, guitar-shaped bowed hybrid (6 strings, guitar tuning)
        { "Arpeggione", 6, { 40, 45, 50, 55, 59, 64 }, 40, 81, true, false,
          1900, 2.0, 0.8, 6500,
          { { 110, 15, 1.0 }, { 190, 20, 1.0 }, { 240, 22, 0.7 }, { 310, 22, 0.6 }, { 400, 25, 0.5 }, { 520, 25, 0.4 } },
          40, 450, 6500, 20, 40,
          { { 500, -3 }, { 1000, -2 }, { 2000, -1 }, { 3000, -5 }, { 5000, -12 }, { 7000, -20 }, { 9000, -26 } },
          0.04,
          { { 190, 22, 0.025 }, { 110, 15, 0.02 } },
          0.6, 12000, 0,
          1.0, 0.95, +3.7 },  // frets: stopped notes ring like open ones
        // 7. Electric, solid body: piezo, almost no acoustic body -> amp / DI
        { "Electric Solid", 4, { 36, 43, 50, 57 }, 36, 88, false, true,
          2800, 3.0, 0.7, 12000,
          { { 320, 15, 0.15 }, { 780, 15, 0.12 }, { 1400, 15, 0.1 }, { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1, 0 } },
          0, 400, 4000, 20, 30,
          { { 400, 0 }, { 800, 0 }, { 1500, 0 }, { 2200, 0 }, { 3500, 0 }, { 6000, 0 }, { 8000, 0 } },
          1.0,
          { { 0, 1, 0 }, { 0, 1, 0 } },
          0.0, 20000, 0,
          0.7, 0.7, -14.0 },
        // 8. Electric, skeleton / "silent" frame: a hint of body, close-miked, intimate
        { "Electric Skeleton", 4, { 36, 43, 50, 57 }, 36, 88, false, true,
          2200, 2.0, 0.8, 10000,
          { { 140, 12, 0.25 }, { 260, 14, 0.2 }, { 520, 15, 0.15 }, { 900, 18, 0.12 }, { 1600, 20, 0.1 }, { 0, 1, 0 } },
          12, 600, 5000, 15, 30,
          { { 600, -6 }, { 1000, -6 }, { 1500, -6 }, { 2200, -6 }, { 3500, -8 }, { 5000, -12 }, { 8000, -20 } },
          0.8,
          { { 0, 1, 0 }, { 0, 1, 0 } },
          0.3, 15000, 4.0,
          0.65, 0.6, -11.5 },
    };
    return p[b >= 0 && b < kNumBodies ? b : 0];
}
} // namespace nostrings
