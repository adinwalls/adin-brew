#include "DistrhoUI.hpp"
#include "../../src/TuneView.hpp"
#include <chrono>

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::CairoGraphicsContext;

class TuneBrewUI : public UI, public tunebrew::TuneView::Host
{
public:
    TuneBrewUI()
        : UI(tunebrew::TuneView::kW, tunebrew::TuneView::kH, true),
          view(this),
          t0(std::chrono::steady_clock::now())
    {
    }

protected:
    // ---- host -> editor ----
    void parameterChanged(uint32_t index, float value) override
    {
        view.setValue((int)index, value);
        if (index < tunebrew::pOutPitch) repaint();
    }

    void uiIdle() override
    {
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

    tunebrew::TuneView view;
    std::chrono::steady_clock::time_point t0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TuneBrewUI)
};

UI* createUI()
{
    return new TuneBrewUI();
}

END_NAMESPACE_DISTRHO
