// KeyDetect — key and tuning detection from a sung melody.
// Accumulates how long the voice dwells on each pitch (10-cent bins over one octave),
// estimates the global tuning offset, folds into 12 pitch classes, and correlates
// against Krumhansl-Kessler key profiles for all 24 major/minor keys.
#pragma once
#include <cmath>
#include <algorithm>

namespace tunebrew {

class KeyDetector {
public:
    static constexpr int kBins = 120;   // 10-cent bins over an octave

    void reset()
    {
        for (int i = 0; i < kBins; ++i) hist[i] = 0.0;
        total = 0.0;
    }

    // midi440: pitch in MIDI units relative to A4 = 440 Hz; weight: seconds (or seconds-equivalent)
    void add(double midi440, double weight)
    {
        double c = std::fmod(midi440 * 100.0, 1200.0);
        if (c < 0) c += 1200.0;
        const int b = std::min(kBins - 1, (int)(c / 10.0));
        hist[b] += weight;
        total += weight;
    }

    double seconds() const { return total; }

    // key: 0..11 major, 12..23 minor (tonic = key % 12); -1 if not enough data
    // alt: best key with a *different* note set; conf: 0..1; offset: cents (-50..50) vs A440
    void evaluate(int& key, float& conf, int& alt, float& offsetCents) const
    {
        key = -1; alt = -1; conf = 0.f; offsetCents = 0.f;
        if (total < 1.0) return;

        // tuning offset: circular mean of the position within the semitone
        double sx = 0, sy = 0;
        for (int b = 0; b < kBins; ++b) {
            if (hist[b] <= 0) continue;
            const double ph = 2.0 * M_PI * std::fmod(b * 10.0 + 5.0, 100.0) / 100.0;
            sx += hist[b] * std::cos(ph); sy += hist[b] * std::sin(ph);
        }
        double off = std::atan2(sy, sx) / (2.0 * M_PI) * 100.0;   // -50..50
        offsetCents = (float)off;

        double pc[12] = { 0 };
        for (int b = 0; b < kBins; ++b) {
            if (hist[b] <= 0) continue;
            const double cents = b * 10.0 + 5.0 - off;
            int p = (int)std::lround(cents / 100.0) % 12;
            if (p < 0) p += 12;
            pc[p] += hist[b];
        }

        double score[24];
        for (int k = 0; k < 24; ++k) {
            const double* prof = k < 12 ? kMajor : kMinor;
            const int tonic = k % 12;
            double x[12], y[12];
            // sqrt compression: which notes occur matters more than how long the tonic is held
            for (int i = 0; i < 12; ++i) { x[i] = std::sqrt(pc[(tonic + i) % 12]); y[i] = prof[i]; }
            score[k] = pearson(x, y);
        }
        int best = 0;
        for (int k = 1; k < 24; ++k) if (score[k] > score[best]) best = k;
        const int rel = best < 12 ? 12 + (best + 9) % 12 : (best % 12 + 3) % 12;   // relative major/minor
        int second = -1;
        for (int k = 0; k < 24; ++k) {
            if (k == best || k == rel) continue;
            if (second < 0 || score[k] > score[second]) second = k;
        }
        key = best; alt = second;
        const double margin = score[best] - score[second];
        const double dataConf = std::min(1.0, total / 12.0);           // ~12 s of held notes for full confidence
        conf = (float)(std::clamp(margin / 0.18, 0.0, 1.0) * dataConf);
    }

private:
    static double pearson(const double* x, const double* y)
    {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += x[i]; my += y[i]; }
        mx /= 12; my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i) {
            const double a = x[i] - mx, b = y[i] - my;
            sxy += a * b; sxx += a * a; syy += b * b;
        }
        return sxy / std::sqrt(sxx * syy + 1e-20);
    }

    // Krumhansl-Kessler key profiles (probe-tone ratings; tonic weighted highest)
    static constexpr double kMajor[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static constexpr double kMinor[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

    double hist[kBins] = { 0 };
    double total = 0.0;
};

} // namespace tunebrew
