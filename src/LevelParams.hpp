// Parameter layout shared by the LevelBrew DSP plugin and its editor.
#pragma once
#include "LevelEngine.hpp"

namespace levelbrew {

enum ParamId {
    // inputs
    pStrength = 0, pRange, pSpeed, pOutput, pBypass, pRealign,
    // outputs (meters / status)
    pOutGain, pOutPos, pOutStatus, pOutAlign, pOutOffset, pOutIn, pOutOut, pOutActivity, pOutFileVer,
    pCount
};

enum Status { kNoFile = 0, kLoading, kReady, kError };
enum Align { kAlignIdle = 0, kAlignListening, kAlignLocked };

struct ParamSpec { const char* label; float min, max, def; float skew; bool integer; bool bipolar; const char* unit; };

static const ParamSpec kSpecs[pCount] = {
    { "STRENGTH",   0, 100, 85, 1, false, false, "%" },
    { "RANGE",      1, 18, 9, 1, false, false, "dB" },
    { "SPEED",      0.1f, 2.0f, 0.4f, 1.8f, false, false, "s" },
    { "OUTPUT",   -24, 12, 0, 1, false, true, "dB" },
    { "Bypass",     0, 1, 0, 1, true, false, "" },
    { "Re-align",   0, 1, 0, 1, true, false, "" },
    { "Ride",     -18, 18, 0, 1, false, true, "dB" },
    { "Position",  -1, 36000, -1, 1, false, false, "s" },
    { "Status",     0, 3, 0, 1, true, false, "" },
    { "Align",      0, 2, 0, 1, true, false, "" },
    { "Offset", -36000, 36000, 0, 1, false, false, "s" },
    { "In",       -90, 6, -90, 1, false, false, "dB" },
    { "Out",      -90, 6, -90, 1, false, false, "dB" },
    { "Activity",   0, 1000, 0, 1, true, false, "" },
    { "File",       0, 1000000, 0, 1, true, false, "" },
};

inline RideSettings settingsFrom(const float* v)
{
    RideSettings rs;
    rs.strength = v[pStrength] * 0.01;
    rs.range = v[pRange];
    rs.speed = v[pSpeed];
    return rs;
}

} // namespace levelbrew
