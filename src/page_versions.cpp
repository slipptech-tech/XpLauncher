#include "app.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

const char* VersionTypeLabel(const std::string& type) {
    if (type == "release") return "Релиз";
    if (type == "snapshot") return "Снимок";
    if (type == "old_beta") return "Старая beta";
    if (type == "old_alpha") return "Старая alpha";
    return type.c_str();
}

ImU32 VersionTypeColor(const std::string& type) {
    if (type == "release") return colGreen;
    if (type == "snapshot") return colYellow;
    return colFaint;
}

} // namespace

void App::DrawVersions() {
    PageHeader("Версии", "Все релизы и снимки Minecraft, установка любых загрузчиков");

    auto manifest = mc.Manifest();
    float availW = ImGui::GetContentRegionAvail().x;
    float gap = ui::S(16);
    float listW = std::max(ui::S(320), availW * 0.40f);
    float detailW = std::max(ui::S(320), availW - listW - gap);
    float panelH = std::max(ui::S(520), ImGui::GetContentRegionAvail().y - ui::S(2));

    // ------------------------------------------------------------ list
    ImGui::BeginGroup();
    ui::BeginCard("##versions_list", ImVec2(listW, panelH));
    {
        float innerW = ImGui::GetContentRegionAvail().x;
        float headerY = ImGui::GetCursorPosY();
        ImGui::PushFont(uiFonts.h3);
        ImGui::TextUnformatted("Список версий");
        float titleH = ImGui::GetTextLineHeight();
        ImGui::PopFont();
        if (manifest) {
            ImGui::SameLine(0, ui::S(10));
            ImGui::PushFont(uiFonts.smallFont);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colFaint));
            std::string count = std::to_string(manifest->versions.size()) + " версий";
            ImGui::TextUnformatted(count.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        ImGui::SameLine(ui::S(20) + innerW - ui::S(32));
        ImGui::SetCursorPosY(headerY + (titleH - ui::S(28)) * 0.5f);
        if (ui::IconButton("##refresh_manifest", ui::IconRefresh, ImVec2(ui::S(32), ui::S(28)), false,
                           0, "Обновить список")) {
            EnsureManifest(true);
        }

        static char filter[64] = {};
        ui::SearchInput("##version_filter_page", filter, sizeof(filter), "Поиск версии", 0.0f);
        if (ui::Toggle("Показывать снимки", &cfg.s.showSnapshots)) cfg.Save();
        ImGui::Separator();

        ImGui::BeginChild("##version_rows", ImVec2(0, 0));
        if (!manifest) {
            ImGui::Dummy(ImVec2(0, ui::S(12)));
            ImVec2 p = ImGui::GetCursorScreenPos();
            ui::Spinner(ImVec2(p.x + ui::S(11), p.y + ui::S(11)), ui::S(10), colAccent);
            ImGui::SameLine(0, ui::S(30));
            std::string error = mc.ManifestError();
            TextMuted(error.empty() ? "Загрузка списка версий..." : error.c_str());
        } else {
            std::string needle = ToLower(Trim(filter));
            int rendered = 0;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            for (size_t i = 0; i < manifest->versions.size(); ++i) {
                const VersionEntry& entry = manifest->versions[i];
                if (!cfg.s.showSnapshots && entry.type != "release") continue;
                if (!needle.empty() && ToLower(entry.id).find(needle) == std::string::npos) continue;
                ImGui::PushID((int)i);
                float rowW = ImGui::GetContentRegionAvail().x;
                ImVec2 p = ImGui::GetCursorScreenPos();
                bool selected = selectedVersion_ == entry.id;
                if (ImGui::Selectable("##row", selected, 0, ImVec2(rowW, ui::S(42)))) {
                    selectedVersion_ = entry.id;
                    cfg.s.selectedVersion = entry.id;
                    cfg.Save();
                }
                ImGui::PopID();

                ImGui::PushFont(uiFonts.semibold);
                dl->AddText(ImVec2(p.x + ui::S(10), p.y + ui::S(5)), colText, entry.id.c_str());
                ImGui::PopFont();
                ImGui::PushFont(uiFonts.tiny);
                std::string date = ui::FormatDate(entry.releaseTime);
                ImVec2 dateSize = ImGui::CalcTextSize(date.c_str());
                dl->AddText(ImVec2(p.x + rowW - dateSize.x - ui::S(10), p.y + ui::S(7)), colFaint,
                            date.c_str());
                const char* typeLabel = VersionTypeLabel(entry.type);
                ImGui::PopFont();
                ImGui::PushFont(uiFonts.tiny);
                dl->AddText(ImVec2(p.x + ui::S(10), p.y + ui::S(23)), VersionTypeColor(entry.type),
                            typeLabel);
                ImGui::PopFont();
                rendered++;
            }
            if (rendered == 0) {
                ImGui::Dummy(ImVec2(0, ui::S(8)));
                TextMuted("Ничего не найдено");
            }
        }
        ImGui::EndChild();
    }
    ui::EndCard();
    ImGui::EndGroup();

    ImGui::SameLine(0, gap);

    // ------------------------------------------------------------ detail
    ImGui::BeginGroup();
    ui::BeginCard("##version_detail", ImVec2(detailW, panelH));
    {
        if (selectedVersion_.empty()) {
            ImGui::Dummy(ImVec2(0, ui::S(40)));
            ImGui::PushFont(uiFonts.h3);
            ImGui::TextUnformatted("Выберите версию");
            ImGui::PopFont();
            TextMuted("Список версий слева. Установка начинается одной кнопкой.");
            ui::EndCard();
            ImGui::EndGroup();
            return;
        }

        const VersionEntry* entry = nullptr;
        if (manifest) {
            for (const auto& v : manifest->versions) {
                if (v.id == selectedVersion_) {
                    entry = &v;
                    break;
                }
            }
        }

        ImGui::PushFont(uiFonts.h1);
        ImGui::TextUnformatted(selectedVersion_.c_str());
        ImGui::PopFont();
        ImGui::PushFont(uiFonts.smallFont);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::ColorConvertU32ToFloat4(VersionTypeColor(entry ? entry->type : "")));
        std::string typeLine = entry ? VersionTypeLabel(entry->type) : "Версия";
        if (entry && !entry->releaseTime.empty()) typeLine += "  ·  " + ui::FormatDate(entry->releaseTime);
        ImGui::TextUnformatted(typeLine.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0, ui::S(6)));
        ImGui::Separator();

        bool baseInstalled = mc.BaseInstalled(selectedVersion_);
        bool loaderInstalled = loaderIdx_ == 0 || mc.LoaderInstalled(selectedVersion_, loaderIdx_);
        bool ready = IsReadyToPlay(selectedVersion_, loaderIdx_);
        bool busy = AnyInstallRunning();

        ImGui::Dummy(ImVec2(0, ui::S(4)));
        ImGui::BeginGroup();
        {
            ImGui::PushFont(uiFonts.smallFont);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
            ImGui::TextUnformatted("Клиент");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::PushFont(uiFonts.semibold);
            ImGui::TextUnformatted(baseInstalled ? "Установлен" : "Не установлен");
            ImGui::PopFont();
        }
        ImGui::EndGroup();
        ImGui::SameLine(0, ui::S(42));
        ImGui::BeginGroup();
        {
            ImGui::PushFont(uiFonts.smallFont);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
            ImGui::TextUnformatted("Загрузчик");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::PushFont(uiFonts.semibold);
            ImGui::TextUnformatted(loaderIdx_ == 0
                                       ? "Vanilla"
                                       : (loaderInstalled ? "Установлен" : LoaderName(loaderIdx_)));
            ImGui::PopFont();
        }
        ImGui::EndGroup();

        ImGui::Dummy(ImVec2(0, ui::S(10)));
        ImGui::PushFont(uiFonts.smallFont);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
        ImGui::TextUnformatted("Тип загрузчика");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::SetNextItemWidth(detailW - ui::S(48));
        if (LoaderCombo("##detail_loader", loaderIdx_, detailW - ui::S(48))) {
            cfg.s.loaderKind = loaderIdx_;
            cfg.Save();
        }

        if (loaderIdx_ > 0) {
            std::vector<LoaderVersion> versions;
            bool loading = false;
            std::string error;
            {
                std::lock_guard lock(loaders_.mutex);
                versions = loaders_.versions;
                loading = loaders_.loading;
                error = loaders_.error;
            }
            ImGui::Dummy(ImVec2(0, ui::S(6)));
            ImGui::PushFont(uiFonts.smallFont);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
            ImGui::TextUnformatted("Версия загрузчика");
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::SetNextItemWidth(detailW - ui::S(48));
            std::string preview =
                cfg.s.loaderVersion.empty() ? "Последняя (авто)" : cfg.s.loaderVersion;
            if (ImGui::BeginCombo("##loader_version", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
                if (ImGui::Selectable("Последняя (авто)", cfg.s.loaderVersion.empty())) {
                    cfg.s.loaderVersion.clear();
                    cfg.Save();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::Separator();
                ImGui::BeginChild("##loader_version_list",
                                  ImVec2(ImGui::GetContentRegionAvail().x, ui::S(260)));
                for (const auto& version : versions) {
                    std::string label = version.version;
                    if (!version.stable) label += "   · beta";
                    if (ImGui::Selectable(label.c_str(), cfg.s.loaderVersion == version.version)) {
                        cfg.s.loaderVersion = version.version;
                        cfg.Save();
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (loading) TextMuted("Загрузка версий загрузчика...");
                if (!error.empty()) TextMuted(error.c_str());
                if (!loading && error.empty() && versions.empty()) TextMuted("Нет доступных версий");
                ImGui::EndChild();
                ImGui::EndCombo();
            }
        }

        ImGui::Dummy(ImVec2(0, ui::S(14)));
        if (ui::PrimaryButton(busy ? "Загрузка..." : "Установить", ImVec2(ui::S(188), ui::S(42)),
                              !busy, ui::IconDownload, !busy)) {
            StartInstall(selectedVersion_, loaderIdx_, SelectedLoaderVersion(), false);
        }
        ImGui::SameLine(0, ui::S(10));
        if (ui::GhostButton(ready ? "Играть" : "Установить и играть", ImVec2(ui::S(208), ui::S(42)),
                            !busy, ui::IconPlay)) {
            StartPlay(selectedVersion_, loaderIdx_, SelectedLoaderVersion());
        }
        ImGui::SameLine(0, ui::S(10));
        if (ui::IconButton("##open_versions", ui::IconFolder, ImVec2(ui::S(42), ui::S(42)), false, 0,
                           "Открыть папку версий")) {
            std::string dir = (mc.VersionsDir() / selectedVersion_).string();
            ui::OpenPath(DirectoryExists(dir) ? dir : GameDirString());
        }

        if (installTask_) {
            ImGui::Dummy(ImVec2(0, ui::S(10)));
            std::string status = installTask_->Status();
            ImGui::PushFont(uiFonts.smallFont);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
            ImGui::TextUnformatted(status.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
            float fraction = installTask_->progress.load();
            char overlay[32];
            snprintf(overlay, sizeof(overlay), "%.0f%%", fraction * 100.0f);
            ui::ProgressBar(fraction, ImVec2(ImGui::GetContentRegionAvail().x, ui::S(10)), colAccent,
                            overlay);
        }

        ImGui::Dummy(ImVec2(0, ui::S(10)));
        ImGui::PushFont(uiFonts.smallFont);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
        ImGui::TextUnformatted("Журнал");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, ui::S(4)));

        float logW = ImGui::GetContentRegionAvail().x;
        float logH = std::max(ui::S(120), ImGui::GetContentRegionAvail().y);
        ImVec2 logPos = ImGui::GetCursorScreenPos();
        ImDrawList* wdl = ImGui::GetWindowDrawList();
        wdl->AddRectFilled(logPos, ImVec2(logPos.x + logW, logPos.y + logH), ui::Hex("#121524"),
                           ui::S(10));
        wdl->AddRect(logPos, ImVec2(logPos.x + logW, logPos.y + logH), colBorder, ui::S(10), 0,
                     1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui::S(12), ui::S(10)));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("##version_log", ImVec2(logW, logH),
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);
        ImGui::PushFont(uiFonts.mono);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::Hex("#AEB8D6")));
        if (installTask_) {
            auto logs = installTask_->Logs();
            for (const auto& line : logs) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
                ImGui::TextUnformatted(line.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::SetScrollHereY(1.0f);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colFaint));
            ImGui::TextUnformatted("Здесь появится журнал установки.");
            ImGui::PopStyleColor();
        }
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }
    ui::EndCard();
    ImGui::EndGroup();
}
