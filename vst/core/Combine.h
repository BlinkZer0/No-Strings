#pragma once
// ============================================================================
//  COMBINE: a new excitation x body x material x sympathetic combination,
//  drawn within musical limits (no crunch pressures, no slack-gut EBows on
//  a carbon cello at 30 s of decay...). Works on raw parameter values indexed
//  by ParamIndex, so the plugin and the web build share it. Playing controls
//  (voice mode, vibrato, keyswitches, master) are left alone except vibrato
//  depth, which follows the articulation.
// ============================================================================
#include <string>
#include "ParamTable.h"

namespace nostrings
{
template <typename Values> // float& operator[] (int)
std::string combineRandom (Values& v, Rng& rng)
{
    auto pick = [&rng] (std::initializer_list<int> options)
    {
        const int k = (int) (rng.uni() * (double) options.size());
        return *(options.begin() + std::min (k, (int) options.size() - 1));
    };
    auto range = [&rng] (double lo, double hi) { return (float) lerp (lo, hi, rng.uni()); };

    // weighted: arco most of the time, then the plucked / struck ones, EBow now and then
    const int exc = pick ({ excArco, excArco, excArco, excArco, excPizzicato, excPizzicato, excBartok, excColLegnoBattuto,
                            excColLegnoTratto, excSpiccato, excSpiccato, excHarmonic, excMallet, excMallet, excEBow, excEBow });
    const int body = pick ({ bodyModern, bodyBaroque, bodyFiveString, bodyCarbon, bodyPiccolo, bodyArpeggione, bodyElectricSolid, bodyElectricSkeleton });
    const bool electric = bodyProfile (body).electric;
    // gut suits the acoustic bodies; steel and the wound sets suit everything
    const int material = electric ? pick ({ matSteel, matSteel, matSilverWound, matTungstenWound, matSynthetic })
                                  : pick ({ matPlainGut, matWoundGut, matSynthetic, matSynthetic, matSteel, matSteel, matSilverWound, matTungstenWound });

    v[pExcitation] = (float) exc;
    v[pBody] = (float) body;
    v[pMaterial] = (float) material;
    v[pTension] = range (0.3, 0.7);
    v[pAge] = range (0.0, 0.6);
    v[pBowPressure] = range (0.35, 0.65);
    v[pBowSpeed] = range (0.35, 0.7);
    v[pBowPosition] = range (0.25, 0.75);
    v[pHarmonic] = (float) pick ({ 2, 2, 3, 4 });
    v[pMalletHardness] = range (0.2, 0.9);
    v[pEbowMode] = (float) pick ({ 0, 0, 0, 1 });
    v[pReleaseTime] = exc == excPizzicato || exc == excBartok || exc == excMallet ? range (0.4, 1.2) : range (0.2, 0.7);

    // sympathetic strings about half the time
    const bool symp = rng.uni() < 0.5;
    v[pSympOn] = symp ? 1.0f : 0.0f;
    if (symp)
    {
        const int mode = pick ({ sympScale, sympScale, sympOpenStrings, sympOpenStrings, sympOpenStrings });
        v[pSympMode] = (float) mode;
        v[pSympCount] = (float) pick ({ 4, 6, 8, 8, 10, 12 });
        v[pSympAmount] = range (0.3, 0.8);
        v[pSympDecay] = range (2.0, 15.0);
        v[pSympBright] = range (0.3, 0.8);
        v[pSympKey] = (float) pick ({ 0, 2, 4, 5, 7, 9, 10 });
        v[pSympScale] = (float) pick ({ scaleMajor, scaleMinor, scaleDorian, scaleMixolydian, scalePentatonic, scaleDrone, scaleDrone });
    }

    if (electric)
    {
        v[pAmpDrive] = range (0.0, 0.75);
        v[pAmpBass] = range (0.35, 0.65);
        v[pAmpMid] = range (0.35, 0.65);
        v[pAmpTreble] = range (0.35, 0.65);
        v[pAmpCab] = rng.uni() < 0.7 ? 1.0f : 0.0f;
    }

    v[pVibDepth] = exc == excArco || exc == excHarmonic || exc == excEBow ? range (8.0, 28.0) : 0.0f;
    v[pReverbSend] = range (0.1, 0.45);
    v[pWidth] = range (0.8, 1.4);
    v[pPresetLevel] = 0.0f;

    return std::string (excitationName (exc)) + " \xc2\xb7 " + bodyProfile (body).name + " \xc2\xb7 " + materialProfile (material).name;
}
} // namespace nostrings
