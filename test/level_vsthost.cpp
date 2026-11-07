// level_vsthost plugin.so take.wav changed.wav
// Drives the real LevelBrew VST2 binary like a DAW: a scan pass (at export speed),
// a ride pass, an edited take, a session save/reopen and Clear. PASS / FAIL.
#include "../src/LevelParams.hpp"
#include <dlfcn.h>
#include <thread>
#include <chrono>
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


struct Fx {
    void* lib; AEffect* fx;
    std::vector<float> L, R, OL, OR;
    float* ins[2]; float* outs[2];
    static constexpr int B = 512;
    double sr;
    Fx(const char* so, double s) : sr(s), L(B), R(B), OL(B), OR(B)
    {
        lib = dlopen(so, RTLD_NOW);
        if (!lib) { printf("dlopen: %s\n", dlerror()); exit(1); }
        auto entry = (AEffect* (*)(HostCb))dlsym(lib, "VSTPluginMain");
        fx = entry(hostCb);
        fx->dispatcher(fx, 0, 0, 0, nullptr, 0);
        fx->dispatcher(fx, 10, 0, 0, nullptr, (float)sr);
        fx->dispatcher(fx, 11, 0, B, nullptr, 0);
        fx->dispatcher(fx, 12, 0, 1, nullptr, 0);
        ins[0] = L.data(); ins[1] = R.data(); outs[0] = OL.data(); outs[1] = OR.data();
    }
    ~Fx() { fx->dispatcher(fx, 12, 0, 0, nullptr, 0); fx->dispatcher(fx, 1, 0, 0, nullptr, 0); }
    float out(int p) { return kSpecs[p].min + fx->getParameter(fx, p) * (kSpecs[p].max - kSpecs[p].min); }
    void idle(double secs)
    {
        ti.flags = 0;
        std::fill(L.begin(), L.end(), 0.f); std::fill(R.begin(), R.end(), 0.f);
        for (int i = 0; i < (int)(secs * 20); ++i) { fx->processReplacing(fx, ins, outs, B); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
    }
    // play the timeline [from, to) with `src` placed at `region`, at `speed` x real time.
    // Returns rms error of applied gain vs `ideal` where the plugin is riding, plus the
    // share of singing time it was riding and the largest gain applied (for scan passes).
    void play(const std::vector<float>& src, double region, double from, double to, const Curve* ideal,
              double& err, double& ridingShare, double& maxGain, double speed = 1.0)
    {
        int64_t pos = (int64_t)(from * sr);
        double e2 = 0; long n = 0, act = 0, rid = 0; maxGain = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (; pos < (int64_t)(to * sr); pos += B) {
            for (int i = 0; i < B; ++i) {
                const int64_t k = pos + i - (int64_t)std::llround(region * sr);
                L[i] = R[i] = (k >= 0 && k < (int64_t)src.size()) ? src[(size_t)k] : 0.f;
            }
            ti.samplePos = (double)pos; ti.flags = 2;   // playing
            fx->processReplacing(fx, ins, outs, B);
            // pace like a real (or freewheeling) engine
            const double due = (pos - from * sr + B) / sr / speed;
            const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (due > el) std::this_thread::sleep_for(std::chrono::duration<double>(due - el));
            double ei = 0, eo = 0;
            for (int i = 0; i < B; ++i) { ei += (double)L[i] * L[i]; eo += (double)OL[i] * OL[i]; }
            if (ei < B * 1e-6) continue;
            const double got = 10 * std::log10(eo / ei);
            maxGain = std::max(maxGain, std::fabs(got));
            if (!ideal) continue;
            bool ok; const float want = ideal->at(pos + B / 2, ok);
            if (!ok) continue;
            ++act;
            if (out(pOutRiding) > 0.5f && (pos - from * sr) / sr > 0.5) { ++rid; e2 += (got - want) * (got - want); ++n; }
        }
        err = n ? std::sqrt(e2 / n) : 99; ridingShare = act ? (double)rid / act : 0;
        idle(0.5);   // transport stops
    }
};

int main(int argc, char** argv)
{
    const char *so = argv[1], *take = argv[2], *changed = argv[3];
    Audio a, b; readWav(take, a); readWav(changed, b);
    const double sr = a.sr, region = 12.37, end = region + a.mono.size() / sr + 0.5;
    const int64_t off = (int64_t)std::llround(region * sr);
    auto ideal1 = buildCurve(analyseAudio(a, off), RideSettings());
    auto ideal2 = buildCurve(analyseAudio(b, off), RideSettings());
    ti.sampleRate = sr; ti.tempo = 120; ti.timeSigNumerator = 4; ti.timeSigDenominator = 4;
    int fails = 0;
    auto check = [&](bool ok, const char* what) { printf("  %s %s\n", ok ? "ok  " : "FAIL", what); fails += !ok; };
    double err, share, mx;
    std::string saved;
    {
        Fx p(so, sr);
        printf("plugin loaded: %d params\n", p.fx->numParams);
        p.idle(0.5);
        check(std::lround(p.out(pOutState)) == kScanEmpty, "starts with nothing scanned");

        // scan pass, like exporting the track (freewheel, 40x real time)
        p.play(a.mono, region, 0, end, nullptr, err, share, mx, 40.0);
        printf("scan pass (export speed): largest gain applied %.3f dB, scanned %.1f s, state %d\n", mx, p.out(pOutScanned), (int)std::lround(p.out(pOutState)));
        check(mx < 0.05, "the scan pass leaves the audio untouched");
        check(p.out(pOutScanned) > a.mono.size() / sr && std::lround(p.out(pOutState)) == kScanReady, "whole take scanned and ready");

        p.play(a.mono, region, 60, 75, ideal1.get(), err, share, mx);
        printf("ride pass 1:00-1:15: riding %.0f%% of singing, gain error rms %.3f dB\n", share * 100, err);
        check(share > 0.9 && err < 0.1, "rides the take on the next playback");

        // the vocal is edited (95-120 s of the take 8 dB quieter); play through it twice
        p.play(b.mono, region, 100, 140, ideal2.get(), err, share, mx);
        printf("edited take, first play: riding %.0f%% of the edited take's singing\n", share * 100);
        p.play(b.mono, region, 100, 140, ideal2.get(), err, share, mx);
        printf("edited take, second play: riding %.0f%%, gain error rms %.3f dB\n", share * 100, err);
        check(share > 0.9 && err < 0.3, "re-scans the edit and rides it from the next pass");

        void* ptr = nullptr;
        const intptr_t sz = p.fx->dispatcher(p.fx, 23, 0, 0, &ptr, 0);
        saved.assign((const char*)ptr, (size_t)sz);
        printf("session state: %.0f KB\n", sz / 1024.0);
    }
    {   // reopen the session in a fresh instance
        Fx p(so, sr);
        p.fx->dispatcher(p.fx, 24, 0, (intptr_t)saved.size(), (void*)saved.data(), 0);
        p.idle(0.5);
        printf("reopened: scanned %.1f s, state %d\n", p.out(pOutScanned), (int)std::lround(p.out(pOutState)));
        p.play(b.mono, region, 150, 165, ideal2.get(), err, share, mx);
        printf("reopened, play 2:30-2:45: riding %.0f%%, gain error rms %.3f dB\n", share * 100, err);
        check(share > 0.9 && err < 0.3, "a reopened session rides without re-scanning");

        p.fx->setParameter(p.fx, pClear, 1.f);
        p.idle(0.5);
        check(std::lround(p.out(pOutState)) == kScanEmpty, "Clear forgets the scan");
    }
    printf(fails ? "FAIL\n" : "PASS\n");
    return fails;
}
