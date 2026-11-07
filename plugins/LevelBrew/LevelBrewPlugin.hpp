// LevelBrew Vocal Rider — DSP side.
//
// The plugin scans the audio on its own track as the DAW plays (or exports) it.
// A worker thread turns what it hears into scan data laid out on the timeline,
// builds a smooth gain curve over the whole scanned take, and publishes it.
// The audio thread applies the curve at the current timeline position, so from
// the second playback on, the vocal is ridden in real time. Every playback
// refreshes the scan; parts that changed are re-scanned and ridden next pass.
#pragma once
#include "DistrhoPlugin.hpp"
#include "../../src/LevelParams.hpp"
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>

START_NAMESPACE_DISTRHO

class LevelBrewPlugin : public Plugin
{
public:
    LevelBrewPlugin()
        : Plugin(levelbrew::pCount, 0, 1)
    {
        using namespace levelbrew;
        for (int i = 0; i < pCount; ++i) values[i] = kSpecs[i].def;
        player.init(getSampleRate());
        scanner.init(getSampleRate());
        if (!isDummyInstance()) worker = std::thread([this] { workerLoop(); });
    }

    ~LevelBrewPlugin() override
    {
        quit.store(true);
        if (worker.joinable()) worker.join();
    }

    // for the editor (same process): the latest scan + ride curve
    std::shared_ptr<const levelbrew::Curve> display() const
    {
        std::lock_guard<std::mutex> l(dispMx);
        return disp;
    }
    uint32_t displayVersion() const { return dispVer.load(); }

protected:
    const char* getLabel() const override { return "LevelBrewVocalRider"; }
    const char* getDescription() const override
    {
        return "Vocal rider: scans the vocal on its track as you play it, then rides its level smoothly on every playback.";
    }
    const char* getMaker() const override { return "Adin Walls"; }
    const char* getLicense() const override { return "GPL-3.0-or-later"; }
    const char* getHomePage() const override { return "https://github.com/adinwalls/adin-brew"; }
    uint32_t getVersion() const override { return d_version(2, 0, 0); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& p) override
    {
        using namespace levelbrew;
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
        case pClear:
            p.name = "Clear Scan"; p.symbol = "clear_scan";
            p.hints = kParameterIsTrigger | kParameterIsInteger | kParameterIsHidden;
            p.description = "Forget the scan; the next playback scans the track again";
            break;
        case pOutGain:     p.name = "Ride"; p.symbol = "ride"; p.hints = kParameterIsOutput; break;
        case pOutPos:      p.name = "Position"; p.symbol = "position"; p.hints = kParameterIsOutput | kParameterIsHidden; break;
        case pOutState:    p.name = "Scan State"; p.symbol = "scan_state"; p.hints = kParameterIsOutput | kParameterIsInteger | kParameterIsHidden; break;
        case pOutScanned:  p.name = "Scanned"; p.symbol = "scanned"; p.hints = kParameterIsOutput | kParameterIsHidden; break;
        case pOutIn:       p.name = "Input Peak"; p.symbol = "in_peak"; p.hints = kParameterIsOutput | kParameterIsHidden; break;
        case pOutOut:      p.name = "Output Peak"; p.symbol = "out_peak"; p.hints = kParameterIsOutput | kParameterIsHidden; break;
        case pOutActivity: p.name = "Activity"; p.symbol = "activity"; p.hints = kParameterIsOutput | kParameterIsInteger | kParameterIsHidden; break;
        case pOutRiding:   p.name = "Riding"; p.symbol = "riding"; p.hints = kParameterIsOutput | kParameterIsInteger | kParameterIsHidden; break;
        }
    }

    void initState(uint32_t, State& state) override
    {
        state.key = "scan"; state.label = "Track Scan"; state.defaultValue = "";
        state.hints = kStateIsOnlyForDSP;
        state.description = "Level data of the scanned vocal (saved with the session)";
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, "scan") != 0) return String();
        std::lock_guard<std::mutex> l(scanMx);
        return String(scanner.encode().c_str());
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "scan") != 0 || value == nullptr || *value == '\0') return;
        std::lock_guard<std::mutex> l(scanMx);
        scanner.decode(value);   // ignored if the session's sample rate changed
    }

    float getParameterValue(uint32_t index) const override
    {
        using namespace levelbrew;
        switch (index) {
        case pOutGain:     return player.meterGain;
        case pOutPos:      return player.meterPos;
        case pOutState:    return (float)scanState.load();
        case pOutScanned:  return scannedSec.load();
        case pOutIn:       return std::max(-90.f, player.meterIn);
        case pOutOut:      return std::max(-90.f, player.meterOut);
        case pOutActivity: return (float)activity;
        case pOutRiding:   return player.meterRiding ? 1.f : 0.f;
        case pClear:       return 0.f;
        }
        return index < pCount ? values[index] : 0.f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        using namespace levelbrew;
        if (index >= pOutGain) return;
        if (index == pClear) { if (value > 0.5f) clearReq.store(true); return; }
        values[index] = value;
        if (index == pBypass) player.bypass.store(value > 0.5f);
        else if (index == pOutput) player.outGainDb.store(value);
        else settingsDirty.store(true);
    }

    void activate() override
    {
        player.init(getSampleRate());
        newRate.store(getSampleRate());
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const TimePosition& tp = getTimePosition();
        player.process(inputs, outputs, 2, frames, tp.playing, (int64_t)tp.frame);
        activityAcc += frames;
        if (activityAcc >= 2048) { activityAcc = 0; activity = (activity + 1) % 1000; }
    }

