// LevelView — the LevelBrew editor, drawn with Cairo in the same 500-series
// rack style as TuneBrew. Framework-agnostic: the DPF UI (or a test renderer)
// feeds it parameter values, the plugin's latest scan + ride curve and mouse
// events in logical coordinates (kW x kH), and receives edits via Host.
#pragma once
#include <cairo/cairo.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include "LevelParams.hpp"

namespace levelbrew {

class LevelView {
public:
    static constexpr double kCheek = 26;
    static constexpr double kFaceW = 860, kFaceH = 540;
    static constexpr double kW = kFaceW + 2 * kCheek, kH = kFaceH;

    struct Host {
        virtual void setParam(int index, float value) = 0;
        virtual void beginEdit(int index) = 0;
        virtual void endEdit(int index) = 0;
        virtual ~Host() {}
    };

    explicit LevelView(Host* h) : host(h)
    {
        for (int i = 0; i < pCount; ++i) values[i] = kSpecs[i].def;
    }

    // ---------------- inputs ----------------
    void setValue(int i, float v) { if (i >= 0 && i < pCount) values[i] = v; }
    float getValue(int i) const { return values[i]; }
    // latest scan + ride curve from the plugin (nullptr: editor can't reach the plugin)
    void setCurve(std::shared_ptr<const Curve> c, bool reachable = true) { cur = std::move(c); linked = reachable; }

    // call at UI refresh rate with a monotonically increasing time in seconds
    void tick(double now)
    {
        nowT = now;
        if (values[pOutActivity] != lastActivity) { lastActivity = values[pOutActivity]; lastActivityT = now; }
        const bool alive = lastActivityT > 0 && now - lastActivityT < 0.3;   // the plugin is processing audio
        const float pos = values[pOutPos];
        running = alive && pos > -0.5f;                                    // ...and the transport is rolling
        if (running) { viewPos = pos; havePos = true; }
        rideShown += (values[pOutGain] - rideShown) * 0.35f;
        const float in = alive ? values[pOutIn] : -90.f, out = alive ? values[pOutOut] : -90.f;
        meterIn = std::max(in, meterIn - 1.2f); meterOut = std::max(out, meterOut - 1.2f);
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
        drawStatus(cr);
        drawOverview(cr);
        drawGraph(cr);
        drawFader(cr);
        drawModules(cr);
        for (int k = 0; k < kNumKnobs; ++k) drawKnob(cr, k);
        cairo_restore(cr);
    }

    // ---------------- mouse (logical window coords) ----------------
    bool mouseDown(double x, double y, bool fine, double timeSec)
    {
        x -= kCheek;
        if (inRect(x, y, kClearBtn)) {
            if (hasScan() && timeSec - clearArmT < 2.0) { commit(pClear, 1.f); values[pClear] = 0; clearArmT = -10; }
            else clearArmT = timeSec;   // click twice to clear (protects the scan)
            return true;
        }
        if (inRect(x, y, kBypassBtn)) { commit(pBypass, values[pBypass] > 0.5f ? 0.f : 1.f); return true; }
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
        hoverBtn = inRect(x, y, kClearBtn) ? 1 : inRect(x, y, kBypassBtn) ? 2 : 0;
        if (dragKnob < 0) return false;
        const int p = kKnobParams[dragKnob];
        if (fine != dragFine) { dragNorm = toNorm(p, values[p]); dragY = y; dragFine = fine; }
        const double n = std::clamp(dragNorm + (dragY - y) / (fine ? 1200.0 : 220.0), 0.0, 1.0);
        const float v = fromNorm(p, n);
        if (v != values[p]) { setValue(p, v); host->setParam(p, v); }
        return true;
    }

    bool scroll(double x, double y, double dy)
    {
        x -= kCheek;
        const int k = knobAt(x, y);
        if (k < 0) return false;
        const int p = kKnobParams[k];
        commit(p, fromNorm(p, std::clamp(toNorm(p, values[p]) + (dy > 0 ? 0.02 : -0.02), 0.0, 1.0)));
        return true;
    }

private:
    // ---------------- data ----------------
    Host* host;
    float values[pCount];
    std::shared_ptr<const Curve> cur;
    bool linked = true;
    double nowT = 0, lastActivityT = -10, clearArmT = -10;
    float lastActivity = 0;
    bool running = false, havePos = false;
    double viewPos = 0;
    float rideShown = 0, meterIn = -90, meterOut = -90;

