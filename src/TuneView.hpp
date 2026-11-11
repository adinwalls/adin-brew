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
        virtual void regionsChanged(const RegionSet&) {}   // phrase pull edits
        virtual ~Host() {}
    };

    explicit TuneView(Host* h) : host(h)
    {
        for (int i = 0; i < pCount; ++i) values[i] = kSpecs[i].def;
        hist.resize(kHistSize);
    }
    ~TuneView() { if (bg) cairo_surface_destroy(bg); if (ctl) cairo_surface_destroy(ctl); }
    TuneView(const TuneView&) = delete;
    TuneView& operator=(const TuneView&) = delete;

    // phrase pull: the engine supplies the pitch scan and song position
    void setEngine(const TuneEngine* e) { eng = e; dirty = true; }
    void setRegions(const RegionSet& r) { if (gdrag == dNone) { regs = r; if (sel >= regs.n) sel = -1; dirty = true; } }
    const RegionSet& getRegions() const { return regs; }
    double getViewStart() const { return viewStart; }
    void setPhraseMode(bool on) { phraseMode = on; dirty = true; }
    void select(int i) { sel = (i >= 0 && i < regs.n) ? i : -1; dirty = true; }

    // ---------------- inputs ----------------
    void setValue(int i, float v) { if (i >= 0 && i < pCount && values[i] != v) { values[i] = v; dirty = true; } }
    float getValue(int i) const { return values[i]; }

    // call at UI refresh rate with a monotonically increasing time in seconds
    // returns true when the picture changed and should be redrawn
    bool tick(double now)
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
        if (s.voiced) lastVoicedT = now;

        // phrase view: follow the song position
        bool moved = false;
        if (eng) {
            const double pos = eng->meterPos;
            const bool playing = eng->meterPlaying && running;
            if (gdrag == dNone && pos >= 0.0 && (playing || std::fabs(pos - lastPos) > 0.05)) {
                const double want = std::max(0.0, pos - 0.5 * kViewLen);
                if (playing || pos < viewStart || pos > viewStart + kViewLen) viewStart = want;
            }
            if (pos != lastPos) { lastPos = pos; moved = true; }
            const uint32_t v = eng->scan.version.load(std::memory_order_relaxed);
            if (v != seenScan) { seenScan = v; moved = true; }
        }
        if (confirmClear && now - confirmT > 2.5) { confirmClear = false; dirty = true; }

        const bool need = dirty || running || learning() || now - lastVoicedT < kWindow + 0.5
                          || (phraseMode && moved);
        dirty = false;
        // at most ~30 pictures a second
        if (need) pending = true;
        if (pending && now - lastFrameT >= 0.03) { pending = false; lastFrameT = now; return true; }
        return false;
    }

    // ---------------- drawing ----------------
    void draw(cairo_t* cr, double pixelW, double pixelH)
    {
        const double sc = std::min(pixelW / kW, pixelH / kH);
        // the wood, faceplates and printing never change: paint them once
        // into an off-screen picture and reuse it on every frame
        const int bw = (int)std::ceil(pixelW), bh = (int)std::ceil(pixelH);
        if (!bg || bw != bgW || bh != bgH || natural() != bgNatural) {
            if (bg) cairo_surface_destroy(bg);
            bg = cairo_surface_create_similar(cairo_get_target(cr), CAIRO_CONTENT_COLOR, bw, bh);
            bgW = bw; bgH = bh; bgNatural = natural(); ctlValid = false;
            cairo_t* b = cairo_create(bg);
            cairo_set_source_rgb(b, 0.05, 0.04, 0.035); cairo_paint(b);
            cairo_scale(b, sc, sc);
            cairo_select_font_face(b, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            drawCheek(b, 0, false);
            drawCheek(b, kW - kCheek, true);
            cairo_translate(b, kCheek, 0);
            drawFaceplate(b);
            drawModules(b);
            cairo_destroy(b);
        }
        // the header, key buttons and knobs change only when a control moves or
        // the sung note changes: keep them in a second picture on top of the first
        CtlKey key;
        std::memset(&key, 0, sizeof key);
        for (int i = 0; i <= pLearn; ++i) key.v[i] = values[i];
        key.hoverKnob = hoverKnob; key.dragKnob = dragKnob; key.hoverCell = hoverCell;
        key.sung = currentTarget() >= 0 ? currentTarget() % 12 : -1;
        key.leds = (voicedNow() ? 1 : 0) | ((voicedNow() && values[pOutVib] > 0.f) ? 2 : 0);
        key.pulse = learning() ? nowT : 0.0;
        if (!ctl || ctlW != bw || ctlH != bh || !ctlValid || std::memcmp(&key, &ctlKey, sizeof key) != 0) {
            if (ctl && (ctlW != bw || ctlH != bh)) { cairo_surface_destroy(ctl); ctl = nullptr; }
            if (!ctl) ctl = cairo_surface_create_similar(cairo_get_target(cr), CAIRO_CONTENT_COLOR, bw, bh);
            ctlW = bw; ctlH = bh; ctlKey = key; ctlValid = true;
            cairo_t* b = cairo_create(ctl);
            cairo_set_source_surface(b, bg, 0, 0); cairo_paint(b);
            cairo_scale(b, sc, sc);
            cairo_select_font_face(b, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_translate(b, kCheek, 0);
            drawHeader(b);
            drawKeyStrip(b);
            for (int k = 0; k < kNumKnobs; ++k) drawKnob(b, k);
            cairo_destroy(b);
        }
        cairo_save(cr);
        cairo_set_source_surface(cr, ctl, 0, 0); cairo_paint(cr);
        cairo_scale(cr, sc, sc);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_translate(cr, kCheek, 0);

        const bool phrases = phraseMode && !learning();
        if (phrases) drawPhraseGraph(cr); else drawGraph(cr);
        drawTabs(cr);
        if (phrases) drawPhrasePanel(cr); else drawReadout(cr);
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
        dirty = true;
        if (inRect(x, y, kScaleBox)) { menuOpen = true; return true; }
        if (inRect(x, y, kLiveTab)) { phraseMode = false; return true; }
        if (inRect(x, y, kPhraseTab)) { phraseMode = true; return true; }
        if (inRect(x, y, kClassicBtn)) { commit(pMode, 0.f); return true; }
        if (inRect(x, y, kNaturalBtn)) { commit(pMode, 1.f); return true; }
        if (inRect(x, y, kDetectBtn)) { commit(pLearn, learning() ? 0.f : 1.f); return true; }
        if (learning()) {
            if (inRect(x, y, applyRect()) && values[pOutKey] >= 0) { applyDetectedKey(); return true; }
            if (inRect(x, y, cancelRect())) { commit(pLearn, 0.f); return true; }
            if (inRect(x, y, kReadout)) return true;
        } else if (phraseMode) {
            if (inRect(x, y, kReadout)) { panelDown(x, y); return true; }
            if (inRect(x, y, kGraph)) { graphDown(x, y); return true; }
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
        if (gdrag != dNone) { graphUp(); return true; }
        if (dragKnob >= 0) { host->endEdit(kKnobParams[dragKnob]); dragKnob = -1; return true; }
        return false;
    }

    bool mouseMove(double x, double y, bool fine)
    {
        x -= kCheek;
        hoverKnob = knobAt(x, y);
        hoverCell = keyCellAt(x, y);
        hoverMenu = menuOpen ? menuItemAt(x, y) : -1;
        dirty = true;
        if (gdrag != dNone) { graphDrag(x); return true; }
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
        dirty = true;
        if (phraseMode && !learning() && inRect(x, y, kGraph)) {
            const int hit = regs.find(timeAt(x));
            if (hit >= 0) {
                sel = hit;
                setPull(std::round(regs.r[hit].pull * 20.f + (dy > 0 ? 1 : -1)) / 20.f);
            } else {
                viewStart = std::max(0.0, viewStart + (dy > 0 ? -1.0 : 1.0));
            }
            return true;
        }
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

    // phrase pull state
    enum GDrag { dNone, dCreate, dResizeL, dResizeR, dPan, dSlider };
    const TuneEngine* eng = nullptr;
    RegionSet regs;
    int sel = -1;
    bool phraseMode = false, confirmClear = false, dirty = true;
    GDrag gdrag = dNone;
    double viewStart = 0.0, lastPos = -1.0, dragT0 = 0.0, dragT1 = 0.0, confirmT = 0.0, lastVoicedT = -100.0;
    float pcenter = 60.f;
    uint32_t seenScan = 0;
    // cached static artwork
    cairo_surface_t* bg = nullptr;
    int bgW = 0, bgH = 0;
    bool bgNatural = true;
    struct CtlKey { float v[pLearn + 1]; int hoverKnob, dragKnob, hoverCell, sung, leds; double pulse; };
    cairo_surface_t* ctl = nullptr;
    CtlKey ctlKey;
    int ctlW = 0, ctlH = 0;
    bool ctlValid = false, pending = true;
    double lastFrameT = -1.0;

    // ---------------- layout (faceplate coords, 860 x 540) ----------------
    struct Rect { double x, y, w, h; };
    static constexpr Rect kScaleBox   = { 254, 14, 220, 34 };
    static constexpr Rect kDetectBtn  = { 486, 14, 108, 34 };
    static constexpr Rect kClassicBtn = { 612, 14, 104, 34 };
    static constexpr Rect kNaturalBtn = { 722, 14, 104, 34 };
    static constexpr Rect kKeyStrip   = { 92, 68, 744, 44 };
    static constexpr Rect kGraph      = { 24, 124, 588, 240 };
    static constexpr Rect kReadout    = { 624, 124, 212, 240 };
    static constexpr Rect kLiveTab    = { 76, 131, 46, 18 };
    static constexpr Rect kPhraseTab  = { 126, 131, 98, 18 };
    static constexpr Rect kOverview   = { 32, 344, 572, 12 };
    static constexpr double kPlotH = 214;      // phrase view: graph height above the overview strip
    static constexpr double kViewLen = 12.0;   // seconds shown in the phrase view
    static constexpr Rect kPullSlider = { 640, 226, 180, 16 };
    static constexpr Rect kNoteDown   = { 694, 272, 34, 24 };
    static constexpr Rect kNoteUp     = { 788, 272, 34, 24 };
    static constexpr Rect kDeleteBtn  = { 638, 322, 96, 28 };
    static constexpr Rect kClearBtn   = { 638, 322, 120, 28 };
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


    // ---------------- phrase pull ----------------
    double timeAt(double x) const { return viewStart + (x - kGraph.x) * kViewLen / kGraph.w; }
    double xAt(double sec) const { return kGraph.x + (sec - viewStart) * kGraph.w / kViewLen; }
    double scanSecs() const { return eng ? eng->scan.len.load(std::memory_order_relaxed) * PitchScan::kHop : 0.0; }
    double songSecs() const
    {
        double L = std::max(20.0, scanSecs());
        for (int i = 0; i < regs.n; ++i) L = std::max(L, regs.r[i].end);
        if (eng && eng->meterPos > 0) L = std::max(L, (double)eng->meterPos);
        return L;
    }
    void notify() { host->regionsChanged(regs); dirty = true; }

    void setPull(float v)
    {
        if (sel < 0) return;
        v = std::clamp(v, 0.f, 1.f);
        if (std::lround(v * 100.f) == std::lround(regs.r[sel].pull * 100.f)) return;
        regs.r[sel].pull = v;
        notify();
    }

    void sliderTo(double x)
    {
        setPull((float)(std::round(std::clamp((x - kPullSlider.x) / kPullSlider.w, 0.0, 1.0) * 100.0) / 100.0));
    }

    void panTo(double x)
    {
        const double frac = std::clamp((x - kOverview.x) / kOverview.w, 0.0, 1.0);
        viewStart = std::max(0.0, frac * songSecs() - 0.5 * kViewLen);
    }

    // free space around a time: the nearest phrase edges on either side
    void gap(double t, int skip, double& lo, double& hi) const
    {
        lo = 0.0; hi = 1e9;
        for (int i = 0; i < regs.n; ++i) {
            if (i == skip) continue;
            if (regs.r[i].end <= t + 1e-9) lo = std::max(lo, regs.r[i].end);
            if (regs.r[i].start >= t - 1e-9) hi = std::min(hi, regs.r[i].start);
        }
    }

    void graphDown(double x, double y)
    {
        if (y >= kOverview.y - 4) { gdrag = dPan; panTo(x); return; }
        // grab the edge of a phrase to resize it
        for (int i = 0; i < regs.n; ++i) {
            if (std::fabs(x - xAt(regs.r[i].start)) < 5) { sel = i; gdrag = dResizeL; return; }
            if (std::fabs(x - xAt(regs.r[i].end)) < 5)   { sel = i; gdrag = dResizeR; return; }
        }
        const double t = std::max(0.0, timeAt(x));
        const int hit = regs.find(t);
        if (hit >= 0) { sel = hit; return; }
        sel = -1; gdrag = dCreate; dragT0 = dragT1 = t;
    }

    void graphDrag(double x)
    {
        if (gdrag == dPan) { panTo(x); return; }
        if (gdrag == dSlider) { sliderTo(x); return; }
        const double t = std::max(0.0, timeAt(std::clamp(x, kGraph.x, kGraph.x + kGraph.w)));
        double lo, hi;
        if (gdrag == dCreate) {
            gap(dragT0, -1, lo, hi);
            dragT1 = std::clamp(t, lo, hi);
        } else if (sel >= 0) {
            PullRegion& r = regs.r[sel];
            gap(0.5 * (r.start + r.end), sel, lo, hi);
            if (gdrag == dResizeL) r.start = std::clamp(t, lo, r.end - 0.1);
            else r.end = std::clamp(t, r.start + 0.1, hi);
        }
    }

    void graphUp()
    {
        const GDrag g = gdrag;
        gdrag = dNone;
        if (g == dCreate) {
            const double a = std::min(dragT0, dragT1), b = std::max(dragT0, dragT1);
            if (b - a >= 0.15 && regs.n < RegionSet::kMax) {
                PullRegion r; r.start = a; r.end = b; r.pull = 1.f; r.shift = 0;
                regs.r[regs.n++] = r;
                regs.sort();
                sel = regs.find(0.5 * (a + b));
                notify();
            }
        } else if (g == dResizeL || g == dResizeR) {
            notify();
        }
        dirty = true;
    }

    void panelDown(double x, double y)
    {
        if (sel >= 0) {
            PullRegion& r = regs.r[sel];
            if (x >= kPullSlider.x - 8 && x < kPullSlider.x + kPullSlider.w + 8 && y >= kPullSlider.y - 8 && y < kPullSlider.y + kPullSlider.h + 8) {
                gdrag = dSlider; sliderTo(x);
            } else if (inRect(x, y, kNoteDown)) { r.shift = std::max(-4, r.shift - 1); notify(); }
            else if (inRect(x, y, kNoteUp))     { r.shift = std::min(4, r.shift + 1); notify(); }
            else if (inRect(x, y, kDeleteBtn)) {
                for (int i = sel; i + 1 < regs.n; ++i) regs.r[i] = regs.r[i + 1];
                --regs.n; sel = -1; notify();
            }
        } else if (regs.n > 0 && inRect(x, y, kClearBtn)) {
            if (confirmClear) { regs.n = 0; confirmClear = false; notify(); }
            else { confirmClear = true; confirmT = nowT; }
        }
    }

    static void fmtTime(double sec, char* buf, size_t n)
    {
        const int m = (int)(sec / 60.0);
        std::snprintf(buf, n, "%d:%04.1f", m, sec - 60.0 * m);
    }

    static C pullColour(float pull) { return mix(kAmber, kCapPink, std::clamp(pull, 0.f, 1.f)); }

    void drawTabs(cairo_t* cr)
    {
        const bool ph = phraseMode && !learning();
        const Rect tabs[2] = { kLiveTab, kPhraseTab };
        const char* names[2] = { "LIVE", "PHRASE PULL" };
        for (int i = 0; i < 2; ++i) {
            const Rect b = tabs[i];
            const bool on = (i == 1) == ph;
            if (on) rgb(cr, kAmber, 0.92); else cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
            rrect(cr, b.x, b.y, b.w, b.h, 4); cairo_fill(cr);
            if (on) cairo_set_source_rgb(cr, 0.08, 0.07, 0.05); else rgb(cr, kPrint, 0.9);
            text(cr, names[i], b.x + b.w / 2, b.y + 12.5, 9, 0);
        }
    }

    void drawPhraseGraph(cairo_t* cr)
    {
        const Rect g = kGraph;
        const double top = g.y, bot = g.y + kPlotH;
        window(cr, g, 8);
        cairo_save(cr);
        rrect(cr, g.x, g.y, g.w, g.h, 8); cairo_clip(cr);

        const PitchScan* sc = eng ? &eng->scan : nullptr;
        const int n = sc ? sc->len.load(std::memory_order_relaxed) : 0;
        const int i0 = std::max(0, (int)std::floor(viewStart / PitchScan::kHop));
        const int i1 = std::min(n, (int)std::ceil((viewStart + kViewLen) / PitchScan::kHop) + 1);

        // centre the view on what is sung in this stretch
        {
            double sum = 0; int cnt = 0;
            for (int i = i0; i < i1; ++i) if (sc->in[i]) { sum += PitchScan::unpack(sc->in[i]); ++cnt; }
            if (cnt > 8) {
                const float want = (float)(sum / cnt);
                if (std::fabs(want - pcenter) > 2.5f) pcenter = want;
            }
        }
        const double lane = g.h / kSpan;
        auto Y = [&](double midi) { return top + kPlotH / 2 - (midi - pcenter) * lane; };

        // note lanes
        char buf[48];
        const int lo = (int)std::floor(pcenter - kSpan / 2) - 1, hi = (int)std::ceil(pcenter + kSpan / 2) + 1;
        for (int m = lo; m <= hi; ++m) {
            const double yc = Y(m);
            if (inScale(m)) {
                cairo_set_source_rgba(cr, 0.30, 0.50, 0.75, 0.10);
                cairo_rectangle(cr, g.x, yc - lane / 2 + 0.5, g.w, lane - 1); cairo_fill(cr);
                if (yc > top + 30 && yc < bot - 4) {
                    const bool tonic = ((m % 12) + 12) % 12 == (int)values[pKey];
                    rgb(cr, tonic ? kAmber : kDim, tonic ? 0.95 : 0.75);
                    noteName(m, buf, sizeof buf);
                    text(cr, buf, g.x + 10, yc + 4, 10, -1);
                }
            }
            cairo_set_source_rgba(cr, 0.4, 0.55, 0.75, 0.10); cairo_set_line_width(cr, 1);
            cairo_move_to(cr, g.x, std::round(yc - lane / 2) + 0.5); cairo_line_to(cr, g.x + g.w, std::round(yc - lane / 2) + 0.5); cairo_stroke(cr);
        }
        // seconds grid
        for (int s = (int)std::ceil(viewStart); s < viewStart + kViewLen; ++s) {
            const double x = std::round(xAt(s)) + 0.5;
            cairo_set_source_rgba(cr, 0.4, 0.55, 0.75, s % 5 == 0 ? 0.22 : 0.09);
            cairo_move_to(cr, x, top); cairo_line_to(cr, x, bot); cairo_stroke(cr);
            if (s % 5 == 0) { fmtTime(s, buf, sizeof buf); buf[std::strlen(buf) - 2] = 0; rgb(cr, kDim, 0.8); text(cr, buf, x + 3, bot - 5, 8, -1); }
        }

        // phrases
        auto band = [&](double a, double b, float pull, int shift, bool selected, bool ghost) {
            const double xa = xAt(a), xb = xAt(b);
            if (xb < g.x || xa > g.x + g.w) return;
            const C c = pullColour(pull);
            rgb(cr, c, ghost ? 0.14 : 0.10 + 0.16 * pull + (selected ? 0.08 : 0.0));
            cairo_rectangle(cr, xa, top, xb - xa, kPlotH); cairo_fill(cr);
            rgb(cr, c, 0.95); cairo_rectangle(cr, xa, top, xb - xa, 3); cairo_fill(cr);
            cairo_set_line_width(cr, selected ? 1.6 : 1.0);
            if (selected) cairo_set_source_rgba(cr, 1, 1, 1, 0.9); else rgb(cr, c, 0.7);
            cairo_move_to(cr, std::round(xa) + .5, top); cairo_line_to(cr, std::round(xa) + .5, bot);
            cairo_move_to(cr, std::round(xb) - .5, top); cairo_line_to(cr, std::round(xb) - .5, bot); cairo_stroke(cr);
            if (selected) {     // grips
                cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
                rrect(cr, xa - 2.5, top + kPlotH / 2 - 12, 5, 24, 2); cairo_fill(cr);
                rrect(cr, xb - 2.5, top + kPlotH / 2 - 12, 5, 24, 2); cairo_fill(cr);
            }
            if (!ghost && xb - xa > 30) {
                if (shift) std::snprintf(buf, sizeof buf, "%.0f%%  %+d", pull * 100.f, shift);
                else std::snprintf(buf, sizeof buf, "%.0f%%", pull * 100.f);
                rgb(cr, kText, 0.95); text(cr, buf, std::max(xa, g.x) + 5, top + 40, 10, -1);
            }
        };
        for (int i = 0; i < regs.n; ++i) band(regs.r[i].start, regs.r[i].end, regs.r[i].pull, regs.r[i].shift, i == sel, false);
        if (gdrag == dCreate && std::fabs(dragT1 - dragT0) > 0.01)
            band(std::min(dragT0, dragT1), std::max(dragT0, dragT1), 1.f, 0, false, true);

        // the vocal: sung pitch (amber) and corrected pitch (teal)
        cairo_save(cr);
        cairo_rectangle(cr, g.x, top, g.w, kPlotH); cairo_clip(cr);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND); cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        for (int pass = 0; pass < 2 && sc; ++pass) {
            const std::vector<uint16_t>& v = pass == 0 ? sc->in : sc->out;
            cairo_set_line_width(cr, pass == 0 ? 1.6 : 2.2);
            rgb(cr, pass == 0 ? kAmber : kAccent, pass == 0 ? 0.85 : 1.0);
            bool pen = false; float last = 0;
            for (int i = i0; i < i1; ++i) {
                if (!v[i]) { pen = false; continue; }
                const float mv = PitchScan::unpack(v[i]);
                const double x = xAt(i * PitchScan::kHop), y = Y(mv);
                if (pen && std::fabs(mv - last) > 3.f) pen = false;
                if (!pen) { cairo_move_to(cr, x, y); cairo_line_to(cr, x + 0.01, y); pen = true; } else cairo_line_to(cr, x, y);
                last = mv;
            }
            cairo_stroke(cr);
        }
        cairo_restore(cr);

        // song position
        const double pos = eng ? (double)eng->meterPos : -1.0;
        if (pos >= viewStart && pos <= viewStart + kViewLen) {
            const double x = std::round(xAt(pos)) + 0.5;
            cairo_set_source_rgba(cr, 1, 1, 1, 0.85); cairo_set_line_width(cr, 1.2);
            cairo_move_to(cr, x, top); cairo_line_to(cr, x, bot); cairo_stroke(cr);
        }

        // overview of the whole song
        {
            const Rect o = kOverview;
            const double L = songSecs();
            cairo_set_source_rgba(cr, 1, 1, 1, 0.07); rrect(cr, o.x, o.y, o.w, o.h, 3); cairo_fill(cr);
            if (sc && n > 0) {      // where there is singing
                rgb(cr, kAmber, 0.55);
                const int cols = (int)o.w;
                for (int cx = 0; cx < cols; ++cx) {
                    const int a = (int)(cx * L / cols / PitchScan::kHop), b = (int)((cx + 1) * L / cols / PitchScan::kHop);
                    bool any = false;
                    for (int i = a; i < b && i < n; i += 4) if (sc->in[i]) { any = true; break; }
                    if (any) cairo_rectangle(cr, o.x + cx, o.y + 4, 1, o.h - 8);
                }
                cairo_fill(cr);
            }
            for (int i = 0; i < regs.n; ++i) {
                rgb(cr, pullColour(regs.r[i].pull), 0.9);
                cairo_rectangle(cr, o.x + regs.r[i].start / L * o.w, o.y + 1, std::max(2.0, (regs.r[i].end - regs.r[i].start) / L * o.w), o.h - 2);
                cairo_fill(cr);
            }
            cairo_set_source_rgba(cr, 1, 1, 1, 0.75); cairo_set_line_width(cr, 1);
            rrect(cr, o.x + std::min(viewStart / L, 1.0) * o.w + .5, o.y - 1.5, std::max(4.0, std::min(kViewLen / L, 1.0) * o.w), o.h + 3, 2); cairo_stroke(cr);
            if (pos >= 0 && pos <= L) { cairo_set_source_rgba(cr, 1, 1, 1, 0.9); cairo_rectangle(cr, o.x + pos / L * o.w - .5, o.y - 2, 1.5, o.h + 4); cairo_fill(cr); }
        }
        cairo_restore(cr);

        // legend
        cairo_set_line_width(cr, 3);
        rgb(cr, kAmber); cairo_move_to(cr, g.x + g.w - 196, g.y + 16); cairo_line_to(cr, g.x + g.w - 180, g.y + 16); cairo_stroke(cr);
        rgb(cr, kDim); text(cr, "SUNG", g.x + g.w - 175, g.y + 20, 9, -1);
        rgb(cr, kAccent); cairo_move_to(cr, g.x + g.w - 116, g.y + 16); cairo_line_to(cr, g.x + g.w - 100, g.y + 16); cairo_stroke(cr);
        rgb(cr, kDim); text(cr, "CORRECTED", g.x + g.w - 95, g.y + 20, 9, -1);

        if (!eng || eng->meterPos < 0) {
            rgb(cr, kDim, 0.9); text(cr, "Phrase Pull needs the song position from the host", g.x + g.w / 2, top + kPlotH / 2, 12, 0);
            text(cr, "(use the LV2, VST3 or VST2 version)", g.x + g.w / 2, top + kPlotH / 2 + 18, 10, 0);
        } else if (n == 0) {
            rgb(cr, kText, 0.85); text(cr, "Play the song once", g.x + g.w / 2, top + kPlotH / 2 - 4, 15, 0);
            rgb(cr, kDim, 0.9); text(cr, "The vocal's pitch is drawn here as it plays. Then drag across a phrase.", g.x + g.w / 2, top + kPlotH / 2 + 16, 10, 0);
        }
    }

    void drawPhrasePanel(cairo_t* cr)
    {
        const Rect r = kReadout;
        window(cr, r, 8);
        char buf[64], a[16], b[16];
        rgb(cr, kDim); text(cr, "PHRASE PULL", r.x + 14, r.y + 22, 9, -1);
        std::snprintf(buf, sizeof buf, "%d phrase%s", regs.n, regs.n == 1 ? "" : "s");
        text(cr, buf, r.x + r.w - 14, r.y + 22, 9, 1);

        if (sel < 0) {
            rgb(cr, kText, 0.9);
            text(cr, "Drag across a phrase", r.x + r.w / 2, r.y + 66, 13, 0);
            text(cr, "to pull it into key.", r.x + r.w / 2, r.y + 84, 13, 0);
            rgb(cr, kDim);
            text(cr, "Click a phrase to adjust it.", r.x + 14, r.y + 118, 10, -1);
            text(cr, "Drag its edges to resize.", r.x + 14, r.y + 134, 10, -1);
            text(cr, "Scroll on it: pull harder / softer.", r.x + 14, r.y + 150, 10, -1);
            text(cr, "Scroll elsewhere, or drag the", r.x + 14, r.y + 166, 10, -1);
            text(cr, "bottom strip, to move along.", r.x + 14, r.y + 182, 10, -1);
            if (regs.n > 0) pushButton(cr, kClearBtn, confirmClear ? "SURE?" : "CLEAR ALL", confirmClear, kRed, 10.5, 11);
            return;
        }
        const PullRegion& p = regs.r[sel];
        fmtTime(p.start, a, sizeof a); fmtTime(p.end, b, sizeof b);
        std::snprintf(buf, sizeof buf, "%s \xe2\x80\x93 %s", a, b);
        rgb(cr, kText, 0.9); text(cr, buf, r.x + r.w / 2, r.y + 44, 11, 0);

        const C c = pullColour(p.pull);
        std::snprintf(buf, sizeof buf, "%.0f%%", p.pull * 100.f);
        rgb(cr, c, 0.18); text(cr, buf, r.x + r.w / 2 + 1, r.y + 87, 38, 0);
        rgb(cr, c); text(cr, buf, r.x + r.w / 2, r.y + 86, 38, 0);

        // pull slider
        const Rect s = kPullSlider;
        cairo_set_source_rgb(cr, 0.02, 0.03, 0.05); rrect(cr, s.x - 2, s.y + 3, s.w + 4, s.h - 6, 5); cairo_fill(cr);
        rgb(cr, c, 0.9); rrect(cr, s.x, s.y + 5, std::max(4.0, s.w * p.pull), s.h - 10, 3); cairo_fill(cr);
        const double hx = s.x + s.w * p.pull;
        cairo_set_source_rgba(cr, 0, 0, 0, 0.5); cairo_arc(cr, hx + 1, s.y + s.h / 2 + 1.5, 8, 0, 2 * M_PI); cairo_fill(cr);
        cairo_set_source_rgb(cr, 0.93, 0.93, 0.92); cairo_arc(cr, hx, s.y + s.h / 2, 7.5, 0, 2 * M_PI); cairo_fill(cr);
        rgb(cr, kDim); text(cr, "AS SET", s.x - 2, s.y + 32, 8, -1); text(cr, "SNAP", s.x + s.w + 2, s.y + 32, 8, 1);

        // move the target note
        rgb(cr, kDim); text(cr, "NOTE", r.x + 16, kNoteDown.y + 16, 9, -1);
        pushButton(cr, kNoteDown, "\xe2\x96\xbc", false, kAmber, 10, 0.001);
        pushButton(cr, kNoteUp, "\xe2\x96\xb2", false, kAmber, 10, 0.001);
        if (p.shift == 0) std::snprintf(buf, sizeof buf, "nearest");
        else std::snprintf(buf, sizeof buf, "%d %s", std::abs(p.shift), p.shift > 0 ? "up" : "down");
        rgb(cr, p.shift ? kAmber : kText); text(cr, buf, (kNoteDown.x + kNoteDown.w + kNoteUp.x) / 2, kNoteDown.y + 16, 11, 0);

        pushButton(cr, kDeleteBtn, "DELETE", true, kRed, 10.5, 11);
    }

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
