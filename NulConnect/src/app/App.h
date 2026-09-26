#pragma once

#include "model/AppModel.h"
#include "windows/Pages.h"

#include <memory>

namespace nc {

class MainWindow;
class Flyout;
class LoginWindow;
class TrayIcon;

class App {
public:
    static int Run(HINSTANCE instance, PWSTR commandLine, int showCommand);

private:
    App() = default;
    ~App();

    bool Initialize(bool background);
    void CreateMessageWindow();
    static LRESULT CALLBACK MessageWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void ShowMainWindow(PageId page = PageId::Home);
    void OnMainWindowCloseRequested();
    void ToggleFlyout();
    void ShowTrayMenu();
    void SyncLoginWindow();
    void ClearWebLoginData(int attempt = 0);
    void Quit();

    AppModel model_;
    std::unique_ptr<MainWindow> main_;
    std::unique_ptr<Flyout> flyout_;
    std::unique_ptr<LoginWindow> login_;
    std::unique_ptr<TrayIcon> tray_;
    HWND messageWindow_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;
    bool quitting_ = false;
};

}  // namespace nc
