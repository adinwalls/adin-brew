// TuneBrew plugin class. Kept in a header so the editor, which runs in the
// same process, can read the pitch scan straight from the engine.
#pragma once
#include "DistrhoPlugin.hpp"
#include "../../src/TuneParams.hpp"
#include <cstring>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif

START_NAMESPACE_DISTRHO

using tunebrew::TuneEngine;
using namespace tunebrew;

class TuneBrewPlugin : public Plugin
{
public:
    TuneBrewPlugin()
        : Plugin(pCount, 0, 2)
    {
        for (int i = 0; i < pCount; ++i) values[i] = kSpecs[i].def;
        engine.init(getSampleRate(), 2);
        applyParams();
        setLatency(engine.getLatency());
    }

protected:
public:
    // for the editor (same process)
    const tunebrew::TuneEngine* enginePtr() const { return &engine; }

protected:
    const char* getLabel() const override { return "TuneBrewAutotune"; }
    const char* getDescription() const override
    {
        return "Real-time vocal pitch correction: key/scale snapping, retune speed from hard-tune to natural, "
               "formant-preserving PSOLA shifting.";
    }
    const char* getMaker() const override { return "Adin Walls"; }
    const char* getLicense() const override { return "GPL-3.0-or-later"; }
    const char* getHomePage() const override { return "https://github.com/adinwalls/adin-brew"; }
    uint32_t getVersion() const override { return d_version(1, 5, 0); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& p) override
    {
        p.hints = kParameterIsAutomatable;
        switch (index) {
        case pKey: {
            p.name = "Key"; p.symbol = "key";
            p.hints |= kParameterIsInteger;
            p.ranges.def = 0; p.ranges.min = 0; p.ranges.max = 11;
            ParameterEnumerationValue* v = new ParameterEnumerationValue[12];
            for (int i = 0; i < 12; ++i) { v[i].value = i; v[i].label = tunebrew::kKeyNames[i]; }
            p.enumValues.count = 12; p.enumValues.restrictedMode = true; p.enumValues.values = v;
            break;
        }
        case pScale: {
            p.name = "Scale"; p.symbol = "scale";
            p.hints |= kParameterIsInteger;
            p.ranges.def = 0; p.ranges.min = 0; p.ranges.max = tunebrew::kScaleCount - 1;
            ParameterEnumerationValue* v = new ParameterEnumerationValue[tunebrew::kScaleCount];
            for (int i = 0; i < tunebrew::kScaleCount; ++i) { v[i].value = i; v[i].label = tunebrew::kScaleNames[i]; }
            p.enumValues.count = tunebrew::kScaleCount; p.enumValues.restrictedMode = true; p.enumValues.values = v;
            break;
        }
        case pRetune:
            p.name = "Retune Speed"; p.shortName = "Retune"; p.symbol = "retune"; p.unit = "ms";
            p.description = "0 = instant hard-tune effect; 20-50 = tight but natural; 100+ = gentle, keeps vibrato";
            p.ranges.def = 20; p.ranges.min = 0; p.ranges.max = 400;
            break;
        case pAmount:
            p.name = "Correction Amount"; p.shortName = "Amount"; p.symbol = "amount"; p.unit = "%";
            p.ranges.def = 100; p.ranges.min = 0; p.ranges.max = 100;
            break;
        case pTranspose:
            p.name = "Transpose"; p.symbol = "transpose"; p.unit = "st";
            p.hints |= kParameterIsInteger;
            p.ranges.def = 0; p.ranges.min = -12; p.ranges.max = 12;
            break;
        case pRefHz:
            p.name = "Reference A4"; p.shortName = "Ref A4"; p.symbol = "ref_hz"; p.unit = "Hz";
            p.ranges.def = 440; p.ranges.min = 415; p.ranges.max = 466;
            break;
        case pMix:
            p.name = "Mix"; p.symbol = "mix"; p.unit = "%";
            p.ranges.def = 100; p.ranges.min = 0; p.ranges.max = 100;
            break;
        case pGain:
            p.name = "Output Gain"; p.shortName = "Gain"; p.symbol = "gain"; p.unit = "dB";
            p.ranges.def = 0; p.ranges.min = -24; p.ranges.max = 12;
            break;
        case pMode: {
            p.name = "Mode"; p.symbol = "mode";
            p.hints |= kParameterIsInteger;
            p.ranges.def = 1; p.ranges.min = 0; p.ranges.max = 1;
            ParameterEnumerationValue* v = new ParameterEnumerationValue[2];
            v[0].value = 0; v[0].label = "Classic"; v[1].value = 1; v[1].label = "Natural";
            p.enumValues.count = 2; p.enumValues.restrictedMode = true; p.enumValues.values = v;
            p.description = "Classic: plain hard correction. Natural: keeps vibrato, slides and sustained-note life";
            break;
        }
        case pVibrato:
            p.name = "Vibrato"; p.symbol = "vibrato"; p.unit = "%";
            p.description = "Natural mode: how much of the singer's own vibrato to keep (100 = untouched, 200 = doubled)";
            p.ranges.def = 100; p.ranges.min = 0; p.ranges.max = 200;
            break;
        case pFlex:
            p.name = "Flex"; p.symbol = "flex"; p.unit = "%";
            p.description = "Natural mode: only correct notes sung close to the target; bends and blue notes pass";
            p.ranges.def = 20; p.ranges.min = 0; p.ranges.max = 100;
            break;
        case pHumanize:
            p.name = "Humanize"; p.symbol = "humanize"; p.unit = "%";
            p.description = "Natural mode: relax correction on long held notes; short notes stay tight";
            p.ranges.def = 30; p.ranges.min = 0; p.ranges.max = 100;
            break;
        case pTransition:
            p.name = "Transition"; p.symbol = "transition"; p.unit = "ms";
            p.description = "Natural mode: let note attacks, scoops and slides through for this long";
            p.ranges.def = 40; p.ranges.min = 0; p.ranges.max = 150;
            break;
        case pLearn:
            p.name = "Detect Key"; p.symbol = "learn";
            p.hints |= kParameterIsBoolean | kParameterIsInteger;
            p.ranges.def = 0; p.ranges.min = 0; p.ranges.max = 1;
            break;
        case pOutPitch:
            p.name = "Detected Pitch"; p.symbol = "detected_hz"; p.unit = "Hz";
            p.hints = kParameterIsOutput;
            p.ranges.def = 0; p.ranges.min = 0; p.ranges.max = 1200;
            break;
        case pOutNote:
            p.name = "Target Note (MIDI)"; p.shortName = "Target"; p.symbol = "target_note";
            p.hints = kParameterIsOutput | kParameterIsInteger;
            p.ranges.def = -1; p.ranges.min = -1; p.ranges.max = 127;
            break;
        case pOutCents:
            p.name = "Correction"; p.symbol = "correction_cents"; p.unit = "cents";
            p.hints = kParameterIsOutput;
            p.ranges.def = 0; p.ranges.min = -1200; p.ranges.max = 1200;
            break;
        case pOutActivity:   // ticks while audio runs, so the editor knows when the transport stops
            p.name = "Activity"; p.symbol = "activity";
            p.hints = kParameterIsOutput | kParameterIsInteger | kParameterIsHidden;
            p.ranges.def = 0; p.ranges.min = 0; p.ranges.max = 1000;
            break;
        case pOutVib:
            p.name = "Vibrato Depth"; p.symbol = "vibrato_depth"; p.unit = "cents";
            p.hints = kParameterIsOutput;
            p.ranges.def = 0; p.ranges.min = 0; p.ranges.max = 1000;
            break;
        case pOutKey:
            p.name = "Detected Key"; p.symbol = "key_found";
            p.hints = kParameterIsOutput | kParameterIsInteger | kParameterIsHidden;
            p.ranges.def = -1; p.ranges.min = -1; p.ranges.max = 23;
            break;
        case pOutKeyConf:
            p.name = "Key Confidence"; p.symbol = "key_confidence";
            p.hints = kParameterIsOutput | kParameterIsHidden;
            p.ranges.def = 0; p.ranges.min = 0; p.ranges.max = 1;
            break;
        case pOutKeyAlt:
            p.name = "Alternative Key"; p.symbol = "key_alt";
            p.hints = kParameterIsOutput | kParameterIsInteger | kParameterIsHidden;
            p.ranges.def = -1; p.ranges.min = -1; p.ranges.max = 23;
            break;
        case pOutTuneOff:
            p.name = "Tuning Offset"; p.symbol = "tuning_offset"; p.unit = "cents";
            p.hints = kParameterIsOutput | kParameterIsHidden;
            p.ranges.def = 0; p.ranges.min = -50; p.ranges.max = 50;
            break;
        case pOutLearnSecs:
            p.name = "Seconds Listened"; p.symbol = "learn_seconds"; p.unit = "s";
            p.hints = kParameterIsOutput | kParameterIsHidden;
            p.ranges.def = 0; p.ranges.min = 0; p.ranges.max = 3600;
            break;
        }
    }

