// phrase_engine vocal48k.f32
// Phrase Pull engine test on a real vocal: a marked phrase must land on the
// scale, the rest of the song must be left as it was, the note shift must move
// the target, and the session text must survive a save / reload. PASS / FAIL.
#include "../src/TuneEngine.hpp"
#include <cstdio>
#include <cstdlib>
using namespace tunebrew;

static std::vector<float> x;
static const double sr = 48000;
static const int kKey = 0, kScale = kMajor;

static bool inScale(int n) { static const int maj[7] = {0,2,4,5,7,9,11}; int pc = ((n - kKey) % 12 + 12) % 12; for (int k : maj) if (k == pc) return true; return false; }
static double offScale(double midi) { double b = 1e9; for (int n = (int)midi - 3; n <= (int)midi + 3; ++n) if (inScale(n)) b = std::min(b, std::fabs(midi - n)); return b * 100.0; }

struct Run { std::vector<float> in, out; };
static Run run(const RegionSet& rs, double secs)
{
    TuneEngine* e = new TuneEngine(); e->init(sr, 1);
    e->key = kKey; e->scale = kScale;              // default Natural settings otherwise
    e->setRegions(rs);
    const size_t n = std::min(x.size(), (size_t)(secs * sr));
    std::vector<float> o(256);
    for (size_t i = 0; i + 256 <= n; i += 256) {
        e->setTimeline(true, true, (double)i);
        const float* in[1] = { &x[i] }; float* out[1] = { o.data() };
        e->process(in, out, 256);
    }
    Run r; const int len = e->scan.len.load();
    for (int i = 0; i < len; ++i) { r.in.push_back(PitchScan::unpack(e->scan.in[i])); r.out.push_back(PitchScan::unpack(e->scan.out[i])); }
    delete e; return r;
}
static double med(std::vector<double> v) { if (v.empty()) return -1; std::sort(v.begin(), v.end()); return v[v.size() / 2]; }

int main(int argc, char** argv)
{
    FILE* f = fopen(argv[1], "rb"); float v; while (fread(&v, 4, 1, f) == 1) x.push_back(v); fclose(f);
    const double secs = 60.0, a = 20.0, b = 40.0;
    bool ok = true;
    auto check = [&](bool c, const char* what) { printf("  %-58s %s\n", what, c ? "ok" : "FAIL"); ok = ok && c; };

    RegionSet none, one, up;
    one.n = 1; one.r[0].start = a; one.r[0].end = b; one.r[0].pull = 1.f;
    up = one; up.r[0].shift = 1;
    const Run r0 = run(none, secs), r1 = run(one, secs), r2 = run(up, secs);

    std::vector<double> in0, off0, off1, outside, stepUp; int voiced = 0;
    for (size_t i = 0; i < r0.in.size() && i < r1.in.size(); ++i) {
        const double t = i * PitchScan::kHop;
        if (r0.in[i] <= 0 || r1.out[i] <= 0 || r0.out[i] <= 0) continue;
        if (t > a + 0.1 && t < b - 0.1) { ++voiced; in0.push_back(offScale(r0.in[i])); off0.push_back(offScale(r0.out[i])); off1.push_back(offScale(r1.out[i])); if (i < r2.out.size() && r2.out[i] > 0) stepUp.push_back(r2.out[i] - r1.out[i]); }
        else if (t < a - 0.1 || t > b + 1.0) outside.push_back(std::fabs(r1.out[i] - r0.out[i]) * 100.0);
    }
    std::sort(off1.begin(), off1.end()); std::sort(off0.begin(), off0.end());
    const double p90a = off0[off0.size() * 9 / 10], p90b = off1[off1.size() * 9 / 10];
    printf("phrase %.0f-%.0f s, %d voiced points\n", a, b, voiced);
    printf("  cents off the scale   sung: median %.1f   normal settings: median %.1f, 90%% within %.1f   pulled 100%%: median %.1f, 90%% within %.1f\n",
           med(in0), med(off0), p90a, med(off1), p90b);
    check(voiced > 300, "the phrase contains singing");
    check(p90b < 0.5 * p90a && med(off1) <= med(off0), "pulled phrase sits closer to the scale");
    check(med(outside) < 0.5, "song outside the phrase is unchanged");
    printf("  note shift +1: median move %.2f semitones\n", med(stepUp));
    check(med(stepUp) >= 0.9 && med(stepUp) <= 2.1, "note shift moves the target one scale step up");

    // session text round trips
    RegionSet many; many.n = 3;
    many.r[0] = { 12.345, 15.5, 0.75f, -1 }; many.r[1] = { 1.0, 2.0, 1.f, 0 }; many.r[2] = { 100.0, 130.25, 0.2f, 2 };
    RegionSet back; back.decode(many.encode().c_str());
    check(back.n == 3 && std::fabs(back.r[0].start - 1.0) < 1e-6 && std::fabs(back.r[1].pull - 0.75f) < 1e-6 && back.r[1].shift == -1 && back.r[2].shift == 2, "phrases survive save / reload (sorted)");
    TuneEngine* e = new TuneEngine(); e->init(sr, 1);
    for (int i = 0; i < 3000; ++i) e->scan.write(i * 0.01, i % 7 ? 60.0 + i * 0.001 : 0.0, 61.0);
    const std::string txt = e->scan.encode();
    TuneEngine* e2 = new TuneEngine(); e2->init(sr, 1);
    bool same = e2->scan.decode(txt.c_str()) && e2->scan.len.load() == 3000;
    for (int i = 0; same && i < 3000; ++i) same = e2->scan.in[i] == e->scan.in[i];
    check(same, "pitch scan survives save / reload");
    printf("  scan text for 30 s: %zu bytes\n", txt.size());
    printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
