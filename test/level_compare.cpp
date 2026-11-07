// level_compare in.wav out_gain.f32 [strength range speed]
// Writes the per-frame gain curve (float32) for comparison with rider.py.
#include "../src/LevelEngine.hpp"
#include <chrono>
using namespace levelbrew;
int main(int argc, char** argv)
{
    auto t0 = std::chrono::steady_clock::now();
    Audio a;
    if (!readWav(argv[1], a)) { printf("read error: %s\n", a.error.c_str()); return 1; }
    auto t1 = std::chrono::steady_clock::now();
    Analysis an; analyse(a, an);
    auto t2 = std::chrono::steady_clock::now();
    RideSettings rs;
    if (argc > 5) { rs.strength = atof(argv[3]); rs.range = atof(argv[4]); rs.speed = atof(argv[5]); }
    std::vector<float> g = gainCurve(an, rs);
    auto t3 = std::chrono::steady_clock::now();
    FILE* f = fopen(argv[2], "wb"); fwrite(g.data(), 4, g.size(), f); fclose(f);
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    printf("sr %.0f ch %d dur %.1fs frames %zu timeref %d | read %.0f ms, analyse %.0f ms, curve %.1f ms\n",
           a.sr, a.channels, an.duration, g.size(), (int)a.hasTimeRef, ms(t0, t1), ms(t1, t2), ms(t2, t3));
}
