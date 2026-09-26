#pragma once

#include "model/AppModel.h"
#include "ui/Host.h"
#include "ui/Navigation.h"
#include "windows/Pages.h"

namespace nc {

class MainWindow : public ui::Host {
public:
    explicit MainWindow(AppModel& model);
    ~MainWindow() override;

    bool Create();
    void Show(PageId page = PageId::Home);
    void ShowPage(PageId page);
    bool IsVisible() const;
    void Hide();

    // Invoked when the user closes the window; the shell decides whether to
    // hide to the tray or quit.
    std::function<void()> onCloseRequested;

protected:
    LRESULT OnMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    void OnLayout(const ui::RectF& client) override;
    void PaintBackground(ui::Canvas& canvas) override;
    D2D1_SIZE_F MinimumSize() const override { return D2D1::SizeF(480, 420); }
    void OnThemeChanged() override;

private:
    class Layout;
    void Refresh();
    void UpdateTrafficObservation();
    void SaveWindowPlacement();
    void RestoreWindowPlacement();

    AppModel& model_;
    int subscription_ = 0;
    Layout* layout_ = nullptr;
    PageId current_ = PageId::Home;
    uint64_t shownBanner_ = 0;
};

}  // namespace nc
