#pragma once

#include "model/AppModel.h"
#include "ui/Host.h"

namespace nc {

// Quick-access panel shown from the notification area icon, in the style of
// the Windows 11 quick settings flyout.
class Flyout : public ui::Host {
public:
    explicit Flyout(AppModel& model);
    ~Flyout() override;

    bool Create();
    // Shows the panel next to the notification area icon rectangle.
    void ShowNear(const RECT& anchor);
    void Hide();
    bool IsShown() const;
    // True when the panel was dismissed moments ago; a click on the tray icon
    // that caused the dismissal should not immediately reopen it.
    bool RecentlyHidden() const;

    std::function<void()> onOpenMainWindow;
    std::function<void()> onOpenSettings;
    std::function<void()> onQuit;

protected:
    LRESULT OnMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    void PaintBackground(ui::Canvas& canvas) override;
    void OnEscape() override { Hide(); }

private:
    class Content;
    void Refresh();
    void ApplyWindowTheme() override;
    D2D1_SIZE_F DesiredSize();

    AppModel& model_;
    Content* content_ = nullptr;
    int subscription_ = 0;
    ULONGLONG hiddenAt_ = 0;
};

}  // namespace nc
