// level_render_ui take.wav out.png mode [posSec rideDb]
// mode: empty | scanning | riding — renders the LevelBrew editor in that state.
#include "../src/LevelView.hpp"
using namespace levelbrew;
struct H : LevelView::Host {
    void setParam(int i, float v) override { printf("set %d=%g\n", i, v); }
    void beginEdit(int) override {} void endEdit(int) override {}
};
int main(int argc, char** argv)
{
    H h; LevelView view(&h);
    const std::string mode = argv[3];
    const double region = 12.37;
    double pos = argc > 4 ? atof(argv[4]) : 0;
    float ride = argc > 5 ? (float)atof(argv[5]) : 0.f;
    Audio a; readWav(argv[1], a);
    const int64_t off = (int64_t)std::llround(region * a.sr);
    std::shared_ptr<Curve> c;
    int state = kScanEmpty; float scanned = 0, riding = 0;
    if (mode == "riding") {
        TrackScanner s; s.init(a.sr); s.feed(off, a.mono.data(), a.mono.size()); s.endRun();
        c = buildCurve(s.analysis(), RideSettings());
        state = kScanReady; scanned = (float)s.scannedSeconds(); riding = 1;
    } else if (mode == "scanning") {   // first playback, part-way through
        TrackScanner s; s.init(a.sr);
        const size_t n = (size_t)((pos - region) * a.sr);
        s.feed(0, std::vector<float>(off, 0.f).data(), (size_t)off);
        s.feed(off, a.mono.data(), n);
        c = buildCurve(s.analysis(), RideSettings());
        state = kScanScanning; scanned = (float)s.scannedSeconds(); ride = 0;
    }
    view.setCurve(c);
    view.setValue(pOutState, (float)state); view.setValue(pOutScanned, scanned);
    for (int i = 0; i < 30; ++i) {
        if (mode != "empty") view.setValue(pOutActivity, (float)(i % 1000));   // empty: transport stopped
        view.setValue(pOutPos, mode == "empty" ? -1.f : (float)pos);
        view.setValue(pOutGain, ride); view.setValue(pOutRiding, riding);
        view.setValue(pOutIn, -14); view.setValue(pOutOut, -11);
        view.tick(i / 30.0);
    }
    const int W = (int)LevelView::kW, Hh = (int)LevelView::kH;
    cairo_surface_t* sf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, W, Hh);
    cairo_t* cr = cairo_create(sf);
    view.draw(cr, W, Hh);
    cairo_surface_write_to_png(sf, argv[2]);
    cairo_destroy(cr); cairo_surface_destroy(sf);
}
