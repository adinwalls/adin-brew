// Parameter layout shared by the LevelBrew DSP plugin and its editor.
#pragma once
#include "LevelEngine.hpp"

namespace levelbrew {

enum ParamId {
    // inputs
    pStrength = 0, pRange, pSpeed, pOutput, pBypass, pClear,
    // outputs (meters / status)
    pOutGain, pOutPos, pOutState, pOutScanned, pOutIn, pOutOut, pOutActivity, pOutRiding,
    pCount
};

enum ScanState { kScanEmpty = 0, kScanScanning, kScanReady };

struct ParamSpec { const char* label; float min, max, def; float skew; bool integer; bool bipolar; const char* unit; };

static const ParamSpec kSpecs[pCount] = {
    { "STRENGTH",   0, 100, 85, 1, false, false, "%" },
    { "RANGE",      1, 18, 9, 1, false, false, "dB" },
    { "SPEED",      0.1f, 2.0f, 0.4f, 1.8f, false, false, "s" },
    { "OUTPUT",   -24, 12, 0, 1, false, true, "dB" },
    { "Bypass",     0, 1, 0, 1, true, false, "" },
    { "Clear Scan", 0, 1, 0, 1, true, false, "" },
    { "Ride",     -18, 18, 0, 1, false, true, "dB" },
    { "Position",  -1, 86400, -1, 1, false, false, "s" },
    { "Scan",       0, 2, 0, 1, true, false, "" },
    { "Scanned",    0, 86400, 0, 1, false, false, "s" },
    { "In",       -90, 6, -90, 1, false, false, "dB" },
    { "Out",      -90, 6, -90, 1, false, false, "dB" },
    { "Activity",   0, 1000, 0, 1, true, false, "" },
    { "Riding",     0, 1, 0, 1, true, false, "" },
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
