// TuneEngine — real-time vocal pitch correction
//   * pitch detection: YIN on a low-passed, decimated mono sum
//   * note targeting: key/scale quantiser with hysteresis
//   * retune: one-pole smoothing of the correction offset (keeps vibrato at slow speeds)
//   * shifting: pitch-synchronous overlap-add (TD-PSOLA) — preserves formants
// Header-only, no allocations in process(), no dependencies.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#include "KeyDetect.hpp"

namespace tunebrew {

enum Scale {
    kChromatic = 0, kMajor, kMinor, kHarmonicMinor, kMelodicMinor,
    kMajorPent, kMinorPent, kBlues, kDorian, kMixolydian, kScaleCount
};

static const char* const kScaleNames[kScaleCount] = {
    "Chromatic", "Major", "Minor", "Harmonic Minor", "Melodic Minor",
    "Major Pentatonic", "Minor Pentatonic", "Blues", "Dorian", "Mixolydian"
};

static const char* const kKeyNames[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

class TuneEngine {
public:
    static constexpr int kMaxCh = 2;
    static constexpr double kMinHz = 70.0;    // lowest trackable pitch (sets latency)
    static constexpr double kMaxHz = 1100.0;

    // ---- parameters (set any time) ----
    int    key = 0;            // 0=C .. 11=B
    int    scale = kChromatic;
    double retuneMs = 20.0;    // 0 = hard tune
    double amount = 1.0;       // 0..1
    double transpose = 0.0;    // semitones
    double refHz = 440.0;
    double mix = 1.0;          // 0..1 wet
    double gain = 1.0;         // linear output gain
    uint16_t noteMask = 0xFFF; // optional per-note enable (relative to C), ANDed with scale

    // natural-mode controls
    int    mode = 1;            // 0 = Classic (plain hard correction), 1 = Natural
    double vibrato = 1.0;       // 0..2: scale applied to the singer's own vibrato (1 = keep)
    double flex = 0.2;          // 0..1: only correct notes sung near the target
    double humanize = 0.3;      // 0..1: relax correction on sustained notes
    double transitionMs = 40.0; // protect note onsets/slides for this long
    bool   learn = false;       // key detection: accumulate while true

    // ---- meters (read from UI thread; approximate) ----
    volatile float meterHz = 0.f;       // detected pitch, 0 if unvoiced
    volatile float meterTarget = -1.f;  // target MIDI note, -1 if none
    volatile float meterCents = 0.f;    // applied shift in cents
    volatile float meterVib = 0.f;      // detected vibrato depth (cents, peak-to-peak), 0 if none
    volatile float meterKey = -1.f;     // detected key 0..11 major, 12..23 minor, -1 none
    volatile float meterKeyConf = 0.f;  // 0..1
    volatile float meterKeyAlt = -1.f;  // runner-up key with a different note set
    volatile float meterTuneOff = 0.f;  // tuning offset vs A440 (cents)
    volatile float meterLearnSecs = 0.f;

    void init(double sampleRate, int channels)
    {
        sr = sampleRate;
        nch = std::max(1, std::min(kMaxCh, channels));
        tMax = sr / kMinHz;
        tMin = sr / kMaxHz;
        dMax = 0.6 * tMax;
        latency = (int)std::ceil(3.4 * tMax) + 16;

        int need = 4 * (latency + (int)tMax) + 64;
        ringSize = 1; while (ringSize < need) ringSize <<= 1;
        ringMask = ringSize - 1;
        for (int c = 0; c < kMaxCh; ++c) { xin[c].assign(ringSize, 0.f); yout[c].assign(ringSize, 0.f); }
        wsum.assign(ringSize, 0.f);
        pk.assign(ringSize, 0.f);
        pkA = (float)std::exp(-2.0 * M_PI * 1500.0 / sr);

        // detection path: decimate to ~24 kHz (or less)
        dec = std::max(1, (int)std::floor(sr / 22050.0 + 0.0001));
        dsr = sr / dec;
        dTmax = (int)std::ceil(dsr / kMinHz);
        dTmin = std::max(2, (int)std::floor(dsr / kMaxHz));
        dWin = dTmax;                       // integration window
        dHop = std::max(32, (int)std::round(dsr * 0.005)); // ~5 ms
        dRingSize = 1; while (dRingSize < 4 * (dWin + dTmax)) dRingSize <<= 1;
        dRing.assign(dRingSize, 0.f);
        diff.assign(dTmax + 2, 0.f);
        cmnd.assign(dTmax + 2, 0.f);
        // 2x one-pole low-pass ~ 1 kHz (keeps fundamental, kills formant octave traps)
        lpA = (float)std::exp(-2.0 * M_PI * 1000.0 / sr);
        // RMS follower ~ 30 ms
        rmsA = (float)std::exp(-1.0 / (0.03 * sr));
        unvT = std::max(32.0, std::round(0.004 * sr));
        reset();
    }

    void reset()
    {
        for (int c = 0; c < kMaxCh; ++c) {
            std::fill(xin[c].begin(), xin[c].end(), 0.f);
            std::fill(yout[c].begin(), yout[c].end(), 0.f);
        }
        std::fill(wsum.begin(), wsum.end(), 0.f);
        std::fill(pk.begin(), pk.end(), 0.f);
        pkLp = 0.f; pol = 1.f;
        std::fill(dRing.begin(), dRing.end(), 0.f);
        t = 0; dt = 0; decCount = 0; decAcc = 0.f; hopCount = 0;
        lp1 = lp2 = 0.f; ms = 0.f;
        sNext = 0.0; dOff = 0.0; aCur = 0.0; aNext = 0.0; lastVoicedS = -1e9; lastVoiced = false;
        histCount = 0; histHead = 0;
        corrSmooth = 0.0; wasVoicedDet = false; curTarget = -1000;
        p1 = p2 = 0.0;
        meterHz = 0.f; meterTarget = -1.f; meterCents = 0.f; meterVib = 0.f;
        nowSec = 0.0; extN = 0; runDir = 0; vibGate = 0.0; noteTime = 0.0; speedSm = 0.0;
        lastTarget = -1000; stableTime = 0.0; lastStable440 = 0.0;
        learnPrev = false; keyEvalCount = 0;
        keys.reset();
    }

    int getLatency() const { return latency; }

    void process(const float* const* in, float* const* out, uint32_t frames)
    {
        for (uint32_t i = 0; i < frames; ++i) {
            const int w = (int)(t & ringMask);
            float mono = 0.f;
            for (int c = 0; c < nch; ++c) { xin[c][w] = in[c][i]; mono += in[c][i]; }
            mono /= (float)nch;
            pkLp = pkA * pkLp + (1.f - pkA) * mono;
            pk[w] = pkLp;
            ++t;
            feedDetector(mono);

            // place every grain whose source material is now fully available
            while (sNext + 2.35 * tMax + 4.0 <= (double)t)
                placeGrain();

            // emit
            const int64_t e = (int64_t)t - 1 - latency;
            if (e >= 0) {
                const int ei = (int)(e & ringMask);
                const float ws = wsum[ei];
                const float inv = 1.f / std::max(ws, 0.5f);
                for (int c = 0; c < nch; ++c) {
                    const float wet = yout[c][ei] * inv;
                    const float dry = xin[c][ei];
                    out[c][i] = (float)(gain * (mix * wet + (1.0 - mix) * dry));
                    yout[c][ei] = 0.f;
                }
                wsum[ei] = 0.f;
            } else {
                for (int c = 0; c < nch; ++c) out[c][i] = 0.f;
            }
        }
    }

private:
    // ---------------- detection ----------------
    void feedDetector(float x)
    {
        ms = rmsA * ms + (1.f - rmsA) * x * x;
        lp1 = lpA * lp1 + (1.f - lpA) * x;
        lp2 = lpA * lp2 + (1.f - lpA) * lp1;
        decAcc += lp2;
        if (++decCount < dec) return;
        dRing[dt & (dRingSize - 1)] = decAcc / (float)dec;
        decAcc = 0.f; decCount = 0; ++dt;
        if (++hopCount >= dHop && dt >= (int64_t)(dWin + dTmax)) {
            hopCount = 0;
            analyse();
        }
    }

    void analyse()
    {
        const int N = dWin + dTmax;
        const int64_t base = dt - N;
        const int m = dRingSize - 1;
        auto X = [&](int j) { return dRing[(base + j) & m]; };

        // YIN difference function
        for (int tau = 1; tau <= dTmax; ++tau) {
            float s = 0.f;
            for (int j = 0; j < dWin; ++j) { const float d = X(j) - X(j + tau); s += d * d; }
            diff[tau] = s;
        }
        cmnd[0] = 1.f;
        double run = 0.0;
        for (int tau = 1; tau <= dTmax; ++tau) {
            run += diff[tau];
            cmnd[tau] = run > 0.0 ? (float)(diff[tau] * tau / run) : 1.f;
        }
        const float thresh = 0.18f;
        int best = -1;
        for (int tau = dTmin; tau < dTmax; ++tau) {
            if (cmnd[tau] < thresh) {
                while (tau + 1 < dTmax && cmnd[tau + 1] < cmnd[tau]) ++tau;
                best = tau; break;
            }
        }
        const float levelDb = 10.f * std::log10(ms + 1e-12f);
        bool voiced = best > 0 && levelDb > -55.f;

        double period = 0.0;
        if (voiced) {
            // parabolic interpolation
            double tauF = best;
            if (best > 1 && best < dTmax) {
                const double a = cmnd[best - 1], b = cmnd[best], c = cmnd[best + 1];
                const double den = a - 2 * b + c;
                if (std::fabs(den) > 1e-12) tauF = best + 0.5 * (a - c) / den;
            }
            period = tauF * dec;
            // median-of-3 against octave blips
            if (wasVoicedDet && p1 > 0 && p2 > 0) {
                double a = period, b = p1, c = p2;
                double med = std::max(std::min(a, b), std::min(std::max(a, b), c));
                p2 = p1; p1 = period; period = med;
            } else { p2 = p1 = period; }
        } else { p1 = p2 = 0.0; }

        double ratio = 1.0;
        const double hopSec = (double)dHop / dsr;
        nowSec += hopSec;
        if (learn && !learnPrev) keys.reset();
        learnPrev = learn;

        if (voiced) {
            const double f = sr / period;
            const double midi = 69.0 + 12.0 * std::log2(f / refHz);
            const bool natural = mode == 1;

            // ---- vibrato separation: track pitch extrema; a regular 3-9 Hz swing is vibrato ----
            if (!wasVoicedDet) {
                ps = midi; centerSm = midi; extN = 0; runDir = 0; runExt = midi; runExtT = nowSec;
                vibGate = 0.0; noteTime = 0.0; speedSm = 0.0; lastCenterEff = midi; lastTarget = -1000;
            } else {
                ps += (1.0 - std::exp(-hopSec * 2.0 * M_PI * 15.0)) * (midi - ps);
            }
            trackExtrema(ps);
            double vc = 0.0, depth = 0.0;
            const bool vibOn = natural && vibratoActive(vc, depth);
            const double aC = 1.0 - std::exp(-hopSec / (vibOn ? 0.06 : 0.012));
            centerSm += aC * ((vibOn ? vc : ps) - centerSm);
            vibGate += (1.0 - std::exp(-hopSec / 0.04)) * ((vibOn ? 1.0 : 0.0) - vibGate);
            const double vib = natural ? vibGate * (midi - centerSm) : 0.0;
            const double centerEff = midi - vib;     // the note the singer is aiming at
            meterVib = vibOn ? (float)(depth * 100.0) : 0.f;

            // ---- target and note timing ----
            const int target = pickTarget(centerEff);
            const double spd = std::fabs(centerEff - lastCenterEff) / hopSec;   // semitones / s
            lastCenterEff = centerEff;
            speedSm += (1.0 - std::exp(-hopSec / 0.02)) * (spd - speedSm);
            if (target != lastTarget) { noteTime = 0.0; lastTarget = target; }
            else noteTime += hopSec;
            const double trSec = transitionMs * 0.001;
            if (natural && trSec > 0.0 && speedSm > 8.0) noteTime = 0.0;   // sliding: treat as transition

            // ---- weights ----
            double w = 1.0;
            if (natural && target > -1000) {
                if (flex > 0.0) {
                    const double dFull = 0.5 - 0.35 * flex;                     // semitones
                    w *= 1.0 - smoothstep(dFull, dFull + 0.15, std::fabs(centerEff - target));
                }
                if (trSec > 0.0) w *= smoothstep(0.0, trSec, noteTime);
            }
            const double corr = (target > -1000) ? (target - centerEff) * amount * w : 0.0;

            // ---- retune speed (humanize slows it on sustained notes) ----
            double rt = retuneMs;
            if (natural) {
                rt += humanize * 400.0 * smoothstep(0.12, 0.5, noteTime);
                if (trSec > 0.0) rt = std::max(rt, 0.25 * transitionMs);
            }
            if (!wasVoicedDet) corrSmooth = (rt <= 0.5) ? corr : 0.0;
            const double alpha = rt <= 0.5 ? 1.0 : 1.0 - std::exp(-hopSec / (rt * 0.001));
            corrSmooth += alpha * (corr - corrSmooth);

            const double vibShift = natural ? (vibrato - 1.0) * vib : 0.0;
            const double shift = corrSmooth + vibShift + transpose;
            ratio = std::pow(2.0, shift / 12.0);
            meterHz = (float)f;
            meterTarget = (float)target;
            meterCents = (float)(shift * 100.0);

            // ---- key learning: count held, steady notes ----
            if (learn) {
                const double m440 = centerEff + 12.0 * std::log2(refHz / 440.0);
                if (speedSm < 3.0) {
                    const double lw = std::min(1.0, std::max(0.0, (10.f * std::log10(ms + 1e-12f) + 50.0) / 20.0));
                    keys.add(m440, hopSec * lw);
                    stableTime += hopSec; lastStable440 = m440;
                } else stableTime = 0.0;
            }
        } else {
            if (learn && wasVoicedDet && stableTime > 0.15) keys.add(lastStable440, 0.3);   // phrase ending
            stableTime = 0.0;
            meterHz = 0.f; meterTarget = -1.f; meterCents = 0.f; meterVib = 0.f;
            curTarget = -1000;
        }
        wasVoicedDet = voiced;

        if (learn && ++keyEvalCount >= 20) {
            keyEvalCount = 0;
            int k, a; float c, o;
            keys.evaluate(k, c, a, o);
            meterKey = (float)k; meterKeyConf = c; meterKeyAlt = (float)a; meterTuneOff = o;
            meterLearnSecs = (float)keys.seconds();
        }

        Hist& h = hist[histHead];
        // effective centre of the YIN comparison region, minus low-pass group delay
        h.center = (double)t - (double)N * dec + 0.5 * dWin * dec + 0.5 * (voiced ? period : dTmax * dec)
                   - 2.0 / (2.0 * M_PI * 1000.0) * sr - 0.5 * dec;
        h.period = period;
        h.voiced = voiced;
        h.ratio = ratio;
        histHead = (histHead + 1) & (kHist - 1);
        if (histCount < kHist) ++histCount;
    }

    static double smoothstep(double a, double b, double x)
    {
        const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    }

    void trackExtrema(double x)
    {
        const double hyst = 0.06;   // 6 cents
        if (runDir >= 0) {
            if (x > runExt) { runExt = x; runExtT = nowSec; }
            else if (x < runExt - hyst) { pushExt(runExtT, runExt); runDir = -1; runExt = x; runExtT = nowSec; }
        } else {
            if (x < runExt) { runExt = x; runExtT = nowSec; }
            else if (x > runExt + hyst) { pushExt(runExtT, runExt); runDir = 1; runExt = x; runExtT = nowSec; }
        }
    }

    void pushExt(double tt, double v)
    {
        for (int i = 3; i > 0; --i) { extT[i] = extT[i - 1]; extV[i] = extV[i - 1]; }
        extT[0] = tt; extV[0] = v;
        if (extN < 4) ++extN;
    }

    // vibrato = the last three turning points alternate regularly (3-9 Hz) with similar size
    bool vibratoActive(double& center, double& depth) const
    {
        if (extN < 3) return false;
        const double dt1 = extT[0] - extT[1], dt2 = extT[1] - extT[2];
        const double a1 = std::fabs(extV[0] - extV[1]), a2 = std::fabs(extV[1] - extV[2]);
        if (dt1 < 0.045 || dt1 > 0.21 || dt2 < 0.045 || dt2 > 0.21) return false;
        if (a1 < 0.08 || a2 < 0.08 || a1 > 2.5 || a2 > 2.5) return false;
        if (std::max(a1, a2) > 3.0 * std::min(a1, a2)) return false;
        if (nowSec - extT[0] > 0.25) return false;
        center = 0.25 * (extV[2] + 2.0 * extV[1] + extV[0]);
        depth = 0.5 * (a1 + a2);
        if (std::fabs(ps - center) > 1.5 * std::max(a1, a2)) return false;   // left the note
        return true;
    }

    bool allowed(int midiNote) const
    {
        const int pc = ((midiNote % 12) + 12) % 12;
        const int rel = ((pc - key) % 12 + 12) % 12;
        static const uint16_t masks[kScaleCount] = {
            0xFFF,
            (1<<0)|(1<<2)|(1<<4)|(1<<5)|(1<<7)|(1<<9)|(1<<11),   // major
            (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<8)|(1<<10),   // natural minor
            (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<8)|(1<<11),   // harmonic minor
            (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<9)|(1<<11),   // melodic minor
            (1<<0)|(1<<2)|(1<<4)|(1<<7)|(1<<9),                  // major pent
            (1<<0)|(1<<3)|(1<<5)|(1<<7)|(1<<10),                 // minor pent
            (1<<0)|(1<<3)|(1<<5)|(1<<6)|(1<<7)|(1<<10),          // blues
            (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<9)|(1<<10),   // dorian
            (1<<0)|(1<<2)|(1<<4)|(1<<5)|(1<<7)|(1<<9)|(1<<10),   // mixolydian
        };
        const int sc = (scale >= 0 && scale < kScaleCount) ? scale : 0;
        return ((masks[sc] >> rel) & 1) && ((noteMask >> pc) & 1);
    }

    int pickTarget(double midi)
    {
        // hysteresis: stick with the current note unless we're clearly nearer another
        if (curTarget > -1000 && allowed(curTarget) && std::fabs(midi - curTarget) < 0.62)
            return curTarget;
        const int c = (int)std::lround(midi);
        int best = -1000; double bd = 1e9;
        for (int k = -6; k <= 6; ++k) {
            const int n = c + k;
            if (!allowed(n)) continue;
            const double d = std::fabs(midi - n);
            if (d < bd) { bd = d; best = n; }
        }
        curTarget = best;
        return best;
    }

    // ---------------- synthesis (TD-PSOLA) ----------------
    struct Hist { double center, period, ratio; bool voiced; };
    static constexpr int kHist = 256;
    Hist hist[kHist];
    int histCount = 0, histHead = 0;

    // interpolate pitch/ratio between the two analyses bracketing `time`
    bool lookup(double time, double& period, double& ratio) const
    {
        if (histCount == 0) return false;
        const Hist* newer = nullptr;
        for (int k = 1; k <= histCount; ++k) {
            const Hist& h = hist[(histHead - k + kHist) & (kHist - 1)];
            if (h.center <= time) {
                if (!h.voiced) return false;
                if (newer && newer->voiced) {
                    const double fr = (time - h.center) / std::max(1.0, newer->center - h.center);
                    period = h.period + fr * (newer->period - h.period);
                    ratio = h.ratio + fr * (newer->ratio - h.ratio);
                } else { period = h.period; ratio = h.ratio; }
                return true;
            }
            newer = &h;
        }
        if (newer && newer->voiced) { period = newer->period; ratio = newer->ratio; return true; }
        return false;
    }

    inline float readIn(int c, double pos) const
    {
        const double fl = std::floor(pos);
        const int64_t i = (int64_t)fl;
        const float f = (float)(pos - fl);
        const std::vector<float>& x = xin[c];
        const float y1 = x[i & ringMask];
        if (f == 0.f) return y1;
        const float y0 = x[(i - 1) & ringMask], y2 = x[(i + 1) & ringMask], y3 = x[(i + 2) & ringMask];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    // Find the next pitch mark one period after `a` by waveform similarity:
    // the candidate whose surrounding cycle best matches the cycle around `a`.
    // Marks found this way are spaced by the true local period, so with no
    // pitch change the grains line up exactly and the output equals the input.
    double nextMark(double a, double T) const
    {
        const int L = std::max(8, (int)T);
        const int half = L / 2;
        const int ai = (int)std::lround(a);
        const int lo = (int)std::floor(a + 0.85 * T), hi = (int)std::ceil(a + 1.15 * T);
        double ea = 0.0;
        for (int k = -half; k < half; ++k) { const double v = pk[(ai + k) & ringMask]; ea += v * v; }
        if (ea < 1e-12) return a + T;
        double best = -2.0; int bi = (int)std::lround(a + T);
        double sc[3] = { 0, 0, 0 }; double prev = -2.0, prev2 = -2.0;
        bool haveParab = false;
        for (int c = lo; c <= hi; ++c) {
            double xy = 0.0, ec = 0.0;
            for (int k = -half; k < half; ++k) {
                const double u = pk[(ai + k) & ringMask], v = pk[(c + k) & ringMask];
                xy += u * v; ec += v * v;
            }
            const double r = xy / std::sqrt(ea * ec + 1e-20);
            if (r > best) { best = r; bi = c; sc[0] = prev; sc[1] = r; haveParab = false; }
            else if (c == bi + 1) { sc[2] = r; haveParab = true; }
            prev2 = prev; prev = r;
        }
        (void)prev2;
        if (best < 0.3 || bi <= lo || bi >= hi) return a + T;   // no convincing match: keep the period
        double off = 0.0;
        if (haveParab) {
            const double den = sc[0] - 2 * sc[1] + sc[2];
            if (std::fabs(den) > 1e-12) off = std::clamp(0.5 * (sc[0] - sc[2]) / den, -0.5, 0.5);
        }
        // keep the sub-sample position relative to a's own fraction
        return (double)bi + off + (a - ai);
    }

    void placeGrain()
    {
        const double s = sNext;
        double P0 = 0.0, R = 1.0;
        const double u0 = s + dOff;                  // input time this output grain maps to
        const bool voiced = lookup(u0, P0, R) && P0 >= tMin && P0 <= tMax;
        double half, a, hop;

        if (voiced) {
            const double r = std::min(4.0, std::max(0.25, R));
            if (!lastVoiced) {
                if (r > 0.8 || (s - lastVoicedS < 0.03 * sr && aCur > 0.0)) {
                    // normal correction range, or a short dropout inside a note:
                    // keep the current offset so the output stays a seamless copy
                    aCur = u0;
                } else {
                    // note onset: anchor on the strongest pulse near u0 so grains are pulse-centred
                    double best = 0.0; int bi = (int)u0;
                    for (int j = (int)(u0 - 0.5 * P0); j <= (int)(u0 + 0.5 * P0); ++j) {
                        const float v = std::fabs(pk[j & ringMask]);
                        if (v > best) { best = v; bi = j; }
                    }
                    aCur = bi;
                    dOff = std::round(std::clamp(aCur - s, -dMax, dMax));
                    aCur = s + dOff;
                }
                aNext = nextMark(aCur, P0);
            }
            const double u = s + dOff;
            int guard = 0;
            while (aNext <= u + 1e-6 && guard++ < 64) { aCur = aNext; aNext = nextMark(aCur, P0); }
            a = (u - aCur <= aNext - u) ? aCur : aNext;
            const double P = std::clamp(aNext - aCur, tMin, tMax);
            half = P;
            // no shift: follow the measured cycles (bit-exact pass-through);
            // otherwise space output pulses at exactly the target period
            hop = (std::fabs(R - 1.0) < 3e-4) ? P : P0 / r;
            lastVoicedS = s;
        } else {
            // unvoiced: plain overlap-add at the current offset (an exact copy)
            half = unvT; hop = unvT;
            a = s + dOff;
        }
        lastVoiced = voiced;

        if (s - half >= 0.0) {
            const int j0 = (int)std::ceil(s - half), j1 = (int)std::floor(s + half);
            const double inv = 1.0 / half;
            for (int j = j0; j <= j1; ++j) {
                const double w = 0.5 * (1.0 + std::cos(M_PI * (j - s) * inv));
                if (w <= 0.0) continue;
                const float wgt = (float)w;
                const int ji = j & ringMask;
                const double src = a + (j - s);
                for (int c = 0; c < nch; ++c) yout[c][ji] += wgt * readIn(c, src);
                wsum[ji] += wgt;
            }
        }
        sNext = s + hop;
    }

    // ---------------- state ----------------
    double sr = 48000.0, dsr = 24000.0, tMax = 0, tMin = 0, unvT = 192;
    int nch = 2, latency = 0;
    int ringSize = 0, ringMask = 0;
    std::vector<float> xin[kMaxCh], yout[kMaxCh], wsum, pk;
    float pkLp = 0.f, pkA = 0.f, pol = 1.f;
    int64_t t = 0;
    double sNext = 0.0, dOff = 0.0, dMax = 0.0, aCur = 0.0, aNext = 0.0, lastVoicedS = -1e9;
    bool lastVoiced = false;

    int dec = 2, dTmax = 0, dTmin = 0, dWin = 0, dHop = 0, dRingSize = 0;
    std::vector<float> dRing, diff, cmnd;
    int64_t dt = 0;
    int decCount = 0, hopCount = 0;
    float decAcc = 0.f, lpA = 0.f, lp1 = 0.f, lp2 = 0.f, rmsA = 0.f, ms = 0.f;

    double corrSmooth = 0.0, p1 = 0.0, p2 = 0.0;
    // natural mode state
    double nowSec = 0.0, ps = 0.0, centerSm = 0.0, vibGate = 0.0, noteTime = 0.0, speedSm = 0.0, lastCenterEff = 0.0;
    double runExt = 0.0, runExtT = 0.0, extT[4] = { 0 }, extV[4] = { 0 };
    int runDir = 0, extN = 0, lastTarget = -1000;
    // key detection state
    KeyDetector keys;
    bool learnPrev = false;
    int keyEvalCount = 0;
    double stableTime = 0.0, lastStable440 = 0.0;
    bool wasVoicedDet = false;
    int curTarget = -1000;
};

} // namespace tunebrew
