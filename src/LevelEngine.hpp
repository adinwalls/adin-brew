// LevelEngine — the LevelBrew vocal rider.
//
// Offline analysis of a whole recorded take (no playback needed), producing a
// smooth gain curve, plus a real-time "player" that applies that curve during
// DAW playback at the matching timeline position and works out by itself where
// the take sits on the timeline.
//
// Framework-agnostic and header-only; used by the DPF plugin, the editor (for
// drawing) and the test harnesses. Built with -ffast-math: no NaN sentinels.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace levelbrew {

static constexpr double kHopSec = 0.010;   // analysis frame hop
static constexpr double kWinSec = 0.040;   // analysis window

// ----------------------------------------------------------------------------
// WAV reading (used by the test tools; the plugin itself never opens files)
// ----------------------------------------------------------------------------
struct Audio {
    double sr = 0;
    int channels = 0;
    std::vector<float> mono;       // channel average
    bool hasTimeRef = false;       // BWF bext time reference (recording position)
    uint64_t timeRef = 0;          // in samples at sr
    std::string error;
};

inline bool readWav(const char* path, Audio& out)
{
    out = Audio();
    FILE* f = std::fopen(path, "rb");
    if (!f) { out.error = "can't open file"; return false; }
    auto rd = [&](void* p, size_t n) { return std::fread(p, 1, n, f) == n; };
    auto u16 = [](const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); };
    auto u32 = [](const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); };
    auto u64 = [&](const uint8_t* p) { return (uint64_t)u32(p) | ((uint64_t)u32(p + 4) << 32); };

    uint8_t hdr[12];
    if (!rd(hdr, 12) || (std::memcmp(hdr, "RIFF", 4) && std::memcmp(hdr, "RF64", 4)) || std::memcmp(hdr + 8, "WAVE", 4)) {
        std::fclose(f); out.error = "not a WAV file"; return false;
    }
    const bool rf64 = !std::memcmp(hdr, "RF64", 4);
    uint64_t ds64Data = 0;
    int fmtTag = 0, ch = 0, bits = 0; uint32_t sr = 0;
    bool haveFmt = false;
    uint8_t ck[8];
    while (rd(ck, 8)) {
        uint64_t size = u32(ck + 4);
        const long here = std::ftell(f);
        if (!std::memcmp(ck, "ds64", 4) && size >= 16) {
            uint8_t b[16]; if (!rd(b, 16)) break;
            ds64Data = u64(b + 8);
        } else if (!std::memcmp(ck, "fmt ", 4) && size >= 16) {
            uint8_t b[40] = {0}; if (!rd(b, std::min<uint64_t>(size, 40))) break;
            fmtTag = (int)u16(b); ch = (int)u16(b + 2); sr = u32(b + 4); bits = (int)u16(b + 14);
            if (fmtTag == 0xFFFE && size >= 26) fmtTag = (int)u16(b + 24);   // extensible: sub-format GUID
            haveFmt = true;
        } else if (!std::memcmp(ck, "bext", 4) && size >= 346) {
            std::vector<uint8_t> b(size); if (!rd(b.data(), size)) break;
            out.timeRef = u64(b.data() + 338);
            out.hasTimeRef = true;
        } else if (!std::memcmp(ck, "data", 4)) {
            if (rf64 && size == 0xFFFFFFFFu) size = ds64Data;
            if (!haveFmt || ch < 1 || sr < 8000) { out.error = "missing or odd format chunk"; break; }
            const bool isFloat = fmtTag == 3;
            if (!((fmtTag == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (isFloat && (bits == 32 || bits == 64)))) {
                out.error = "unsupported WAV encoding"; break;
            }
            const int bps = bits / 8;
            const uint64_t frames = size / (uint64_t)(bps * ch);
            out.sr = sr; out.channels = ch;
            out.mono.resize(frames);
            std::vector<uint8_t> buf((size_t)65536 * bps * ch);
            uint64_t done = 0;
            const double inv = 1.0 / ch;
            while (done < frames) {
                const uint64_t n = std::min<uint64_t>(65536, frames - done);
                const size_t got = std::fread(buf.data(), (size_t)bps * ch, n, f);
                if (got == 0) break;
                const uint8_t* p = buf.data();
                for (size_t i = 0; i < got; ++i) {
                    double acc = 0;
                    for (int c = 0; c < ch; ++c, p += bps) {
                        double v;
                        switch (bits) {
                        case 8:  v = ((int)p[0] - 128) / 128.0; break;
                        case 16: v = (int16_t)u16(p) / 32768.0; break;
                        case 24: v = ((int32_t)(((uint32_t)p[0] << 8) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 24)) >> 8) / 8388608.0; break;
                        case 32: if (isFloat) { float x; std::memcpy(&x, p, 4); v = x; } else v = (int32_t)u32(p) / 2147483648.0; break;
                        default: { double x; std::memcpy(&x, p, 8); v = x; } break;
                        }
                        acc += v;
                    }
                    out.mono[done + i] = (float)(acc * inv);
                }
                done += got;
            }
            out.mono.resize(done);
            // keep scanning for a bext chunk after the data (rare, but legal)
            if (std::fseek(f, here + (long)size + (long)(size & 1), SEEK_SET) != 0) break;
            continue;
        }
        if (std::fseek(f, here + (long)size + (long)(size & 1), SEEK_SET) != 0) break;
    }
    std::fclose(f);
    if (out.mono.empty() && out.error.empty()) out.error = "no audio in file";
    return out.error.empty();
}

