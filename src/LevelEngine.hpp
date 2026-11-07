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
#include <cstring>
#include <string>
#include <vector>

namespace levelbrew {

static constexpr double kHopSec = 0.010;   // analysis frame hop
static constexpr double kWinSec = 0.040;   // analysis window

// ----------------------------------------------------------------------------
// WAV reading (PCM 8/16/24/32, float 32/64, WAVE_FORMAT_EXTENSIBLE, RF64, BWF)
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
// Analysis of a whole take (the slow part, done once per file version)
// ----------------------------------------------------------------------------
struct Analysis {
    double sr = 0;
    int hop = 0, win = 0;
    double duration = 0;              // seconds
    std::vector<double> lev;          // weighted level per frame (dB, 40 ms Hann)
    std::vector<float> peak;          // peak |x| per hop (for the waveform display)
    std::vector<float> alev;          // 10 ms rectangular level (dB) for alignment
    std::vector<uint8_t> on;          // singing (incl. sibilants)
    std::vector<uint8_t> levelOn;     // singing used for level estimate
    bool hasTimeRef = false;
    double timeRefSec = 0;
    size_t frames() const { return lev.size(); }
    // time (s) of frame i's centre
    double frameTime(size_t i) const { return (i * (double)hop + win * 0.5) / sr; }
};

static constexpr double kAlignFloorDb = -70.0;

inline void analyse(const Audio& a, Analysis& out)
{
    out = Analysis();
    out.sr = a.sr;
    out.hop = (int)(a.sr * kHopSec);
    out.win = (int)(a.sr * kWinSec);
    out.duration = a.mono.size() / a.sr;
    out.hasTimeRef = a.hasTimeRef;
    out.timeRefSec = a.timeRef / a.sr;
    const size_t N = a.mono.size();
    const int hop = out.hop, win = out.win;

    Biquad hp; hp.highpass(90, M_SQRT1_2, a.sr);
    Biquad s1, s2; s1.highpass(5000, 0.54119610, a.sr); s2.highpass(5000, 1.30656296, a.sr);
    std::vector<float> xw(N), xh(N);
    for (size_t i = 0; i < N; ++i) {
        const double x = a.mono[i];
        xw[i] = (float)hp.run(x);
        xh[i] = (float)s2.run(s1.run(x));
    }
    const size_t n = N > (size_t)win ? (N - win) / hop + 1 : 1;
    std::vector<double> w(win);
    for (int i = 0; i < win; ++i) w[i] = 0.5 - 0.5 * std::cos(2 * M_PI * i / (win - 1));
    out.lev.resize(n);
    std::vector<double> sib(n);
    for (size_t i = 0; i < n; ++i) {
        double e = 0, eh = 0;
        const size_t o = i * hop;
        const int m = (int)std::min<size_t>(win, N - std::min(N, o));
        for (int k = 0; k < m; ++k) {
            const double a1 = xw[o + k] * w[k], a2 = xh[o + k] * w[k];
            e += a1 * a1; eh += a2 * a2;
        }
        e = e / win + 1e-12; eh = eh / win + 1e-12;
        out.lev[i] = 10 * std::log10(e);
        sib[i] = eh / e;
    }
    const size_t nh = N / hop + 1;
    out.peak.assign(nh, 0.f);
    out.alev.assign(nh, (float)kAlignFloorDb);
    for (size_t j = 0; j < nh; ++j) {
        float pk = 0; double e = 0; size_t c = 0;
        for (size_t k = j * hop; k < std::min(N, (j + 1) * hop); ++k, ++c) {
            pk = std::max(pk, std::fabs(a.mono[k]));
            e += (double)xw[k] * xw[k];
        }
        out.peak[j] = pk;
        if (c) out.alev[j] = (float)std::max(kAlignFloorDb, 10 * std::log10(e / c + 1e-12));
    }

    // which frames are singing (not silence / breaths / sibilants)
    const double top = percentile(out.lev, 95), floorEst = percentile(out.lev, 5);
    const double thr = std::max(top - 24.0, floorEst + 12.0);
    std::vector<uint8_t> raw(n);
    for (size_t i = 0; i < n; ++i) raw[i] = out.lev[i] > thr;
    out.on.resize(n);
    for (size_t i = 0; i < n; ++i) {   // 7-tap majority (scipy median_filter, mode reflect)
        int c = 0;
        for (int d = -3; d <= 3; ++d) {
            long j = (long)i + d;
            if (j < 0) j = -j - 1;
            if (j >= (long)n) j = 2 * (long)n - j - 1;
            c += raw[(size_t)std::clamp<long>(j, 0, (long)n - 1)];
        }
        out.on[i] = c >= 4;
    }
    out.levelOn.resize(n);
    for (size_t i = 0; i < n; ++i) out.levelOn[i] = out.on[i] && !(sib[i] > 0.6);
}

// ----------------------------------------------------------------------------
// Gain curve (cheap: re-run whenever Strength / Range / Speed change)
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

