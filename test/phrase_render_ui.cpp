// phrase_render_ui vocal48k.f32 out.png [selected=1]
// Renders the Phrase Pull view after "playing" 40 s of a vocal, and drives the
// editor with mouse events: drag to mark a phrase, slider, note shift, delete.
#include "../src/TuneView.hpp"
#include <cairo/cairo.h>
#include <cstdio>
#include <cstdlib>
using namespace tunebrew;
struct H : TuneView::Host {
    TuneEngine* e; int calls = 0;
    void setParam(int, float) override {} void beginEdit(int) override {} void endEdit(int) override {}
    void regionsChanged(const RegionSet& r) override { ++calls; RegionSet b; b.decode(r.encode().c_str()); e->setRegions(b); }
};
int main(int argc, char** argv)
{
    FILE* f = fopen(argv[1], "rb"); std::vector<float> x; float v; while (fread(&v, 4, 1, f) == 1) x.push_back(v); fclose(f);
    const bool selected = argc > 3 ? atoi(argv[3]) != 0 : true;
    TuneEngine* e = new TuneEngine(); e->init(48000, 1); e->key = 8; e->scale = kMajor; e->refHz = 433.6;
    H h; h.e = e; TuneView view(&h);
    view.setValue(pKey, 8); view.setValue(pScale, kMajor); view.setValue(pRefHz, 433.6f);
    view.setEngine(e); view.setPhraseMode(true);
    bool ok = true;
    auto check = [&](bool c, const char* what) { printf("  %-52s %s\n", what, c ? "ok" : "FAIL"); ok = ok && c; };
    std::vector<float> o(1600); const size_t blk = 1600; double now = 0;
    auto play = [&](double from, double to) {
        for (size_t i = (size_t)(from * 48000); i + blk <= (size_t)(to * 48000) && i + blk <= x.size(); i += blk) {
            e->setTimeline(true, true, (double)i);
            const float* in[1] = { &x[i] }; float* out[1] = { o.data() }; e->process(in, out, blk);
            view.setValue(pOutPitch, e->meterHz); view.setValue(pOutNote, e->meterTarget); view.setValue(pOutCents, e->meterCents);
            view.setValue(pOutActivity, (float)((i / blk) % 1000)); now += blk / 48000.0; view.tick(now);
        }
    };
    play(0, 40);
    // stop: the view stays where the song stopped, song position in the middle
    e->setTimeline(true, false, 40.0 * 48000); now += 1.0; view.tick(now);
    const double X0 = TuneView::kCheek + 24, W = 588, Y = 240;          // graph left edge / width in window coords
    auto xs = [&](double sec) { return X0 + (sec - 28.0) * W / 12.0; };   // times below read as if the view began at 28 s
    const double d = view.getViewStart() - 28.0;   // shift for the expected values
    // mark two phrases by dragging
    view.mouseDown(xs(29.0), Y, false, 1.0); view.mouseMove(xs(31.0), Y, false); view.mouseMove(xs(32.5), Y, false); view.mouseUp(xs(32.5), Y);
    view.mouseDown(xs(36.0), Y, false, 2.0); view.mouseMove(xs(34.0), Y, false); view.mouseUp(xs(34.0), Y);
    check(view.getRegions().n == 2, "dragging marks two phrases");
    check(std::fabs(view.getRegions().r[0].start - 29.0 - d) < 0.05 && std::fabs(view.getRegions().r[0].end - 32.5 - d) < 0.05, "phrase covers the dragged stretch");
    // a drag that runs into a phrase stops at its edge
    view.mouseDown(xs(33.0), Y, false, 3.0); view.mouseMove(xs(35.5), Y, false); view.mouseUp(xs(35.5), Y);
    check(view.getRegions().n == 3 && std::fabs(view.getRegions().r[1].end - 34.0 - d) < 0.02, "phrases cannot overlap");
    // select the first, drag the pull slider to ~60 %, shift the note up, resize
    view.mouseDown(xs(30.0), Y, false, 4.0); view.mouseUp(xs(30.0), Y);
    view.mouseDown(TuneView::kCheek + 640 + 0.6 * 180, 234, false, 5.0); view.mouseUp(0, 0);
    check(std::fabs(view.getRegions().r[0].pull - 0.6f) < 0.011f, "slider sets the pull");
    view.scroll(xs(30.0), Y, 1.0);
    check(std::fabs(view.getRegions().r[0].pull - 0.65f) < 0.011f, "scrolling on a phrase changes the pull");
    view.mouseDown(TuneView::kCheek + 800, 284, false, 6.0); view.mouseUp(0, 0);
    check(view.getRegions().r[0].shift == 1, "note button shifts the target");
    view.mouseDown(xs(32.5), Y, false, 7.0); view.mouseMove(xs(32.0), Y, false); view.mouseUp(xs(32.0), Y);
    check(std::fabs(view.getRegions().r[0].end - 32.0 - d) < 0.05, "dragging an edge resizes");
    // delete the middle one
    view.mouseDown(xs(33.5), Y, false, 8.0); view.mouseUp(0, 0);
    view.mouseDown(TuneView::kCheek + 660, 336, false, 9.0); view.mouseUp(0, 0);
    check(view.getRegions().n == 2, "delete removes the selected phrase");
    check(e->regions().n == 2 && h.calls >= 6, "edits reach the engine");
    // replay so the corrected line shows the pulled phrases
    view.mouseDown(TuneView::kCheek + 640 + 180, 234, false, 10.0); view.mouseUp(0, 0);   // nothing selected: no-op
    view.mouseDown(xs(30.0), Y, false, 11.0); view.mouseUp(0, 0);
    view.mouseDown(TuneView::kCheek + 640 + 180, 234, false, 12.0); view.mouseUp(0, 0);   // back to 100 %
    view.mouseDown(TuneView::kCheek + 708, 284, false, 13.0); view.mouseUp(0, 0);         // note back to nearest
    e->init(48000, 1); play(26, 40);
    e->setTimeline(true, false, 40.0 * 48000); now += 1.0; view.tick(now);
    if (!selected) { view.select(-1); }
    const int Wd = (int)TuneView::kW, Hd = (int)TuneView::kH;
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, Wd, Hd); cairo_t* cr = cairo_create(surf);
    view.draw(cr, Wd, Hd); view.draw(cr, Wd, Hd);   // second draw uses the cached artwork
    cairo_surface_write_to_png(surf, argv[2]); cairo_destroy(cr); cairo_surface_destroy(surf);
    printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
