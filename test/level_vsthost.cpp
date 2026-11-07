// level_vsthost plugin.so take.wav changed.wav work.wav
// Drives the real LevelBrew VST2 binary like a DAW: gives it the take via state,
// plays it with the take placed at an unknown timeline position, then rewrites
// the file on disk and checks the plugin re-scans it. Prints PASS / FAIL.
#include "../src/LevelParams.hpp"
#include <dlfcn.h>
#include <thread>
#include <chrono>
#include <fstream>
using namespace levelbrew;

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

static void copyFile(const char* a, const char* b) { std::ifstream i(a, std::ios::binary); std::ofstream o(b, std::ios::binary); o << i.rdbuf(); }

int main(int argc, char** argv)
{
    const char *so = argv[1], *take = argv[2], *changed = argv[3], *work = argv[4];
    copyFile(take, work);
    void* lib = dlopen(so, RTLD_NOW);
    if (!lib) { printf("dlopen: %s\n", dlerror()); return 1; }
    auto entry = (AEffect* (*)(HostCb))dlsym(lib, "VSTPluginMain");
    AEffect* fx = entry(hostCb);
    const double sr = 44100; const int B = 512;
    fx->dispatcher(fx, 0, 0, 0, nullptr, 0);          // open
    fx->dispatcher(fx, 10, 0, 0, nullptr, (float)sr); // sample rate
    fx->dispatcher(fx, 11, 0, B, nullptr, 0);         // block size
    fx->dispatcher(fx, 12, 0, 1, nullptr, 0);         // resume
    ti.sampleRate = sr; ti.tempo = 120; ti.timeSigNumerator = 4; ti.timeSigDenominator = 4;
    printf("plugin params %d, in %d out %d\n", fx->numParams, fx->numInputs, fx->numOutputs);

    // give it the file through state, like a session load
    std::string chunk = std::string("file") + '\0' + work + '\0' + "offset" + '\0' + '\0' + '\0';
    fx->dispatcher(fx, 24, 0, (intptr_t)chunk.size(), (void*)chunk.data(), 0);

    Audio a; readWav(take, a);
    std::vector<float> L(B), R(B), OL(B), OR(B);
    float* ins[2] = { L.data(), R.data() }; float* outs[2] = { OL.data(), OR.data() };
    auto outParam = [&](int p) { return kSpecs[p].min + fx->getParameter(fx, p) * (kSpecs[p].max - kSpecs[p].min); };
    auto idle = [&](double secs) {
        ti.flags = 0;
        std::fill(L.begin(), L.end(), 0.f); std::fill(R.begin(), R.end(), 0.f);
        for (int i = 0; i < (int)(secs * 20); ++i) { fx->processReplacing(fx, ins, outs, B); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
    };
    const double region = 12.37;
    int fails = 0;
    // play a stretch of the timeline; returns rms error vs the ideal curve after it has synced
    auto play = [&](const Audio& src, const Curve& ideal, double from, double secs, const char* name) {
        int64_t pos = (int64_t)(from * sr);
        double err2 = 0; long n = 0; double syncedAt = -1;
        for (int b = 0; b < (int)(secs * sr / B); ++b, pos += B) {
            for (int i = 0; i < B; ++i) {
                const int64_t k = pos + i - (int64_t)(region * sr);
                L[i] = R[i] = (k >= 0 && k < (int64_t)src.mono.size()) ? src.mono[k] : 0.f;
            }
            ti.samplePos = (double)pos; ti.flags = 2;   // playing
            fx->processReplacing(fx, ins, outs, B);
            if (b % 8 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));   // let the worker thread run
            const bool locked = std::lround(outParam(pOutAlign)) == kAlignLocked;
            if (locked && syncedAt < 0) syncedAt = (pos - from * sr) / sr;
            if (locked && (pos - from * sr) / sr > syncedAt + 0.35) {
                double ei = 0, eo = 0;
                for (int i = 0; i < B; ++i) { ei += (double)L[i] * L[i]; eo += (double)OL[i] * OL[i]; }
                if (ei > B * 1e-6) {
                    const double got = 10 * std::log10(eo / ei);
                    const double want = ideal.at((pos + B / 2) / sr - region);
                    err2 += (got - want) * (got - want); ++n;
                }
            }
        }
        const double rms = n ? std::sqrt(err2 / n) : 99;
        printf("%-34s synced after %5.1fs, take start %.3fs, gain error rms %.3f dB\n", name, syncedAt, outParam(pOutOffset), rms);
        return rms;
    };
    auto idealFor = [&](const char* path) {
        Audio x; readWav(path, x); Analysis an; analyse(x, an);
        Curve c; c.sr = an.sr; c.duration = an.duration; c.hop = an.hop; c.win = an.win; c.g = gainCurve(an, RideSettings());
        return c;
    };

    idle(3.0);
    printf("status after load: %d (2 = ready), file version %d\n", (int)std::lround(outParam(pOutStatus)), (int)std::lround(outParam(pOutFileVer)));
    if (std::lround(outParam(pOutStatus)) != kReady) { printf("FAIL: file not loaded\n"); return 1; }
    const Curve c1 = idealFor(take);

    if (play(a, c1, 60, 25, "play from 1:00 (first sync)") > 0.15) ++fails;
    if (play(a, c1, 150, 10, "seek to 2:30 (already synced)") > 0.15) ++fails;

    void* p = nullptr;
    const intptr_t sz = fx->dispatcher(fx, 23, 0, 0, &p, 0);
    std::string saved((const char*)p, sz);
    const size_t o = saved.find(std::string("offset") + '\0');
    printf("saved session state: offset = \"%s\"\n", o == std::string::npos ? "?" : saved.c_str() + o + 7);
    if (o == std::string::npos || std::fabs(std::atof(saved.c_str() + o + 7) - region) > 0.011) ++fails;

    // the take is changed on disk (re-recorded / edited): plugin must re-scan by itself
    const int ver0 = (int)std::lround(outParam(pOutFileVer));
    copyFile(changed, work);
    double waited = 0;
    while (waited < 8 && (int)std::lround(outParam(pOutFileVer)) == ver0) { idle(0.25); waited += 0.25; }
    printf("file changed on disk: re-scanned after %.2fs (version %d -> %d)\n", waited, ver0, (int)std::lround(outParam(pOutFileVer)));
    if ((int)std::lround(outParam(pOutFileVer)) == ver0) ++fails;
    Audio a2; readWav(changed, a2);
    const Curve c2 = idealFor(changed);
    if (play(a2, c2, 105, 20, "play changed take from 1:45") > 0.15) ++fails;

    fx->dispatcher(fx, 12, 0, 0, nullptr, 0);
    fx->dispatcher(fx, 1, 0, 0, nullptr, 0);   // close
    printf(fails ? "FAIL\n" : "PASS\n");
    return fails;
}
