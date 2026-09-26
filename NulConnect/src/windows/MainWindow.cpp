#include "pch.h"
#include "windows/MainWindow.h"
#include "core/Localization.h"
#include "core/Str.h"

namespace nc {

using namespace ui;

namespace {
constexpr const wchar_t* kPlacementKey = L"Software\\NulStudio\\NulConnect";
constexpr float kCompactThreshold = 760.0f;
}  // namespace

// Root widget: navigation pane on the left, page title, banner and the
// scrolling page on the right.
class MainWindow::Layout : public Widget {
public:
    Layout(MainWindow& window, AppModel& model) : window_(window), model_(model) {
        nav_ = Emplace<NavigationView>();
        auto account = std::make_unique<AccountCard>();
        account_ = account.get();
        account_->onClick = [this] { window_.ShowPage(PageId::Account); };
        nav_->SetHeader(std::move(account));
        std::vector<NavigationView::Item> items;
        for (PageId id : {PageId::Home, PageId::Account, PageId::Connection, PageId::Statistics, PageId::Helper}) {
            items.push_back({static_cast<int>(id), PageIcon(id), PageTitle(id), false});
        }
        for (PageId id : {PageId::General, PageId::About}) {
            items.push_back({static_cast<int>(id), PageIcon(id), PageTitle(id), true});
        }
        nav_->SetItems(std::move(items));
        nav_->onSelect = [this](int id) { window_.ShowPage(static_cast<PageId>(id)); };
        title_ = Emplace<TextBlock>(L"", TextStyle::Title);
        title_->SetWrap(false);
        banner_ = Emplace<InfoBar>(Severity::Informational, L"", L"");
        banner_->SetClosable(true);
        banner_->SetVisible(false);
        banner_->onClose = [this] {
            banner_->SetVisible(false);
            model_.DismissBanner();
        };
        scroll_ = Emplace<ScrollView>();
    }

    void SetPage(std::unique_ptr<Page> page, PageId id) {
        page_ = page.get();
        page_->SetPadding(36, 4, 36, 36);
        page_->SetMaxWidth(1000);
        scroll_->SetContent(std::move(page));
        scroll_->ScrollTo(0, false);
        title_->SetText(PageTitle(id));
        nav_->Select(static_cast<int>(id), true);
    }

    Page* CurrentPage() const { return page_; }
    AccountCard* Account() const { return account_; }
    NavigationView* Nav() const { return nav_; }
    InfoBar* BannerBar() const { return banner_; }

    void Arrange(const RectF& rect) override {
        bounds_ = rect;
        nav_->SetCompact(Width(rect) < kCompactThreshold);
        float pane = nav_->PaneWidth();
        nav_->Arrange(RectF{rect.left, rect.top, rect.left + pane, rect.bottom});
        content_ = RectF{rect.left + pane, rect.top, rect.right, rect.bottom};
        float inner = std::min(Width(content_) - 72, 1000.0f);
        float left = content_.left + std::max(36.0f, (Width(content_) - inner) / 2);
        float y = content_.top + 28;
        title_->Arrange(MakeRect(left, y, inner, 40));
        y += 52;
        if (banner_->IsVisible()) {
            float height = banner_->Measure(inner);
            banner_->Arrange(MakeRect(left, y, inner, height));
            y += height + 12;
        }
        scroll_->Arrange(RectF{content_.left, y, content_.right, content_.bottom});
    }

