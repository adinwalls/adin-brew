// TuneView — the TuneBrew editor, drawn with Cairo in a 500-series rack style:
// wooden cheeks, navy module faceplates, coloured knob caps, glowing LEDs and
// illuminated push buttons. Framework-agnostic: the DPF UI (or a test renderer)
// feeds it parameter values, meters and mouse events in logical coordinates
// (kW x kH), and receives edits via Host.
#pragma once
#include <cairo/cairo.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>
#include "TuneParams.hpp"

namespace tunebrew {

class TuneView {
public:
    static constexpr double kCheek = 26;                       // wooden side panels
    static constexpr double kFaceW = 860, kFaceH = 540;
    static constexpr double kW = kFaceW + 2 * kCheek, kH = kFaceH;

    struct Host {
        virtual void setParam(int index, float value) = 0;
        virtual void beginEdit(int index) = 0;
        virtual void endEdit(int index) = 0;
        virtual ~Host() {}
    };

    explicit TuneView(Host* h) : host(h)
    {
        for (int i = 0; i < pCount; ++i) values[i] = kSpecs[i].def;
        hist.resize(kHistSize);
    }

    // ---------------- inputs ----------------
    void setValue(int i, float v) { if (i >= 0 && i < pCount) values[i] = v; }
    float getValue(int i) const { return values[i]; }

    // call at UI refresh rate with a monotonically increasing time in seconds
    void tick(double now)
    {
        nowT = now;
        if (values[pOutActivity] != lastActivity) { lastActivity = values[pOutActivity]; lastActivityT = now; }
        running = now - lastActivityT < 0.3;
        const float hz = running ? values[pOutPitch] : 0.f;
        Sample s;
        s.t = now;
        if (hz > 20.f) {
            const double ref = values[pRefHz] > 0 ? values[pRefHz] : 440.0;
            s.in = (float)(69.0 + 12.0 * std::log2(hz / ref));
            s.out = s.in + values[pOutCents] * 0.01f;
            s.target = (int)std::lround(values[pOutNote]);
            s.voiced = true;
            s.vib = values[pOutVib] > 0.f;
        }
        hist[histHead] = s;
        histHead = (histHead + 1) % kHistSize;
        if (histCount < kHistSize) ++histCount;

        float sum = 0; int n = 0;
        for (int k = 0; k < std::min(histCount, 90); ++k) {
            const Sample& q = at(k);
            if (q.voiced) { sum += q.out; ++n; }
        }
        if (n > 4) {
            const float want = sum / n;
            if (std::fabs(want - center) > 3.5f) center += (want - center) * 0.08f;
        }
    }

    // ---------------- drawing ----------------
    void draw(cairo_t* cr, double pixelW, double pixelH)
    {
        const double sc = std::min(pixelW / kW, pixelH / kH);
        cairo_save(cr);
        cairo_set_source_rgb(cr, 0.05, 0.04, 0.035); cairo_paint(cr);
        cairo_scale(cr, sc, sc);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

        drawCheek(cr, 0, false);
        drawCheek(cr, kW - kCheek, true);
        cairo_translate(cr, kCheek, 0);

        drawFaceplate(cr);
        drawHeader(cr);
        drawKeyStrip(cr);
        drawGraph(cr);
        drawReadout(cr);
        drawModules(cr);
        for (int k = 0; k < kNumKnobs; ++k) drawKnob(cr, k);
        if (menuOpen) drawMenu(cr);
        cairo_restore(cr);
    }

    // ---------------- mouse (logical window coords) ----------------
    bool mouseDown(double x, double y, bool fine, double timeSec)
    {
        x -= kCheek;
        if (menuOpen) {
            const int item = menuItemAt(x, y);
            if (item >= 0) commit(pScale, (float)item);
            menuOpen = false;
            return true;
        }
        if (inRect(x, y, kScaleBox)) { menuOpen = true; return true; }
        if (inRect(x, y, kClassicBtn)) { commit(pMode, 0.f); return true; }
        if (inRect(x, y, kNaturalBtn)) { commit(pMode, 1.f); return true; }
        if (inRect(x, y, kDetectBtn)) { commit(pLearn, learning() ? 0.f : 1.f); return true; }
        if (learning()) {
            if (inRect(x, y, applyRect()) && values[pOutKey] >= 0) { applyDetectedKey(); return true; }
            if (inRect(x, y, cancelRect())) { commit(pLearn, 0.f); return true; }
            if (inRect(x, y, kReadout)) return true;
        }
        const int cell = keyCellAt(x, y);
        if (cell >= 0) { commit(pKey, (float)cell); return true; }
        const int k = knobAt(x, y);
        if (k >= 0) {
            const int p = kKnobParams[k];
            if (lastClickKnob == k && timeSec - lastClickTime < 0.35) {   // double-click = default
                commit(p, kSpecs[p].def);
                lastClickKnob = -1;
                return true;
            }
            lastClickKnob = k; lastClickTime = timeSec;
            dragKnob = k; dragY = y; dragNorm = toNorm(p, values[p]); dragFine = fine;
            host->beginEdit(p);
            return true;
        }
        return false;
    }

    bool mouseUp(double, double)
    {
        if (dragKnob >= 0) { host->endEdit(kKnobParams[dragKnob]); dragKnob = -1; return true; }
        return false;
    }

    bool mouseMove(double x, double y, bool fine)
    {
        x -= kCheek;
        hoverKnob = knobAt(x, y);
        hoverCell = keyCellAt(x, y);
        hoverMenu = menuOpen ? menuItemAt(x, y) : -1;
        if (dragKnob < 0) return false;
        if (fine != dragFine) { dragNorm = toNorm(kKnobParams[dragKnob], values[kKnobParams[dragKnob]]); dragY = y; dragFine = fine; }
        const int p = kKnobParams[dragKnob];
        const double n = std::clamp(dragNorm + (dragY - y) / (fine ? 1200.0 : 220.0), 0.0, 1.0);
        float v = fromNorm(p, n);
        if (kSpecs[p].integer) v = std::round(v);
        if (v != values[p]) { values[p] = v; host->setParam(p, v); }
        return true;
    }

    bool scroll(double x, double y, double dy)
    {
        x -= kCheek;
        if (inRect(x, y, kScaleBox)) {
            int s = (int)values[pScale] + (dy > 0 ? -1 : 1);
            commit(pScale, (float)((s + kScaleCount) % kScaleCount));
            return true;
        }
        if (keyCellAt(x, y) >= 0) {
            int s = (int)values[pKey] + (dy > 0 ? 1 : -1);
            commit(pKey, (float)((s + 12) % 12));
            return true;
        }
        const int k = knobAt(x, y);
        if (k < 0) return false;
        const int p = kKnobParams[k];
        float v;
        if (kSpecs[p].integer) v = values[p] + (dy > 0 ? 1 : -1);
        else v = fromNorm(p, std::clamp(toNorm(p, values[p]) + (dy > 0 ? 0.02 : -0.02), 0.0, 1.0));
        commit(p, std::clamp(v, kSpecs[p].min, kSpecs[p].max));
        return true;
    }

