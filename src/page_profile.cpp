#include "app.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::string FormatPlaytime(long long seconds) {
    if (seconds <= 0) return "—";
    long long hours = seconds / 3600;
    long long minutes = (seconds % 3600) / 60;
    if (hours > 0) {
        return minutes > 0 ? std::to_string(hours) + " ч " + std::to_string(minutes) + " мин"
                           : std::to_string(hours) + " ч";
    }
    if (minutes > 0) return std::to_string(minutes) + " мин";
    return std::to_string(seconds) + " сек";
}

std::string FormatLaunchDate(const std::string& stamp) {
    if (stamp.size() >= 16 && stamp[4] == '-' && stamp[7] == '-') {
        return ui::FormatDate(stamp.substr(0, 10));
    }
    return stamp.empty() ? "—" : stamp;
}

std::string FormatLaunchTime(const std::string& stamp) {
    if (stamp.size() >= 16) return stamp.substr(11, 5);
    return {};
}

int CountInstalledVersions(const fs::path& versionsDir) {
    std::error_code ec;
    int count = 0;
    if (!fs::is_directory(versionsDir, ec)) return 0;
    for (const auto& entry : fs::directory_iterator(versionsDir, ec)) {
        if (ec) break;
        if (!entry.is_directory(ec)) continue;
        std::string id = entry.path().filename().string();
        if (FileExists(entry.path() / (id + ".json"))) count++;
    }
    return count;
}

void StatTile(const char* id, ui::IconType icon, const char* label, const std::string& value,
              const std::string& subtitle, float width, float height) {
    ui::BeginCard(id, ImVec2(width, height));
    ImGui::PushFont(uiFonts.tiny);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::PushFont(uiFonts.h2);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopFont();
    ImGui::PushFont(uiFonts.tiny);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colFaint));
    ImGui::TextUnformatted(subtitle.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();
    ImVec2 center(windowPos.x + windowSize.x - ui::S(34),
                  windowPos.y + windowSize.y - ui::S(30));
    dl->AddCircleFilled(center, ui::S(18), ui::Alpha(colAccent, 26), 32);
    ui::Icon(dl, icon, center, ui::S(19), ui::Alpha(colAccent, 210), 1.7f);
    ui::EndCard();
}

void InfoRow(const char* label, const std::string& value, bool accent = false) {
    float rowW = ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::PushFont(uiFonts.smallFont);
    float lineH = ImGui::GetTextLineHeight();
    dl->AddText(p, colMuted, label);
    std::string shown = ui::Ellipsize(value, rowW * 0.56f, uiFonts.smallFont);
    ImVec2 vs = ImGui::CalcTextSize(shown.c_str());
    dl->AddText(ImVec2(p.x + rowW - vs.x, p.y), accent ? colAccent : colText, shown.c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(rowW, lineH));
}

} // namespace

