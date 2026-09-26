#pragma once

#include "model/AppModel.h"
#include "ui/Controls.h"
#include "windows/Visuals.h"

namespace nc {

enum class PageId { Home = 1, Account, Connection, Statistics, Helper, General, About };

// A settings page: a vertical stack of sections and cards that reads its
// state from the model in Refresh().
class Page : public ui::StackPanel {
public:
    Page(AppModel& model, ui::Host* host);
    virtual void Refresh() = 0;
    // Whether this page shows live traffic.
    virtual bool WantsTraffic() const { return false; }

protected:
    ui::TextBlock* AddSection(const std::wstring& title);
    ui::SettingsCard* AddCard(ui::Icon icon, const std::wstring& title, const std::wstring& description = {});

    AppModel& model_;
    ui::Host* host_;
    // Expires with the page; guards delayed callbacks.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

std::unique_ptr<Page> CreatePage(PageId id, AppModel& model, ui::Host* host);
const std::wstring& PageTitle(PageId id);
ui::Icon PageIcon(PageId id);

}  // namespace nc