    void Paint(Canvas& canvas) override {
        const Palette& p = Theme::Current();
        // Content layer with a rounded top-left corner, as in Settings.
        RectF layer{content_.left, content_.top, content_.right + 16, content_.bottom + 16};
        canvas.FillRoundRect(layer, Theme::OverlayRadius, p.contentBackground);
        canvas.StrokeRoundRect(layer, Theme::OverlayRadius, p.cardStroke);
        PaintChildren(canvas);
    }

private:
    MainWindow& window_;
    AppModel& model_;
    NavigationView* nav_ = nullptr;
    AccountCard* account_ = nullptr;
    TextBlock* title_ = nullptr;
    InfoBar* banner_ = nullptr;
    ScrollView* scroll_ = nullptr;
    Page* page_ = nullptr;
    RectF content_{};
};

MainWindow::MainWindow(AppModel& model) : model_(model) {}

MainWindow::~MainWindow() {
    if (subscription_) model_.Unsubscribe(subscription_);
    model_.SetTrafficObserver("main", false);
}

bool MainWindow::Create() {
    if (!CreateHostWindow(L"NulConnect.MainWindow", L"NulConnect", WS_OVERLAPPEDWINDOW, 0, CW_USEDEFAULT, CW_USEDEFAULT,
                          CW_USEDEFAULT, CW_USEDEFAULT, nullptr)) {
        return false;
    }
    // Size the window in DIPs for the monitor it opened on.
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int width = std::min(ToPixels(1000), static_cast<int>(work.right - work.left) - 40);
    int height = std::min(ToPixels(700), static_cast<int>(work.bottom - work.top) - 40);
    SetWindowPos(hwnd_, nullptr, work.left + (work.right - work.left - width) / 2, work.top + (work.bottom - work.top - height) / 2,
                 width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    RestoreWindowPlacement();

    auto layout = std::make_unique<Layout>(*this, model_);
    layout_ = layout.get();
    SetRoot(std::move(layout));
    ShowPage(PageId::Home);
    subscription_ = model_.Subscribe([this] { Refresh(); });
    Refresh();
    return true;
}

void MainWindow::Show(PageId page) {
    if (page != current_) ShowPage(page);
    if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    else ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    UpdateTrafficObservation();
    model_.RefreshHelperState(false);
}

bool MainWindow::IsVisible() const {
    return hwnd_ && IsWindowVisible(hwnd_) && !IsIconic(hwnd_);
}

void MainWindow::Hide() {
    SaveWindowPlacement();
    ShowWindow(hwnd_, SW_HIDE);
    UpdateTrafficObservation();
}

void MainWindow::ShowPage(PageId page) {
    current_ = page;
    SetFocus(nullptr);
    layout_->SetPage(CreatePage(page, model_, this), page);
    if (page == PageId::Helper) model_.RefreshHelperState(true);
    UpdateTrafficObservation();
    InvalidateLayout();
}

void MainWindow::UpdateTrafficObservation() {
    bool wants = IsVisible() && layout_ && layout_->CurrentPage() && layout_->CurrentPage()->WantsTraffic();
    model_.SetTrafficObserver("main", wants);
}

void MainWindow::Refresh() {
    if (!layout_) return;
    const auto& summary = model_.sessionSummary();
    if (summary && !model_.NeedsLogin()) {
        layout_->Account()->Set(Widen(summary->username), model_.ServerDisplayText(), true);
    } else {
        layout_->Account()->Set(Tr(L"Not logged in"), model_.ServerDisplayText(), false);
    }
    // Only warnings and errors are shown as a banner; progress is visible on
    // the pages themselves.
    const Banner& banner = model_.banner();
    InfoBar* bar = layout_->BannerBar();
    bool important = !banner.text.empty() &&
                     (banner.severity == BannerSeverity::Warning || banner.severity == BannerSeverity::Error);
    if (important && banner.id != shownBanner_) {
        shownBanner_ = banner.id;
        bar->Set(banner.severity == BannerSeverity::Error ? Severity::Error : Severity::Warning, L"", banner.text);
        bar->SetVisible(true);
    } else if (banner.text.empty() && bar->IsVisible()) {
        bar->SetVisible(false);
    }
    if (Page* page = layout_->CurrentPage()) page->Refresh();
    InvalidateLayout();
}

void MainWindow::OnLayout(const RectF& client) {
    Host::OnLayout(client);
}

void MainWindow::PaintBackground(Canvas& canvas) {
    Host::PaintBackground(canvas);
}

void MainWindow::OnThemeChanged() {
    Host::OnThemeChanged();
    if (layout_) ShowPage(current_);
}

LRESULT MainWindow::OnMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CLOSE:
        if (onCloseRequested) onCloseRequested();
        return 0;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED || wParam == SIZE_RESTORED || wParam == SIZE_MAXIMIZED) UpdateTrafficObservation();
        break;
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE) model_.RefreshHelperState(false);
        break;
    default:
        break;
    }
    return Host::OnMessage(message, wParam, lParam);
}

void MainWindow::SaveWindowPlacement() {
    WINDOWPLACEMENT placement{sizeof(placement)};
    if (!GetWindowPlacement(hwnd_, &placement)) return;
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kPlacementKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(key, L"WindowPlacement", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&placement), sizeof(placement));
        RegCloseKey(key);
    }
}

void MainWindow::RestoreWindowPlacement() {
    WINDOWPLACEMENT placement{};
    DWORD size = sizeof(placement);
    if (RegGetValueW(HKEY_CURRENT_USER, kPlacementKey, L"WindowPlacement", RRF_RT_REG_BINARY, nullptr, &placement, &size) !=
            ERROR_SUCCESS ||
        size != sizeof(placement) || placement.length != sizeof(placement)) {
        return;
    }
    // Only restore when the saved rectangle is still on a monitor.
    if (!MonitorFromRect(&placement.rcNormalPosition, MONITOR_DEFAULTTONULL)) return;
    placement.showCmd = SW_HIDE;
    SetWindowPlacement(hwnd_, &placement);
}

}  // namespace nc
