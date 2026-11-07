// LevelBrew Vocal Rider — DSP side.
//
// A background worker reads the chosen vocal WAV (no playback needed), analyses
// the whole take and publishes a gain curve. During playback the audio thread
// looks up the gain for the current timeline position, so the ride is visible
// and audible in real time. The worker also re-scans the file whenever it
// changes on disk, and works out where the take sits on the timeline by
// matching what it hears against the file.
#include "DistrhoPlugin.hpp"
#include "../../src/LevelParams.hpp"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <sys/stat.h>

START_NAMESPACE_DISTRHO

using namespace levelbrew;

class LevelBrewPlugin : public Plugin
{
public:
    LevelBrewPlugin()
        : Plugin(pCount, 0, 2)
    {
        for (int i = 0; i < pCount; ++i) values[i] = kSpecs[i].def;
        player.init(getSampleRate());
        if (!isDummyInstance()) worker = std::thread([this] { workerLoop(); });
    }

    ~LevelBrewPlugin() override
    {
        { std::lock_guard<std::mutex> l(mx); quit = true; }
        cv.notify_all();
        if (worker.joinable()) worker.join();
        delete player.curve.load();
        for (auto& r : retired) delete r.first;
    }

protected:
    const char* getLabel() const override { return "LevelBrewVocalRider"; }
    const char* getDescription() const override
    {
        return "Vocal rider for recorded takes: reads the whole vocal file, then rides its level smoothly during playback.";
    }
    const char* getMaker() const override { return "Adin Walls"; }
    const char* getLicense() const override { return "GPL-3.0-or-later"; }
    const char* getHomePage() const override { return "https://github.com/adinwalls/adin-brew"; }
    uint32_t getVersion() const override { return d_version(1, 0, 0); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& p) override
    {
        const ParamSpec& s = kSpecs[index];
        p.hints = kParameterIsAutomatable;
        p.ranges.min = s.min; p.ranges.max = s.max; p.ranges.def = s.def;
        p.unit = s.unit;
        switch (index) {
        case pStrength:
            p.name = "Strength"; p.symbol = "strength";
            p.description = "How much of the level difference to even out (100 = fully level)";
            break;
        case pRange:
            p.name = "Range"; p.symbol = "range";
            p.description = "Largest boost or cut the rider may apply";
            break;
        case pSpeed:
            p.name = "Speed"; p.symbol = "speed";
            p.description = "How slowly the rider moves: small = follows words, large = follows phrases";
            break;
        case pOutput:
            p.name = "Output"; p.symbol = "output";
            break;
        case pBypass:
            p.name = "Bypass"; p.symbol = "bypass";
            p.hints |= kParameterIsBoolean | kParameterIsInteger;
            p.designation = kParameterDesignationBypass;
            break;
        case pRealign:
            p.name = "Re-align"; p.symbol = "realign";
            p.hints = kParameterIsTrigger | kParameterIsInteger | kParameterIsHidden;
            p.description = "Forget the take's timeline position and find it again during playback";
            break;
        case pOutGain:      p.name = "Ride"; p.symbol = "ride"; p.hints = kParameterIsOutput; break;
        case pOutPos:       p.name = "Take Position"; p.symbol = "position"; p.hints = kParameterIsOutput | kParameterIsHidden; break;
        case pOutStatus:    p.name = "File Status"; p.symbol = "file_status"; p.hints = kParameterIsOutput | kParameterIsInteger | kParameterIsHidden; break;
        case pOutAlign:     p.name = "Alignment"; p.symbol = "alignment"; p.hints = kParameterIsOutput | kParameterIsInteger | kParameterIsHidden; break;
        case pOutOffset:    p.name = "Take Start"; p.symbol = "take_start"; p.hints = kParameterIsOutput | kParameterIsHidden; break;
        case pOutIn:        p.name = "Input Peak"; p.symbol = "in_peak"; p.hints = kParameterIsOutput | kParameterIsHidden; break;
        case pOutOut:       p.name = "Output Peak"; p.symbol = "out_peak"; p.hints = kParameterIsOutput | kParameterIsHidden; break;
        case pOutActivity:  p.name = "Activity"; p.symbol = "activity"; p.hints = kParameterIsOutput | kParameterIsInteger | kParameterIsHidden; break;
        case pOutFileVer:   p.name = "File Version"; p.symbol = "file_version"; p.hints = kParameterIsOutput | kParameterIsInteger | kParameterIsHidden; break;
        }
    }

