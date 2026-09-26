#pragma once

#include "model/AppModel.h"
#include "ui/Controls.h"

namespace nc {

// Colors and glyph that represent a connection phase.
ui::Color PhaseColor(ConnectionPhase phase);
ui::Icon PhaseIcon(ConnectionPhase phase);

// Draws the NulConnect logo (two linked nodes) inside `box`.
void DrawBrandLogo(ui::Canvas& canvas, const ui::RectF& box, bool monochrome = false, ui::Color mono = {});

// Large animated status indicator: tinted disc with the phase glyph, a
// spinning arc while connecting and a soft pulse while connected.
class StatusGlyph : public ui::Widget {
public:
    explicit StatusGlyph(float size) : size_(size) {}
    void SetPhase(ConnectionPhase phase);
    float Measure(float) override { return size_ + 16; }
    float MeasureWidth() override { return size_ + 16; }
    void Paint(ui::Canvas& canvas) override;

private:
    float size_;
    ConnectionPhase phase_ = ConnectionPhase::Disconnected;
    ui::Animated tint_{0};
    ui::Color from_{};
    ui::Color to_{};
    double phaseStart_ = 0;
};

// Area chart of the last minute of download/upload rates.
class TrafficChart : public ui::Widget {
public:
    explicit TrafficChart(float height = 140) : height_(height) {}
    void SetSamples(const std::deque<TrafficSample>& samples) {
        samples_ = samples;
        Invalidate();
    }
    float Measure(float) override { return height_; }
    void Paint(ui::Canvas& canvas) override;

private:
    float height_;
    std::deque<TrafficSample> samples_;
};

// A compact rate readout: colored arrow, caption and value.
class RateTile : public ui::Widget {
public:
    RateTile(ui::Icon icon, std::wstring caption, bool download) : icon_(icon), caption_(std::move(caption)), download_(download) {}
    void SetValue(std::wstring value) {
        if (value != value_) {
            value_ = std::move(value);
            Invalidate();
        }
    }
    float Measure(float) override { return 52; }
    void Paint(ui::Canvas& canvas) override;

private:
    ui::Icon icon_;
    std::wstring caption_;
    std::wstring value_;
    bool download_;
};

// Windows 11 Settings-style account card at the top of the navigation pane.
class AccountCard : public ui::Widget {
public:
    void Set(std::wstring name, std::wstring detail, bool signedIn);
    std::function<void()> onClick;
    float Measure(float) override { return 72; }
    void Paint(ui::Canvas& canvas) override;
    bool IsInteractive() const override { return true; }
    void OnMouseEnter() override { Invalidate(); }
    void OnMouseLeave() override { Invalidate(); }
    void OnMouseUp(ui::PointF point, ui::MouseButton button) override;

private:
    std::wstring name_;
    std::wstring detail_;
    bool signedIn_ = false;
};

// Section title above a group of cards.
std::unique_ptr<ui::TextBlock> SectionHeader(const std::wstring& text);
// Secondary text used as a card's trailing value.
std::unique_ptr<ui::TextBlock> ValueText(const std::wstring& text);

}  // namespace nc