    const double sigma = std::max(1.0, rs.speed * 1000.0 / (kHopSec * 1000.0));
    std::vector<double> en(n), msk(n);
    for (size_t i = 0; i < n; ++i) { msk[i] = an.levelOn[i]; en[i] = an.levelOn[i] ? std::pow(10.0, an.lev[i] / 10.0) : 0.0; }
    const std::vector<double> num = gaussSmooth(en, sigma), den = gaussSmooth(msk, sigma);
    std::vector<double> local(n);
    std::vector<size_t> good;
    for (size_t i = 0; i < n; ++i) {
        if (den[i] > 1e-4) { local[i] = 10 * std::log10(std::max(num[i], 1e-12) / std::max(den[i], 1e-4)); good.push_back(i); }
    }
    // fill frames far from any singing with the nearest valid value
    for (size_t gi = 0, i = 0; i < n; ++i) {
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
    g = gaussSmooth(g, std::max(1.0, 120.0 / (kHopSec * 1000.0)));
    for (size_t i = 0; i < n; ++i) out[i] = (float)g[i];
    return out;
}

// gain (dB) at a time (s) in the take; 0 outside it
inline float gainAt(const Analysis& an, const std::vector<float>& g, double t)
{
    if (g.empty() || t < 0 || t > an.duration) return 0.f;
    const double f = (t * an.sr - an.win * 0.5) / an.hop;
    if (f <= 0) return g.front();
    const size_t i = (size_t)f;
    if (i + 1 >= g.size()) return g.back();
    const double fr = f - i;
    return (float)(g[i] + (g[i + 1] - g[i]) * fr);
}

// ----------------------------------------------------------------------------
// Alignment: where does the take sit on the DAW timeline?
// The player records 10 ms input levels during playback; we slide them along the
// take's own 10 ms levels and pick the best match (Pearson correlation).
// ----------------------------------------------------------------------------
struct AlignResult { bool ok = false; double offsetSec = 0; double score = 0; };

// in: input levels (dB) for consecutive 10 ms hops starting at timeline time t0 (s)
inline double alignScoreAt(const std::vector<float>& file, const float* in, int m, long d)
{
    double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0; int c = 0;
    for (int k = 0; k < m; ++k) {
        const long j = d + k;
        const double y = (j >= 0 && j < (long)file.size()) ? file[(size_t)j] : kAlignFloorDb;
        const double x = in[k];
        sx += x; sy += y; sxx += x * x; syy += y * y; sxy += x * y; ++c;
    }
    const double vx = sxx - sx * sx / c, vy = syy - sy * sy / c;
    if (vx < 1e-6 || vy < 1e-6) return 0;
    return (sxy - sx * sy / c) / std::sqrt(vx * vy);
}

inline bool hasSinging(const float* in, int m)
{
    int loud = 0;
    float mx = (float)kAlignFloorDb;
    for (int k = 0; k < m; ++k) mx = std::max(mx, in[k]);
    for (int k = 0; k < m; ++k) loud += in[k] > mx - 20 && in[k] > -60;
    double s = 0, ss = 0;
    for (int k = 0; k < m; ++k) { s += in[k]; ss += in[k] * in[k]; }
    const double sd = std::sqrt(std::max(0.0, ss / m - (s / m) * (s / m)));
    return loud >= 150 && sd > 4.0;   // at least ~1.5 s of singing
}

inline AlignResult alignSearch(const Analysis& an, const float* in, int m, double t0)
{
    AlignResult r;
    if (m < 300 || !hasSinging(in, m)) return r;
    const long nf = (long)an.alev.size();
    std::vector<double> sc((size_t)(nf + m));
    long best = 0; double bs = -2;
    for (long d = -m + 1; d < nf; ++d) {
        const double s = alignScoreAt(an.alev, in, m, d);
        sc[(size_t)(d + m - 1)] = s;
        if (s > bs) { bs = s; best = d; }
    }
    double second = -2;   // best score away from the winning peak
    for (long d = -m + 1; d < nf; ++d)
        if (std::labs(d - best) > 50) second = std::max(second, sc[(size_t)(d + m - 1)]);
    r.score = bs;
    // The plugin hears the very file it analysed, so the true position scores
    // ~1.0; repeated choruses can still reach ~0.95, hence the tight bar.
    r.ok = bs > 0.97 && bs - second > 0.02;
    r.offsetSec = t0 - best * kHopSec;
    return r;
}

// ----------------------------------------------------------------------------
// Real-time player (audio thread side). The worker thread owns Analysis/curves
// and publishes immutable Curve objects through an atomic pointer.
// ----------------------------------------------------------------------------
struct Curve {
    double sr = 0, duration = 0;
    int hop = 0, win = 0;
    std::vector<float> g;
    float at(double t) const
    {
        if (g.empty() || t < 0 || t > duration) return 0.f;
        const double f = (t * sr - win * 0.5) / hop;
        if (f <= 0) return g.front();
        const size_t i = (size_t)f;
        if (i + 1 >= g.size()) return g.back();
        return (float)(g[i] + (g[i + 1] - g[i]) * (f - i));
    }
};

// lock-free ring of 10 ms input levels written by the audio thread
struct LevelRing {
    static constexpr int kSize = 4096;            // ~41 s
    float lev[kSize];
    double t[kSize];                              // timeline time (s) of each hop start
    std::atomic<uint64_t> count{0};
    std::atomic<uint64_t> epoch{0};               // bumps on every seek / discontinuity
    void push(float l, double tt)
    {
        const uint64_t c = count.load(std::memory_order_relaxed);
        lev[c % kSize] = l; t[c % kSize] = tt;
        count.store(c + 1, std::memory_order_release);
    }
    // copy the latest contiguous run (same epoch) of up to maxN hops
    int latest(std::vector<float>& out, double& t0, int maxN, uint64_t sinceCount) const
    {
        const uint64_t c = count.load(std::memory_order_acquire);
        uint64_t from = c > (uint64_t)maxN ? c - maxN : 0;
        from = std::max(from, sinceCount);
        if (c - from > (uint64_t)kSize - 64) from = c - (kSize - 64);
        const int m = (int)(c - from);
        out.resize(m);
        for (int k = 0; k < m; ++k) out[k] = lev[(from + k) % kSize];
        t0 = m ? t[from % kSize] : 0;
        return m;
    }
};

class Player {
public:
    // set by the host wrapper
    std::atomic<const Curve*> curve{nullptr};
    std::atomic<bool> aligned{false};
    std::atomic<double> offsetSec{0};             // timeline time (s) where the take starts
    std::atomic<bool> bypass{false};
    std::atomic<float> outGainDb{0};
    LevelRing ring;