    int dragKnob = -1, hoverKnob = -1, hoverBtn = 0, lastClickKnob = -1;
    double dragY = 0, dragNorm = 0, lastClickTime = -10;
    bool dragFine = false;

    bool hasScan() const { return cur && cur->an.frames() > 0 && values[pOutScanned] > 0.f; }
    int scanState() const { return (int)std::lround(values[pOutState]); }
    bool riding() const { return running && values[pOutRiding] > 0.5f && !bypassed(); }

    // ---------------- layout ----------------
    struct Rect { double x, y, w, h; };
    static constexpr Rect kScanBox   = { 250, 14, 370, 34 };
    static constexpr Rect kClearBtn  = { 630, 14, 96, 34 };
    static constexpr Rect kBypassBtn = { 738, 14, 100, 34 };
    static constexpr Rect kOverview  = { 24, 106, 812, 40 };
    static constexpr Rect kGraph     = { 24, 158, 588, 206 };
    static constexpr Rect kFaderWin  = { 624, 158, 212, 206 };
    static constexpr double kModTop = 380, kSeamX = 520;
    static constexpr int kNumKnobs = 4;
    static constexpr int kKnobParams[kNumKnobs] = { pStrength, pRange, pSpeed, pOutput };
    static constexpr double kKnobY = 456, kKnobR = 22;
    static constexpr double kWindow = 8.0;   // seconds shown in the scrolling graph

    double knobX(int k) const { return k < 3 ? 110 + k * 150 : 600; }
    int knobAt(double x, double y) const
    {
        for (int k = 0; k < kNumKnobs; ++k)
            if (std::hypot(x - knobX(k), y - kKnobY) < kKnobR + 8) return k;
        return -1;
    }

    // ---------------- colours ----------------
    struct C { double r, g, b; };
    static constexpr C kNavy    = { 0.118, 0.235, 0.384 };
    static constexpr C kNavyHi  = { 0.165, 0.298, 0.463 };
    static constexpr C kNavyLo  = { 0.075, 0.153, 0.259 };
    static constexpr C kScreen  = { 0.035, 0.050, 0.080 };
    static constexpr C kScreen2 = { 0.060, 0.090, 0.135 };
    static constexpr C kText    = { 0.960, 0.965, 0.975 };
    static constexpr C kPrint   = { 0.840, 0.870, 0.910 };
    static constexpr C kDim     = { 0.560, 0.630, 0.720 };
    static constexpr C kAccent  = { 0.250, 0.880, 0.820 };   // levelled vocal
    static constexpr C kAmber   = { 1.000, 0.690, 0.250 };   // ride curve
    static constexpr C kRed     = { 1.000, 0.240, 0.200 };
    static constexpr C kYellow  = { 1.000, 0.860, 0.180 };
    static constexpr C kGreen   = { 0.300, 0.950, 0.350 };
    static constexpr C kCapBlue  = { 0.280, 0.610, 0.860 };
    static constexpr C kCapGrey  = { 0.860, 0.870, 0.880 };
    static constexpr C kCapPink  = { 0.930, 0.310, 0.560 };
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
    static void text(cairo_t* cr, const char* s, double x, double y, double size, int align)
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
    // text clipped to a width, with an ellipsis at the start (keeps the file name visible)
    static void textFit(cairo_t* cr, const std::string& s, double x, double y, double size, double maxW)
    {
        cairo_set_font_size(cr, size);
        std::string t = s;
        cairo_text_extents_t e; cairo_text_extents(cr, t.c_str(), &e);
        while (e.x_advance > maxW && t.size() > 4) { t = "..." + t.substr(4); cairo_text_extents(cr, t.c_str(), &e); }
        cairo_move_to(cr, x, y); cairo_show_text(cr, t.c_str()); cairo_new_path(cr);
    }

    // ---------------- hardware primitives (shared look with TuneBrew) ----------------
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
        for (int i = 0; i < 22; ++i) {
            const double gx = x + 1.5 + std::fmod(i * 7.37 + (right ? 3.1 : 0.0), w - 3);
            const double a = 0.06 + 0.07 * (0.5 + 0.5 * std::sin(i * 2.17));
            cairo_set_source_rgba(cr, 0.25, 0.10, 0.03, a);
            cairo_set_line_width(cr, 0.6 + 0.8 * (0.5 + 0.5 * std::sin(i * 1.3)));
            cairo_move_to(cr, gx, 0);
            for (double yy = 0; yy <= h; yy += 30) cairo_line_to(cr, gx + 1.4 * std::sin(yy * 0.011 + i), yy);
            cairo_stroke(cr);
        }
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

