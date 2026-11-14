#include "app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace {

void SettingsLabel(const char* text) {
    ImGui::PushFont(uiFonts.smallFont);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void SettingsTitle(const char* title, const char* subtitle) {
    ImGui::PushFont(uiFonts.h3);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    TextMuted(subtitle);
}

} // namespace

void App::DrawSettings() {
    PageHeader("Настройки", "Память, Java, папка игры и поведение лаунчера");

    if (confirmAction_ != 0 && ImGui::GetTime() > confirmUntil_) confirmAction_ = 0;
    auto armConfirm = [this](int action, const std::string& what) {
        double now = ImGui::GetTime();
        if (confirmAction_ == action && now <= confirmUntil_) {
            confirmAction_ = 0;
            return true;
        }
        confirmAction_ = action;
        confirmUntil_ = now + 4.0;
        ToastMsg(what + ": нажмите ещё раз для подтверждения", 0);
        return false;
    };

    float availW = ImGui::GetContentRegionAvail().x;
    float saveH = ui::S(40);
    ImVec2 toolbarPos = ImGui::GetCursorScreenPos();
    ImGui::PushFont(uiFonts.smallFont);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(toolbarPos.x, toolbarPos.y + (saveH - ImGui::GetTextLineHeight()) * 0.5f),
        colFaint, "Изменения применяются сразу");
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(toolbarPos.x + availW - ui::S(224), toolbarPos.y));
    if (ui::PrimaryButton("Сохранить настройки", ImVec2(ui::S(224), saveH), true, ui::IconCheck,
                          false)) {
        SaveConfig();
        cfg.Save();
        ToastMsg("Настройки сохранены", 1);
    }
    ImGui::Dummy(ImVec2(0, ui::S(10)));

    // ------------------------------------------------------------ memory
    ui::BeginCard("##settings_memory", ImVec2(availW, ui::S(300)));
    {
        SettingsTitle("Производительность", "Сколько памяти выделять и какие аргументы передавать JVM");
        ImGui::Dummy(ImVec2(0, ui::S(8)));
        SettingsLabel("Оперативная память");
        {
            MEMORYSTATUSEX mem{};
            mem.dwLength = sizeof(mem);
            if (GlobalMemoryStatusEx(&mem) && mem.ullTotalPhys > 0) {
                long long totalGb = (long long)((mem.ullTotalPhys + (1ull << 30) - 1) >> 30);
                std::string hint = "в системе " + std::to_string(totalGb) + " ГБ";
                ImGui::SameLine(0, ui::S(12));
                ImGui::PushFont(uiFonts.tiny);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colFaint));
                ImGui::TextUnformatted(hint.c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }
        }
        float trackW = std::max(ui::S(120), ImGui::GetContentRegionAvail().x - ui::S(96));
        ImVec2 sp = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##ram_slider", ImVec2(trackW, ui::S(30)));
        bool ramHover = ImGui::IsItemHovered();
        bool ramActive = ImGui::IsItemActive();
        int ram = std::clamp(cfg.s.ramMb, 1024, 16384);
        if (ramActive) {
            float t = (ImGui::GetIO().MousePos.x - sp.x) / std::max(1.0f, trackW);
            int v = 1024 + (int)std::lround(std::clamp(t, 0.0f, 1.0f) * (16384 - 1024));
            v = std::clamp((v / 512) * 512, 1024, 16384);
            if (v != cfg.s.ramMb) {
                cfg.s.ramMb = v;
                cfg.Save();
                ram = v;
            }
        }
        float frac = (float)(ram - 1024) / (float)(16384 - 1024);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float cy = sp.y + ui::S(15);
        dl->AddRectFilled(ImVec2(sp.x, cy - ui::S(4)), ImVec2(sp.x + trackW, cy + ui::S(4)),
                          ui::Hex("#23283D"), ui::S(4));
        dl->AddRectFilled(ImVec2(sp.x, cy - ui::S(4)), ImVec2(sp.x + trackW * frac, cy + ui::S(4)),
                          colAccent, ui::S(4));
        float knobX = sp.x + trackW * frac;
        dl->AddCircleFilled(ImVec2(knobX, cy), (ramHover || ramActive) ? ui::S(10) : ui::S(9),
                            ImGui::GetColorU32(ImGuiCol_Text));
        dl->AddCircleFilled(ImVec2(knobX, cy), ui::S(5), colAccent);
        ImGui::SameLine(0, ui::S(10));
        ImGui::SetCursorPosY(sp.y + ui::S(3) - ImGui::GetWindowPos().y);
        std::string ramText = std::to_string(ram) + " МБ";
        ui::Badge(ramText.c_str(), colAccent);

        ImGui::Dummy(ImVec2(0, ui::S(4)));
        const int presets[] = {2048, 4096, 6144, 8192, 12288, 16384};
        for (int i = 0; i < 6; ++i) {
            if (i) ImGui::SameLine(0, ui::S(8));
            ImGui::PushID(i);
            int gb = presets[i] / 1024;
            std::string label = std::to_string(gb) + " ГБ";
            bool active = cfg.s.ramMb == presets[i];
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImVec2 size(ui::S(84), ui::S(30));
            ImGui::InvisibleButton("##ram_preset", size);
            bool hovered = ImGui::IsItemHovered();
            ImU32 bg = active ? ui::Alpha(colAccent, 46)
                              : (hovered ? ui::Alpha(IM_COL32(255, 255, 255, 255), 18)
                                         : ui::Alpha(IM_COL32(255, 255, 255, 255), 10));
            dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), bg, ui::S(10));
            if (active) {
                dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), ui::Alpha(colAccent, 130),
                            ui::S(10), 0, 1.0f);
            }
            ImGui::PushFont(uiFonts.semibold);
            ImVec2 ts = ImGui::CalcTextSize(label.c_str());
            dl->AddText(ImVec2(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f),
                        active ? colAccent : colText, label.c_str());
            ImGui::PopFont();
            if (ImGui::IsItemClicked()) {
                cfg.s.ramMb = presets[i];
                cfg.Save();
            }
            ImGui::PopID();
        }
        ImGui::Dummy(ImVec2(0, ui::S(8)));
        SettingsLabel("Дополнительные аргументы JVM");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##jvm_args", "-XX:+UseG1GC -Dfile.encoding=UTF-8", setJvmArgs_,
                                 sizeof(setJvmArgs_));
    }
    ui::EndCard();
    ImGui::Dummy(ImVec2(0, ui::S(14)));

    // ------------------------------------------------------------ game
    ui::BeginCard("##settings_game", ImVec2(availW, ui::S(520)));
    {
        SettingsTitle("Игра", "Папка установки, окно и поведение лаунчера");
        ImGui::Dummy(ImVec2(0, ui::S(8)));
        SettingsLabel("Папка игры");
        ImGui::SetNextItemWidth(availW - ui::S(142));
        ImGui::InputText("##game_dir", setGameDir_, sizeof(setGameDir_));
        ImGui::SameLine(0, ui::S(8));
        if (ui::IconButton("##game_dir_browse", ui::IconFolder, ImVec2(ui::S(36), ui::S(36)), false,
                           0, "Выбрать папку")) {
            std::string path;
            if (ui::BrowseFolder("Папка игры", path)) {
                snprintf(setGameDir_, sizeof(setGameDir_), "%s", path.c_str());
            }
        }
        ImGui::SameLine(0, ui::S(6));
        if (ui::IconButton("##game_dir_open", ui::IconArrowRight, ImVec2(ui::S(36), ui::S(36)), false,
                           0, "Открыть папку")) {
            ui::OpenPath(setGameDir_);
        }

        ImGui::Dummy(ImVec2(0, ui::S(8)));
        SettingsLabel("Разрешение окна игры");
        int width = cfg.s.winW;
        int height = cfg.s.winH;
        ImGui::SetNextItemWidth(ui::S(140));
        if (ImGui::InputInt("##res_w", &width, 0, 0)) {
            cfg.s.winW = std::clamp(width, 640, 7680);
            cfg.Save();
        }
        ImGui::SameLine(0, ui::S(8));
        ImGui::PushFont(uiFonts.body);
        ImGui::TextUnformatted("×");
        ImGui::PopFont();
        ImGui::SameLine(0, ui::S(8));
        ImGui::SetNextItemWidth(ui::S(140));
        if (ImGui::InputInt("##res_h", &height, 0, 0)) {
            cfg.s.winH = std::clamp(height, 480, 4320);
            cfg.Save();
        }
        ImGui::Dummy(ImVec2(0, ui::S(6)));
        const struct {
            const char* label;
            int w;
            int h;
        } resPresets[] = {{"1280 × 720", 1280, 720},
                          {"1600 × 900", 1600, 900},
                          {"1920 × 1080", 1920, 1080},
                          {"2560 × 1440", 2560, 1440}};
        for (int i = 0; i < 4; ++i) {
            if (i) ImGui::SameLine(0, ui::S(8));
            bool active = cfg.s.winW == resPresets[i].w && cfg.s.winH == resPresets[i].h;
            if (ui::GhostButton(resPresets[i].label, ImVec2(ui::S(130), ui::S(30)), true,
                                active ? ui::IconCheck : ui::IconNone)) {
                cfg.s.winW = resPresets[i].w;
                cfg.s.winH = resPresets[i].h;
                cfg.Save();
            }
        }

        ImGui::Dummy(ImVec2(0, ui::S(10)));
        if (ui::Toggle("Запускать в полноэкранном режиме", &cfg.s.fullscreen)) cfg.Save();
        if (ui::Toggle("Сворачивать лаунчер после запуска игры", &cfg.s.closeOnLaunch)) cfg.Save();
        if (ui::Toggle("Разворачивать лаунчер после выхода из игры", &cfg.s.restoreAfterExit)) cfg.Save();
        if (ui::Toggle("Открывать последнюю вкладку при запуске", &cfg.s.launchToLastPage)) cfg.Save();
        if (ui::Toggle("Показывать снимки (snapshots)", &cfg.s.showSnapshots)) cfg.Save();
    }
    ui::EndCard();
    ImGui::Dummy(ImVec2(0, ui::S(14)));

    // ------------------------------------------------------------ java
    ui::BeginCard("##settings_java", ImVec2(availW, ui::S(292)));
    {
        SettingsTitle("Java", "Лаунчер сам подбирает подходящую версию, либо укажите путь вручную");
        ImGui::Dummy(ImVec2(0, ui::S(8)));
        std::vector<JavaInstall> javaList;
        {
            std::lock_guard lock(javaMutex_);
            javaList = javaList_;
        }
        std::string preview = "Автоопределение";
        for (const auto& java : javaList) {
            if (!cfg.s.javaPath.empty() && java.path == cfg.s.javaPath) {
                preview = "Java " + std::to_string(java.major) + "  ·  " + java.path;
            }
        }
        if (!cfg.s.javaPath.empty() && preview == "Автоопределение") {
            preview = "Вручную: " + cfg.s.javaPath;
        }
        ImGui::SetNextItemWidth(availW - ui::S(66));
        if (ImGui::BeginCombo("##java_select", preview.c_str())) {
            if (ImGui::Selectable("Автоопределение", cfg.s.javaPath.empty())) {
                cfg.s.javaPath.clear();
                cfg.Save();
                ImGui::CloseCurrentPopup();
            }
            ImGui::Separator();
            for (const auto& java : javaList) {
                std::string label =
                    "Java " + std::to_string(java.major) + "   ·   " + java.path;
                if (ImGui::Selectable(label.c_str(), cfg.s.javaPath == java.path)) {
                    cfg.s.javaPath = java.path;
                    cfg.Save();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine(0, ui::S(8));
        if (ui::IconButton("##java_rescan", ui::IconRefresh, ImVec2(ui::S(36), ui::S(36)), false, 0,
                           "Найти Java заново")) {
            InvalidateJavaCache();
            javaScanStarted_ = false;
            EnsureJavaScan();
        }

        ImGui::Dummy(ImVec2(0, ui::S(6)));
        if (javaLoading_.load()) {
            TextMuted("Сканирование установленных версий Java...");
        } else {
            std::string found = "Найдено установок: " + std::to_string(javaList.size());
            TextMuted(found.c_str());
        }
        ImGui::Dummy(ImVec2(0, ui::S(6)));
        std::string javaVersion = cfg.s.selectedVersion.empty() ? GameVersion() : cfg.s.selectedVersion;
        int requiredJava = std::max(mc.RequiredJavaMajor(javaVersion), 8);
        if (ui::GhostButton("Указать папку Java вручную", ImVec2(ui::S(280), ui::S(34)), true,
                            ui::IconFolder)) {
            std::string path;
            if (ui::BrowseFolder("Папка Java (с bin\\java.exe)", path)) {
                fs::path exe = fs::path(path) / "bin" / "java.exe";
                if (FileExists(exe)) cfg.s.javaPath = exe.string();
                else cfg.s.javaPath = path;
                cfg.Save();
                ToastMsg("Путь Java сохранён", 1);
            }
        }
        ImGui::SameLine(0, ui::S(8));
        std::string installLabel = "Скачать Java " + std::to_string(requiredJava);
        if (javaTask_ && javaTask_->IsRunning()) installLabel = "Загрузка Java...";
        if (ui::GhostButton(installLabel.c_str(), ImVec2(ui::S(240), ui::S(34)), true,
                            ui::IconDownload)) {
            StartJavaInstall(requiredJava, false, "", 0);
        }
        ImGui::SameLine(0, ui::S(8));
        if (ui::GhostButton("Папка Java", ImVec2(ui::S(180), ui::S(34)), !cfg.s.javaPath.empty(),
                            ui::IconFolder)) {
            fs::path javaPath(cfg.s.javaPath);
            ui::OpenPath(javaPath.has_parent_path() ? javaPath.parent_path().string()
                                                    : javaPath.string());
        }
        ImGui::Dummy(ImVec2(0, ui::S(2)));
        ImGui::PushFont(uiFonts.tiny);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colFaint));
        ImGui::TextUnformatted("Для Minecraft 1.17+ нужна Java 17, для 1.20.5+ — Java 21.");
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ui::EndCard();
    ImGui::Dummy(ImVec2(0, ui::S(14)));

    // ------------------------------------------------------------ appearance
    ui::BeginCard("##settings_look", ImVec2(availW, ui::S(268)));
    {
        SettingsTitle("Внешний вид", "Акцентный цвет и плавность интерфейса");
        ImGui::Dummy(ImVec2(0, ui::S(8)));
        SettingsLabel("Акцент");
        const char* accents[] = {"#7C5CFF", "#3AD6F0", "#44DDA0", "#FF5D7A",
                                 "#FFC857", "#FF7A45", "#5B8CFF"};
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int i = 0; i < 7; ++i) {
            if (i) ImGui::SameLine(0, ui::S(12));
            ImGui::PushID(i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##accent", ImVec2(ui::S(30), ui::S(30)));
            bool hovered = ImGui::IsItemHovered();
            bool selected = ToLower(cfg.s.accent) == ToLower(accents[i]);
            ImVec2 center(p.x + ui::S(15), p.y + ui::S(15));
            ImU32 color = ui::Hex(accents[i]);
            dl->AddCircleFilled(center, ui::S(12), color, 32);
            if (hovered) dl->AddCircle(center, ui::S(15), ui::Alpha(color, 180), 32, 2.0f);
            if (selected) dl->AddCircle(center, ui::S(16), color, 32, 2.0f);
            if (ImGui::IsItemClicked()) {
                cfg.s.accent = accents[i];
                ui::SetAccent(cfg.s.accent);
                cfg.Save();
            }
            ImGui::PopID();
        }
        ImGui::Dummy(ImVec2(0, ui::S(8)));
        if (ui::Toggle("Плавные анимации при переключении вкладок", &cfg.s.animations)) {
            if (!cfg.s.animations) pageAnim_ = 1.0f;
            cfg.Save();
        }
    }
    ui::EndCard();
    ImGui::Dummy(ImVec2(0, ui::S(14)));

    // ------------------------------------------------------------ curseforge
    ui::BeginCard("##settings_cf", ImVec2(availW, ui::S(220)));
    {
        SettingsTitle("CurseForge", "API-ключ для поиска и скачивания модов");
        ImGui::Dummy(ImVec2(0, ui::S(8)));
        SettingsLabel("API-ключ (console.curseforge.com)");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##cf_key", "Вставьте ключ CurseForge", setCfKey_,
                                 sizeof(setCfKey_));
        ImGui::Dummy(ImVec2(0, ui::S(6)));
        bool valid = CurseForgeClient::KeyLooksValid(Trim(setCfKey_));
        ImGui::PushFont(uiFonts.smallFont);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(valid ? colGreen
                                                                                  : colFaint));
        ImGui::TextUnformatted(valid ? "Ключ заполнен — моды CurseForge доступны"
                                     : "Без ключа доступен только каталог Modrinth");
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ui::EndCard();
    ImGui::Dummy(ImVec2(0, ui::S(14)));

    // ------------------------------------------------------------ maintenance
    ui::BeginCard("##settings_maintenance", ImVec2(availW, ui::S(300)));
    {
        SettingsTitle("Обслуживание", "Быстрый доступ к папкам и сброс данных лаунчера");
        ImGui::Dummy(ImVec2(0, ui::S(8)));
        if (ui::GhostButton("Папка игры", ImVec2(ui::S(200), ui::S(34)), true, ui::IconFolder)) {
            std::error_code ec;
            fs::create_directories(GameDirString(), ec);
            ui::OpenPath(GameDirString());
        }
        ImGui::SameLine(0, ui::S(8));
        if (ui::GhostButton("Папка модов", ImVec2(ui::S(200), ui::S(34)), true, ui::IconMod)) {
            std::error_code ec;
            fs::create_directories(mc.ModsDir(), ec);
            ui::OpenPath(mc.ModsDir().string());
        }
        ImGui::SameLine(0, ui::S(8));
        if (ui::GhostButton("Скриншоты", ImVec2(ui::S(200), ui::S(34)), true, ui::IconGrid)) {
            std::error_code ec;
            fs::path shots = fs::path(GameDirString()) / "screenshots";
            fs::create_directories(shots, ec);
            ui::OpenPath(shots.string());
        }

        ImGui::Dummy(ImVec2(0, ui::S(8)));
        if (ui::GhostButton("Папка лаунчера", ImVec2(ui::S(200), ui::S(34)), true, ui::IconCube)) {
            ui::OpenPath(cfg.dataDir.string());
        }
        ImGui::SameLine(0, ui::S(8));
        if (ui::GhostButton("Открыть журнал", ImVec2(ui::S(200), ui::S(34)), true,
                            ui::IconConsole)) {
            ui::OpenPath(LogPath().string());
        }
        ImGui::SameLine(0, ui::S(8));
        if (ui::GhostButton("Очистить журнал", ImVec2(ui::S(200), ui::S(34)), true, ui::IconTrash)) {
            WriteTextFile(LogPath(), "");
            LogLine("Журнал очищен");
            ToastMsg("Журнал очищен", 1);
        }

        ImGui::Dummy(ImVec2(0, ui::S(12)));
        SettingsLabel("Сброс данных");
        ImGui::Dummy(ImVec2(0, ui::S(6)));
        bool armStats = confirmAction_ == 1;
        if (ui::GhostButton(armStats ? "Точно сбросить статистику?" : "Сбросить статистику",
                            ImVec2(ui::S(300), ui::S(34)), true, ui::IconRefresh)) {
            if (armConfirm(1, "Сброс статистики")) {
                cfg.s.lastLaunch.clear();
                cfg.s.launchCount = 0;
                cfg.s.playSeconds = 0;
                cfg.s.recentVersions.clear();
                cfg.Save();
                ToastMsg("Статистика сброшена", 1);
            }
        }
        ImGui::SameLine(0, ui::S(8));
        bool armSettings = confirmAction_ == 2;
        if (ui::GhostButton(armSettings ? "Точно сбросить настройки?" : "Сбросить настройки",
                            ImVec2(ui::S(300), ui::S(34)), true, ui::IconWarn)) {
            if (armConfirm(2, "Сброс настроек")) {
                cfg.s.ramMb = 4096;
                cfg.s.javaPath.clear();
                cfg.s.jvmArgs.clear();
                cfg.s.winW = 1280;
                cfg.s.winH = 720;
                cfg.s.fullscreen = false;
                cfg.s.closeOnLaunch = true;
                cfg.s.showSnapshots = false;
                cfg.s.accent = "#7C5CFF";
                cfg.s.animations = true;
                cfg.s.restoreAfterExit = true;
                cfg.s.launchToLastPage = true;
                ui::SetAccent(cfg.s.accent);
                SyncSettingsBuffers();
                cfg.Save();
                ToastMsg("Настройки сброшены", 1);
            }
        }
    }
    ui::EndCard();
    ImGui::Dummy(ImVec2(0, ui::S(14)));

    // ------------------------------------------------------------ about
    ui::BeginCard("##settings_about", ImVec2(availW, ui::S(150)));
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(p, ImVec2(p.x + ui::S(46), p.y + ui::S(46)),
                          ui::Alpha(colAccent, 50), ui::S(12));
        ui::Icon(dl, ui::IconCube, ImVec2(p.x + ui::S(23), p.y + ui::S(23)), ui::S(26), colAccent,
                 2.0f);
        ImGui::Dummy(ImVec2(ui::S(58), ui::S(46)));
        ImGui::SameLine(0, ui::S(14));
        ImGui::BeginGroup();
        ImGui::PushFont(uiFonts.h3);
        ImGui::TextUnformatted("XpLauncher 1.0.0");
        ImGui::PopFont();
        TextMuted("C++20 · ImGui · DirectX 11 · WinHTTP");
        ImGui::EndGroup();

        ImGui::Dummy(ImVec2(0, ui::S(8)));
        ImGui::PushFont(uiFonts.tiny);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colFaint));
        std::string dataLine = "Данные: " + cfg.dataDir.string();
        ImGui::TextUnformatted(ui::Ellipsize(dataLine, ImGui::GetContentRegionAvail().x,
                                             uiFonts.tiny).c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ui::EndCard();
}
