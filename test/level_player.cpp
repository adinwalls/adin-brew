// level_player in.wav — simulates a DAW playing the take placed at an unknown
// timeline position, checks that the player aligns itself and rides correctly.
#include "../src/LevelEngine.hpp"
using namespace levelbrew;
int main(int argc, char** argv)
{
    Audio a; if (!readWav(argv[1], a)) return 1;
    Analysis an; analyse(a, an);
    Curve* c = new Curve; c->sr = an.sr; c->duration = an.duration; c->hop = an.hop; c->win = an.win; c->g = gainCurve(an, RideSettings());
    const double sr = a.sr, regionStart = 12.37;      // where the user put the take (plugin doesn't know)
    Player p; p.init(sr); p.curve.store(c);
    const int B = 512;
    std::vector<float> in(B), out(B);
    int fails = 0;
    for (double playFrom : { 60.0, 140.0, 20.0 }) {    // user starts playback at several spots
        p.aligned.store(false); p.offsetSec.store(0);
        int64_t pos = (int64_t)(playFrom * sr);
        uint64_t since = p.ring.count.load();
        double alignedAfter = -1; AlignResult res;
        double err2 = 0; long nerr = 0;
        for (int b = 0; b < (int)(25 * sr / B); ++b, pos += B) {
            for (int i = 0; i < B; ++i) {
                const int64_t k = pos + i - (int64_t)(regionStart * sr);
                in[i] = (k >= 0 && k < (int64_t)a.mono.size()) ? a.mono[k] : 0.f;
            }
            const float* ip[1] = { in.data() }; float* op[1] = { out.data() };
            p.process(ip, op, 1, B, true, pos);
            // "worker": try aligning every ~0.5 s
            if (!p.aligned.load() && b % 43 == 0) {
                std::vector<float> h; double t0;
                const int m = p.ring.latest(h, t0, 2000, since);
                res = alignSearch(an, h.data(), m, t0);
                if (res.ok) { p.offsetSec.store(res.offsetSec); p.aligned.store(true); alignedAfter = (pos - playFrom * sr) / sr; }
            }
            if (p.aligned.load() && (pos - playFrom * sr) / sr > alignedAfter + 0.35) {   // compare applied gain to the ideal curve at the block end
                const double t = (pos + B) / sr - regionStart;
                const double want = c->at(t), got = p.meterGain;
                err2 += (want - got) * (want - got); ++nerr;
            }
        }
        const double rms = nerr ? std::sqrt(err2 / nerr) : 99;
        printf("play from %5.1fs: aligned after %.1fs, offset %.3fs (true %.2f), score %.3f, gain error rms %.3f dB\n",
               playFrom, alignedAfter, res.offsetSec, regionStart, res.score, rms);
        if (alignedAfter < 0 || std::fabs(res.offsetSec - regionStart) > 0.011 || rms > 0.1) ++fails;
    }
    printf(fails ? "FAIL\n" : "PASS\n");
    return fails;
}