// ----------------------------------------------------------------------------
// DSP helpers
// ----------------------------------------------------------------------------
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void highpass(double fc, double q, double sr)   // bilinear, prewarped (= scipy butter sections)
    {
        const double w = 2 * M_PI * fc / sr, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al;
        b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
        z1 = z2 = 0;
    }
    inline double run(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

// Gaussian smoothing, scipy gaussian_filter1d(mode="nearest", truncate=4)
inline std::vector<double> gaussSmooth(const std::vector<double>& x, double sigma)
{
    const int n = (int)x.size();
    const int r = (int)(4.0 * sigma + 0.5);
    std::vector<double> k(2 * r + 1);
    double s = 0;
    for (int i = -r; i <= r; ++i) { k[i + r] = std::exp(-0.5 * i * i / (sigma * sigma)); s += k[i + r]; }
    for (double& v : k) v /= s;
    std::vector<double> y(n);
    for (int i = 0; i < n; ++i) {
        double acc = 0;
        const int lo = i - r, hi = i + r;
        if (lo >= 0 && hi < n) { for (int j = 0; j <= 2 * r; ++j) acc += k[j] * x[lo + j]; }
        else for (int j = 0; j <= 2 * r; ++j) acc += k[j] * x[std::clamp(lo + j, 0, n - 1)];
        y[i] = acc;
    }
    return y;
}

// numpy-style percentile (linear interpolation)
inline double percentile(std::vector<double> v, double p)
{
    if (v.empty()) return 0;
    const double pos = p / 100.0 * (v.size() - 1);
    const size_t i = (size_t)pos;
    std::nth_element(v.begin(), v.begin() + i, v.end());
    const double a = v[i];
    if (i + 1 >= v.size()) return a;
    const double b = *std::min_element(v.begin() + i + 1, v.end());
    return a + (b - a) * (pos - i);
}

// ----------------------------------------------------------------------------
// Scanned take, laid out on the timeline. Index i = 10 ms hop i of the timeline
// (timeline sample i*hop). Frame i is the 40 ms window over hops i..i+3.
// ----------------------------------------------------------------------------
static constexpr double kFloorDb = -70.0;    // 10 ms level floor
static constexpr double kSilentDb = -100.0;  // below this: no audio at all (empty timeline)

struct Analysis {
    double sr = 0;
    int hop = 0;
    int64_t origin = 0;                  // timeline hop index of element 0
    std::vector<double> lev;             // frame level (dB); -120 where not scanned
    std::vector<float> peak;             // hop peak |x| (display)
    std::vector<uint8_t> valid;          // frame scanned
    std::vector<uint8_t> sib;            // frame is sibilant
    std::vector<uint8_t> on;             // singing (incl. sibilants)
    std::vector<uint8_t> levelOn;        // singing used for level estimate
    std::vector<uint8_t> fresh;          // scanned during the playback in progress (not ridden yet)
    size_t frames() const { return lev.size(); }
    double frameTime(size_t i) const { return ((double)(origin + (int64_t)i) * hop + 2.0 * hop) / sr; }
    double startTime() const { return (double)origin * hop / sr; }
    double endTime() const { return ((double)(origin + (int64_t)lev.size()) + 3.0) * hop / sr; }
};

// singing detection over the scanned frames
inline void detectSinging(Analysis& an)
{
    const size_t n = an.frames();
    std::vector<double> sc;
    sc.reserve(n);
    for (size_t i = 0; i < n; ++i) if (an.valid[i] && an.lev[i] > kSilentDb) sc.push_back(an.lev[i]);
    an.on.assign(n, 0); an.levelOn.assign(n, 0);
    if (sc.size() < 10) return;
    const double top = percentile(sc, 95), floorEst = percentile(sc, 5);
    const double thr = std::max(top - 24.0, floorEst + 12.0);
    std::vector<uint8_t> raw(n);
    for (size_t i = 0; i < n; ++i) raw[i] = an.valid[i] && an.lev[i] > thr;
    for (size_t i = 0; i < n; ++i) {   // 7-tap majority (scipy median_filter, mode reflect)
        int c = 0;
        for (int d = -3; d <= 3; ++d) {
            long j = (long)i + d;
            if (j < 0) j = -j - 1;
            if (j >= (long)n) j = 2 * (long)n - j - 1;
            c += raw[(size_t)std::clamp<long>(j, 0, (long)n - 1)];
        }
        an.on[i] = c >= 4 && an.valid[i];
        an.levelOn[i] = an.on[i] && !an.sib[i];
    }
}

// ----------------------------------------------------------------------------
// Gain curve (cheap: re-run whenever the scan or Strength / Range / Speed change)
// ----------------------------------------------------------------------------
struct RideSettings {
    double strength = 0.85;   // 0..1
    double range = 9.0;       // max boost/cut dB
    double speed = 0.4;       // smoothing seconds (bigger = slower, gentler)
};

inline std::vector<float> gainCurve(const Analysis& an, const RideSettings& rs)
{
    const size_t n = an.frames();
    std::vector<float> out(n, 0.f);
    std::vector<size_t> act;
    for (size_t i = 0; i < n; ++i) if (an.levelOn[i]) act.push_back(i);
    if (act.size() < 5) return out;

    const double sigma = std::max(1.0, rs.speed / kHopSec);
    std::vector<double> en(n), msk(n);
    for (size_t i = 0; i < n; ++i) { msk[i] = an.levelOn[i]; en[i] = an.levelOn[i] ? std::pow(10.0, an.lev[i] / 10.0) : 0.0; }
    const std::vector<double> num = gaussSmooth(en, sigma), den = gaussSmooth(msk, sigma);
    std::vector<double> local(n);
    std::vector<size_t> good;
    for (size_t i = 0; i < n; ++i) {
        if (den[i] > 1e-4) { local[i] = 10 * std::log10(std::max(num[i], 1e-12) / std::max(den[i], 1e-4)); good.push_back(i); }
    }
    for (size_t gi = 0, i = 0; i < n; ++i) {   // far from any singing: nearest valid value
        if (den[i] > 1e-4) continue;
        while (gi + 1 < good.size() && good[gi + 1] < i) ++gi;
        if (i < good.front()) local[i] = local[good.front()];
        else if (i > good.back()) local[i] = local[good.back()];
        else { const size_t a = good[gi], b = good[gi + 1]; local[i] = local[a] + (local[b] - local[a]) * double(i - a) / double(b - a); }
    }
    std::vector<double> la; la.reserve(act.size());
    for (size_t i : act) la.push_back(local[i]);
    const double target = percentile(la, 50);

    std::vector<double> g(n);
    for (size_t i = 0; i < n; ++i) g[i] = std::clamp((target - local[i]) * rs.strength, -rs.range, rs.range);
    // gaps / breaths: hold the neighbouring singing's gain, but never boost much
    std::vector<double> dist(n);
    {
        size_t ai = 0;
        for (size_t i = 0; i < n; ++i) {
            while (ai + 1 < act.size() && act[ai + 1] <= i) ++ai;
            double gi;
            if (i <= act.front()) gi = g[act.front()];
            else if (i >= act.back()) gi = g[act.back()];
            else { const size_t a = act[ai], b = act[ai + 1]; gi = (a == b) ? g[a] : g[a] + (g[b] - g[a]) * double(i - a) / double(b - a); }
            const size_t dl = i >= act[ai] ? i - act[ai] : act[ai] - i;
            const size_t dr = (ai + 1 < act.size()) ? (act[ai + 1] > i ? act[ai + 1] - i : i - act[ai + 1]) : dl;
            dist[i] = (double)std::min(dl, dr);
            if (!an.levelOn[i]) g[i] = std::min(gi, 1.5);
        }
    }
    // long silences (> ~1 s from singing): relax back to 0 dB
    for (size_t i = 0; i < n; ++i) g[i] *= std::clamp(1.0 - (dist[i] - 50.0) / 100.0, 0.0, 1.0);
    g = gaussSmooth(g, std::max(1.0, 0.120 / kHopSec));
    for (size_t i = 0; i < n; ++i) out[i] = (float)g[i];
    return out;
}

// What the player and the editor use: the analysis plus its ride curve.
struct Curve {
    Analysis an;
    std::vector<float> g;          // gain dB per frame
    std::vector<uint8_t> ride;     // frame may be ridden (scanned, not fresh)
    // gain (dB) at a timeline sample; ok = this spot may be ridden
    float at(int64_t pos, bool& ok) const
    {
        ok = false;
        if (g.size() < 2) return 0.f;
        const double f = (double)(pos - 2 * (int64_t)an.hop) / an.hop - (double)an.origin;
        if (f < 0 || f >= (double)(g.size() - 1)) return 0.f;
        const size_t i = (size_t)f;
        ok = ride[i] && ride[i + 1];
        return (float)(g[i] + (g[i + 1] - g[i]) * (f - (double)i));
    }
    float atTime(double t) const { bool ok; return at((int64_t)std::llround(t * an.sr), ok); }
};

inline std::shared_ptr<Curve> buildCurve(Analysis an, const RideSettings& rs)
{
    auto c = std::make_shared<Curve>();
    detectSinging(an);
    c->g = gainCurve(an, rs);
    c->ride.resize(an.frames());
    for (size_t i = 0; i < an.frames(); ++i) c->ride[i] = an.valid[i] && !an.fresh[i];
    c->an = std::move(an);
    return c;
}

// ----------------------------------------------------------------------------
// TrackScanner: turns the audio heard during playback into scan data.
// Runs on the worker thread (never the audio thread).
// ----------------------------------------------------------------------------
class TrackScanner {
public:
    static constexpr int kChunk = 1024;
    enum : uint8_t { kHasHop = 1, kHasFrame = 2, kIsSib = 4 };

    void init(double sampleRate)
    {
        if (sampleRate == sr) return;
        sr = sampleRate;
        hop = std::max(1, (int)(sr * kHopSec + 0.5));
        win = 4 * hop;
        hann.resize(win);
        for (int i = 0; i < win; ++i) hann[i] = 0.5 - 0.5 * std::cos(2 * M_PI * i / (win - 1));
        bw.assign(win, 0.f); bh.assign(win, 0.f); cw.assign(hop, 0.f); ch.assign(hop, 0.f);
        clear();
    }
    int hopSize() const { return hop; }
    double sampleRate() const { return sr; }

    void clear() { store.clear(); endRun(); dirty = true; }

    // contiguous timeline audio heard during playback (mono)
    void feed(int64_t pos, const float* x, size_t n)
    {
        for (size_t i = 0; i < n; ++i) {
            const int64_t p = pos + (int64_t)i;
            if (!runActive || p != expect) startRun();
            expect = p + 1;
            if (p < 0) continue;
            if (curHop < 0) {
                if (p % hop != 0) continue;
                curHop = p / hop; fill = 0;
            }
            const double v = x[i];
            const float xw = (float)hp.run(v), xh = (float)s2.run(s1.run(v));
            cw[fill] = xw; ch[fill] = xh;
            e10 += (double)xw * xw;
            pk = std::max(pk, (float)std::fabs(v));
            if (++fill == hop) completeHop();
        }
    }

    // transport stopped or jumped: what was scanned in this pass becomes rideable
    void endRun()
    {
        if (runActive && freshInRun) dirty = true;
        runActive = false; runId = 0; curHop = -1; freshInRun = 0;
        holdFrom = holdTo = -1; urgentFlag = false;
    }

    bool running() const { return runActive; }
    bool scanningFresh() const { return runActive && freshInRun > 0; }
    // something changed that needs a new curve; newHops batches live scanning
    bool needsRebuild() const { return dirty || newHops >= 50; }
    // a change in the take was just detected: rebuild now so the player stops riding it
    bool urgent() const { return urgentFlag; }

    Analysis analysis()
    {
        dirty = false; newHops = 0; urgentFlag = false;
        Analysis an;
        an.sr = sr; an.hop = hop;
        if (store.empty()) return an;
        int64_t lo = INT64_MAX, hi = INT64_MIN;
        for (auto& kv : store) {
            const Chunk& c = kv.second;
            for (int k = 0; k < kChunk; ++k) if (c.flags[k] & kHasFrame) {
                const int64_t idx = kv.first * kChunk + k;
                lo = std::min(lo, idx); hi = std::max(hi, idx);
            }
        }
        if (lo > hi) return an;
        hi = std::min(hi, lo + (int64_t)(4 * 3600 / kHopSec));   // sanity cap: 4 hours
        const size_t n = (size_t)(hi - lo + 1);
        an.origin = lo;
        an.lev.assign(n, -120.0); an.peak.assign(n, 0.f);
        an.valid.assign(n, 0); an.sib.assign(n, 0); an.fresh.assign(n, 0);
        for (auto& kv : store) {
            const Chunk& c = kv.second;
            for (int k = 0; k < kChunk; ++k) {
                const int64_t idx = kv.first * kChunk + k - lo;
                if (idx < 0 || idx >= (int64_t)n) continue;
                if (c.flags[k] & kHasHop) an.peak[(size_t)idx] = c.peak[k];
                if (c.flags[k] & kHasFrame) {
                    an.valid[(size_t)idx] = 1;
                    an.lev[(size_t)idx] = c.lev[k];
                    an.sib[(size_t)idx] = (c.flags[k] & kIsSib) != 0;
                    const int64_t ti = kv.first * kChunk + k;
                    an.fresh[(size_t)idx] = runActive && (c.pass[k] == runId || (ti >= holdFrom && ti <= holdTo));
                }
            }
        }
        return an;
    }

    double scannedSeconds() const
    {
        size_t c = 0;
        for (auto& kv : store) for (int k = 0; k < kChunk; ++k) c += (kv.second.flags[k] & kHasFrame) != 0;
        return c * kHopSec;
    }

    // ---- session state (compact, base64) ----
    std::string encode() const
    {
        std::string bin;
        auto put = [&](const void* p, size_t n) { bin.append((const char*)p, n); };
        int64_t runStart = -1; std::string run; uint32_t count = 0;
        auto flush = [&] {
            if (count) { put(&runStart, 8); put(&count, 4); bin += run; }
            run.clear(); count = 0; runStart = -1;
        };
        int64_t prev = INT64_MIN;
        for (auto& kv : store) {
            const Chunk& c = kv.second;
            for (int k = 0; k < kChunk; ++k) {
                if (!(c.flags[k] & (kHasHop | kHasFrame))) continue;
                const int64_t idx = kv.first * kChunk + k;
                if (idx != prev + 1) flush();
                if (runStart < 0) runStart = idx;
                prev = idx;
                const uint8_t fl = c.flags[k] & (kHasHop | kHasFrame | kIsSib);
                const int16_t lv = (int16_t)std::clamp(std::lround(c.lev[k] * 100.0), -32000L, 32000L);
                const int16_t al = (int16_t)std::clamp(std::lround(c.alev[k] * 100.0), -32000L, 32000L);
                const uint8_t pq = (uint8_t)std::clamp(std::lround(std::sqrt(std::max(0.f, c.peak[k])) * 255.0), 0L, 255L);
                run.push_back((char)fl); run.append((const char*)&lv, 2); run.append((const char*)&al, 2); run.push_back((char)pq);
                ++count;
            }
        }
        flush();
        char hdr[64];
        std::snprintf(hdr, sizeof hdr, "LB2 %d %d ", (int)std::lround(sr), hop);
        return std::string(hdr) + base64(bin);
    }

    bool decode(const std::string& s)
    {
        // header "LB2 <rate> <hop> " (parsed by hand: sscanf would need a newer glibc)
        if (s.compare(0, 4, "LB2 ") != 0) return false;
        const char* c0 = s.c_str() + 4; char* e = nullptr;
        const long srIn = std::strtol(c0, &e, 10);
        if (e == c0) return false;
        const char* c1 = e; const long hopIn = std::strtol(c1, &e, 10);
        if (e == c1 || hopIn != hop || std::labs(srIn - std::lround(sr)) > 1) return false;
        const std::string bin = unbase64(std::string(e));
        std::map<int64_t, Chunk> st;
        size_t p = 0;
        while (p + 12 <= bin.size()) {
            int64_t start; uint32_t count;
            std::memcpy(&start, bin.data() + p, 8); std::memcpy(&count, bin.data() + p + 8, 4); p += 12;
            if (p + (size_t)count * 6 > bin.size() || start < 0) return false;
            for (uint32_t j = 0; j < count; ++j, p += 6) {
                const int64_t idx = start + j;
                Chunk& c = st[idx / kChunk];
                const int k = (int)(idx % kChunk);
                int16_t lv, al; std::memcpy(&lv, bin.data() + p + 1, 2); std::memcpy(&al, bin.data() + p + 3, 2);
                const float pq = (uint8_t)bin[p + 5] / 255.f;
                c.flags[k] = (uint8_t)bin[p];
                c.lev[k] = lv / 100.f;
                c.alev[k] = al / 100.f;
                c.peak[k] = pq * pq;
                c.pass[k] = 0;
            }
        }
        store.swap(st);
        endRun();
        dirty = true;
        return true;
    }

private:
    struct Chunk {
        float lev[kChunk], alev[kChunk], peak[kChunk];
        uint8_t flags[kChunk];
        uint32_t pass[kChunk];
        Chunk()
        {
            std::fill(lev, lev + kChunk, -120.f); std::fill(alev, alev + kChunk, (float)kFloorDb); std::fill(peak, peak + kChunk, 0.f);
            std::memset(flags, 0, sizeof flags); std::memset(pass, 0, sizeof pass);
        }
    };
    Chunk& chunkAt(int64_t idx, int& k) { k = (int)(idx % kChunk); return store[idx / kChunk]; }

    void startRun()
    {
        endRun();
        runActive = true; runId = nextRunId++; if (nextRunId == 0) nextRunId = 1;
        curHop = -1; fill = 0; runHops = 0; e10 = 0; pk = 0;
        hp.highpass(90, M_SQRT1_2, sr);
        s1.highpass(5000, 0.54119610, sr); s2.highpass(5000, 1.30656296, sr);
        std::fill(bw.begin(), bw.end(), 0.f); std::fill(bh.begin(), bh.end(), 0.f);
        chgHist.assign(30, 0); chgCount = 0; chgPos = 0;
    }

    void completeHop()
    {
        const int64_t h = curHop;
        // slide the 4-hop window
        std::memmove(bw.data(), bw.data() + hop, sizeof(float) * (win - hop));
        std::memmove(bh.data(), bh.data() + hop, sizeof(float) * (win - hop));
        std::memcpy(bw.data() + win - hop, cw.data(), sizeof(float) * hop);
        std::memcpy(bh.data() + win - hop, ch.data(), sizeof(float) * hop);
        ++runHops;
        const float alev = (float)std::max(kFloorDb, 10 * std::log10(e10 / hop + 1e-12));
        const bool warm = runHops > 10;   // filters settled after ~100 ms

        int k; Chunk& c = chunkAt(h, k);
        const bool had = c.flags[k] & kHasHop;
        bool changed = false;
        if (!had || warm) {
            if (had) changed = std::fabs(c.alev[k] - alev) > 3.f && std::max(c.alev[k], alev) > -50.f;
            c.alev[k] = alev; c.peak[k] = pk; c.flags[k] |= kHasHop;
            if (!had) { c.pass[k] = runId; ++freshInRun; ++newHops; }
        }
        // sustained difference from the stored scan = the take was edited / replaced here
        chgCount += (int)changed - chgHist[(size_t)chgPos];
        chgHist[(size_t)chgPos] = changed; chgPos = (chgPos + 1) % 30;
        if (chgCount >= 20) {
            for (int64_t j = h - 29; j <= h; ++j) {
                int kj; Chunk& cj = chunkAt(std::max<int64_t>(0, j), kj);
                if (cj.pass[kj] != runId) { cj.pass[kj] = runId; ++freshInRun; }
            }
            // the rest of the edit lies just ahead and still holds the old scan:
            // don't ride from here until 2 s after the changes stop
            if (holdTo < h) { holdFrom = h - 29; urgentFlag = true; }
            holdTo = h + 200;
            dirty = true;
        }

        if (runHops >= 4 && h >= 3) {   // the 40 ms frame over hops h-3..h
            const int64_t f = h - 3;
            int kf; Chunk& cf = chunkAt(f, kf);
            const bool hadF = cf.flags[kf] & kHasFrame;
            if (!hadF || runHops > 13) {
                double e = 0, eh = 0;
                for (int j = 0; j < win; ++j) {
                    const double a1 = bw[(size_t)j] * hann[(size_t)j], a2 = bh[(size_t)j] * hann[(size_t)j];
                    e += a1 * a1; eh += a2 * a2;
                }
                e = e / win + 1e-12; eh = eh / win + 1e-12;
                cf.lev[kf] = (float)(10 * std::log10(e));
                cf.flags[kf] = (uint8_t)((cf.flags[kf] & ~kIsSib) | kHasFrame | (eh / e > 0.6 ? kIsSib : 0));
            }
        }
        ++curHop; fill = 0; e10 = 0; pk = 0;
    }

    static std::string base64(const std::string& in)
    {
        static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string o; o.reserve((in.size() + 2) / 3 * 4);
        size_t i = 0;
        for (; i + 2 < in.size(); i += 3) {
            const uint32_t v = ((uint8_t)in[i] << 16) | ((uint8_t)in[i + 1] << 8) | (uint8_t)in[i + 2];
            o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += t[v & 63];
        }
        if (i < in.size()) {
            uint32_t v = (uint8_t)in[i] << 16;
            if (i + 1 < in.size()) v |= (uint8_t)in[i + 1] << 8;
            o += t[v >> 18]; o += t[(v >> 12) & 63];
            o += i + 1 < in.size() ? t[(v >> 6) & 63] : '=';
            o += '=';
        }
        return o;
    }
    static std::string unbase64(const std::string& in)
    {
        auto val = [](char c) -> int {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+') return 62;
            if (c == '/') return 63;
            return -1;
        };
        std::string o; uint32_t acc = 0; int bits = 0;
        for (char c : in) {
            const int v = val(c); if (v < 0) continue;
            acc = (acc << 6) | (uint32_t)v; bits += 6;
            if (bits >= 8) { bits -= 8; o += (char)((acc >> bits) & 0xFF); }
        }
        return o;
    }

    std::map<int64_t, Chunk> store;
    double sr = 0;
    int hop = 441, win = 1764;
    std::vector<double> hann;
    std::vector<float> bw, bh, cw, ch;
    Biquad hp, s1, s2;
    bool runActive = false, dirty = false;
    uint32_t runId = 0, nextRunId = 1;
    int64_t expect = 0, curHop = -1, holdFrom = -1, holdTo = -1;
    bool urgentFlag = false;
    int fill = 0, runHops = 0, chgCount = 0, chgPos = 0;
    std::vector<uint8_t> chgHist = std::vector<uint8_t>(30, 0);
    double e10 = 0;
    float pk = 0;
    size_t freshInRun = 0, newHops = 0;
};

// whole file as if played once from timeline 0 (tests / offline tools)
inline Analysis analyseAudio(const Audio& a, int64_t timelineOffset = 0)
{
    TrackScanner s; s.init(a.sr);
    s.feed(timelineOffset, a.mono.data(), a.mono.size());
    s.endRun();
    return s.analysis();
}

// ----------------------------------------------------------------------------
// Audio thread -> worker: lock-free single-producer/single-consumer sample FIFO
// ----------------------------------------------------------------------------
class SampleFifo {
public:
    static constexpr uint64_t kN = 1u << 21, kRec = 8192;   // ~44 s of mono audio at 48 kHz
    SampleFifo() : buf(kN), rec(kRec) {}

    bool push(int64_t pos, const float* x, uint32_t n)   // audio thread
    {
        const uint64_t ws = wSamp.load(std::memory_order_relaxed), wr = wRec.load(std::memory_order_relaxed);
        if (wr - rRec.load(std::memory_order_acquire) >= kRec || ws + n - rSamp.load(std::memory_order_acquire) > kN) {
            overflow.store(true, std::memory_order_relaxed);
            return false;
        }
        for (uint32_t i = 0; i < n; ++i) buf[(ws + i) % kN] = x[i];
        rec[wr % kRec] = { pos, n, ws };
        wSamp.store(ws + n, std::memory_order_release);
        wRec.store(wr + 1, std::memory_order_release);
        return true;
    }

    // worker: f(pos, samples, n) for each pushed block, in order
    template <class F> size_t drain(F&& f)
    {
        const uint64_t wr = wRec.load(std::memory_order_acquire);
        uint64_t rr = rRec.load(std::memory_order_relaxed);
        size_t total = 0;
        for (; rr < wr; ++rr) {
            const Rec r = rec[rr % kRec];
            const uint64_t a = r.start % kN;
            if (a + r.n <= kN) f(r.pos, &buf[a], (size_t)r.n);
            else {
                const size_t first = (size_t)(kN - a);
                f(r.pos, &buf[a], first);
                f(r.pos + (int64_t)first, &buf[0], (size_t)r.n - first);
            }
            total += r.n;
            rSamp.store(r.start + r.n, std::memory_order_release);
            rRec.store(rr + 1, std::memory_order_release);
        }
        return total;
    }
    bool takeOverflow() { return overflow.exchange(false); }

private:
    struct Rec { int64_t pos; uint32_t n; uint64_t start; };
    std::vector<float> buf;
    std::vector<Rec> rec;
    std::atomic<uint64_t> wSamp{0}, rSamp{0}, wRec{0}, rRec{0};
    std::atomic<bool> overflow{false};
};

// ----------------------------------------------------------------------------
// Real-time player (audio thread). The worker publishes immutable Curves.
// ----------------------------------------------------------------------------
class Player {
public:
    std::atomic<const Curve*> curve{nullptr};
    std::atomic<bool> bypass{false};
    std::atomic<float> outGainDb{0};
    SampleFifo fifo;

    // meters (read by the wrapper after process)
    float meterGain = 0, meterPos = -1, meterIn = -90, meterOut = -90;
    bool meterRiding = false;

    void init(double sampleRate)
    {
        sr = sampleRate;
        smooth = 1.0 - std::exp(-1.0 / (0.004 * sr));
        fadeStep = 1.0 / (0.3 * sr);   // ride fades in/out over 300 ms
        g = 1; rideMix = 0;
    }

    // pos: host timeline position of the first sample (only meaningful when playing)
    void process(const float* const* in, float* const* out, int nch, uint32_t frames, bool playing, int64_t pos)
    {
        const Curve* c = curve.load(std::memory_order_acquire);
        const bool byp = bypass.load(std::memory_order_relaxed);
        const double trim = std::pow(10.0, outGainDb.load(std::memory_order_relaxed) / 20.0);
        double pin = 0, pout = 0;
        bool anyRide = false;
        for (uint32_t i = 0; i < frames; ++i) {
            bool ok = false;
            float gd = 0;
            if (playing && c && !byp) gd = c->at(pos + (int64_t)i, ok);
            rideMix = ok ? std::min(1.0, rideMix + fadeStep) : std::max(0.0, rideMix - fadeStep);
            const double target = rideMix > 0 ? std::pow(10.0, gd * rideMix / 20.0) : 1.0;
            g += (target - g) * smooth;
            anyRide |= ok;
            double m = 0;
            for (int ch = 0; ch < nch; ++ch) {
                const double x = in[ch][i];
                m += x;
                const double y = x * g * trim;
                out[ch][i] = (float)y;
                pin = std::max(pin, std::fabs(x)); pout = std::max(pout, std::fabs(y));
            }
            mono[monoN++] = (float)(m / nch);
            if (monoN == kMono) flushMono(playing, pos + (int64_t)i + 1);
        }
        flushMono(playing, pos + (int64_t)frames);
        meterGain = (float)(20 * std::log10(std::max(g, 1e-6)));
        meterPos = playing ? (float)(pos / sr) : -1.f;
        meterIn = (float)(20 * std::log10(pin + 1e-9));
        meterOut = (float)(20 * std::log10(pout + 1e-9));
        meterRiding = anyRide;
    }

private:
    static constexpr int kMono = 4096;
    void flushMono(bool playing, int64_t endPos)
    {
        if (monoN && playing) fifo.push(endPos - monoN, mono, (uint32_t)monoN);
        monoN = 0;
    }
    float mono[kMono];
    int monoN = 0;
    double sr = 48000, smooth = 0.01, g = 1, rideMix = 0, fadeStep = 1e-4;
};

} // namespace levelbrew