void App::DrawProfile() {
    PageHeader("Профиль", "Аккаунт, история запусков и статистика игрока");

    auto manifest = mc.Manifest();
    float availW = ImGui::GetContentRegionAvail().x;
    float gap = ui::S(16);
    float leftW = std::clamp(availW * 0.30f, ui::S(300), ui::S(360));
    float rightW = std::max(ui::S(320), availW - leftW - gap);
    float panelH = std::max(ui::S(480), ImGui::GetContentRegionAvail().y - ui::S(2));

    const Account* account = cfg.Active();

    // ------------------------------------------------------------ player card
    ImGui::BeginGroup();
    ui::BeginCard("##profile_card", ImVec2(leftW, panelH));
    {
        float innerW = ImGui::GetContentRegionAvail().x;
        float avatarSize = ui::S(92);
        ImGui::SetCursorPosX(ui::S(20) + (innerW - avatarSize) * 0.5f);
        ImVec2 avatarPos = ImGui::GetCursorScreenPos();
        DrawAvatar(account ? account->name : "", avatarPos, avatarSize);
        ImGui::Dummy(ImVec2(avatarSize, avatarSize));

        ImGui::Dummy(ImVec2(0, ui::S(10)));
        ImGui::PushFont(uiFonts.h2);
        std::string name = account ? account->name : "Гость";
        std::string shownName = ui::Ellipsize(name, innerW - ui::S(8), uiFonts.h2);
        float nameW = ImGui::CalcTextSize(shownName.c_str()).x;
        ImGui::SetCursorPosX(ui::S(20) + (innerW - nameW) * 0.5f);
        ImGui::TextUnformatted(shownName.c_str());
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0, ui::S(2)));
        ImGui::PushFont(uiFonts.smallFont);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::ColorConvertU32ToFloat4(account ? colGreen : colFaint));
        std::string mode = account ? (account->type == "msa" ? "Microsoft" : "Оффлайн-режим")
                                   : "Аккаунт не выбран";
        float modeW = ImGui::CalcTextSize(mode.c_str()).x;
        ImGui::SetCursorPosX(ui::S(20) + (innerW - modeW) * 0.5f);
        ImGui::TextUnformatted(mode.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();

        if (account && !account->uuid.empty()) {
            ImGui::PushFont(uiFonts.mono);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colFaint));
            std::string uuid = ui::Ellipsize(account->uuid, innerW - ui::S(8), uiFonts.mono);
            float uuidW = ImGui::CalcTextSize(uuid.c_str()).x;
            ImGui::SetCursorPosX(ui::S(20) + (innerW - uuidW) * 0.5f);
            ImGui::TextUnformatted(uuid.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }

        ImGui::Dummy(ImVec2(0, ui::S(12)));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, ui::S(4)));

        InfoRow("Аккаунтов", std::to_string(cfg.accounts.size()));
        InfoRow("Последний запуск", FormatLaunchDate(cfg.s.lastLaunch),
                !cfg.s.lastLaunch.empty());
        InfoRow("Последняя версия",
                cfg.s.lastVersion.empty() ? "—" : cfg.s.lastVersion);
        InfoRow("Установлено версий", std::to_string(CountInstalledVersions(mc.VersionsDir())));
        InfoRow("Модов в сборке", std::to_string(installedMods_.size()));

        ImGui::Dummy(ImVec2(0, ui::S(14)));
        if (ui::SoftButton("Сменить аккаунт", ImVec2(innerW, ui::S(36)), true, ui::IconUsers)) {
            SetPage(Page::Accounts);
        }
        ImGui::Dummy(ImVec2(0, ui::S(8)));
        if (ui::GhostButton(account ? "Открыть папку игры" : "Создать аккаунт",
                            ImVec2(innerW, ui::S(36)), true,
                            account ? ui::IconFolder : ui::IconPlus)) {
            if (account) ui::OpenPath(GameDirString());
            else SetPage(Page::Accounts);
        }
    }
    ui::EndCard();
    ImGui::EndGroup();

    ImGui::SameLine(0, gap);

    // ------------------------------------------------------------ right side
    ImGui::BeginGroup();
    float statGap = gap;
    float statW = (rightW - statGap * 2.0f) / 3.0f;
    float statH = ui::S(114);
    float stackGap = statGap + ImGui::GetStyle().ItemSpacing.y * 2.0f;

    std::string lastDate = FormatLaunchDate(cfg.s.lastLaunch);
    std::string lastTime = FormatLaunchTime(cfg.s.lastLaunch);
    std::string lastSub = lastTime.empty() ? "Запусков ещё не было"
                                           : lastTime + "  ·  " + cfg.s.lastVersion;
    StatTile("##stat_last", ui::IconClock, "ПОСЛЕДНИЙ ЗАПУСК", lastDate, lastSub, statW, statH);
    ImGui::SameLine(0, statGap);
    StatTile("##stat_launches", ui::IconChart, "ВСЕГО ЗАПУСКОВ",
             std::to_string(cfg.s.launchCount), "за всё время", statW, statH);
    ImGui::SameLine(0, statGap);
    StatTile("##stat_playtime", ui::IconPlay, "ВРЕМЯ В ИГРЕ", FormatPlaytime(cfg.s.playSeconds),
             "суммарно", statW, statH);
    ImGui::Dummy(ImVec2(0, gap));

    // quick launch
    ui::BeginCard("##profile_quick", ImVec2(rightW, ui::S(168)));
    {
        float quickInner = ImGui::GetContentRegionAvail().x;
        ImGui::PushFont(uiFonts.h3);
        ImGui::TextUnformatted("Быстрый запуск");
        ImGui::PopFont();
        ImGui::SameLine(0, ui::S(12));
        ImGui::PushFont(uiFonts.smallFont);
        TextMuted(gameRunning_.load() ? "Minecraft уже запущен" : "Версия и загрузчик из настроек");
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0, ui::S(10)));
        float frameH = ImGui::GetFrameHeight();
        if (VersionCombo("##profile_version", selectedVersion_, manifest.get(),
                         cfg.s.showSnapshots, ui::S(200))) {
            cfg.s.selectedVersion = selectedVersion_;
            cfg.Save();
        }
        ImGui::SameLine(0, ui::S(10));
        if (LoaderCombo("##profile_loader", loaderIdx_, ui::S(150))) {
            cfg.s.loaderKind = loaderIdx_;
            cfg.Save();
        }
        ImGui::SameLine(0, ui::S(12));
        bool running = gameRunning_.load();
        float playW = std::max(ui::S(150), quickInner - ui::S(200) - ui::S(150) - ui::S(32));
        if (ui::PrimaryButton(running ? "Игра запущена" : "Играть",
                              ImVec2(playW, frameH), !running, ui::IconPlay, !running)) {
            StartPlay(GameVersion(), loaderIdx_, SelectedLoaderVersion());
        }

        ImGui::Dummy(ImVec2(0, ui::S(10)));
        bool ready = IsReadyToPlay(GameVersion(), loaderIdx_);
        std::string status = ready
                                 ? "Клиент установлен — можно запускать"
                                 : "Клиент не установлен — лаунчер скачает всё при запуске";
        ImGui::PushFont(uiFonts.smallFont);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::ColorConvertU32ToFloat4(ready ? colGreen : colMuted));
        ImGui::TextUnformatted(status.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ui::EndCard();
    ImGui::Dummy(ImVec2(0, gap));

    // recent versions
    float recentH = std::max(ui::S(180), panelH - statH - ui::S(168) - stackGap * 2.0f);
    ui::BeginCard("##profile_recent", ImVec2(rightW, recentH));
    {
        ImGui::PushFont(uiFonts.h3);
        ImGui::TextUnformatted("Недавние версии");
        ImGui::PopFont();
        ImGui::SameLine(0, ui::S(12));
        ImGui::PushFont(uiFonts.smallFont);
        TextMuted("История запусков");
        ImGui::PopFont();
        ImGui::Separator();

        const std::vector<std::string>& recent = cfg.s.recentVersions;
        if (recent.empty()) {
            float innerW = ImGui::GetContentRegionAvail().x;
            ImVec2 winPos = ImGui::GetWindowPos();
            ImVec2 winSize = ImGui::GetWindowSize();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            float cy = winPos.y + winSize.y * 0.46f;
            ImVec2 iconCenter(winPos.x + winSize.x * 0.5f, cy - ui::S(26));
            dl->AddCircleFilled(iconCenter, ui::S(30), ui::Alpha(colAccent, 24), 40);
            ui::Icon(dl, ui::IconClock, iconCenter, ui::S(26), ui::Alpha(colAccent, 210), 1.8f);

            ImGui::PushFont(uiFonts.semibold);
            const char* title = "История пока пуста";
            float titleW = ImGui::CalcTextSize(title).x;
            ImGui::SetCursorPosX(ui::S(20) + (innerW - titleW) * 0.5f);
            ImGui::SetCursorPosY(cy + ui::S(12) - winPos.y);
            ImGui::TextUnformatted(title);
            ImGui::PopFont();
            ImGui::PushFont(uiFonts.smallFont);
            const char* hint = "Запустите игру — здесь появятся последние версии";
            float hintW = ImGui::CalcTextSize(hint).x;
            ImGui::SetCursorPosX(ui::S(20) + (innerW - hintW) * 0.5f);
            ImGui::TextUnformatted(hint);
            ImGui::PopFont();
        } else {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            for (size_t i = 0; i < recent.size(); ++i) {
                const std::string& version = recent[i];
                ImGui::PushID((int)i);
                float rowW = ImGui::GetContentRegionAvail().x;
                float rowH = ui::S(54);
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("##recent_row", ImVec2(rowW, rowH));

                bool installed = mc.BaseInstalled(version);
                ImGui::PushFont(uiFonts.semibold);
                dl->AddText(ImVec2(p.x + ui::S(10), p.y + ui::S(10)), colText, version.c_str());
                ImGui::PopFont();
                ImGui::PushFont(uiFonts.tiny);
                dl->AddText(ImVec2(p.x + ui::S(10), p.y + ui::S(31)),
                            installed ? colGreen : colFaint,
                            installed ? "Установлена" : "Не установлена");
                ImGui::PopFont();

                float btnW = ui::S(104);
                ImGui::SetCursorScreenPos(
                    ImVec2(p.x + rowW - btnW, p.y + (rowH - ui::S(30)) * 0.5f));
                bool launched = false;
                if (installed) {
                    launched = ui::GhostButton("Играть", ImVec2(btnW, ui::S(30)), true, ui::IconPlay);
                } else {
                    launched = ui::SoftButton("Установить", ImVec2(btnW, ui::S(30)), true,
                                              ui::IconDownload);
                }
                if (launched) {
                    selectedVersion_ = version;
                    loaderIdx_ = 0;
                    cfg.s.loaderKind = 0;
                    cfg.Save();
                    if (installed) StartPlay(version, 0, "");
                    else StartInstall(version, 0, "", false);
                }
                ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH));
                ImGui::Separator();
                ImGui::PopID();
            }
        }
    }
    ui::EndCard();
    ImGui::EndGroup();
}
