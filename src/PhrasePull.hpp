// Phrase Pull — data shared by the TuneBrew engine and editor:
//   * RegionSet: phrases on the song timeline that get pulled harder into key
//   * PitchScan: the vocal's pitch along the timeline, learned during playback,
//     so the editor can show the take as a picture to draw phrases on
// Header-only. Both are saved with the session as short text.
#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>

namespace tunebrew {

struct PullRegion {
    double start = 0.0, end = 0.0;   // seconds on the song timeline
    float  pull = 1.f;               // 0..1: 0 = normal settings, 1 = snap fully
    int    shift = 0;                // move the target note by this many scale steps
};

struct RegionSet {
    static constexpr int kMax = 64;
    int n = 0;
    PullRegion r[kMax];

    int find(double sec) const
    {
        for (int i = 0; i < n; ++i) if (sec >= r[i].start && sec < r[i].end) return i;
        return -1;
    }

    void sort()
    {
        std::sort(r, r + n, [](const PullRegion& a, const PullRegion& b) { return a.start < b.start; });
    }

    // "start_ms,end_ms,pull_percent,shift;..."
    std::string encode() const
    {
        std::string s;
        char buf[96];
        for (int i = 0; i < n; ++i) {
            std::snprintf(buf, sizeof buf, "%ld,%ld,%d,%d;", std::lround(r[i].start * 1000.0),
                          std::lround(r[i].end * 1000.0), (int)std::lround(r[i].pull * 100.f), r[i].shift);
            s += buf;
        }
        return s;
    }

    void decode(const char* c)
    {
        n = 0;
        if (!c) return;
        while (*c && n < kMax) {
            long v[4]; int k = 0;
            for (; k < 4; ++k) {
                char* e = nullptr;
                v[k] = std::strtol(c, &e, 10);
                if (e == c) break;
                c = e;
                if (*c == ',') ++c;
            }
            if (k < 4) break;
            if (*c == ';') ++c;
            if (v[1] <= v[0]) continue;
            r[n].start = v[0] * 0.001; r[n].end = v[1] * 0.001;
            r[n].pull = std::clamp((float)v[2] * 0.01f, 0.f, 1.f);
            r[n].shift = std::clamp((int)v[3], -4, 4);
            ++n;
        }
        sort();
    }
};

struct PitchScan {
    static constexpr double kHop = 0.01;      // seconds per entry
    static constexpr int kMax = 180000;       // 30 minutes
    // pitch in hundredths of a semitone (MIDI note x 100); 0 = no singing
    std::vector<uint16_t> in, out;
    std::atomic<int> len{0};
    std::atomic<uint32_t> version{0};

    PitchScan() : in(kMax, 0), out(kMax, 0) {}

    static uint16_t pack(double midi)
    {
        return midi <= 0.0 ? 0 : (uint16_t)std::clamp(std::lround(midi * 100.0), 1L, 65535L);
    }
    static float unpack(uint16_t v) { return (float)v * 0.01f; }

    // audio thread
    void write(double sec, double midiIn, double midiOut)
    {
        const long i = (long)(sec / kHop);
        if (i < 0 || i >= kMax) return;
        in[i] = pack(midiIn); out[i] = pack(midiOut);
        if (i >= len.load(std::memory_order_relaxed)) len.store((int)i + 1, std::memory_order_relaxed);
        version.fetch_add(1, std::memory_order_relaxed);
    }

    void clear()
    {
        std::fill(in.begin(), in.end(), 0); std::fill(out.begin(), out.end(), 0);
        len.store(0); version.fetch_add(1);
    }

    // "TB1 <entries> <base64 of 16-bit little-endian input pitch>"
    std::string encode() const
    {
        const int n = len.load();
        std::string bin((size_t)n * 2, '\0');
        for (int i = 0; i < n; ++i) { bin[2 * i] = (char)(in[i] & 0xFF); bin[2 * i + 1] = (char)(in[i] >> 8); }
        char hdr[32]; std::snprintf(hdr, sizeof hdr, "TB1 %d ", n);
        return std::string(hdr) + base64(bin);
    }

    bool decode(const char* c)
    {
        if (!c || std::string(c).compare(0, 4, "TB1 ") != 0) return false;
        char* e = nullptr;
        const long n = std::strtol(c + 4, &e, 10);
        if (e == c + 4 || n < 0 || n > kMax) return false;
        while (*e == ' ') ++e;
        const std::string bin = unbase64(e);
        if ((long)bin.size() < n * 2) return false;
        clear();
        for (long i = 0; i < n; ++i)
            in[i] = (uint16_t)((unsigned char)bin[2 * i] | ((unsigned char)bin[2 * i + 1] << 8));
        len.store((int)n); version.fetch_add(1);
        return true;
    }

private:
    static std::string base64(const std::string& s)
    {
        static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string o; o.reserve((s.size() + 2) / 3 * 4);
        size_t i = 0;
        for (; i + 2 < s.size(); i += 3) {
            const unsigned v = ((unsigned char)s[i] << 16) | ((unsigned char)s[i + 1] << 8) | (unsigned char)s[i + 2];
            o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += t[v & 63];
        }
        if (i + 1 == s.size()) {
            const unsigned v = (unsigned char)s[i] << 16;
            o += t[v >> 18]; o += t[(v >> 12) & 63]; o += "==";
        } else if (i + 2 == s.size()) {
            const unsigned v = ((unsigned char)s[i] << 16) | ((unsigned char)s[i + 1] << 8);
            o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += '=';
        }
        return o;
    }

    static std::string unbase64(const char* c)
    {
        std::string o; unsigned acc = 0; int bits = 0;
        for (; *c; ++c) {
            int v;
            const char ch = *c;
            if (ch >= 'A' && ch <= 'Z') v = ch - 'A';
            else if (ch >= 'a' && ch <= 'z') v = ch - 'a' + 26;
            else if (ch >= '0' && ch <= '9') v = ch - '0' + 52;
            else if (ch == '+') v = 62;
            else if (ch == '/') v = 63;
            else continue;
            acc = (acc << 6) | (unsigned)v; bits += 6;
            if (bits >= 8) { bits -= 8; o += (char)((acc >> bits) & 0xFF); }
        }
        return o;
    }
};

} // namespace tunebrew