    // meters (read by the wrapper after process)
    float meterGain = 0, meterPos = -1, meterIn = -90, meterOut = -90;

    void init(double sampleRate)
    {
        sr = sampleRate;
        hopN = std::max(1, (int)std::lround(sr * kHopSec));
        smooth = 1.0 - std::exp(-1.0 / (0.004 * sr));
        fadeStep = 1.0 / (0.3 * sr);   // ride fades in/out over 300 ms (alignment, bypass)
        rideMix = 0;
        hp.highpass(90, M_SQRT1_2, sr);
        g = 1; hopAcc = 0; hopE = 0; hopPos = -1; lastEnd = -1;
    }

    // timelineFrame: host transport position of the first sample (only valid if playing)
    void process(const float* const* in, float* const* out, int nch, uint32_t frames, bool playing, int64_t timelineFrame)
    {
        const Curve* c = curve.load(std::memory_order_acquire);
        const bool ride = playing && c && aligned.load(std::memory_order_relaxed) && !bypass.load(std::memory_order_relaxed);
        const double off = offsetSec.load(std::memory_order_relaxed);
        const double trim = std::pow(10.0, outGainDb.load(std::memory_order_relaxed) / 20.0);

        if (!playing || timelineFrame != lastEnd) { hopAcc = 0; hopE = 0; hopPos = -1; ring.epoch.fetch_add(1); }
        lastEnd = playing ? timelineFrame + frames : -1;

        double pin = 0, pout = 0;
        float gdb = 0;
        for (uint32_t i = 0; i < frames; ++i) {
            double target = 1.0;
            rideMix = ride ? std::min(1.0, rideMix + fadeStep) : std::max(0.0, rideMix - fadeStep);
            if (rideMix > 0 && c && playing) {
                const double t = (timelineFrame + (int64_t)i) / sr - off;
                gdb = c->at(t) * (float)rideMix;
                target = std::pow(10.0, gdb / 20.0);
            }
            g += (target - g) * smooth;
            double mono = 0;
            for (int ch = 0; ch < nch; ++ch) {
                const double x = in[ch][i];
                mono += x;
                const double y = x * g * trim;
                out[ch][i] = (float)y;
                pin = std::max(pin, std::fabs(x)); pout = std::max(pout, std::fabs(y));
            }
            if (playing) {   // 10 ms level history for alignment, hops locked to the timeline grid
                const int64_t pos = timelineFrame + i;
                if (hopPos < 0) { if (pos % hopN != 0) continue; hopPos = pos; }
                const double w = hp.run(mono / nch);
                hopE += w * w;
                if (++hopAcc == hopN) {
                    ring.push((float)std::max(kAlignFloorDb, 10 * std::log10(hopE / hopN + 1e-12)), hopPos / sr);
                    hopPos += hopN; hopAcc = 0; hopE = 0;
                }
            }
        }
        meterGain = (float)(20 * std::log10(std::max(g, 1e-6)));
        meterPos = (playing && c) ? (float)(timelineFrame / sr - off) : -1.f;
        meterIn = (float)(20 * std::log10(pin + 1e-9));
        meterOut = (float)(20 * std::log10(pout + 1e-9));
    }

private:
    double sr = 48000, smooth = 0.01, g = 1, hopE = 0, rideMix = 0, fadeStep = 1e-4;
    int hopN = 480, hopAcc = 0;
    int64_t hopPos = -1, lastEnd = -1;
    Biquad hp;
};

} // namespace levelbrew
