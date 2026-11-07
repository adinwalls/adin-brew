// level_compare in.wav out_gain.f32 — scans the file as if played from timeline 0
// and writes the per-frame gain curve (float32) for comparison with rider.py.
#include "../src/LevelEngine.hpp"
#include <chrono>
using namespace levelbrew;
int main(int argc, char** argv)
{
    Audio a;
    if (!readWav(argv[1], a)) { printf("read error: %s\n", a.error.c_str()); return 1; }
    auto t0 = std::chrono::steady_clock::now();
    auto c = buildCurve(analyseAudio(a), RideSettings());
    auto t1 = std::chrono::steady_clock::now();
    FILE* f = fopen(argv[2], "wb"); fwrite(c->g.data(), 4, c->g.size(), f); fclose(f);
    printf("frames %zu, origin %lld, scan+curve %.0f ms\n", c->g.size(), (long long)c->an.origin,
           std::chrono::duration<double, std::milli>(t1 - t0).count());
}
