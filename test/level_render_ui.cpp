// level_render_ui take.wav out.png posSec rideDb [align bypass hoverX hoverY]
#include "../src/LevelView.hpp"
#include <chrono>
using namespace levelbrew;
struct H : LevelView::Host {
    void setParam(int i, float v) override { printf("set %d=%g\n", i, v); }
    void beginEdit(int) override {} void endEdit(int) override {}
    void openFile() override { printf("open file\n"); }
};
int main(int argc, char** argv)
{
    H h; LevelView view(&h);
    const double pos = atof(argv[3]); const float ride = atof(argv[4]);
    const int align = argc > 5 ? atoi(argv[5]) : 2, byp = argc > 6 ? atoi(argv[6]) : 0;
    if (argv[1][0]) {
        view.setFile(argv[1]);
        view.setValue(pOutStatus, kReady); view.setValue(pOutFileVer, 1);
        while (view.isLoading()) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    view.setValue(pOutAlign, align); view.setValue(pOutOffset, 12.37f); view.setValue(pBypass, byp);
    for (int i = 0; i < 30; ++i) {
        view.setValue(pOutActivity, i % 1000); view.setValue(pOutPos, pos > 0 ? (float)pos : -1.f);
        view.setValue(pOutGain, ride); view.setValue(pOutIn, -14); view.setValue(pOutOut, -11);
        view.tick(i / 30.0);
    }
    if (argc > 8) view.mouseMove(atof(argv[7]), atof(argv[8]), false);
    const int W = (int)LevelView::kW, Hh = (int)LevelView::kH;
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, W, Hh);
    cairo_t* cr = cairo_create(s);
    view.draw(cr, W, Hh);
    cairo_surface_write_to_png(s, argv[2]);
    cairo_destroy(cr); cairo_surface_destroy(s);
}
