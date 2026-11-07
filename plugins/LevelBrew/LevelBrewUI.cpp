#include "DistrhoUI.hpp"
#include "LevelBrewPlugin.hpp"
#include "../../src/LevelView.hpp"
#include <chrono>

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::CairoGraphicsContext;

class LevelBrewUI : public UI, public levelbrew::LevelView::Host
{
public:
    LevelBrewUI()
        : UI(levelbrew::LevelView::kW, levelbrew::LevelView::kH, true),
          view(this),
          t0(std::chrono::steady_clock::now())
    {
        // the editor draws straight from the plugin's scan (same process)
        plugin = static_cast<LevelBrewPlugin*>(static_cast<Plugin*>(getPluginInstancePointer()));
        view.setCurve(nullptr, plugin != nullptr);
    }

protected:
    // ---- host -> editor ----
    void parameterChanged(uint32_t index, float value) override
    {
        view.setValue((int)index, value);
        if (index < levelbrew::pOutGain) repaint();
    }

    void uiIdle() override
    {
        if (plugin) {
            const uint32_t v = plugin->displayVersion();
            if (v != seenVersion) { seenVersion = v; view.setCurve(plugin->display()); }
        }
        view.tick(now());
        repaint();
    }

    // ---- editor -> host ----
    void setParam(int index, float value) override { setParameterValue((uint32_t)index, value); repaint(); }
    void beginEdit(int index) override { editParameter((uint32_t)index, true); }
    void endEdit(int index) override { editParameter((uint32_t)index, false); }

    // ---- drawing & input ----
    void onCairoDisplay(const CairoGraphicsContext& context) override
    {
        view.draw(context.handle, getWidth(), getHeight());
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;
        const bool fine = (ev.mod & DGL_NAMESPACE::kModifierShift) != 0;
        const bool used = ev.press ? view.mouseDown(ev.pos.getX(), ev.pos.getY(), fine, ev.time / 1000.0)
                                   : view.mouseUp(ev.pos.getX(), ev.pos.getY());
        if (used) repaint();
        return used;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const bool fine = (ev.mod & DGL_NAMESPACE::kModifierShift) != 0;
        view.mouseMove(ev.pos.getX(), ev.pos.getY(), fine);
        repaint();
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const bool used = view.scroll(ev.pos.getX(), ev.pos.getY(), ev.delta.getY());
        if (used) repaint();
        return used;
    }

private:
    double now() const
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }

    LevelBrewPlugin* plugin = nullptr;
    uint32_t seenVersion = 0;
    levelbrew::LevelView view;
    std::chrono::steady_clock::time_point t0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelBrewUI)
};

UI* createUI()
{
    return new LevelBrewUI();
}

END_NAMESPACE_DISTRHO
