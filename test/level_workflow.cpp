// level_workflow take.wav changed.wav — simulates the DAW workflow on the engine:
// scan pass, ride pass, edited take (re-scan), session save/restore. PASS / FAIL.
#include "../src/LevelEngine.hpp"
using namespace levelbrew;

struct Sim {
    double sr; TrackScanner sc; Player pl; RideSettings rs;
    std::vector<std::shared_ptr<Curve>> keep;
    explicit Sim(double s) : sr(s) { sc.init(s); pl.init(s); }
    void worker(bool stopped)
    {
        pl.fifo.drain([&](int64_t p, const float* x, size_t n) { sc.feed(p, x, n); });
        if (stopped) sc.endRun();
        if (sc.needsRebuild()) { keep.push_back(buildCurve(sc.analysis(), rs)); pl.curve.store(keep.back().get()); }
    }
    // play [from, to) of the timeline with `src` placed at `region`; returns rms error of the
    // applied gain vs ref (only where ref says it should ride) and the max |gain| where it shouldn't
    void play(const std::vector<float>& src, double region, double from, double to, const Curve* ref,
              double& errRms, double& strayMax, double skipFrom = -1, double skipTo = -1, double* ridingFrac = nullptr)
    {
        const int B = 512;
        std::vector<float> in(B), out(B);
        int64_t pos = (int64_t)(from * sr);
        double e2 = 0; long n = 0, nr = 0; strayMax = 0;
        for (; pos < (int64_t)(to * sr); pos += B) {
            for (int i = 0; i < B; ++i) {
                const int64_t k = pos + i - (int64_t)std::llround(region * sr);
                in[i] = (k >= 0 && k < (int64_t)src.size()) ? src[(size_t)k] : 0.f;
            }
            const float* ip[1] = { in.data() }; float* op[1] = { out.data() };
            pl.process(ip, op, 1, B, true, pos);
            if ((pos / B) % 20 == 0) worker(false);
            const double t = (pos + B) / sr;
            if (t - from < 0.5) continue;
            if (ref) {
                bool ok; const float want = ref->at(pos + B, ok);
                if (ok && !(t > skipFrom - 0.5 && t < skipTo + 0.5)) { e2 += (pl.meterGain - want) * (pl.meterGain - want); ++n; nr += pl.meterRiding; }
            } else strayMax = std::max(strayMax, (double)std::fabs(pl.meterGain));
        }
        worker(true);
        errRms = n ? std::sqrt(e2 / n) : 0;
        if (ridingFrac) *ridingFrac = n ? (double)nr / n : 0;
    }
};

int main(int, char** argv)
{
    Audio a, b; readWav(argv[1], a); readWav(argv[2], b);
    const double sr = a.sr, region = 12.37, end = region + a.mono.size() / sr + 1;
    const int64_t off = (int64_t)std::llround(region * sr);
    auto ref1 = buildCurve(analyseAudio(a, off), RideSettings());
    auto ref2 = buildCurve(analyseAudio(b, off), RideSettings());
    auto fileC = buildCurve(analyseAudio(a, 0), RideSettings());
    int fails = 0;
    auto check = [&](bool ok, const char* what) { printf("  %s %s\n", ok ? "ok  " : "FAIL", what); fails += !ok; };

    // timeline curve vs the take on its own
    double md = 0;
    for (double t = 1; t < a.mono.size() / sr - 1; t += 0.05) md = std::max(md, (double)std::fabs(ref1->atTime(t + region) - fileC->atTime(t)));
    printf("timeline scan vs whole-file analysis: max diff %.3f dB\n", md);
    check(md < 0.3, "scanning on the timeline gives the same ride as analysing the file");

    Sim s(sr);
    double err, stray;
    s.play(a.mono, region, 0, end, nullptr, err, stray);
    printf("pass 1 (scan, whole song): largest gain applied %.3f dB, scanned %.1f s\n", stray, s.sc.scannedSeconds());
    check(stray < 0.01, "first pass only scans, it doesn't ride");

    s.play(a.mono, region, 60, 90, ref1.get(), err, stray);
    printf("pass 2 (play 1:00-1:30): ride error rms %.4f dB\n", err);
    check(err < 0.05, "second pass rides the scanned take");

    s.play(a.mono, region, 150, 160, ref1.get(), err, stray);
    printf("seek to 2:30: ride error rms %.4f dB\n", err);
    check(err < 0.05, "rides correctly after a seek");

    // the take is edited: 95-120 s of the take is 8 dB quieter (timeline 107.37-132.37)
    double frac;
    // riding pauses from the detected edit until 2 s after the changes stop
    s.play(b.mono, region, 100, 140, ref2.get(), err, stray, 107.37, 134.87, &frac);
    printf("pass 3 (edited take, 1:40-2:20): outside the edit riding %.0f%% of the time, %.2f dB from the new ride\n", frac * 100, err);
    check(frac > 0.95 && err < 0.4, "unchanged parts keep riding while the edit is re-scanned");
    {
        bool ok; s.keep.back()->at((int64_t)(120 * sr), ok);
        const float now = s.keep.back()->atTime(120), want = ref2->atTime(120);
        printf("  after pass 3, gain at 2:00 = %+.2f dB (edited take needs %+.2f, old take %+.2f)\n", now, want, ref1->atTime(120));
        check(std::fabs(now - want) < 0.3, "the edit was re-scanned");
    }
    s.play(b.mono, region, 100, 140, ref2.get(), err, stray);
    printf("pass 4 (edited take again): ride error rms %.4f dB\n", err);
    check(err < 0.3, "the edited take is ridden from the next pass");

    // session save / restore
    const std::string st = s.sc.encode();
    Sim r(sr);
    const bool dec = r.sc.decode(st);
    r.worker(true);
    printf("session state: %.0f KB for %.1f s scanned\n", st.size() / 1024.0, s.sc.scannedSeconds());
    check(dec, "state decodes");
    r.play(b.mono, region, 100, 140, ref2.get(), err, stray);
    printf("after reopening: ride error rms %.4f dB\n", err);
    check(err < 0.3, "a reopened session rides straight away");

    printf(fails ? "FAIL\n" : "PASS\n");
    return fails;
}