    void initState(uint32_t index, State& state) override
    {
        if (index == 0) {
            state.key = "file"; state.label = "Vocal File"; state.defaultValue = "";
            state.hints = kStateIsFilenamePath;
            state.description = "The recorded vocal take (WAV) to ride";
        } else {
            state.key = "offset"; state.label = "Take Start"; state.defaultValue = "";
            state.hints = kStateIsHostReadable | kStateIsOnlyForDSP;
        }
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, "file") == 0) { std::lock_guard<std::mutex> l(mx); return String(reqPath.c_str()); }
        if (std::strcmp(key, "offset") == 0 && player.aligned.load()) {
            char buf[64]; std::snprintf(buf, sizeof buf, "%.6f", player.offsetSec.load());
            return String(buf);
        }
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "file") == 0) {
            {
                std::lock_guard<std::mutex> l(mx);
                if (reqPath == value) return;
                if (!reqPath.empty()) { restoredOffset = false; player.aligned.store(false); }   // user picked another take
                reqPath = value; ++reqSerial;
            }
            cv.notify_all();
        } else if (std::strcmp(key, "offset") == 0) {
            if (value && *value) { player.offsetSec.store(std::atof(value)); player.aligned.store(true); restoredOffset = true; }
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index) {
        case pOutGain:     return player.meterGain;
        case pOutPos:      return player.meterPos;
        case pOutStatus:   return (float)status.load();
        case pOutAlign:    return status.load() != kReady ? 0.f : player.aligned.load() ? (float)kAlignLocked : (float)kAlignListening;
        case pOutOffset:   return (float)player.offsetSec.load();
        case pOutIn:       return std::max(-90.f, player.meterIn);
        case pOutOut:      return std::max(-90.f, player.meterOut);
        case pOutActivity: return (float)activity;
        case pOutFileVer:  return (float)fileVer.load();
        case pRealign:     return 0.f;
        }
        return index < pCount ? values[index] : 0.f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= pOutGain) return;
        if (index == pRealign) {
            if (value > 0.5f) { player.aligned.store(false); realign.store(true); cv.notify_all(); }
            return;
        }
        values[index] = value;
        if (index == pBypass) player.bypass.store(value > 0.5f);
        else if (index == pOutput) player.outGainDb.store(value);
        else curveDirty.store(true);
    }

    void activate() override { player.init(getSampleRate()); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const TimePosition& tp = getTimePosition();
        player.process(inputs, outputs, 2, frames, tp.playing, (int64_t)tp.frame);
        activityAcc += frames;
        if (activityAcc >= 2048) { activityAcc = 0; activity = (activity + 1) % 1000; }
    }

