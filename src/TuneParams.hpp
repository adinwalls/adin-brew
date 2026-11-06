// Parameter layout shared by the DSP plugin and the editor.
#pragma once
#include "TuneEngine.hpp"

namespace tunebrew {

enum ParamId {
    // inputs
    pKey = 0, pScale, pRetune, pAmount, pTranspose, pRefHz, pMix, pGain,
    pMode, pVibrato, pFlex, pHumanize, pTransition, pLearn,
    // outputs (meters)
    pOutPitch, pOutNote, pOutCents, pOutActivity, pOutVib,
    pOutKey, pOutKeyConf, pOutKeyAlt, pOutTuneOff, pOutLearnSecs,
    pCount
};

struct ParamSpec { const char* label; float min, max, def; float skew; bool integer; bool bipolar; const char* unit; };

static const ParamSpec kSpecs[pCount] = {
    { "Key",         0, 11, 0, 1, true, false, "" },
    { "Scale",       0, kScaleCount - 1, 0, 1, true, false, "" },
    { "RETUNE",      0, 400, 20, 2.2f, false, false, "ms" },
    { "AMOUNT",      0, 100, 100, 1, false, false, "%" },
    { "TRANSPOSE", -12, 12, 0, 1, true, true, "st" },
    { "REF A4",    415, 466, 440, 1, false, false, "Hz" },
    { "MIX",         0, 100, 100, 1, false, false, "%" },
    { "GAIN",      -24, 12, 0, 1, false, true, "dB" },
    { "Mode",        0, 1, 1, 1, true, false, "" },
    { "VIBRATO",     0, 200, 100, 1, false, false, "%" },
    { "FLEX",        0, 100, 20, 1, false, false, "%" },
    { "HUMANIZE",    0, 100, 30, 1, false, false, "%" },
    { "TRANSITION",  0, 150, 40, 1, false, false, "ms" },
    { "Learn",       0, 1, 0, 1, true, false, "" },
    { "Pitch",       0, 1200, 0, 1, false, false, "Hz" },
    { "Target",     -1, 127, -1, 1, true, false, "" },
    { "Cents",   -1200, 1200, 0, 1, false, false, "c" },
    { "Activity",    0, 1000, 0, 1, true, false, "" },
    { "Vibrato",     0, 1000, 0, 1, false, false, "c" },
    { "Key found",  -1, 23, -1, 1, true, false, "" },
    { "Confidence",  0, 1, 0, 1, false, false, "" },
    { "Alt key",    -1, 23, -1, 1, true, false, "" },
    { "Tuning",    -50, 50, 0, 1, false, false, "c" },
    { "Listened",    0, 3600, 0, 1, false, false, "s" },
};

static const char* const kModeNames[2] = { "Classic", "Natural" };

} // namespace tunebrew