    bool isMenuOpen() const { return menuOpen; }

private:
    // ---------------- data ----------------
    // note: no NaN sentinels, the plugin is built with -ffast-math
    struct Sample { double t = -1; float in = 0, out = 0; int target = -1; bool voiced = false, vib = false; };
    static constexpr int kHistSize = 1024;
    std::vector<Sample> hist;
    int histHead = 0, histCount = 0;
    const Sample& at(int k) const { return hist[(histHead - 1 - k + kHistSize) % kHistSize]; }

    Host* host;
    float values[pCount];
    double nowT = 0, lastActivityT = -10;
    float lastActivity = -1;
    bool running = false;
    float center = 60.f;

    int dragKnob = -1, hoverKnob = -1, hoverCell = -1, hoverMenu = -1, lastClickKnob = -1;
    double dragY = 0, dragNorm = 0, lastClickTime = -10;
    bool dragFine = false, menuOpen = false;

    // ---------------- layout (faceplate coords, 860 x 540) ----------------
    struct Rect { double x, y, w, h; };
    static constexpr Rect kScaleBox   = { 254, 14, 220, 34 };
    static constexpr Rect kDetectBtn  = { 486, 14, 108, 34 };
    static constexpr Rect kClassicBtn = { 612, 14, 104, 34 };
    static constexpr Rect kNaturalBtn = { 722, 14, 104, 34 };
    static constexpr Rect kKeyStrip   = { 92, 68, 744, 44 };
    static constexpr Rect kGraph      = { 24, 124, 588, 240 };
    static constexpr Rect kReadout    = { 624, 124, 212, 240 };
    static constexpr double kModTop = 380, kSeamX = 470;
    static constexpr int  kNumKnobs   = 10;
    static constexpr int  kKnobParams[kNumKnobs] = { pRetune, pVibrato, pFlex, pHumanize, pTransition,
                                                     pAmount, pTranspose, pMix, pRefHz, pGain };
    static constexpr double kKnobY = 452, kKnobR = 19;
    static constexpr double kWindow = 6.0;   // seconds shown in the graph
    static constexpr double kSpan = 15.0;    // semitones shown in the graph

    // ---------------- palette ----------------
    struct C { double r, g, b; };
    static constexpr C kNavy    = { 0.118, 0.235, 0.384 };
    static constexpr C kNavyHi  = { 0.165, 0.298, 0.463 };
    static constexpr C kNavyLo  = { 0.075, 0.153, 0.259 };
    static constexpr C kScreen  = { 0.035, 0.050, 0.080 };
    static constexpr C kScreen2 = { 0.060, 0.090, 0.135 };
    static constexpr C kText    = { 0.960, 0.965, 0.975 };
    static constexpr C kPrint   = { 0.840, 0.870, 0.910 };   // faceplate silkscreen
    static constexpr C kDim     = { 0.560, 0.630, 0.720 };
    static constexpr C kAccent  = { 0.250, 0.880, 0.820 };   // corrected trace
    static constexpr C kAmber   = { 1.000, 0.690, 0.250 };
    static constexpr C kRed     = { 1.000, 0.240, 0.200 };
    static constexpr C kYellow  = { 1.000, 0.860, 0.180 };
    static constexpr C kGreen   = { 0.300, 0.950, 0.350 };
    // knob caps
    static constexpr C kCapPink  = { 0.930, 0.310, 0.560 };
    static constexpr C kCapBlue  = { 0.280, 0.610, 0.860 };
    static constexpr C kCapGrey  = { 0.860, 0.870, 0.880 };
    static constexpr C kCapGreen = { 0.150, 0.640, 0.360 };
    static constexpr C kCapBlack = { 0.200, 0.210, 0.230 };

    static void rgb(cairo_t* cr, C c, double a = 1.0) { cairo_set_source_rgba(cr, c.r, c.g, c.b, a); }
    static C mix(C a, C b, double t) { return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; }
    static bool inRect(double x, double y, Rect r) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }
    static void rrect(cairo_t* cr, double x, double y, double w, double h, double r)
    {
        cairo_new_sub_path(cr);
        cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
        cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
        cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
        cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
        cairo_close_path(cr);
    }
    static void text(cairo_t* cr, const char* s, double x, double y, double size, int align /*-1 left,0 centre,1 right*/)
    {
        cairo_set_font_size(cr, size);
        cairo_text_extents_t e; cairo_text_extents(cr, s, &e);
        double tx = x;
        if (align == 0) tx = x - (e.width / 2 + e.x_bearing);
        else if (align == 1) tx = x - (e.width + e.x_bearing);
        cairo_move_to(cr, tx, y);
        cairo_show_text(cr, s);
        cairo_new_path(cr);
    }

