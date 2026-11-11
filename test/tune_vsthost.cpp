#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>


struct AEffect;
typedef intptr_t (*HostCb)(AEffect*, int32_t, int32_t, intptr_t, void*, float);
struct AEffect {
    int32_t magic;
    intptr_t (*dispatcher)(AEffect*, int32_t, int32_t, intptr_t, void*, float);
    void (*process)(AEffect*, float**, float**, int32_t);
    void (*setParameter)(AEffect*, int32_t, float);
    float (*getParameter)(AEffect*, int32_t);
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t resvd1, resvd2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void *object, *user;
    int32_t uniqueID, version;
    void (*processReplacing)(AEffect*, float**, float**, int32_t);
    void (*processDoubleReplacing)(AEffect*, double**, double**, int32_t);
    char future[56];
};
struct VstTimeInfo {
    double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos;
    int32_t timeSigNumerator, timeSigDenominator, smpteOffset, smpteFrameRate, samplesToNextClock, flags;
};
static VstTimeInfo ti;
static intptr_t hostCb(AEffect*, int32_t op, int32_t, intptr_t, void*, float)
{
    if (op == 1) return 2400;            // version
    if (op == 7) return (intptr_t)&ti;   // get time
    return 0;
}

// tune_vsthost TuneBrew-vst.so vocal48k.f32
// Drives the real TuneBrew VST2 binary like a DAW: plays 20 s with a running
// transport, saves the session, reopens it in a fresh instance and checks the
// pitch scan and marked phrases come back. PASS / FAIL.
int main(int argc, char** argv)
{
    FILE* f = fopen(argv[2], "rb"); std::vector<float> x; float v; while (fread(&v, 4, 1, f) == 1) x.push_back(v); fclose(f);
    const int B = 512; const double sr = 48000;
    ti.sampleRate = sr; ti.tempo = 120; ti.timeSigNumerator = 4; ti.timeSigDenominator = 4;
    void* lib = dlopen(argv[1], RTLD_NOW);
    if (!lib) { printf("dlopen: %s\n", dlerror()); return 1; }
    auto entry = (AEffect* (*)(HostCb))dlsym(lib, "VSTPluginMain");
    auto open = [&]() { AEffect* fx = entry(hostCb); fx->dispatcher(fx, 0, 0, 0, nullptr, 0); fx->dispatcher(fx, 10, 0, 0, nullptr, (float)sr);
                        fx->dispatcher(fx, 11, 0, B, nullptr, 0); fx->dispatcher(fx, 12, 0, 1, nullptr, 0); return fx; };
    auto chunk = [&](AEffect* fx) { void* p = nullptr; const intptr_t n = fx->dispatcher(fx, 23, 0, 0, &p, 0); return std::string((const char*)p, (size_t)n); };
    bool ok = true;
    auto check = [&](bool c, const char* what) { printf("  %-52s %s\n", what, c ? "ok" : "FAIL"); ok = ok && c; };

    AEffect* a = open();
    std::vector<float> L(B), R(B), OL(B), OR(B); float* ins[2] = { L.data(), R.data() }; float* outs[2] = { OL.data(), OR.data() };
    double peak = 0;
    const size_t from = (size_t)(140 * sr), to = (size_t)(160 * sr);
    for (size_t pos = from; pos + B <= to && pos + B <= x.size(); pos += B) {
        for (int i = 0; i < B; ++i) L[i] = R[i] = x[pos + i];
        ti.samplePos = (double)pos; ti.flags = 2;   // playing
        a->processReplacing(a, ins, outs, B);
        for (int i = 0; i < B; ++i) peak = std::max(peak, (double)std::fabs(OL[i]));
    }
    check(a->initialDelay > 1000 && a->initialDelay < 4000, "reports its latency");
    check(peak > 0.05 && peak < 1.5, "audio comes out");
    const std::string saved = chunk(a);
    const size_t at = saved.find("TB1 ");
    const long entries = at == std::string::npos ? 0 : std::strtol(saved.c_str() + at + 4, nullptr, 10);
    printf("  session data %zu bytes, pitch scan %ld entries\n", saved.size(), entries);
    check(entries > 15900 && entries < 16100, "pitch scan covers the song up to where it played");
    check(saved.find("regions") != std::string::npos, "phrases are part of the session");

    AEffect* b = open();
    b->dispatcher(b, 24, 0, (intptr_t)saved.size(), (void*)saved.data(), 0);
    check(chunk(b) == saved, "a reopened session has the same scan and phrases");
    a->dispatcher(a, 12, 0, 0, nullptr, 0); a->dispatcher(a, 1, 0, 0, nullptr, 0);
    b->dispatcher(b, 12, 0, 0, nullptr, 0); b->dispatcher(b, 1, 0, 0, nullptr, 0);
    printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
