#include "app.hpp"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace {

void SmallLabel(const char* text) {
    ImGui::PushFont(uiFonts.tiny);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

} // namespace

void App::DrawHome() {
    PageHeader("Главная", "Быстрый запуск, последние обновления и состояние лаунчера");

    auto manifest = mc.Manifest();
    float availW = ImGui::GetContentRegionAvail().x;
    if (availW < ui::S(200)) availW = ui::S(200);

    if (installTask_ && installTask_->IsRunning()) {
        ui::BeginCard("##install_strip", ImVec2(availW, ui::S(96)));
        ImGui::PushFont(uiFonts.h3);
        ImGui::TextUnformatted(installTask_->title.c_str());
        ImGui::PopFont();
        std::string status = installTask_->Status();
        ImGui::PushFont(uiFonts.smallFont);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
        ImGui::TextUnformatted(status.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, ui::S(2)));
        float fraction = installTask_->progress.load();
        char overlay[32];
        snprintf(overlay, sizeof(overlay), "%.0f%%", fraction * 100.0f);
        ui::ProgressBar(fraction, ImVec2(ImGui::GetContentRegionAvail().x, ui::S(10)), colAccent,
                        overlay);
        ui::EndCard();
        ImGui::Dummy(ImVec2(0, ui::S(14)));
    }

    // ---------------------------------------------------------------- hero
    const float heroH = ui::S(224);
    ui::BeginCard("##hero", ImVec2(availW, heroH), ui::Hex("#161A2E"));
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 center(winPos.x + availW - ui::S(158), winPos.y + heroH * 0.52f);
        for (int i = 0; i < 14; ++i) {
            dl->AddCircleFilled(center, ui::S(96) - i * ui::S(5.5f), ui::Alpha(colAccent, 3), 64);
        }
        dl->AddCircle(center, ui::S(66), ui::Alpha(colAccent, 55), 64, 1.0f);
        dl->AddCircle(center, ui::S(100), ui::Alpha(colCyan, 26), 64, 1.0f);
        ui::Icon(dl, ui::IconCube, center, ui::S(104), ui::Alpha(colAccent, 235), 2.4f);
        dl->AddCircleFilled(ImVec2(center.x + ui::S(86), center.y - ui::S(74)), ui::S(4), colCyan);
        dl->AddCircleFilled(ImVec2(center.x - ui::S(82), center.y + ui::S(66)), ui::S(3), colAccent);

        ui::Badge("XPLAUNCHER  ·  MINECRAFT", colAccent);
        ImGui::Dummy(ImVec2(0, ui::S(6)));

        ImGui::PushFont(uiFonts.h1);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colText));
        ImGui::TextUnformatted("Твой Minecraft");
        ImGui::PopStyleColor();
        ImGui::SameLine(0, ui::S(12));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colAccent));
        ImGui::TextUnformatted("в один клик");
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::PushFont(uiFonts.smallFont);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
        ImGui::PushTextWrapPos(availW * 0.56f);
        ImGui::TextUnformatted(
            "Любые версии, Fabric, Forge, NeoForge и Quilt, каталоги модов и автоматическая "
            "установка всего необходимого.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::PopFont();

        const float controlH = ui::S(38);
        ImGui::SetCursorPos(ImVec2(ui::S(20), heroH - ui::S(18) - controlH));
        if (VersionCombo("##home_version", selectedVersion_, manifest.get(), cfg.s.showSnapshots,
                         ui::S(196))) {
            cfg.s.selectedVersion = selectedVersion_;
            cfg.Save();
        }
        ImGui::SameLine(0, ui::S(10));
        if (LoaderCombo("##home_loader", loaderIdx_, ui::S(160))) {
            cfg.s.loaderKind = loaderIdx_;
            cfg.Save();
        }
        ImGui::SameLine(0, ui::S(12));
        bool running = gameRunning_.load();
        if (ui::PrimaryButton(running ? "Игра запущена" : "Играть", ImVec2(ui::S(186), controlH),
                              !running, ui::IconPlay, !running)) {
            StartPlay(GameVersion(), loaderIdx_, SelectedLoaderVersion());
        }
    }
    ui::EndCard();
    ImGui::Dummy(ImVec2(0, ui::S(16)));

    // ---------------------------------------------------------------- stats
    float gap = ui::S(16);
    float cardW = (availW - gap * 2.0f) / 3.0f;
    float cardH = ui::S(168);
    float actionY = cardH - ui::S(18) - ui::S(30);

    ImGui::BeginGroup();
    ui::BeginCard("##stat_account", ImVec2(cardW, cardH));
    {
        SmallLabel("АККАУНТ");
        ImGui::Dummy(ImVec2(0, ui::S(8)));
        const Account* account = cfg.Active();
        if (account) {
            ImGui::BeginGroup();
            ImVec2 p = ImGui::GetCursorScreenPos();
            DrawAvatar(account->name, p, ui::S(40));
            ImGui::Dummy(ImVec2(ui::S(40), ui::S(40)));
            ImGui::SameLine(0, ui::S(12));
            ImGui::BeginGroup();
            ImGui::PushFont(uiFonts.h3);
            std::string name = ui::Ellipsize(account->name, cardW - ui::S(110), uiFonts.h3);
            ImGui::TextUnformatted(name.c_str());
            ImGui::PopFont();
            TextMuted(account->type == "msa" ? "Microsoft" : "Оффлайн-аккаунт");
            ImGui::EndGroup();
            ImGui::EndGroup();
        } else {
            TextMuted("Аккаунт не выбран");
        }
        ImGui::SetCursorPosY(actionY);
        if (ui::SoftButton(account ? "Профиль" : "Создать аккаунт",
                           ImVec2(ui::S(150), ui::S(30)))) {
            SetPage(account ? Page::Profile : Page::Accounts);
        }
    }
    ui::EndCard();
    ImGui::EndGroup();

    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    ui::BeginCard("##stat_version", ImVec2(cardW, cardH));
    {
        SmallLabel("ПОСЛЕДНИЙ РЕЛИЗ");
        ImGui::Dummy(ImVec2(0, ui::S(8)));
        ImGui::PushFont(uiFonts.h2);
        ImGui::TextUnformatted(manifest ? manifest->latestRelease.c_str() : "—");
        ImGui::PopFont();
        TextMuted(manifest ? "Актуальная версия Minecraft" : "Загрузка списка версий...");
        ImGui::SetCursorPosY(actionY);
        if (ui::SoftButton("Все версии", ImVec2(ui::S(150), ui::S(30)), manifest != nullptr,
                           ui::IconArrowRight)) {
            SetPage(Page::Versions);
        }
    }
    ui::EndCard();
    ImGui::EndGroup();

    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    ui::BeginCard("##stat_java", ImVec2(cardW, cardH));
    {
        SmallLabel("JAVA");
        ImGui::Dummy(ImVec2(0, ui::S(8)));
        std::vector<JavaInstall> javaList;
        {
            std::lock_guard lock(javaMutex_);
            javaList = javaList_;
        }
        std::string mainText = "Поиск...";
        std::string subText = "Сканирование системы";
        if (!javaList.empty()) {
            int best = 0;
            for (const auto& java : javaList) best = std::max(best, java.major);
            mainText = "Java " + std::to_string(best);
            subText = "Найдено установок: " + std::to_string(javaList.size());
        } else if (javaLoading_.load()) {
            mainText = "Поиск...";
            subText = "Сканирование системы";
        } else if (javaTask_ && javaTask_->IsRunning()) {
            mainText = "Установка Java";
            subText = "Скачивание с Adoptium";
        } else {
            mainText = "Не найдена";
            subText = "Лаунчер установит её сам при запуске";
        }
        ImGui::PushFont(uiFonts.h2);
        ImGui::TextUnformatted(mainText.c_str());
        ImGui::PopFont();
        TextMuted(subText.c_str());
        ImGui::SetCursorPosY(actionY);
        if (ui::SoftButton("Настройки Java", ImVec2(ui::S(150), ui::S(30)))) {
            SetPage(Page::Settings);
        }
    }
    ui::EndCard();
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0, ui::S(16)));

    // ---------------------------------------------------------------- recent
    const float rowH = ui::S(50);
    ui::BeginCard("##recent", ImVec2(availW, ui::S(334)));
    {
        float innerW = ImGui::GetContentRegionAvail().x;
        float headerY = ImGui::GetCursorPosY();
        ImGui::PushFont(uiFonts.h3);
        ImGui::TextUnformatted("Последние версии");
        float titleH = ImGui::GetTextLineHeight();
        ImGui::PopFont();
        ImGui::SameLine(ui::S(20) + innerW - ui::S(128));
        ImGui::SetCursorPosY(headerY + (titleH - ui::S(28)) * 0.5f);
        if (ui::SoftButton("Все версии", ImVec2(ui::S(128), ui::S(28)), manifest != nullptr,
                           ui::IconArrowRight)) {
            SetPage(Page::Versions);
        }
        ImGui::Dummy(ImVec2(0, ui::S(4)));
        ImGui::Separator();

        if (!manifest) {
            ImGui::Dummy(ImVec2(0, ui::S(10)));
            ImVec2 p = ImGui::GetCursorScreenPos();
            ui::Spinner(ImVec2(p.x + ui::S(11), p.y + ui::S(11)), ui::S(11), colAccent);
            ImGui::SameLine(0, ui::S(30));
            std::string error = mc.ManifestError();
            TextMuted(error.empty() ? "Загрузка списка версий..." : error.c_str());
        } else {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            int shown = 0;
            for (const auto& entry : manifest->versions) {
                if (entry.type != "release") continue;
                if (shown >= 4) break;
                ImGui::PushID(shown);
                float rowW = ImGui::GetContentRegionAvail().x;
                ImVec2 p = ImGui::GetCursorScreenPos();

                ImGui::PushFont(uiFonts.semibold);
                dl->AddText(ImVec2(p.x + ui::S(10), p.y + ui::S(8)), colText, entry.id.c_str());
                ImGui::PopFont();
                ImGui::PushFont(uiFonts.tiny);
                std::string sub = "Релиз  ·  " + ui::FormatDate(entry.releaseTime);
                dl->AddText(ImVec2(p.x + ui::S(10), p.y + ui::S(28)), colFaint, sub.c_str());
                ImGui::PopFont();

                bool installed = mc.BaseInstalled(entry.id);
                ImGui::SetCursorScreenPos(
                    ImVec2(p.x + rowW - ui::S(120), p.y + (rowH - ui::S(30)) * 0.5f));
                bool pressed = false;
                if (installed) {
                    pressed = ui::GhostButton("Играть", ImVec2(ui::S(120), ui::S(30)), true,
                                              ui::IconPlay);
                } else {
                    pressed = ui::SoftButton("Установить", ImVec2(ui::S(120), ui::S(30)), true,
                                             ui::IconDownload);
                }
                if (pressed) {
                    selectedVersion_ = entry.id;
                    loaderIdx_ = 0;
                    cfg.s.loaderKind = 0;
                    cfg.Save();
                    if (installed) StartPlay(entry.id, 0, "");
                    else StartInstall(entry.id, 0, "", false);
                }

                ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH));
                if (shown < 3) {
                    dl->AddLine(ImVec2(p.x + ui::S(10), p.y + rowH - 1),
                                ImVec2(p.x + rowW - ui::S(10), p.y + rowH - 1), colBorder, 1.0f);
                }
                ImGui::Dummy(ImVec2(0, ui::S(1)));
                ImGui::PopID();
                shown++;
            }
        }
    }
    ui::EndCard();
}