    // ---------------- hardware primitives ----------------
    void drawCheek(cairo_t* cr, double x, bool right)
    {
        const double w = kCheek, h = kH;
        cairo_save(cr);
        rrect(cr, x, 0, w, h, 5); cairo_clip(cr);
        cairo_pattern_t* g = cairo_pattern_create_linear(x, 0, x + w, 0);
        cairo_pattern_add_color_stop_rgb(g, 0.0, right ? 0.50 : 0.43, right ? 0.24 : 0.20, right ? 0.10 : 0.08);
        cairo_pattern_add_color_stop_rgb(g, 0.5, 0.66, 0.35, 0.16);
        cairo_pattern_add_color_stop_rgb(g, 1.0, right ? 0.43 : 0.50, right ? 0.20 : 0.24, right ? 0.08 : 0.10);
        cairo_set_source(cr, g); cairo_paint(cr); cairo_pattern_destroy(g);
        // grain
        for (int i = 0; i < 22; ++i) {
            const double gx = x + 1.5 + std::fmod(i * 7.37 + (right ? 3.1 : 0.0), w - 3);
            const double a = 0.06 + 0.07 * (0.5 + 0.5 * std::sin(i * 2.17));
            cairo_set_source_rgba(cr, 0.25, 0.10, 0.03, a);
            cairo_set_line_width(cr, 0.6 + 0.8 * (0.5 + 0.5 * std::sin(i * 1.3)));
            cairo_move_to(cr, gx, 0);
            for (double yy = 0; yy <= h; yy += 30)
                cairo_line_to(cr, gx + 1.4 * std::sin(yy * 0.011 + i), yy);
            cairo_stroke(cr);
        }
        // edge shading
        cairo_set_source_rgba(cr, 1, 0.85, 0.6, 0.12); cairo_rectangle(cr, right ? x + w - 2 : x, 0, 2, h); cairo_fill(cr);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.35); cairo_rectangle(cr, right ? x : x + w - 3, 0, 3, h); cairo_fill(cr);
        cairo_restore(cr);
    }

    static void screw(cairo_t* cr, double x, double y, double ang)
    {
        cairo_pattern_t* g = cairo_pattern_create_radial(x - 1.5, y - 1.5, 0.5, x, y, 5.5);
        cairo_pattern_add_color_stop_rgb(g, 0, 0.92, 0.93, 0.95);
        cairo_pattern_add_color_stop_rgb(g, 1, 0.42, 0.45, 0.50);
        cairo_set_source(cr, g); cairo_arc(cr, x, y, 5, 0, 2 * M_PI); cairo_fill(cr); cairo_pattern_destroy(g);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.45); cairo_set_line_width(cr, 1.3);
        for (int k = 0; k < 2; ++k) {
            const double a = ang + k * M_PI / 2;
            cairo_move_to(cr, x - 3.4 * std::cos(a), y - 3.4 * std::sin(a));
            cairo_line_to(cr, x + 3.4 * std::cos(a), y + 3.4 * std::sin(a));
        }
        cairo_stroke(cr);
    }

    static void led(cairo_t* cr, double x, double y, double r, C c, double on)
    {
        if (on > 0.02) {
            cairo_pattern_t* g = cairo_pattern_create_radial(x, y, r * 0.3, x, y, r * 4.6);
            cairo_pattern_add_color_stop_rgba(g, 0.0, c.r, c.g, c.b, 0.75 * on);
            cairo_pattern_add_color_stop_rgba(g, 0.3, c.r, c.g, c.b, 0.30 * on);
            cairo_pattern_add_color_stop_rgba(g, 1.0, c.r, c.g, c.b, 0.0);
            cairo_set_source(cr, g); cairo_arc(cr, x, y, r * 4.6, 0, 2 * M_PI); cairo_fill(cr); cairo_pattern_destroy(g);
        }
        cairo_set_source_rgb(cr, 0.05, 0.05, 0.06); cairo_arc(cr, x, y, r + 1.2, 0, 2 * M_PI); cairo_fill(cr);
        const C off = mix(c, C{0.08, 0.08, 0.09}, 0.78);
        const C body = mix(off, c, on);
        cairo_pattern_t* g = cairo_pattern_create_radial(x - r * 0.35, y - r * 0.35, r * 0.1, x, y, r);
        const C hi = mix(body, C{1, 1, 1}, 0.25 + 0.45 * on);
        cairo_pattern_add_color_stop_rgb(g, 0, hi.r, hi.g, hi.b);
        cairo_pattern_add_color_stop_rgb(g, 1, body.r, body.g, body.b);
        cairo_set_source(cr, g); cairo_arc(cr, x, y, r, 0, 2 * M_PI); cairo_fill(cr); cairo_pattern_destroy(g);
    }

    // lens lamp beside a button: dark tinted glass when off, glowing when on
    static void lamp(cairo_t* cr, Rect l, C c, double on)
    {
        if (on > 0.02) {
            const double cx = l.x + l.w / 2, cy = l.y + l.h / 2;
            // wide soft bloom + tight hot halo
            const double R = std::max(l.w * 2.4, l.h * 0.95);
            cairo_pattern_t* gl = cairo_pattern_create_radial(cx, cy, 1, cx, cy, R);
            cairo_pattern_add_color_stop_rgba(gl, 0.0, c.r, c.g, c.b, 0.55 * on);
            cairo_pattern_add_color_stop_rgba(gl, 0.4, c.r, c.g, c.b, 0.22 * on);
            cairo_pattern_add_color_stop_rgba(gl, 1.0, c.r, c.g, c.b, 0.0);
            cairo_set_source(cr, gl); cairo_rectangle(cr, cx - R, cy - R, 2 * R, 2 * R); cairo_fill(cr);
            cairo_pattern_destroy(gl);
            cairo_set_source_rgba(cr, c.r, c.g, c.b, 0.35 * on);
            rrect(cr, l.x - 2.5, l.y - 2.5, l.w + 5, l.h + 5, 4); cairo_fill(cr);
        }
        const C off = mix(c, C{0.06, 0.05, 0.05}, 0.72);
        const C body = mix(off, c, on);
        cairo_pattern_t* g = cairo_pattern_create_linear(l.x, l.y, l.x + l.w, l.y + l.h);
        const C hi = mix(body, C{1, 1, 1}, 0.15 + 0.6 * on);
        cairo_pattern_add_color_stop_rgb(g, 0, hi.r, hi.g, hi.b);
        const C mid = mix(body, C{1, 1, 1}, 0.18 * on);
        cairo_pattern_add_color_stop_rgb(g, 0.55, mid.r, mid.g, mid.b);
        const C lo = mix(body, C{0, 0, 0}, 0.25);
        cairo_pattern_add_color_stop_rgb(g, 1, lo.r, lo.g, lo.b);
        cairo_set_source(cr, g); rrect(cr, l.x, l.y, l.w, l.h, 2); cairo_fill(cr); cairo_pattern_destroy(g);
        // glass highlight
        cairo_set_source_rgba(cr, 1, 1, 1, 0.18 + 0.25 * on);
        rrect(cr, l.x + 1.5, l.y + 1.5, l.w * 0.45, l.h * 0.35, 1.5); cairo_fill(cr);
    }

    // plastic push button with a lamp beside it that lights when the function is on
    static void pushButton(cairo_t* cr, Rect b, const char* label, bool lit, C litCol, double size = 11,
                           double lampW = 13, bool dimLabel = false, double litLevel = 1.0)
    {
        const double gap = 3;
        const Rect cap  = { b.x, b.y, b.w - lampW - gap, b.h };
        const Rect lens = { b.x + b.w - lampW, b.y + 2, lampW, b.h - 4 };
        // shared dark bezel
        cairo_set_source_rgb(cr, 0.03, 0.04, 0.06); rrect(cr, b.x - 2.5, b.y - 2.5, b.w + 5, b.h + 5, 5); cairo_fill(cr);
        // cap: off-white plastic, slightly domed
        cairo_set_source_rgba(cr, 0, 0, 0, 0.5); rrect(cr, cap.x + 0.5, cap.y + 1.5, cap.w, cap.h, 3.5); cairo_fill(cr);
        cairo_pattern_t* g = cairo_pattern_create_linear(cap.x, cap.y, cap.x, cap.y + cap.h);
        cairo_pattern_add_color_stop_rgb(g, 0.00, 0.975, 0.975, 0.965);
        cairo_pattern_add_color_stop_rgb(g, 0.55, 0.915, 0.915, 0.905);
        cairo_pattern_add_color_stop_rgb(g, 1.00, 0.760, 0.765, 0.770);
        cairo_set_source(cr, g); rrect(cr, cap.x, cap.y, cap.w, cap.h, 3.5); cairo_fill(cr); cairo_pattern_destroy(g);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.8); cairo_set_line_width(cr, 1);
        cairo_move_to(cr, cap.x + 3.5, cap.y + 1.2); cairo_line_to(cr, cap.x + cap.w - 3.5, cap.y + 1.2); cairo_stroke(cr);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.18); rrect(cr, cap.x + .5, cap.y + .5, cap.w - 1, cap.h - 1, 3.5); cairo_stroke(cr);
        // printed label
        if (label && *label) {
            cairo_set_source_rgba(cr, 0.12, 0.13, 0.15, dimLabel ? 0.45 : 1.0);
            text(cr, label, cap.x + cap.w / 2, cap.y + cap.h / 2 + size * 0.36, size, 0);
        }
        lamp(cr, lens, litCol, lit ? litLevel : 0.0);
    }

    // recessed dark window
    static void window(cairo_t* cr, Rect r, double rad = 6)
    {
        cairo_set_source_rgba(cr, 1, 1, 1, 0.10); rrect(cr, r.x - 1, r.y - 1, r.w + 2, r.h + 2.5, rad + 1); cairo_fill(cr);
        cairo_set_source_rgb(cr, 0.02, 0.03, 0.05); rrect(cr, r.x - 1, r.y - 1.5, r.w + 2, r.h + 2, rad + 1); cairo_fill(cr);
        cairo_pattern_t* g = cairo_pattern_create_linear(0, r.y, 0, r.y + r.h);
        cairo_pattern_add_color_stop_rgb(g, 0, kScreen.r, kScreen.g, kScreen.b);
        cairo_pattern_add_color_stop_rgb(g, 1, kScreen2.r, kScreen2.g, kScreen2.b);
        cairo_set_source(cr, g); rrect(cr, r.x, r.y, r.w, r.h, rad); cairo_fill(cr); cairo_pattern_destroy(g);
    }

    // ---------------- helpers ----------------
    void applyDetectedKey()
    {
        const int k = (int)std::lround(values[pOutKey]);
        if (k < 0) return;
        commit(pKey, (float)(k % 12));
        commit(pScale, (float)(k < 12 ? kMajor : kMinor));
        const float off = values[pOutTuneOff];
        commit(pRefHz, std::fabs(off) >= 3.f ? (float)(std::round(4400.0 * std::pow(2.0, off / 1200.0)) / 10.0) : 440.f);
        commit(pLearn, 0.f);
    }

    void commit(int p, float v)
    {
        values[p] = v;
        host->beginEdit(p); host->setParam(p, v); host->endEdit(p);
    }

    float fromNorm(int p, double n) const
    {
        const ParamSpec& s = kSpecs[p];
        return (float)(s.min + (s.max - s.min) * std::pow(n, (double)s.skew));
    }
    double toNorm(int p, float v) const
    {
        const ParamSpec& s = kSpecs[p];
        const double l = std::clamp((v - s.min) / (s.max - s.min), 0.0f, 1.0f);
        return std::pow(l, 1.0 / s.skew);
    }

    double knobX(int k) const
    {
        if (k < 5) return 47.0 + 94.0 * k;
        return kSeamX + 4 + 38.6 + 77.2 * (k - 5);
    }
    int knobAt(double x, double y) const
    {
        for (int k = 0; k < kNumKnobs; ++k)
            if (std::fabs(x - knobX(k)) < 36 && y > kKnobY - 32 && y < kKnobY + 56) return k;
        return -1;
    }
    int keyCellAt(double x, double y) const
    {
        if (!inRect(x, y, kKeyStrip)) return -1;
        return std::clamp((int)((x - kKeyStrip.x) / (kKeyStrip.w / 12)), 0, 11);
    }
    Rect applyRect() const  { return { kReadout.x + 14, kReadout.y + kReadout.h - 42, 110, 28 }; }
    Rect cancelRect() const { return { kReadout.x + 134, kReadout.y + kReadout.h - 42, 64, 28 }; }
    bool learning() const { return values[pLearn] > 0.5f; }
    bool natural() const { return values[pMode] > 0.5f; }
    static bool naturalOnly(int p) { return p == pVibrato || p == pFlex || p == pHumanize || p == pTransition; }

    Rect menuRect() const { return { kScaleBox.x, kScaleBox.y + kScaleBox.h + 6, kScaleBox.w, 28.0 * kScaleCount + 8 }; }
    int menuItemAt(double x, double y) const
    {
        const Rect m = menuRect();
        if (!inRect(x, y, m)) return -1;
        const int i = (int)((y - m.y - 4) / 28);
        return (i >= 0 && i < kScaleCount) ? i : -1;
    }

    bool inScale(int midi) const
    {
        static const uint16_t masks[kScaleCount] = {
            0xFFF, 0xAB5, 0x5AD, 0x9AD, 0xAAD, 0x295, 0x4A9, 0x4E9, 0x6AD, 0x6B5 };
        const int rel = (((midi % 12) + 12) % 12 - (int)values[pKey] + 24) % 12;
        return (masks[std::clamp((int)values[pScale], 0, kScaleCount - 1)] >> rel) & 1;
    }

    static void noteName(int midi, char* buf, size_t n)
    {
        std::snprintf(buf, n, "%s%d", kKeyNames[((midi % 12) + 12) % 12], midi / 12 - 1);
    }
    static void keyName(int k, char* buf, size_t n)
    {
        if (k < 0) { std::snprintf(buf, n, "\xe2\x80\x93"); return; }
        std::snprintf(buf, n, "%s %s", kKeyNames[k % 12], k < 12 ? "major" : "minor");
    }

    bool voicedNow() const { return histCount > 0 && at(0).voiced; }
    int currentTarget() const { return voicedNow() ? at(0).target : -1; }

    // ---------------- faceplate ----------------
    void drawFaceplate(cairo_t* cr)
    {
        // thin black rail between wood and modules
        cairo_set_source_rgb(cr, 0.06, 0.06, 0.07); cairo_rectangle(cr, 0, 0, kFaceW, kFaceH); cairo_fill(cr);
        auto plate = [&](double x, double y, double w, double h) {
            cairo_pattern_t* g = cairo_pattern_create_linear(0, y, 0, y + h);
            cairo_pattern_add_color_stop_rgb(g, 0, kNavyHi.r, kNavyHi.g, kNavyHi.b);
            cairo_pattern_add_color_stop_rgb(g, 0.5, kNavy.r, kNavy.g, kNavy.b);
            cairo_pattern_add_color_stop_rgb(g, 1, kNavyLo.r, kNavyLo.g, kNavyLo.b);
            cairo_set_source(cr, g); cairo_rectangle(cr, x, y, w, h); cairo_fill(cr); cairo_pattern_destroy(g);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.10); cairo_rectangle(cr, x, y, w, 1); cairo_fill(cr);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.06); cairo_rectangle(cr, x, y, 1, h); cairo_fill(cr);
            cairo_set_source_rgba(cr, 0, 0, 0, 0.35); cairo_rectangle(cr, x + w - 1, y, 1, h); cairo_fill(cr);
        };
        plate(4, 0, kFaceW - 8, kModTop - 4);                     // main module
        plate(4, kModTop, kSeamX - 6, kFaceH - kModTop);          // correction module
        plate(kSeamX + 2, kModTop, kFaceW - kSeamX - 6, kFaceH - kModTop);   // output module
        screw(cr, 16, 12, 0.4); screw(cr, kFaceW - 16, 12, 1.1);
        screw(cr, 16, kModTop - 16, 0.9); screw(cr, kFaceW - 16, kModTop - 16, 0.2);
    }

    void drawHeader(cairo_t* cr)
    {
        // logo mark: a tuning fork with vibration arcs
        {
            const double fx = 42, top = 14, bend = 34, w = 5.5;   // prongs at fx +- w
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            // soft glow
            cairo_set_source_rgba(cr, kAmber.r, kAmber.g, kAmber.b, 0.18); cairo_set_line_width(cr, 7);
            cairo_move_to(cr, fx - w, top); cairo_line_to(cr, fx - w, bend);
            cairo_arc_negative(cr, fx, bend, w, M_PI, 0); cairo_line_to(cr, fx + w, top); cairo_stroke(cr);
            // prongs + U
            rgb(cr, kAmber); cairo_set_line_width(cr, 3.2);
            cairo_move_to(cr, fx - w, top); cairo_line_to(cr, fx - w, bend);
            cairo_arc_negative(cr, fx, bend, w, M_PI, 0); cairo_line_to(cr, fx + w, top); cairo_stroke(cr);
            // stem + ball
            cairo_set_line_width(cr, 3.2);
            cairo_move_to(cr, fx, bend + w); cairo_line_to(cr, fx, 49); cairo_stroke(cr);
            cairo_arc(cr, fx, 50.5, 2.8, 0, 2 * M_PI); cairo_fill(cr);
            // vibration arcs
            cairo_set_line_width(cr, 1.6);
            for (int i = 0; i < 2; ++i) {
                const double rr = 5.0 + i * 4.5;
                cairo_set_source_rgba(cr, kAmber.r, kAmber.g, kAmber.b, 0.85 - 0.35 * i);
                cairo_arc(cr, fx + w + 1, top + 7, rr, -0.55 * M_PI / 2, 0.55 * M_PI / 2); cairo_stroke(cr);
                cairo_arc(cr, fx - w - 1, top + 7, rr, M_PI - 0.55 * M_PI / 2, M_PI + 0.55 * M_PI / 2); cairo_stroke(cr);
            }
        }
        rgb(cr, kText); text(cr, "TuneBrew", 66, 33, 25, -1);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_ITALIC, CAIRO_FONT_WEIGHT_NORMAL);
        rgb(cr, kPrint); text(cr, "by Adin Walls", 68, 50, 11, -1);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

        // status LEDs
        const bool v = voicedNow();
        led(cr, 228, 21, 3.6, kGreen, v ? 1.0 : 0.0);
        led(cr, 228, 39, 3.6, kYellow, (v && values[pOutVib] > 0.f) ? 1.0 : 0.0);
        rgb(cr, kPrint); text(cr, "SIG", 236, 24, 8, -1); text(cr, "VIB", 236, 42, 8, -1);

        // scale selector (recessed window)
        const Rect b = kScaleBox;
        window(cr, b, 5);
        char buf[64];
        std::snprintf(buf, sizeof buf, "%s %s", kKeyNames[(int)values[pKey] % 12], kScaleNames[std::clamp((int)values[pScale], 0, kScaleCount - 1)]);
        rgb(cr, kDim);  text(cr, "SCALE", b.x + 12, b.y + 21, 9, -1);
        rgb(cr, kAmber); text(cr, buf, b.x + 56, b.y + 23, 14, -1);
        rgb(cr, kDim);
        cairo_move_to(cr, b.x + b.w - 22, b.y + 14); cairo_line_to(cr, b.x + b.w - 12, b.y + 14); cairo_line_to(cr, b.x + b.w - 17, b.y + 21);
        cairo_close_path(cr); cairo_fill(cr);

        // detect key: lit amber while listening (pulsing)
        const bool on = learning();
        const double pulse = 0.75 + 0.25 * std::sin(nowT * 6.0);
        pushButton(cr, kDetectBtn, on ? "LISTENING" : "DETECT KEY", on, kAmber, 10.5, 13, false, pulse);

        // mode buttons
        pushButton(cr, kClassicBtn, "CLASSIC", !natural(), kAmber);
        pushButton(cr, kNaturalBtn, "NATURAL", natural(), kGreen);
    }

    void drawKeyStrip(cairo_t* cr)
    {
        rgb(cr, kPrint); text(cr, "KEY", 34, 95, 11, -1);
        const double cw = kKeyStrip.w / 12;
        const int tgt = currentTarget();
        for (int i = 0; i < 12; ++i) {
            const Rect b = { kKeyStrip.x + i * cw + 3, kKeyStrip.y, cw - 6, kKeyStrip.h };
            const bool tonic = i == (int)values[pKey];
            const bool in = inScale(i);
            const bool sung = tgt >= 0 && (tgt % 12) == i;
            // lamp: green = key (home note), yellow = in scale, red = being sung now
            const C lc = sung ? kRed : tonic ? kGreen : kYellow;
            pushButton(cr, b, kKeyNames[i], sung || tonic || in, lc, 14, 11, !in && !tonic);
            if (hoverCell == i) { cairo_set_source_rgba(cr, 0.3, 0.6, 1.0, 0.16); rrect(cr, b.x, b.y, b.w - 14, b.h, 3.5); cairo_fill(cr); }
        }
    }

    double graphY(double midi) const
    {
        const Rect g = kGraph;
        return g.y + g.h / 2 - (midi - center) * (g.h / kSpan);
    }

    void drawGraph(cairo_t* cr)
    {
        const Rect g = kGraph;
        window(cr, g, 8);
        cairo_save(cr);
        rrect(cr, g.x, g.y, g.w, g.h, 8); cairo_clip(cr);

        const double lane = g.h / kSpan;
        const int lo = (int)std::floor(center - kSpan / 2) - 1, hi = (int)std::ceil(center + kSpan / 2) + 1;
        char buf[16];
        for (int m = lo; m <= hi; ++m) {
            const double yc = graphY(m);
            if (inScale(m)) {
                cairo_set_source_rgba(cr, 0.30, 0.50, 0.75, 0.10);
                cairo_rectangle(cr, g.x, yc - lane / 2 + 0.5, g.w, lane - 1); cairo_fill(cr);
                if (yc > g.y + 10 && yc < g.y + g.h - 4) {
                    const bool tonic = ((m % 12) + 12) % 12 == (int)values[pKey];
                    rgb(cr, tonic ? kAmber : kDim, tonic ? 0.95 : 0.75);
                    noteName(m, buf, sizeof buf);
                    text(cr, buf, g.x + 10, yc + 4, 10, -1);
                }
            }
            cairo_set_source_rgba(cr, 0.4, 0.55, 0.75, 0.10); cairo_set_line_width(cr, 1);
            cairo_move_to(cr, g.x, std::round(yc - lane / 2) + 0.5); cairo_line_to(cr, g.x + g.w, std::round(yc - lane / 2) + 0.5); cairo_stroke(cr);
        }
        cairo_set_source_rgba(cr, 0.4, 0.55, 0.75, 0.10);
        for (int s = 1; s < (int)kWindow; ++s) {
            const double x = g.x + g.w - s * (g.w / kWindow);
            cairo_move_to(cr, std::round(x) + .5, g.y); cairo_line_to(cr, std::round(x) + .5, g.y + g.h); cairo_stroke(cr);
        }
        drawTrace(cr, false);
        drawTrace(cr, true);
        // glass sheen
        cairo_pattern_t* sh = cairo_pattern_create_linear(0, g.y, 0, g.y + g.h * 0.5);
        cairo_pattern_add_color_stop_rgba(sh, 0, 1, 1, 1, 0.05);
        cairo_pattern_add_color_stop_rgba(sh, 1, 1, 1, 1, 0.0);
        cairo_set_source(cr, sh); cairo_rectangle(cr, g.x, g.y, g.w, g.h * 0.5); cairo_fill(cr); cairo_pattern_destroy(sh);
        cairo_restore(cr);

        // legend
        cairo_set_line_width(cr, 3);
        rgb(cr, kAmber); cairo_move_to(cr, g.x + g.w - 196, g.y + 16); cairo_line_to(cr, g.x + g.w - 180, g.y + 16); cairo_stroke(cr);
        rgb(cr, kDim); text(cr, "INPUT", g.x + g.w - 175, g.y + 20, 9, -1);
        rgb(cr, kAccent); cairo_move_to(cr, g.x + g.w - 116, g.y + 16); cairo_line_to(cr, g.x + g.w - 100, g.y + 16); cairo_stroke(cr);
        rgb(cr, kDim); text(cr, "CORRECTED", g.x + g.w - 95, g.y + 20, 9, -1);
        bool any = false;
        for (int k = 0; k < std::min(histCount, 400); ++k) if (at(k).voiced) { any = true; break; }
        if (!any) { rgb(cr, kDim, 0.8); text(cr, "Sing or press play...", g.x + g.w / 2, g.y + g.h / 2 + 5, 14, 0); }
    }

    void drawTrace(cairo_t* cr, bool out)
    {
        const Rect g = kGraph;
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        for (int pass = 0; pass < 2; ++pass) {      // soft phosphor glow, then the line
            cairo_set_line_width(cr, pass == 0 ? (out ? 7.0 : 5.0) : (out ? 2.4 : 1.6));
            rgb(cr, out ? kAccent : kAmber, pass == 0 ? 0.14 : (out ? 1.0 : 0.85));
            bool pen = false; double lastY = 0;
            for (int k = 0; k < histCount; ++k) {
                const Sample& s = at(k);
                const double age = nowT - s.t;
                if (age > kWindow + 0.2) break;
                if (!s.voiced) { if (pen) cairo_stroke(cr); pen = false; continue; }
                const double x = g.x + g.w - age * (g.w / kWindow);
                const double y = graphY(out ? s.out : s.in);
                if (pen && std::fabs(y - lastY) > g.h * 0.4) { cairo_stroke(cr); pen = false; }
                if (!pen) { cairo_move_to(cr, x, y); pen = true; } else cairo_line_to(cr, x, y);
                lastY = y;
            }
            if (pen) cairo_stroke(cr);
        }
    }

    // vertical LED ladder for the amount of correction being applied
    void drawShiftLadder(cairo_t* cr, double x, double y0, double y1)
    {
        static const int steps[10] = { 2, 5, 10, 15, 20, 30, 40, 60, 80, 100 };
        const double mag = voicedNow() ? std::fabs(values[pOutCents]) : 0.0;
        const double dy = (y1 - y0) / 9.0;
        rgb(cr, kPrint); text(cr, "SHIFT", x, y0 - 14, 8, 0);
        for (int i = 0; i < 10; ++i) {
            const double yy = y1 - i * dy;
            const C c = i < 5 ? kGreen : i < 8 ? kYellow : kRed;
            led(cr, x, yy, 3.6, c, mag >= steps[i] ? 1.0 : 0.0);
            if (i % 3 == 0 || i == 9) {
                char b[8]; std::snprintf(b, sizeof b, "%d", steps[i]);
                rgb(cr, kDim); text(cr, b, x - 9, yy + 3, 7, 1);
            }
        }
        rgb(cr, kDim); text(cr, "cents", x, y1 + 16, 7, 0);
    }

    void drawLearnPanel(cairo_t* cr)
    {
        const Rect r = kReadout;
        window(cr, r, 8);
        cairo_set_source_rgba(cr, kAmber.r, kAmber.g, kAmber.b, 0.55); cairo_set_line_width(cr, 1.2);
        rrect(cr, r.x + .6, r.y + .6, r.w - 1.2, r.h - 1.2, 8); cairo_stroke(cr);
        char buf[64];
        const float secs = values[pOutLearnSecs];
        const int k = (int)std::lround(values[pOutKey]);
        const float conf = values[pOutKeyConf];
        rgb(cr, kDim); text(cr, "KEY DETECTION", r.x + 14, r.y + 22, 9, -1);
        std::snprintf(buf, sizeof buf, "%.0fs heard", secs);
        text(cr, buf, r.x + r.w - 14, r.y + 22, 9, 1);
        if (k < 0) {
            rgb(cr, kText, 0.85); text(cr, "Play the vocal...", r.x + r.w / 2, r.y + 76, 15, 0);
            rgb(cr, kDim); text(cr, "15-30 s of singing works best", r.x + r.w / 2, r.y + 98, 10, 0);
        } else {
            keyName(k, buf, sizeof buf);
            rgb(cr, kAmber); text(cr, buf, r.x + r.w / 2, r.y + 64, 24, 0);
            // confidence as an LED row
            const int lit = (int)std::lround(conf * 10.0);
            for (int i = 0; i < 10; ++i) {
                const C c = i < 4 ? kRed : i < 7 ? kYellow : kGreen;
                led(cr, r.x + 26 + i * 17.8, r.y + 88, 3.6, c, i < lit ? 1.0 : 0.0);
            }
            const char* word = conf >= 0.7f ? "confident" : conf >= 0.4f ? "fairly sure" : "not sure yet";
            std::snprintf(buf, sizeof buf, "%.0f%%  %s", conf * 100.f, word);
            rgb(cr, kText); text(cr, buf, r.x + r.w / 2, r.y + 112, 11, 0);
            const int rel = k < 12 ? 12 + (k + 9) % 12 : (k % 12 + 3) % 12;
            char kb[24]; keyName(rel, kb, sizeof kb);
            rgb(cr, kDim);
            std::snprintf(buf, sizeof buf, "Same notes as %s", kb); text(cr, buf, r.x + 14, r.y + 140, 10, -1);
            keyName((int)std::lround(values[pOutKeyAlt]), kb, sizeof kb);
            std::snprintf(buf, sizeof buf, "Next best: %s", kb); text(cr, buf, r.x + 14, r.y + 157, 10, -1);
            const float off = values[pOutTuneOff];
            std::snprintf(buf, sizeof buf, "Tuning %+.0fc  (A4 = %.1f Hz)", off, 440.0 * std::pow(2.0, off / 1200.0));
            text(cr, buf, r.x + 14, r.y + 174, 10, -1);
        }
        pushButton(cr, applyRect(), "APPLY", k >= 0, kGreen, 11, 13, k < 0);
        pushButton(cr, cancelRect(), "STOP", true, kRed, 11, 11);
    }

    void drawReadout(cairo_t* cr)
    {
        if (learning()) { drawLearnPanel(cr); return; }
        const Rect r = kReadout;
        window(cr, r, 8);
        const double cx = r.x + 84;
        const bool voiced = voicedNow();
        char buf[32];

        rgb(cr, kDim); text(cr, "NOTE", cx, r.y + 22, 9, 0);
        if (voiced && at(0).target >= 0) {
            noteName(at(0).target, buf, sizeof buf);
            // warm display glow
            rgb(cr, kAmber, 0.18); text(cr, buf, cx + 1, r.y + 79, 46, 0);
            rgb(cr, kAmber); text(cr, buf, cx, r.y + 78, 46, 0);
        } else { rgb(cr, kAmber, 0.25); text(cr, "--", cx, r.y + 78, 46, 0); }

        // tuning meter: 11 LEDs, -50..+50 cents relative to the target
        const double mx = r.x + 16, mw = 136, my = r.y + 104;
        double dev = 0.0; bool show = false;
        if (voiced && at(0).target >= 0) { dev = (at(0).in - at(0).target) * 100.0; show = true; }
        const int pos = show ? (int)std::lround(std::clamp(dev, -50.0, 50.0) / 10.0) + 5 : -1;
        for (int i = 0; i < 11; ++i) {
            const int d = std::abs(i - 5);
            const C c = d == 0 ? kGreen : d <= 2 ? kYellow : kRed;
            led(cr, mx + i * (mw / 10.0), my, 3.4, c, i == pos ? 1.0 : 0.0);
        }
        rgb(cr, kDim); text(cr, "FLAT", mx - 2, my + 18, 8, -1); text(cr, "SHARP", mx + mw + 2, my + 18, 8, 1);
        if (show) {
            std::snprintf(buf, sizeof buf, "%+.0f\xc2\xa2", dev);
            rgb(cr, kText); text(cr, buf, mx + mw / 2, my + 19, 10, 0);
        }

        // details
        rgb(cr, kDim);
        text(cr, "VIBRATO", r.x + 16, r.y + 172, 9, -1);
        text(cr, "INPUT", r.x + 16, r.y + 196, 9, -1);
        text(cr, "SHIFT", r.x + 16, r.y + 220, 9, -1);
        const double vx = r.x + 160;
        const bool vibNow = voiced && values[pOutVib] > 0.f;
        if (vibNow) { std::snprintf(buf, sizeof buf, "%.0fc %s", values[pOutVib], natural() ? "kept" : "flat"); rgb(cr, kYellow); }
        else { std::snprintf(buf, sizeof buf, "\xe2\x80\x93"); rgb(cr, kText); }
        text(cr, buf, vx, r.y + 172, 11, 1);
        rgb(cr, kText);
        if (voiced) std::snprintf(buf, sizeof buf, "%.1f Hz", values[pOutPitch]); else std::snprintf(buf, sizeof buf, "\xe2\x80\x93");
        text(cr, buf, vx, r.y + 196, 11, 1);
        if (voiced) std::snprintf(buf, sizeof buf, "%+.0f c", values[pOutCents]); else std::snprintf(buf, sizeof buf, "\xe2\x80\x93");
        text(cr, buf, vx, r.y + 220, 11, 1);

        drawShiftLadder(cr, r.x + r.w - 20, r.y + 40, r.y + 208);
    }

    void formatValue(int p, char* buf, size_t n) const
    {
        const float v = values[p];
        switch (p) {
        case pRetune:     std::snprintf(buf, n, "%.0f ms", v < 0.5f ? 0.f : v); break;
        case pTransition: std::snprintf(buf, n, "%.0f ms", v); break;
        case pTranspose:  std::snprintf(buf, n, "%+d st", (int)std::lround(v)); break;
        case pRefHz:      std::snprintf(buf, n, "%.1f Hz", v); break;
        case pGain:       std::snprintf(buf, n, "%+.1f dB", v); break;
        default:          std::snprintf(buf, n, "%.0f%%", v); break;
        }
    }

    static C capColour(int p)
    {
        switch (p) {
        case pRetune:     return kCapPink;
        case pVibrato:    return kCapBlue;
        case pFlex:       return kCapGrey;
        case pHumanize:   return kCapBlue;
        case pTransition: return kCapGreen;
        case pAmount:     return kCapGrey;
        case pTranspose:  return kCapBlack;
        case pMix:        return kCapGrey;
        case pRefHz:      return kCapBlue;
        case pGain:       return kCapPink;
        }
        return kCapGrey;
    }

    void drawModules(cairo_t* cr)
    {
        // module screws
        screw(cr, 16, kModTop + 12, 0.7);            screw(cr, kSeamX - 14, kModTop + 12, 0.2);
        screw(cr, kSeamX + 14, kModTop + 12, 1.0);   screw(cr, kFaceW - 16, kModTop + 12, 0.5);
        // titles
        rgb(cr, kText); text(cr, "CORRECTION", 34, kModTop + 16, 11, -1);
        rgb(cr, kText); text(cr, "OUTPUT", kSeamX + 30, kModTop + 16, 11, -1);
        rgb(cr, natural() ? kGreen : kDim, natural() ? 0.9 : 0.6);
        text(cr, natural() ? "NATURAL ENGINE" : "CLASSIC ENGINE", kSeamX - 30, kModTop + 16, 9, 1);
        // silk-screen dividers
        cairo_set_source_rgba(cr, kPrint.r, kPrint.g, kPrint.b, 0.35); cairo_set_line_width(cr, 1);
        cairo_move_to(cr, 34, kModTop + 24.5); cairo_line_to(cr, kSeamX - 30, kModTop + 24.5); cairo_stroke(cr);
        cairo_move_to(cr, kSeamX + 30, kModTop + 24.5); cairo_line_to(cr, kFaceW - 30, kModTop + 24.5); cairo_stroke(cr);
        // bottom labels
        rgb(cr, kPrint);
        text(cr, "TB 1", 16, kFaceH - 10, 9, -1); text(cr, "PITCH / NATURAL", kSeamX - 14, kFaceH - 10, 9, 1);
        text(cr, "TB 2", kSeamX + 14, kFaceH - 10, 9, -1); text(cr, "LEVEL / TUNING", kFaceW - 16, kFaceH - 10, 9, 1);
        screw(cr, kSeamX / 2, kFaceH - 13, 0.3); screw(cr, (kSeamX + kFaceW) / 2, kFaceH - 13, 1.2);
    }

    void drawKnob(cairo_t* cr, int k)
    {
        const int p = kKnobParams[k];
        const double cx = knobX(k), cy = kKnobY, r = kKnobR;
        const double a0 = 0.75 * M_PI, a1 = 2.25 * M_PI;
        const double n = toNorm(p, values[p]);
        const double av = a0 + (a1 - a0) * n;
        const bool active = dragKnob == k || hoverKnob == k;
        const bool dim = naturalOnly(p) && !natural();
        if (dim) cairo_push_group(cr);
        cairo_new_path(cr);

        // printed scale ticks
        rgb(cr, kPrint, 0.85); cairo_set_line_width(cr, 1.2); cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
        for (int i = 0; i <= 10; ++i) {
            const double a = a0 + (a1 - a0) * i / 10.0;
            const double r0 = r + 4, r1 = r + ((i % 5 == 0) ? 9.5 : 7);
            cairo_move_to(cr, cx + std::cos(a) * r0, cy + std::sin(a) * r0);
            cairo_line_to(cr, cx + std::cos(a) * r1, cy + std::sin(a) * r1);
        }
        cairo_stroke(cr);
        if (kSpecs[p].bipolar) { rgb(cr, kPrint); text(cr, "0", cx, cy - r - 11, 7, 0); }

        // shadow + dark skirt
        cairo_set_source_rgba(cr, 0, 0, 0, 0.45); cairo_arc(cr, cx + 1.5, cy + 2.5, r + 2.5, 0, 2 * M_PI); cairo_fill(cr);
        cairo_pattern_t* sk = cairo_pattern_create_linear(cx, cy - r, cx, cy + r);
        cairo_pattern_add_color_stop_rgb(sk, 0, 0.30, 0.31, 0.33);
        cairo_pattern_add_color_stop_rgb(sk, 1, 0.07, 0.07, 0.08);
        cairo_set_source(cr, sk); cairo_arc(cr, cx, cy, r + 2, 0, 2 * M_PI); cairo_fill(cr); cairo_pattern_destroy(sk);

        // coloured cap with knurled rim
        const C cap = capColour(p);
        const C rim = mix(cap, C{0, 0, 0}, 0.28);
        rgb(cr, rim); cairo_arc(cr, cx, cy, r, 0, 2 * M_PI); cairo_fill(cr);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.18); cairo_set_line_width(cr, 1.0);
        for (int i = 0; i < 28; ++i) {
            const double a = i * 2 * M_PI / 28;
            cairo_move_to(cr, cx + std::cos(a) * (r - 3.2), cy + std::sin(a) * (r - 3.2));
            cairo_line_to(cr, cx + std::cos(a) * r, cy + std::sin(a) * r);
        }
        cairo_stroke(cr);
        cairo_pattern_t* g = cairo_pattern_create_radial(cx - r * 0.35, cy - r * 0.45, r * 0.1, cx, cy, r * 0.85);
        const C hi = mix(cap, C{1, 1, 1}, 0.38);
        cairo_pattern_add_color_stop_rgb(g, 0, hi.r, hi.g, hi.b);
        cairo_pattern_add_color_stop_rgb(g, 1, cap.r, cap.g, cap.b);
        cairo_set_source(cr, g); cairo_arc(cr, cx, cy, r - 3.2, 0, 2 * M_PI); cairo_fill(cr); cairo_pattern_destroy(g);
        if (active) { cairo_set_source_rgba(cr, 1, 1, 1, 0.16); cairo_arc(cr, cx, cy, r - 3.2, 0, 2 * M_PI); cairo_fill(cr); }

        // pointer line (dark on light caps)
        const bool lightCap = cap.r + cap.g + cap.b > 2.2;
        if (lightCap) cairo_set_source_rgb(cr, 0.12, 0.12, 0.14); else cairo_set_source_rgb(cr, 0.98, 0.98, 0.98);
        cairo_set_line_width(cr, 2.6); cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_move_to(cr, cx + std::cos(av) * r * 0.25, cy + std::sin(av) * r * 0.25);
        cairo_line_to(cr, cx + std::cos(av) * (r - 1.5), cy + std::sin(av) * (r - 1.5));
        cairo_stroke(cr);

        // value + label (silk-screen)
        char buf[32]; formatValue(p, buf, sizeof buf);
        rgb(cr, active ? kAmber : kText); text(cr, buf, cx, cy + r + 26, 11, 0);
        rgb(cr, kPrint); text(cr, kSpecs[p].label, cx, cy + r + 41, 9, 0);
        if (dim) { cairo_pop_group_to_source(cr); cairo_paint_with_alpha(cr, 0.33); }
    }

    void drawMenu(cairo_t* cr)
    {
        const Rect m = menuRect();
        cairo_set_source_rgba(cr, 0, 0, 0, 0.45); rrect(cr, m.x + 3, m.y + 5, m.w, m.h, 8); cairo_fill(cr);
        window(cr, m, 7);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.15); cairo_set_line_width(cr, 1); rrect(cr, m.x + .5, m.y + .5, m.w - 1, m.h - 1, 7); cairo_stroke(cr);
        for (int i = 0; i < kScaleCount; ++i) {
            const double y = m.y + 4 + i * 28;
            const bool sel = i == (int)values[pScale];
            if (hoverMenu == i) { cairo_set_source_rgba(cr, 1, 1, 1, 0.08); rrect(cr, m.x + 4, y, m.w - 8, 28, 5); cairo_fill(cr); }
            led(cr, m.x + 18, y + 14, 3.2, kGreen, sel ? 1.0 : 0.0);
            rgb(cr, sel ? kAmber : kText); text(cr, kScaleNames[i], m.x + 34, y + 19, 13, -1);
        }
    }
};

} // namespace tunebrew