private:
    // ------------------------------------------------------------- worker thread
    void publish(std::shared_ptr<levelbrew::Curve> c)
    {
        const levelbrew::Curve* old = player.curve.exchange(c.get(), std::memory_order_acq_rel);
        (void)old;
        retired.emplace_back(c, std::chrono::steady_clock::now());   // keeps it alive
        { std::lock_guard<std::mutex> l(dispMx); disp = c; }
        dispVer.fetch_add(1);
    }

    void workerLoop()
    {
        using namespace levelbrew;
        using clock = std::chrono::steady_clock;
        clock::time_point lastData = clock::now(), lastBuild = clock::now() - std::chrono::seconds(1);
        bool first = true;
        while (!quit.load()) {
            bool busy = false;
            {
                std::lock_guard<std::mutex> l(scanMx);
                const double r = newRate.exchange(0);
                if (r > 0) scanner.init(r);   // clears the scan only if the rate really changed
                if (clearReq.exchange(false)) scanner.clear();
                if (player.fifo.takeOverflow()) scanner.endRun();
                const size_t got = player.fifo.drain([&](int64_t p, const float* x, size_t n) { scanner.feed(p, x, n); });
                const auto now = clock::now();
                if (got) { lastData = now; busy = true; }
                else if (scanner.running() && now - lastData > std::chrono::milliseconds(250)) scanner.endRun();   // transport stopped

                const bool settings = settingsDirty.exchange(false);
                if (first || settings || scanner.urgent() || (scanner.needsRebuild() && (!scanner.running() || now - lastBuild > std::chrono::milliseconds(400)))) {
                    float v[pCount]; std::memcpy(v, values, sizeof v);
                    publish(buildCurve(scanner.analysis(), settingsFrom(v)));
                    scannedSec.store((float)scanner.scannedSeconds());
                    lastBuild = now; first = false;
                }
                scanState.store(scannedSec.load() <= 0.f ? kScanEmpty : scanner.scanningFresh() ? kScanScanning : kScanReady);
            }
            // free curves the audio thread can no longer be using (keep the newest)
            const auto now = clock::now();
            while (retired.size() > 1 && now - retired[1].second > std::chrono::seconds(2)) retired.pop_front();
            std::this_thread::sleep_for(std::chrono::milliseconds(busy ? 5 : 30));
        }
    }

    levelbrew::Player player;
    mutable std::mutex scanMx;              // guards scanner
    levelbrew::TrackScanner scanner;
    std::deque<std::pair<std::shared_ptr<levelbrew::Curve>, std::chrono::steady_clock::time_point>> retired;

    mutable std::mutex dispMx;
    std::shared_ptr<const levelbrew::Curve> disp;
    std::atomic<uint32_t> dispVer{0};

    std::atomic<bool> quit{false}, clearReq{false}, settingsDirty{false};
    std::atomic<double> newRate{0};
    std::atomic<int> scanState{levelbrew::kScanEmpty};
    std::atomic<float> scannedSec{0};
    std::thread worker;

    float values[levelbrew::pCount];
    uint32_t activityAcc = 0;
    int activity = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelBrewPlugin)
};

END_NAMESPACE_DISTRHO
