#include "app.hpp"

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

void App::DrawMods() {
    PageHeader("Моды", "Каталоги Modrinth и CurseForge с установкой в папку игры");

    if (installedModsDirty_) RefreshInstalledMods();
    if (!modsInitialized_) {
        modsInitialized_ = true;
        modGameVersion_ = cfg.s.selectedVersion;
        modLoaderKind_ = loaderIdx_ > 0 ? loaderIdx_ : -1;
        modSearchDebounce_ = 0.0;
    }

    auto manifest = mc.Manifest();
    float availW = ImGui::GetContentRegionAvail().x;
    float gap = ui::S(16);

    const char* providers[] = {"Modrinth", "CurseForge"};
    ImVec2 segPos = ImGui::GetCursorScreenPos();
    if (ui::Segmented("##mod_provider", providers, 2, &modsProvider_, ui::S(280))) {
        modSearchDebounce_ = 0.0;
        StartModSearch(false);
    }
    {
        std::lock_guard lock(modsAsync_.mutex);
        if (modsAsync_.total > 0) {
            ImGui::PushFont(uiFonts.smallFont);
            std::string total = "Найдено: " + ui::FormatCount(modsAsync_.total);
            ImVec2 ts = ImGui::CalcTextSize(total.c_str());
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(segPos.x + ui::S(280) + ui::S(16),
                       segPos.y + (ui::S(38) - ts.y) * 0.5f),
                colFaint, total.c_str());
            ImGui::PopFont();
        }
    }
    ImGui::Dummy(ImVec2(0, ui::S(6)));

    // ------------------------------------------------------------ filters
    float comboW = ui::S(176);
    float searchW = std::max(ui::S(180), availW - comboW * 2 - ui::S(20));
    ui::SearchInput("##mod_search", modSearch_, sizeof(modSearch_), "Поиск модов...", searchW);
    ImGui::SameLine(0, ui::S(10));
    VersionCombo("##mod_game_version", modGameVersion_, manifest.get(), true, comboW,
                 "Любая версия", true);
    ImGui::SameLine(0, ui::S(10));
    LoaderCombo("##mod_loader_filter", modLoaderKind_, comboW, true);
    ImGui::Dummy(ImVec2(0, ui::S(8)));

    if (modsProvider_ == 1 && !CurseForgeClient::KeyLooksValid(cfg.s.cfApiKey)) {
        ui::BeginCard("##cf_warning", ImVec2(availW, ui::S(72)), ui::Hex("#221A14"));
        ImGui::PushFont(uiFonts.body);
        ImGui::TextUnformatted("Для CurseForge нужен API-ключ");
        ImGui::PopFont();
        ImGui::SameLine(0, ui::S(14));
        ImGui::PushFont(uiFonts.smallFont);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
        ImGui::TextUnformatted("Получите ключ на console.curseforge.com и вставьте в настройках.");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::SameLine(0, ui::S(10));
        if (ui::SoftButton("Настройки", ImVec2(ui::S(132), ui::S(30)), true, ui::IconSettings)) {
            SetPage(Page::Settings);
        }
        ui::EndCard();
        ImGui::Dummy(ImVec2(0, ui::S(12)));
    }

    std::string currentKey = std::to_string(modsProvider_) + "|" + Trim(modSearch_) + "|" +
                             modGameVersion_ + "|" + std::to_string(modLoaderKind_);
    if (currentKey != modSearchLast_) {
        modSearchLast_ = currentKey;
        modSearchDebounce_ = 0.0;
    }
    modSearchDebounce_ += ImGui::GetIO().DeltaTime;
    {
        std::string activeKey;
        bool loading = false;
        {
            std::lock_guard lock(modsAsync_.mutex);
            activeKey = modsAsync_.key;
            loading = modsAsync_.loading;
        }
        if (!loading && activeKey != currentKey && modSearchDebounce_ > 0.45) {
            StartModSearch(false);
        }
    }

    float resultsW = std::max(ui::S(340), availW * 0.58f);
    float installedW = std::max(ui::S(280), availW - resultsW - gap);
    float panelH = std::max(ui::S(380), ImGui::GetContentRegionAvail().y - ui::S(2));

    // ------------------------------------------------------------ results
    ImGui::BeginGroup();
    ui::BeginCard("##mod_results", ImVec2(resultsW, panelH));
    {
        float innerW = ImGui::GetContentRegionAvail().x;
        float headerY = ImGui::GetCursorPosY();
        ImGui::PushFont(uiFonts.h3);
        ImGui::TextUnformatted("Результаты поиска");
        float titleH = ImGui::GetTextLineHeight();
        ImGui::PopFont();
        ImGui::SameLine(ui::S(20) + innerW - ui::S(64));
        ImGui::SetCursorPosY(headerY + (titleH - ui::S(28)) * 0.5f);
        if (ui::IconButton("##mods_refresh", ui::IconRefresh, ImVec2(ui::S(32), ui::S(28)), false, 0,
                           "Обновить")) {
            StartModSearch(false);
            installedModsDirty_ = true;
        }
        ImGui::SameLine(0, 0);
        if (ui::IconButton("##mods_open_folder", ui::IconFolder, ImVec2(ui::S(32), ui::S(28)), false,
                           0, "Открыть папку модов")) {
            std::error_code ec;
            fs::create_directories(mc.ModsDir(), ec);
            ui::OpenPath(mc.ModsDir().string());
        }
        ImGui::Separator();

        std::vector<ModHit> hits;
        std::string error;
        bool loading = false;
        bool hasMore = false;
        {
            std::lock_guard lock(modsAsync_.mutex);
            hits = modsAsync_.hits;
            error = modsAsync_.error;
            loading = modsAsync_.loading;
            hasMore = modsAsync_.hasMore;
        }

        ImGui::BeginChild("##mod_hits", ImVec2(0, 0));
        if (!error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colRed));
            ImGui::TextWrapped("%s", error.c_str());
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, ui::S(6)));
        }
        if (loading && hits.empty()) {
            ImGui::Dummy(ImVec2(0, ui::S(30)));
            ImVec2 p = ImGui::GetCursorScreenPos();
            ui::Spinner(ImVec2(p.x + ui::S(13), p.y + ui::S(13)), ui::S(12), colAccent);
            ImGui::SameLine(0, ui::S(36));
            TextMuted("Поиск модов...");
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        int providerNow = modsProvider_;
        for (size_t i = 0; i < hits.size(); ++i) {
            const ModHit& hit = hits[i];
            ImGui::PushID((int)i);
            float rowW = ImGui::GetContentRegionAvail().x;
            float rowH = ui::S(82);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##mod_row", ImVec2(rowW, rowH));
            bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) OpenModFiles(hit, providerNow);
            if (hovered) {
                dl->AddRectFilled(p, ImVec2(p.x + rowW, p.y + rowH),
                                  ui::Alpha(IM_COL32(255, 255, 255, 255), 9), ui::S(12));
            }
            float iconSize = ui::S(52);
            ImVec2 iconMin(p.x + ui::S(8), p.y + (rowH - iconSize) * 0.5f);
            ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
            ImTextureID icon = hit.iconUrl.empty() ? 0 : textures.Get(hit.iconUrl);
            if (icon) {
                dl->AddImageRounded(icon, iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1),
                                    IM_COL32(255, 255, 255, 255), ui::S(11));
            } else {
                dl->AddRectFilled(iconMin, iconMax, ui::Alpha(colAccent, 40), ui::S(11));
                ui::Icon(dl, ui::IconMod, ImVec2((iconMin.x + iconMax.x) * 0.5f,
                                                 (iconMin.y + iconMax.y) * 0.5f),
                         ui::S(24), colAccent, 1.7f);
            }
            float textX = iconMax.x + ui::S(12);
            float textW = std::max(ui::S(60), rowW - (textX - p.x) - ui::S(34));
            ImGui::PushFont(uiFonts.semibold);
            std::string title = ui::Ellipsize(hit.title, textW, uiFonts.semibold);
            dl->AddText(ImVec2(textX, p.y + ui::S(10)), colText, title.c_str());
            ImGui::PopFont();
            ImGui::PushFont(uiFonts.smallFont);
            std::string desc = ui::Ellipsize(hit.description, textW, uiFonts.smallFont);
            dl->AddText(ImVec2(textX, p.y + ui::S(32)), colMuted, desc.c_str());
            ImGui::PopFont();
            ImGui::PushFont(uiFonts.tiny);
            std::string meta = hit.author.empty() ? "Неизвестный автор" : hit.author;
            meta += "   ·   " + ui::FormatCount(hit.downloads) + " загрузок";
            if (!hit.categories.empty()) meta += "   ·   " + hit.categories.front();
            dl->AddText(ImVec2(textX, p.y + ui::S(54)), colFaint, meta.c_str());
            ui::Icon(dl, ui::IconArrowRight, ImVec2(p.x + rowW - ui::S(18), p.y + rowH * 0.5f),
                     ui::S(13), hovered ? colAccent : colFaint, 1.6f);
            ImGui::PopFont();
            ImGui::PopID();
        }
        if (!hits.empty()) {
            ImGui::Dummy(ImVec2(0, ui::S(4)));
            if (hasMore) {
                if (loading) {
                    ImGui::PushFont(uiFonts.smallFont);
                    TextMuted("Загрузка...");
                    ImGui::PopFont();
                } else if (ui::GhostButton("Показать ещё",
                                           ImVec2(ImGui::GetContentRegionAvail().x, ui::S(36)))) {
                    StartModSearch(true);
                }
            }
        } else if (error.empty() && !loading) {
            ImGui::Dummy(ImVec2(0, ui::S(30)));
            TextMuted("Моды не найдены. Попробуйте изменить фильтры.");
        }
        ImGui::EndChild();
    }
    ui::EndCard();
    ImGui::EndGroup();

    ImGui::SameLine(0, gap);

    // ------------------------------------------------------------ installed
    ImGui::BeginGroup();
    ui::BeginCard("##mod_installed", ImVec2(installedW, panelH));
    {
        float innerW = ImGui::GetContentRegionAvail().x;
        float headerY = ImGui::GetCursorPosY();
        ImGui::PushFont(uiFonts.h3);
        float titleH = ImGui::GetTextLineHeight();
        ImGui::TextUnformatted("Установленные");
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::PushFont(uiFonts.smallFont);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colFaint));
        std::string count = std::to_string(installedMods_.size()) + " шт.";
        ImGui::TextUnformatted(count.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::SameLine(ui::S(20) + innerW - ui::S(28));
        ImGui::SetCursorPosY(headerY + (titleH - ui::S(28)) * 0.5f);
        if (ui::IconButton("##installed_refresh", ui::IconRefresh, ImVec2(ui::S(28), ui::S(28)), false,
                           0, "Обновить")) {
            installedModsDirty_ = true;
        }
        ImGui::Separator();

        ImGui::BeginChild("##installed_list", ImVec2(0, 0));
        if (installedMods_.empty()) {
            ImGui::Dummy(ImVec2(0, ui::S(30)));
            ImGui::PushFont(uiFonts.body);
            ImGui::TextUnformatted("Модов пока нет");
            ImGui::PopFont();
            TextMutedWrapped("Найдите мод слева и скачайте его — файл попадёт в папку mods "
                             "текущей сборки.");
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (size_t i = 0; i < installedMods_.size(); ++i) {
            InstalledMod& mod = installedMods_[i];
            ImGui::PushID((int)i);
            float rowW = ImGui::GetContentRegionAvail().x;
            float rowH = ui::S(58);
            ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(p, ImVec2(p.x + rowW, p.y + rowH),
                              ui::Alpha(IM_COL32(255, 255, 255, 255), 6), ui::S(10));
            ImGui::PushFont(uiFonts.semibold);
            std::string name = ui::Ellipsize(mod.name, rowW - ui::S(128), uiFonts.semibold);
            dl->AddText(ImVec2(p.x + ui::S(12), p.y + ui::S(9)),
                        mod.enabled ? colText : ui::Alpha(colMuted, 160), name.c_str());
            ImGui::PopFont();
            ImGui::PushFont(uiFonts.tiny);
            std::string meta = mod.version.empty() ? mod.file : mod.version;
            if (mod.size > 0) meta += "   ·   " + ui::FormatSize(mod.size);
            if (!mod.enabled) meta += "   ·   выключен";
            dl->AddText(ImVec2(p.x + ui::S(12), p.y + ui::S(32)), colFaint, meta.c_str());
            ImGui::PopFont();

            ImVec2 togglePos(p.x + rowW - ui::S(92), p.y + (rowH - ui::S(22)) * 0.5f);
            ImGui::SetCursorScreenPos(togglePos);
            ImGui::InvisibleButton("##mod_toggle", ImVec2(ui::S(40), ui::S(22)));
            bool toggleHovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) {
                if (SetModEnabled(mod, !mod.enabled)) {
                    mod.enabled = !mod.enabled;
                    ToastMsg(mod.enabled ? "Мод включён" : "Мод выключен", 1);
                } else {
                    ToastMsg("Не удалось переименовать файл мода", 2);
                }
            }
            ImU32 track = mod.enabled ? colAccent : ui::Hex("#2A3047");
            dl->AddRectFilled(togglePos, ImVec2(togglePos.x + ui::S(40), togglePos.y + ui::S(22)),
                              toggleHovered ? ui::Lighten(track, 0.08f) : track, ui::S(11));
            float knobR = ui::S(8);
            float knobX = mod.enabled ? togglePos.x + ui::S(40) - knobR - ui::S(3)
                                      : togglePos.x + knobR + ui::S(3);
            dl->AddCircleFilled(ImVec2(knobX, togglePos.y + ui::S(11)), knobR,
                                IM_COL32(255, 255, 255, 245));

            ImGui::SetCursorScreenPos(ImVec2(p.x + rowW - ui::S(46), p.y + (rowH - ui::S(28)) * 0.5f));
            if (ui::IconButton("##mod_delete", ui::IconTrash, ImVec2(ui::S(28), ui::S(28)), false,
                               colFaint, "Удалить мод")) {
                deleteTarget_ = mod;
                deleteNeedOpen_ = true;
            }
            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH));
            ImGui::Dummy(ImVec2(rowW, ui::S(6)));
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    ui::EndCard();
    ImGui::EndGroup();
}