    static void lamp(cairo_t* cr, Rect l, C c, double on)
    {
        if (on > 0.02) {
            const double cx = l.x + l.w / 2, cy = l.y + l.h / 2;
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
        cairo_set_source_rgba(cr, 1, 1, 1, 0.18 + 0.25 * on);
        rrect(cr, l.x + 1.5, l.y + 1.5, l.w * 0.45, l.h * 0.35, 1.5); cairo_fill(cr);
    }

    static void pushButton(cairo_t* cr, Rect b, const char* label, bool lit, C litCol, bool hover, double size = 11,
                           double lampW = 13, bool dimLabel = false)
    {
        const double gap = 3;
        const Rect cap  = { b.x, b.y, lampW > 0 ? b.w - lampW - gap : b.w, b.h };
        const Rect lens = { b.x + b.w - lampW, b.y + 2, lampW, b.h - 4 };
        cairo_set_source_rgb(cr, 0.03, 0.04, 0.06); rrect(cr, b.x - 2.5, b.y - 2.5, b.w + 5, b.h + 5, 5); cairo_fill(cr);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.5); rrect(cr, cap.x + 0.5, cap.y + 1.5, cap.w, cap.h, 3.5); cairo_fill(cr);
        cairo_pattern_t* g = cairo_pattern_create_linear(cap.x, cap.y, cap.x, cap.y + cap.h);
        const double hv = hover ? 0.025 : 0.0;
        cairo_pattern_add_color_stop_rgb(g, 0.00, 0.975 + hv, 0.975 + hv, 0.965 + hv);
        cairo_pattern_add_color_stop_rgb(g, 0.55, 0.915 + hv, 0.915 + hv, 0.905 + hv);
        cairo_pattern_add_color_stop_rgb(g, 1.00, 0.760, 0.765, 0.770);
        cairo_set_source(cr, g); rrect(cr, cap.x, cap.y, cap.w, cap.h, 3.5); cairo_fill(cr); cairo_pattern_destroy(g);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.8); cairo_set_line_width(cr, 1);
        cairo_move_to(cr, cap.x + 3.5, cap.y + 1.2); cairo_line_to(cr, cap.x + cap.w - 3.5, cap.y + 1.2); cairo_stroke(cr);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.18); rrect(cr, cap.x + .5, cap.y + .5, cap.w - 1, cap.h - 1, 3.5); cairo_stroke(cr);
        cairo_set_source_rgba(cr, 0.12, 0.13, 0.15, dimLabel ? 0.45 : 1.0);
        text(cr, label, cap.x + cap.w / 2, cap.y + cap.h / 2 + size * 0.36, size, 0);
        if (lampW > 0) lamp(cr, lens, litCol, lit ? 1.0 : 0.0);
    }

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
    void commit(int p, float v)
    {
        setValue(p, v);
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
        return std::pow(std::clamp((v - s.min) / (s.max - s.min), 0.f, 1.f), 1.0 / s.skew);
    }
    void formatValue(int p, char* buf, size_t n) const
    {
        const float v = values[p];
        switch (p) {
        case pRange:  std::snprintf(buf, n, "%.1f dB", v); break;
        case pSpeed:  std::snprintf(buf, n, "%.2f s", v); break;
        case pOutput: std::snprintf(buf, n, "%+.1f dB", v); break;
        default:      std::snprintf(buf, n, "%.0f%%", v); break;
        }
    }
    static void fmtTime(double t, char* buf, size_t n)
    {
        const bool neg = t < 0; t = std::fabs(t);
        const int m = (int)(t / 60); const double s = t - m * 60;
        std::snprintf(buf, n, "%s%d:%04.1f", neg ? "-" : "", m, s);
    }
    bool bypassed() const { return values[pBypass] > 0.5f; }

    // first and last singing on the timeline (s)
    void singingSpan(double& a, double& b) const
    {
        a = cur->an.startTime(); b = cur->an.endTime();
        const Analysis& an = cur->an;
        size_t i0 = 0, i1 = an.frames();
        while (i0 < i1 && !an.on[i0]) ++i0;
        while (i1 > i0 && !an.on[i1 - 1]) --i1;
        if (i0 < i1) { a = an.frameTime(i0); b = an.frameTime(i1 - 1); }
    }

    // centre of the scrolling graph (timeline s)
    double graphCentre() const
    {
        if (havePos) return viewPos;
        if (!hasScan()) return 0;
        double a, b; singingSpan(a, b);
        return a + kWindow * 0.3;
    }

    // ---------------- panels ----------------
    void drawFaceplate(cairo_t* cr)
    {
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
        plate(4, 0, kFaceW - 8, kModTop - 4);
        plate(4, kModTop, kSeamX - 6, kFaceH - kModTop);
        plate(kSeamX + 2, kModTop, kFaceW - kSeamX - 6, kFaceH - kModTop);
        screw(cr, 16, 12, 0.4); screw(cr, kFaceW - 16, 12, 1.1);
        screw(cr, 16, kModTop - 16, 0.9); screw(cr, kFaceW - 16, kModTop - 16, 0.2);
    }

    void drawHeader(cairo_t* cr)
    {
        {   // logo mark: a fader with a ride arrow
            const double fx = 40;
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            rgb(cr, kAmber, 0.18); cairo_set_line_width(cr, 7);
            cairo_move_to(cr, fx, 14); cairo_line_to(cr, fx, 50); cairo_stroke(cr);
            rgb(cr, kAmber); cairo_set_line_width(cr, 2.4);
            cairo_move_to(cr, fx, 14); cairo_line_to(cr, fx, 50); cairo_stroke(cr);
            rrect(cr, fx - 8, 27, 16, 9, 2); cairo_fill(cr);
            cairo_set_source_rgb(cr, 0.10, 0.15, 0.25); cairo_rectangle(cr, fx - 6, 31, 12, 1.4); cairo_fill(cr);
            rgb(cr, kAmber); cairo_set_line_width(cr, 1.6);
            cairo_move_to(cr, fx + 13, 22); cairo_line_to(cr, fx + 13, 42); cairo_stroke(cr);
            cairo_move_to(cr, fx + 10, 25); cairo_line_to(cr, fx + 13, 21); cairo_line_to(cr, fx + 16, 25); cairo_stroke(cr);
            cairo_move_to(cr, fx + 10, 39); cairo_line_to(cr, fx + 13, 43); cairo_line_to(cr, fx + 16, 39); cairo_stroke(cr);
        }
        rgb(cr, kText); text(cr, "LevelBrew", 66, 33, 25, -1);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_ITALIC, CAIRO_FONT_WEIGHT_NORMAL);
        rgb(cr, kPrint); text(cr, "by Adin Walls", 68, 50, 11, -1);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

        // scan readout
        window(cr, kScanBox, 5);
        rgb(cr, kPrint); text(cr, "TRACK SCAN", kScanBox.x + 4, kScanBox.y - 3, 7, -1);
        char buf[96];
        if (!linked) { rgb(cr, kRed); text(cr, "Editor can't reach the plugin", kScanBox.x + 12, kScanBox.y + 22, 12, -1); }
        else if (!hasScan()) { rgb(cr, kDim); text(cr, "Nothing scanned yet", kScanBox.x + 12, kScanBox.y + 22, 12.5, -1); }
        else {
            char a[24], b[24], d[24]; double s0, s1; singingSpan(s0, s1);
            fmtTime(values[pOutScanned], d, sizeof d); fmtTime(s0, a, sizeof a); fmtTime(s1, b, sizeof b);
            std::snprintf(buf, sizeof buf, "%s scanned   vocal %s - %s", d, a, b);
            rgb(cr, kText); text(cr, buf, kScanBox.x + 12, kScanBox.y + 22, 12.5, -1);
        }
        const bool armed = nowT - clearArmT < 2.0;
        pushButton(cr, kClearBtn, armed ? "SURE?" : "CLEAR", armed, kRed, hoverBtn == 1, 11, 0, !hasScan());
        pushButton(cr, kBypassBtn, "BYPASS", bypassed(), kRed, hoverBtn == 2);
    }

    void drawStatus(cairo_t* cr)
    {
        const char* msg; C c = kDim; double on = 0;
        const int st = scanState();
        const double blink = 0.5 + 0.5 * std::sin(nowT * 8);
        if (!linked) { msg = "Open this plugin in a host that runs the editor alongside it (LV2, VST3 or VST2)."; c = kRed; on = 1; }
        else if (st == kScanEmpty || !hasScan()) {
            msg = running ? "Scanning the track..." : "Play the vocal track through once to scan it. Exporting the track scans it faster.";
            if (running) { c = kYellow; on = blink; }
        } else if (st == kScanScanning) { msg = "Scanning... this part rides from the next playback."; c = kYellow; on = blink; }
        else if (bypassed()) { msg = "Bypassed."; c = kRed; on = 1; }
        else if (riding()) { msg = "Riding. Any changes to the take are picked up as you play."; c = kAccent; on = 1; }
        else if (running) { msg = "Playing a part that hasn't been scanned yet."; c = kYellow; on = blink; }
        else { msg = "Scanned and ready. Press play."; c = kGreen; on = 1; }
        led(cr, 30, 79, 3.6, c, on);
        rgb(cr, kPrint); text(cr, msg, 42, 83, 11.5, -1);
    }

    void drawOverview(cairo_t* cr)
    {
        const Rect r = kOverview;
        window(cr, r, 4);
        if (!hasScan()) return;
        const Analysis& an = cur->an;
        cairo_save(cr); rrect(cr, r.x, r.y, r.w, r.h, 4); cairo_clip(cr);
        double t0, t1; singingSpan(t0, t1);
        t0 = std::max(an.startTime(), t0 - 2.0); t1 = std::min(an.endTime(), t1 + 2.0);
        if (t1 - t0 < 1) t1 = t0 + 1;
        const double mid = r.y + r.h / 2;
        const int W = (int)r.w;
        auto frameAt = [&](double t) { return (long)((t * an.sr - 2.0 * an.hop) / an.hop) - (long)an.origin; };
        for (int px = 0; px < W; ++px) {
            const long a = std::max(0L, frameAt(t0 + (t1 - t0) * px / W)), b = std::min((long)an.frames(), std::max(a + 1, frameAt(t0 + (t1 - t0) * (px + 1) / W)));
            float pk = 0; bool fresh = false, valid = false;
            for (long i = a; i < b; ++i) { pk = std::max(pk, an.peak[(size_t)i]); fresh |= an.fresh[(size_t)i] != 0; valid |= an.valid[(size_t)i] != 0; }
            if (!valid) continue;
            const double h = std::min(1.0, std::sqrt((double)pk)) * (r.h / 2 - 3);
            rgb(cr, fresh ? kYellow : kDim, fresh ? 0.45 : 0.55);
            cairo_rectangle(cr, r.x + px, mid - h, 1, 2 * h); cairo_fill(cr);
        }
        const double rng = std::max(1.0f, values[pRange]);
        rgb(cr, kAmber, bypassed() ? 0.35 : 0.9); cairo_set_line_width(cr, 1.3);
        for (int px = 0; px < W; ++px) {
            const double g = cur->atTime(t0 + (t1 - t0) * (px + 0.5) / W);
            const double y = mid - g / rng * (r.h / 2 - 4);
            if (px == 0) cairo_move_to(cr, r.x + px, y); else cairo_line_to(cr, r.x + px, y);
        }
        cairo_stroke(cr);
        const double c0 = graphCentre();
        const double x0 = r.x + (c0 - kWindow * 0.4 - t0) / (t1 - t0) * r.w, x1 = r.x + (c0 + kWindow * 0.6 - t0) / (t1 - t0) * r.w;
        cairo_set_source_rgba(cr, 1, 1, 1, 0.08); cairo_rectangle(cr, x0, r.y, x1 - x0, r.h); cairo_fill(cr);
        if (havePos) {
            rgb(cr, kText, running ? 0.9 : 0.5); cairo_set_line_width(cr, 1.5);
            const double px = r.x + (viewPos - t0) / (t1 - t0) * r.w;
            cairo_move_to(cr, px, r.y); cairo_line_to(cr, px, r.y + r.h); cairo_stroke(cr);
        }
        cairo_restore(cr);
    }

    void drawGraph(cairo_t* cr)
    {
        const Rect r = kGraph;
        window(cr, r, 6);
        rgb(cr, kPrint); text(cr, "RIDE", r.x + 4, r.y - 4, 7, -1);
        cairo_save(cr); rrect(cr, r.x, r.y, r.w, r.h, 6); cairo_clip(cr);

        const double topDb = -6, botDb = -54;
        auto yLev = [&](double db) { return r.y + 8 + (topDb - db) / (topDb - botDb) * (r.h - 16); };
        cairo_set_line_width(cr, 1);
        for (double db = -12; db >= botDb; db -= 12) {
            rgb(cr, kDim, 0.12); cairo_move_to(cr, r.x, yLev(db)); cairo_line_to(cr, r.x + r.w, yLev(db)); cairo_stroke(cr);
            char b[16]; std::snprintf(b, sizeof b, "%.0f", db); rgb(cr, kDim, 0.5); text(cr, b, r.x + 6, yLev(db) - 3, 8, -1);
        }
        const double c0 = graphCentre(), t0 = c0 - kWindow * 0.4, t1 = c0 + kWindow * 0.6;
        auto xT = [&](double t) { return r.x + (t - t0) / (t1 - t0) * r.w; };
        if (hasScan()) {
            const Analysis& an = cur->an;
            const long f0 = std::max(0L, (long)((t0 * an.sr - 2.0 * an.hop) / an.hop) - (long)an.origin - 1);
            const long f1 = std::min((long)an.frames() - 1, (long)((t1 * an.sr - 2.0 * an.hop) / an.hop) - (long)an.origin + 1);
            const bool byp = bypassed();
            auto frameG = [&](long i) { return byp ? 0.0 : (double)cur->g[(size_t)i]; };
            if (f1 > f0) {
                // before: filled grey envelope (scanned frames only)
                bool pen = false; double lastX = 0;
                for (long i = f0; i <= f1; ++i) {
                    const bool v = an.valid[(size_t)i];
                    const double x = xT(an.frameTime((size_t)i));
                    if (v && !pen) { cairo_move_to(cr, x, r.y + r.h); pen = true; }
                    if (v) { cairo_line_to(cr, x, yLev(std::max(botDb - 6, an.lev[(size_t)i]))); lastX = x; }
                    if (pen && (!v || i == f1)) { cairo_line_to(cr, lastX, r.y + r.h); cairo_close_path(cr); pen = false; }
                }
                rgb(cr, kDim, 0.28); cairo_fill(cr);
                // after: levelled vocal (singing only; yellow where it's only just been scanned)
                cairo_set_line_width(cr, 1.6);
                for (int pass = 0; pass < 2; ++pass) {
                    rgb(cr, pass ? kYellow : kAccent, pass ? 0.8 : 1.0);
                    pen = false;
                    for (long i = f0; i <= f1; ++i) {
                        if (!an.on[(size_t)i] || (an.fresh[(size_t)i] != 0) != (pass == 1)) { pen = false; continue; }
                        const double x = xT(an.frameTime((size_t)i)), y = yLev(an.lev[(size_t)i] + (pass ? 0.0 : frameG(i)));
                        if (pen) cairo_line_to(cr, x, y); else { cairo_move_to(cr, x, y); pen = true; }
                    }
                    cairo_stroke(cr);
                }
                // ride curve on its own +-range scale, centred in the window
                const double rng = std::max(1.0f, values[pRange]), mid = r.y + r.h / 2, half = r.h / 2 - 14;
                rgb(cr, kAmber, 0.25); cairo_set_line_width(cr, 1);
                cairo_move_to(cr, r.x, mid); cairo_line_to(cr, r.x + r.w, mid); cairo_stroke(cr);
                cairo_set_line_width(cr, 2.2);
                for (int pass = 0; pass < 2; ++pass) {
                    rgb(cr, kAmber, byp ? 0.3 : pass ? 0.3 : 1.0);
                    pen = false;
                    for (long i = f0; i <= f1; ++i) {
                        if (!cur->ride[(size_t)i] != (pass == 1)) { pen = false; continue; }
                        const double x = xT(an.frameTime((size_t)i)), y = mid - frameG(i) / rng * half;
                        if (pen) cairo_line_to(cr, x, y); else { cairo_move_to(cr, x, y); pen = true; }
                    }
                    cairo_stroke(cr);
                }
                char b[24]; std::snprintf(b, sizeof b, "+%.0f dB", rng);
                rgb(cr, kAmber, 0.8); text(cr, b, r.x + r.w - 6, mid - half - 2, 8, 1);
                std::snprintf(b, sizeof b, "-%.0f dB", rng); text(cr, b, r.x + r.w - 6, mid + half + 9, 8, 1);
            }
            rgb(cr, kDim, 0.8); text(cr, "original", r.x + 30, r.y + 16, 9, -1);
            rgb(cr, kAccent); text(cr, "levelled", r.x + 82, r.y + 16, 9, -1);
            rgb(cr, kAmber); text(cr, "ride", r.x + 134, r.y + 16, 9, -1);
            rgb(cr, kYellow, 0.8); text(cr, "scanning", r.x + 162, r.y + 16, 9, -1);
        } else {
            rgb(cr, kDim);
            text(cr, running ? "Scanning..." : "Play the vocal track to scan it", r.x + r.w / 2, r.y + r.h / 2, 13, 0);
        }
        const double px = xT(c0);
        rgb(cr, kText, havePos ? (running ? 0.9 : 0.45) : 0.15); cairo_set_line_width(cr, 1.5);
        cairo_move_to(cr, px, r.y); cairo_line_to(cr, px, r.y + r.h); cairo_stroke(cr);
        if (havePos) { char tb[32]; fmtTime(c0, tb, sizeof tb); rgb(cr, kDim); text(cr, tb, px + 5, r.y + r.h - 6, 9, -1); }
        cairo_restore(cr);
    }

    // the moving rider fader
    void drawFader(cairo_t* cr)
    {
        const Rect r = kFaderWin;
        window(cr, r, 6);
        const double rng = std::max(1.0f, values[pRange]);
        const bool live = riding();
        const double g = live || std::fabs(rideShown) > 0.05 ? std::clamp((double)rideShown, -rng, rng) : 0.0;
        const double sx = r.x + 62, sy0 = r.y + 22, sy1 = r.y + r.h - 22;
        auto yG = [&](double db) { return (sy0 + sy1) / 2 - db / rng * (sy1 - sy0) / 2; };
        cairo_set_source_rgb(cr, 0.01, 0.01, 0.02); rrect(cr, sx - 3, sy0, 6, sy1 - sy0, 3); cairo_fill(cr);
        rgb(cr, kPrint, 0.8); cairo_set_line_width(cr, 1);
        for (int i = -2; i <= 2; ++i) {
            const double db = rng * i / 2.0, y = yG(db);
            cairo_move_to(cr, sx - 26, y); cairo_line_to(cr, sx - 14, y); cairo_stroke(cr);
            char b[16]; std::snprintf(b, sizeof b, i == 0 ? "0" : std::fabs(db - std::round(db)) > 0.05 ? "%+.1f" : "%+.0f", db);
            text(cr, b, sx - 30, y + 3, 8, 1);
        }
        if (std::fabs(g) > 0.05) {
            rgb(cr, kAmber, 0.85);
            const double y0 = yG(0), y1 = yG(g);
            cairo_rectangle(cr, sx - 2, std::min(y0, y1), 4, std::fabs(y1 - y0)); cairo_fill(cr);
        }
        const double cy = yG(g);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.5); rrect(cr, sx - 17, cy - 9, 36, 20, 3); cairo_fill(cr);
        cairo_pattern_t* pg = cairo_pattern_create_linear(0, cy - 10, 0, cy + 10);
        cairo_pattern_add_color_stop_rgb(pg, 0, 0.92, 0.92, 0.93);
        cairo_pattern_add_color_stop_rgb(pg, 0.5, 0.70, 0.71, 0.73);
        cairo_pattern_add_color_stop_rgb(pg, 1, 0.45, 0.46, 0.48);
        cairo_set_source(cr, pg); rrect(cr, sx - 18, cy - 10, 36, 20, 3); cairo_fill(cr); cairo_pattern_destroy(pg);
        rgb(cr, live ? kAmber : C{0.2, 0.2, 0.22}); cairo_rectangle(cr, sx - 15, cy - 1, 30, 2); cairo_fill(cr);
        char b[24];
        std::snprintf(b, sizeof b, "%+.1f", g);
        rgb(cr, live ? kText : kDim); text(cr, b, r.x + 160, r.y + r.h / 2 + 4, 26, 0);
        rgb(cr, kDim); text(cr, "dB RIDE", r.x + 160, r.y + r.h / 2 + 22, 9, 0);
        const int st = scanState();
        const char* what = bypassed() ? "BYPASSED" : !hasScan() ? (running ? "SCANNING" : "NOT SCANNED")
                         : live ? (g > 0.3 ? "BOOSTING" : g < -0.3 ? "CUTTING" : "STEADY")
                         : running ? (st == kScanScanning ? "SCANNING" : "NOT RIDING") : "READY";
        rgb(cr, bypassed() ? kRed : kPrint); text(cr, what, r.x + 160, r.y + 40, 9, 0);
    }

    void drawMeter(cairo_t* cr, double x, double y, double w, const char* label, float db)
    {
        rgb(cr, kPrint); text(cr, label, x - 8, y + 8, 8, 1);
        cairo_set_source_rgb(cr, 0.02, 0.03, 0.05); rrect(cr, x, y, w, 10, 2); cairo_fill(cr);
        const double n = std::clamp((db + 60.0) / 60.0, 0.0, 1.0);
        const int segs = 30;
        for (int i = 0; i < segs; ++i) {
            const double f = (i + 0.5) / segs;
            const C c = f > 0.95 ? kRed : f > 0.8 ? kYellow : kGreen;
            rgb(cr, c, f <= n ? 0.95 : 0.12);
            cairo_rectangle(cr, x + 2 + i * (w - 4) / segs, y + 2, (w - 4) / segs - 1.5, 6); cairo_fill(cr);
        }
    }

    void drawModules(cairo_t* cr)
    {
        rgb(cr, kPrint); text(cr, "RIDER", 20, kModTop + 20, 10, -1);
        text(cr, "OUTPUT", kSeamX + 16, kModTop + 20, 10, -1);
        screw(cr, 16, kFaceH - 14, 0.7); screw(cr, kSeamX - 14, kFaceH - 14, 1.3);
        screw(cr, kSeamX + 14, kFaceH - 14, 0.1); screw(cr, kFaceW - 16, kFaceH - 14, 0.9);
        drawMeter(cr, 700, kModTop + 52, 130, "IN", meterIn);
        drawMeter(cr, 700, kModTop + 76, 130, "OUT", meterOut);
        rgb(cr, kDim); text(cr, "-60", 702, kModTop + 102, 7, -1); text(cr, "0 dB", 830, kModTop + 102, 7, 1);
    }

    C capColour(int p) const
    {
        switch (p) { case pStrength: return kCapPink; case pRange: return kCapBlue; case pSpeed: return kCapGrey; default: return kCapBlack; }
    }

    void drawKnob(cairo_t* cr, int k)
    {
        const int p = kKnobParams[k];
        const double cx = knobX(k), cy = kKnobY, r = kKnobR;
        const double a0 = 0.75 * M_PI, a1 = 2.25 * M_PI;
        const double av = a0 + (a1 - a0) * toNorm(p, values[p]);
        const bool active = dragKnob == k || hoverKnob == k;
        cairo_new_path(cr);
        rgb(cr, kPrint, 0.85); cairo_set_line_width(cr, 1.2); cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
        for (int i = 0; i <= 10; ++i) {
            const double a = a0 + (a1 - a0) * i / 10.0;
            const double r0 = r + 4, r1 = r + ((i % 5 == 0) ? 9.5 : 7);
            cairo_move_to(cr, cx + std::cos(a) * r0, cy + std::sin(a) * r0);
            cairo_line_to(cr, cx + std::cos(a) * r1, cy + std::sin(a) * r1);
        }
        cairo_stroke(cr);
        if (kSpecs[p].bipolar) { rgb(cr, kPrint); text(cr, "0", cx + std::cos(a0 + (a1 - a0) * toNorm(p, 0)) * (r + 15), cy + std::sin(a0 + (a1 - a0) * toNorm(p, 0)) * (r + 15) + 3, 7, 0); }
        cairo_set_source_rgba(cr, 0, 0, 0, 0.45); cairo_arc(cr, cx + 1.5, cy + 2.5, r + 2.5, 0, 2 * M_PI); cairo_fill(cr);
        cairo_pattern_t* sk = cairo_pattern_create_linear(cx, cy - r, cx, cy + r);
        cairo_pattern_add_color_stop_rgb(sk, 0, 0.30, 0.31, 0.33);
        cairo_pattern_add_color_stop_rgb(sk, 1, 0.07, 0.07, 0.08);
        cairo_set_source(cr, sk); cairo_arc(cr, cx, cy, r + 2, 0, 2 * M_PI); cairo_fill(cr); cairo_pattern_destroy(sk);
        const C cap = capColour(p);
        rgb(cr, mix(cap, C{0, 0, 0}, 0.28)); cairo_arc(cr, cx, cy, r, 0, 2 * M_PI); cairo_fill(cr);
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
        const bool lightCap = cap.r + cap.g + cap.b > 2.2;
        if (lightCap) cairo_set_source_rgb(cr, 0.12, 0.12, 0.14); else cairo_set_source_rgb(cr, 0.98, 0.98, 0.98);
        cairo_set_line_width(cr, 2.6); cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_move_to(cr, cx + std::cos(av) * r * 0.25, cy + std::sin(av) * r * 0.25);
        cairo_line_to(cr, cx + std::cos(av) * (r - 1.5), cy + std::sin(av) * (r - 1.5));
        cairo_stroke(cr);
        char buf[32]; formatValue(p, buf, sizeof buf);
        rgb(cr, active ? kAmber : kText); text(cr, buf, cx, cy + r + 26, 11, 0);
        rgb(cr, kPrint); text(cr, kSpecs[p].label, cx, cy + r + 41, 9, 0);
    }
};

} // namespace levelbrew