    // ---- session state: marked phrases and the pitch scan ----
    void initState(uint32_t index, State& state) override
    {
        if (index == 0) {
            state.key = "regions"; state.label = "Phrase Pull"; state.defaultValue = "";
            state.description = "Phrases pulled harder into key (start ms, end ms, pull %, note shift)";
        } else {
            state.key = "scan"; state.label = "Pitch Scan"; state.defaultValue = "";
            state.hints = kStateIsOnlyForDSP;
            state.description = "Pitch of the vocal along the song, shown in the Phrase Pull view";
        }
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, "regions") == 0) return String(engine.regions().encode().c_str());
        if (std::strcmp(key, "scan") == 0) return String(engine.scan.encode().c_str());
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "regions") == 0) {
            tunebrew::RegionSet rs; rs.decode(value);
            engine.setRegions(rs);
        } else if (std::strcmp(key, "scan") == 0) {
            if (value && *value) engine.scan.decode(value);
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index) {
        case pOutPitch: return engine.meterHz;
        case pOutNote:  return engine.meterTarget;
        case pOutCents: return engine.meterCents;
        case pOutActivity: return (float)activity;
        case pOutVib:   return engine.meterVib;
        case pOutKey:   return engine.meterKey;
        case pOutKeyConf: return engine.meterKeyConf;
        case pOutKeyAlt: return engine.meterKeyAlt;
        case pOutTuneOff: return engine.meterTuneOff;
        case pOutLearnSecs: return engine.meterLearnSecs;
        }
        return index < pCount ? values[index] : 0.f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= pOutPitch) return;
        values[index] = value;
        applyParams();
    }

    void activate() override
    {
        engine.init(getSampleRate(), 2);
        applyParams();
        setLatency(engine.getLatency());
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
#if defined(__SSE__)
        // tiny decaying filter values would otherwise slow the CPU to a crawl in silence
        const unsigned int csr = _mm_getcsr();
        _mm_setcsr(csr | 0x8040);
#endif
#if defined(DISTRHO_PLUGIN_TARGET_CLAP)
        engine.setTimeline(false, false, 0.0);   // DPF gives CLAP plugins no song position
#else
        const TimePosition& tp = getTimePosition();
        engine.setTimeline(true, tp.playing, (double)tp.frame);
#endif
        engine.process(inputs, outputs, frames);
#if defined(__SSE__)
        _mm_setcsr(csr);
#endif
        activityAcc += frames;
        if (activityAcc >= 2048) { activityAcc = 0; activity = (activity + 1) % 1000; }
    }

private:
    void applyParams()
    {
        engine.key = std::max(0, std::min(11, (int)std::lround(values[pKey])));
        engine.scale = std::max(0, std::min(tunebrew::kScaleCount - 1, (int)std::lround(values[pScale])));
        engine.retuneMs = values[pRetune];
        engine.amount = values[pAmount] * 0.01;
        engine.transpose = std::lround(values[pTranspose]);
        engine.refHz = values[pRefHz];
        engine.mix = values[pMix] * 0.01;
        engine.gain = std::pow(10.0, values[pGain] / 20.0);
        engine.mode = values[pMode] > 0.5f ? 1 : 0;
        engine.vibrato = values[pVibrato] * 0.01;
        engine.flex = values[pFlex] * 0.01;
        engine.humanize = values[pHumanize] * 0.01;
        engine.transitionMs = values[pTransition];
        engine.learn = values[pLearn] > 0.5f;
    }

    TuneEngine engine;
    uint32_t activityAcc = 0;
    int activity = 0;
    float values[pCount];

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TuneBrewPlugin)
};

END_NAMESPACE_DISTRHO