private:
    // ------------------------------------------------------------- worker thread
    void publish(Curve* c)
    {
        const Curve* old = player.curve.exchange(c, std::memory_order_acq_rel);
        if (old) retired.emplace_back(old, std::chrono::steady_clock::now());
    }

    void buildCurve()
    {
        Curve* c = new Curve;
        c->sr = an.sr; c->duration = an.duration; c->hop = an.hop; c->win = an.win;
        float v[pCount]; std::memcpy(v, values, sizeof v);
        c->g = gainCurve(an, settingsFrom(v));
        publish(c);
    }

    bool loadFile(const std::string& path)
    {
        Audio a;
        if (!readWav(path.c_str(), a)) return false;
        analyse(a, an);
        haveAnalysis = true;
        buildCurve();
        return true;
    }

    static bool fileStamp(const std::string& p, int64_t& mtime, int64_t& size)
    {
        struct stat st;
        if (::stat(p.c_str(), &st) != 0) return false;
        mtime = (int64_t)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
        size = (int64_t)st.st_size;
        return true;
    }

    void workerLoop()
    {
        using clock = std::chrono::steady_clock;
        uint64_t loadedSerial = 0;
        std::string path;
        int64_t loadedM = 0, loadedS = 0, pendM = -1, pendS = -1;
        clock::time_point nextPoll = clock::now(), nextAlign = clock::now(), pendSince;
        uint64_t seenEpoch = player.ring.epoch.load(), epochCount = player.ring.count.load();
        int verifyFails = 0;
        std::vector<float> hist;

        for (;;) {
            uint64_t serial;
            {
                std::unique_lock<std::mutex> l(mx);
                cv.wait_for(l, std::chrono::milliseconds(100));
                if (quit) return;
                serial = reqSerial; path = reqPath;
            }
            const auto now = clock::now();

            // 1) a new file was chosen
            if (serial != loadedSerial) {
                loadedSerial = serial;
                if (!restoredOffset) player.aligned.store(false);   // keep a position restored from the session
                if (path.empty()) { publish(nullptr); haveAnalysis = false; status.store(kNoFile); continue; }
                status.store(kLoading);
                fileStamp(path, loadedM, loadedS);
                if (loadFile(path)) { status.store(kReady); fileVer.fetch_add(1); }
                else { publish(nullptr); haveAnalysis = false; status.store(kError); }
                pendM = pendS = -1;
                nextPoll = now + std::chrono::seconds(1);
                continue;
            }

            // 2) watch the file: re-scan once it has changed and stopped changing
            if (!path.empty() && now >= nextPoll) {
                nextPoll = now + std::chrono::seconds(1);
                int64_t m, s;
                if (!fileStamp(path, m, s)) { if (status.load() == kReady) { status.store(kError); } }
                else if (m != loadedM || s != loadedS || status.load() == kError) {
                    if (m != pendM || s != pendS) { pendM = m; pendS = s; pendSince = now; }
                    else if (now - pendSince >= std::chrono::milliseconds(1500)) {
                        // keep riding with the old curve until the new one is ready
                        if (loadFile(path)) { status.store(kReady); fileVer.fetch_add(1); loadedM = m; loadedS = s; verifyFails = 0; }
                        else if (status.load() != kReady) status.store(kError);
                        pendM = pendS = -1;
                    }
                }
            }

            // 3) settings changed: rebuild the curve (cheap)
            if (curveDirty.exchange(false) && haveAnalysis) buildCurve();

            // 4) alignment: listen to playback and find / verify the take's position
            if (realign.exchange(false)) verifyFails = 0;
            const uint64_t ep = player.ring.epoch.load();
            if (ep != seenEpoch) { seenEpoch = ep; epochCount = player.ring.count.load(); }
            if (haveAnalysis && now >= nextAlign) {
                double t0;
                if (!player.aligned.load()) {
                    nextAlign = now + std::chrono::milliseconds(500);
                    const int m = player.ring.latest(hist, t0, 2000, epochCount);
                    const AlignResult r = alignSearch(an, hist.data(), m, t0);
                    if (r.ok) { player.offsetSec.store(r.offsetSec); player.aligned.store(true); verifyFails = 0; }
                } else {
                    // a region moved or a different take is playing? check every 2 s
                    nextAlign = now + std::chrono::seconds(2);
                    const int m = player.ring.latest(hist, t0, 500, epochCount);
                    if (m >= 300 && hasSinging(hist.data(), m)) {
                        const long d = std::lround((t0 - player.offsetSec.load()) / kHopSec);
                        const double sc = alignScoreAt(an.alev, hist.data(), m, d);
                        verifyFails = sc < 0.6 ? verifyFails + 1 : 0;
                        if (verifyFails >= 2) { player.aligned.store(false); verifyFails = 0; }
                    }
                }
            }

            // 5) free curves the audio thread can no longer be using
            while (!retired.empty() && now - retired.front().second > std::chrono::seconds(2)) {
                delete retired.front().first;
                retired.pop_front();
            }
        }
    }

    Player player;
    Analysis an;              // worker thread only
    bool haveAnalysis = false;
    std::deque<std::pair<const Curve*, std::chrono::steady_clock::time_point>> retired;

    mutable std::mutex mx;
    std::condition_variable cv;
    std::string reqPath;
    uint64_t reqSerial = 0;
    bool quit = false;
    std::atomic<bool> restoredOffset{false}, curveDirty{false}, realign{false};
    std::atomic<int> status{kNoFile};
    std::atomic<int> fileVer{0};
    std::thread worker;

    float values[pCount];
    uint32_t activityAcc = 0;
    int activity = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelBrewPlugin)
};

Plugin* createPlugin()
{
    return new LevelBrewPlugin();
}

END_NAMESPACE_DISTRHO
