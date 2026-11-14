#include "app.hpp"

#include "imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void TextColor(ImU32 color, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

} // namespace

// ------------------------------------------------------------------ helpers

bool VersionCombo(const char* id, std::string& value, const VersionManifest* manifest,
                  bool showSnapshots, float width, const char* emptyLabel, bool allowAny) {
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    std::string preview = value.empty() ? (emptyLabel ? emptyLabel : "Выберите версию") : value;
    if (ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        static char filter[64] = {};
        if (ImGui::IsWindowAppearing()) {
            filter[0] = 0;
            ImGui::SetKeyboardFocusHere();
        }
        float listW = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth(listW);
        ImGui::InputTextWithHint("##version_filter", "Поиск версии...", filter, sizeof(filter));
        ImGui::Separator();
        ImGui::BeginChild("##version_list", ImVec2(listW, ui::S(320)));
        if (allowAny) {
            if (ImGui::Selectable("Любая версия", value.empty())) {
                value.clear();
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::Separator();
        }
        if (!manifest) {
            TextColor(colFaint, "Список версий загружается...");
        } else {
            std::string needle = ToLower(Trim(filter));
            int rendered = 0;
            for (size_t i = 0; i < manifest->versions.size(); ++i) {
                const VersionEntry& entry = manifest->versions[i];
                if (!showSnapshots && entry.type != "release") continue;
                if (!needle.empty() && ToLower(entry.id).find(needle) == std::string::npos) continue;
                if (++rendered > 900) break;
                std::string label = entry.id;
                if (entry.type != "release") label += "   · снимок";
                ImGui::PushID((int)i);
                if (ImGui::Selectable(label.c_str(), value == entry.id)) {
                    value = entry.id;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            if (rendered == 0) TextColor(colFaint, "Ничего не найдено");
        }
        ImGui::EndChild();
        ImGui::EndCombo();
    }
    return changed;
}

bool LoaderCombo(const char* id, int& kind, float width, bool allowAny) {
    static const char* names[] = {"Vanilla", "Fabric", "Quilt", "Forge", "NeoForge"};
    bool changed = false;
    int clamped = std::clamp(kind, allowAny ? -1 : 0, 4);
    std::string preview = clamped < 0 ? "Любой загрузчик" : names[clamped];
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, preview.c_str())) {
        if (allowAny) {
            if (ImGui::Selectable("Любой загрузчик", kind < 0)) {
                kind = -1;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::Separator();
        }
        for (int i = 0; i < 5; ++i) {
            if (ImGui::Selectable(names[i], kind == i)) {
                kind = i;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

std::string AvatarUrl(const std::string& name) {
    if (name.empty()) return {};
    return "https://mc-heads.net/avatar/" + UrlEncode(name) + "/64";
}

void PageHeader(const char* title, const char* subtitle) {
    ImGui::PushFont(uiFonts.h1);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colText));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::PushFont(uiFonts.smallFont);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
    ImGui::TextUnformatted(subtitle);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, ui::S(16)));
}

void TextMuted(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void TextMutedWrapped(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

// ------------------------------------------------------------------ lifecycle

void App::Init(void* hwnd, void* d3dDevice) {
    hwnd_ = (HWND)hwnd;
    device_ = d3dDevice;

    cfg.Load();
    LogInit(cfg.dataDir / "launcher.log");
    LogLine("XpLauncher 1.0.0 стартует");

    ui::SetAccent(cfg.s.accent);
    SyncSettingsBuffers();

    selectedVersion_ = cfg.s.selectedVersion;
    loaderIdx_ = std::clamp(cfg.s.loaderKind, 0, 4);
    modGameVersion_ = cfg.s.selectedVersion;
    modLoaderKind_ = loaderIdx_ > 0 ? loaderIdx_ : -1;
    nickname_[0] = 0;
    std::snprintf(msaClientId_, sizeof(msaClientId_), "%s", cfg.s.msaClientId.c_str());

    int lastPage = std::clamp(cfg.s.lastPage, (int)Page::Home, (int)Page::Settings);
    page_ = cfg.s.launchToLastPage ? (Page)lastPage : Page::Home;
    pageAnim_ = 1.0f;
    if (cfg.s.pendingServerStart) {
        cfg.s.pendingServerStart = false;
        cfg.Save();
        if (IsElevated()) {
            serverAutoStart_ = true;
            page_ = Page::Server;
        } else {
            ToastMsg("Сервер не запущен: требуются права администратора", 2);
        }
    }

    EnsureManifest();
    EnsureJavaScan();
    RefreshInstalledMods();
}

void App::Shutdown() {
    msa_.cancel.store(true);
    JoinMicrosoftWorker();
    if (serverRunning_.load()) StopServer();
    if (serverStdin_) {
        CloseHandle(serverStdin_);
        serverStdin_ = nullptr;
    }
    textures.Shutdown();
    cfg.Save();
    LogLine("XpLauncher завершает работу");
}

// ------------------------------------------------------------------ frame

void App::Frame() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 display = io.DisplaySize;

    if (installTask_ && !installTask_->IsRunning()) OnInstallFinished();
    PollMicrosoftFlow();

    if (lastModTask_ && !lastModTask_->IsRunning()) {
        int state = lastModTask_->state.load();
        std::string error = lastModTask_->Error();
        lastModTask_.reset();
        if (state == (int)TaskState::Done) {
            ToastMsg("Мод установлен");
            installedModsDirty_ = true;
        } else if (state == (int)TaskState::Failed) {
            ToastMsg(error.empty() ? "Не удалось установить мод" : error, 2);
        }
    }

    if (gameWasRunning_ && !gameRunning_.load()) {
        gameWasRunning_ = false;
        if (launchStartUnix_ > 0) {
            int64_t played = NowSeconds() - launchStartUnix_;
            if (played > 0) cfg.s.playSeconds += played;
            launchStartUnix_ = 0;
            cfg.Save();
        }
        if (cfg.s.restoreAfterExit && hwnd_) {
            if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
            SetForegroundWindow(hwnd_);
        }
        ToastMsg("Игра завершена");
    }
    if (gameRunning_.load()) gameWasRunning_ = true;

    if (serverTask_ && !serverTask_->IsRunning()) {
        int state = serverTask_->state.load();
        std::string error = serverTask_->Error();
        bool startNow = serverStartAfterPrepare_;
        serverStartAfterPrepare_ = false;
        serverTask_.reset();
        if (state == (int)TaskState::Done) {
            if (startNow) StartServerProcess();
        } else if (state == (int)TaskState::Failed) {
            ToastMsg(error.empty() ? "Не удалось подготовить сервер" : error, 2);
        }
    }
    if (javaTask_ && !javaTask_->IsRunning()) {
        int state = javaTask_->state.load();
        std::string error = javaTask_->Error();
        bool launchNow = javaLaunchAfter_;
        std::string game = javaPendingGame_;
        int kind = javaPendingKind_;
        javaTask_.reset();
        javaLaunchAfter_ = false;
        javaPendingGame_.clear();
        javaPendingKind_ = 0;
        if (state == (int)TaskState::Done) {
            InvalidateJavaCache();
            javaScanStarted_ = false;
            EnsureJavaScan();
            ToastMsg("Java установлена", 1);
            if (launchNow) LaunchNow(game, kind);
        } else if (state == (int)TaskState::Failed) {
            ToastMsg(error.empty() ? "Не удалось установить Java" : error, 2);
        }
    }
    if (serverExitNotify_) {
        serverExitNotify_ = false;
        ToastMsg("Сервер остановлен");
    }

    if (pageAnim_ < 1.0f) {
        pageAnim_ = std::min(1.0f, pageAnim_ + ImGui::GetIO().DeltaTime / 0.28f);
    }

    if (serverAutoStart_) {
        serverAutoStart_ = false;
        PrepareServer(true);
    }

    EnsureManifest();
    if (auto manifest = mc.Manifest()) {
        if (selectedVersion_.empty()) {
            selectedVersion_ = manifest->latestRelease;
            if (modGameVersion_.empty()) modGameVersion_ = selectedVersion_;
        }
        if (cfg.s.selectedVersion.empty()) cfg.s.selectedVersion = selectedVersion_;
    }
    EnsureLoaderVersions();
    EnsureJavaScan();
    textures.Pump(device_);

    const ImGuiWindowFlags rootFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(display);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##root", nullptr, rootFlags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    ui::DrawBackdrop(ImGui::GetWindowDrawList(), display);
    DrawSidebar(display);
    DrawTitleBar(display);

    float sideW = ui::S(238);
    float animT = cfg.s.animations ? pageAnim_ : 1.0f;
    float fade = animT * animT * (3.0f - 2.0f * animT);
    float lift = 1.0f - std::pow(1.0f - animT, 3.0f);
    ImVec2 contentPos(sideW + ui::S(26), ui::S(56) + ui::S(14) + ui::S(14) * (1.0f - lift));
    ImVec2 contentSize(std::max(ui::S(400), display.x - (sideW + ui::S(26)) - ui::S(26)),
                       std::max(ui::S(200), display.y - (ui::S(56) + ui::S(14)) - ui::S(18)));
    ImGui::SetCursorScreenPos(contentPos);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##page", contentSize, ImGuiChildFlags_None, ImGuiWindowFlags_None);
    ImGuiWindow* pageWindow = ImGui::GetCurrentContext()->CurrentWindow;
    if (pageScrollReset_) {
        pageScrollReset_ = false;
        ImGui::SetScrollY(0.0f);
    }
    switch (page_) {
        case Page::Home: DrawHome(); break;
        case Page::Versions: DrawVersions(); break;
        case Page::Mods: DrawMods(); break;
        case Page::Profile: DrawProfile(); break;
        case Page::Accounts: DrawAccounts(); break;
        case Page::Downloads: DrawDownloads(); break;
        case Page::Server: DrawServer(); break;
        case Page::Settings: DrawSettings(); break;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    if (cfg.s.animations && fade < 0.999f) {
        ImGuiContext& g = *ImGui::GetCurrentContext();
        for (ImGuiWindow* window : g.Windows) {
            if (!window || !window->DrawList || window->DrawList->VtxBuffer.Size == 0) continue;
            const ImGuiWindow* parent = window;
            while (parent && parent != pageWindow) parent = parent->ParentWindow;
            if (!parent) continue;
            for (ImDrawVert& vertex : window->DrawList->VtxBuffer) {
                ImU32 alpha = (ImU32)(((vertex.col >> IM_COL32_A_SHIFT) & 0xFF) * fade + 0.5f);
                vertex.col = (vertex.col & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
            }
        }
    }

    DrawModals();
    DrawToasts();
    AnimatePopups();

    ImGui::End();
}

void App::AnimatePopups() {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    const double now = ImGui::GetTime();
    constexpr double kDuration = 0.16;

    popupAnimSeen_.clear();
    for (ImGuiWindow* window : g.Windows) {
        if (!window || !window->Active || !window->DrawList) continue;
        if ((window->Flags & ImGuiWindowFlags_Popup) == 0) continue;
        if (window->DrawList->VtxBuffer.Size == 0) continue;
        popupAnimSeen_.insert(window->ID);
        if (window->Appearing || popupAnimStart_.find(window->ID) == popupAnimStart_.end()) {
            popupAnimStart_[window->ID] = now;
        }
    }
    for (auto it = popupAnimStart_.begin(); it != popupAnimStart_.end();) {
        if (popupAnimSeen_.count(it->first) == 0) it = popupAnimStart_.erase(it);
        else ++it;
    }

    if (!cfg.s.animations) return;

    for (ImGuiWindow* window : g.Windows) {
        if (!window || !window->Active || !window->DrawList) continue;
        if ((window->Flags & ImGuiWindowFlags_Popup) == 0) continue;
        auto it = popupAnimStart_.find(window->ID);
        if (it == popupAnimStart_.end()) continue;
        float t = (float)std::clamp((now - it->second) / kDuration, 0.0, 1.0);
        if (t >= 1.0f) continue;
        float fade = t * t * (3.0f - 2.0f * t);
        for (ImDrawVert& vertex : window->DrawList->VtxBuffer) {
            ImU32 alpha = (ImU32)(((vertex.col >> IM_COL32_A_SHIFT) & 0xFF) * fade + 0.5f);
            vertex.col = (vertex.col & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
        }
    }
}

// ------------------------------------------------------------------ chrome

void App::DrawTitleBar(ImVec2 display) {
    const float h = ui::S(56);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::SetCursorScreenPos(ImVec2(0, 0));
    ImGui::InvisibleButton("##title_drag", ImVec2(std::max(ui::S(50), display.x - ui::S(160)), h));
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (IsZoomed(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
        else ShowWindow(hwnd_, SW_MAXIMIZE);
    } else if (ImGui::IsItemClicked()) {
        ReleaseCapture();
        SendMessageW(hwnd_, WM_NCLBUTTONDOWN, (WPARAM)HTCAPTION, 0);
    }

    float logoSize = ui::S(30);
    ImVec2 logoMin(ui::S(20), (h - logoSize) * 0.5f);
    ImVec2 logoMax(logoMin.x + logoSize, logoMin.y + logoSize);
    dl->AddRectFilled(logoMin, logoMax, colAccent, ui::S(9));
    dl->AddRectFilled(logoMin, ImVec2(logoMax.x, logoMin.y + logoSize * 0.6f),
                      ui::Alpha(IM_COL32(255, 255, 255, 255), 18), ui::S(9));
    ui::Icon(dl, ui::IconCube, ImVec2((logoMin.x + logoMax.x) * 0.5f, (logoMin.y + logoMax.y) * 0.5f),
             ui::S(17), IM_COL32(255, 255, 255, 250), 1.8f);

    ImGui::PushFont(uiFonts.logo);
    ImVec2 logoTextPos(logoMax.x + ui::S(12), (h - ImGui::GetFontSize()) * 0.5f);
    dl->AddText(logoTextPos, colText, "XpLauncher");
    float textW = ImGui::CalcTextSize("XpLauncher").x;
    ImGui::PopFont();

    ImGui::PushFont(uiFonts.tiny);
    ImVec2 badgePos(logoTextPos.x + textW + ui::S(9), (h - ui::S(20)) * 0.5f);
    dl->AddRectFilled(badgePos, ImVec2(badgePos.x + ui::S(40), badgePos.y + ui::S(20)),
                      ui::Alpha(colAccent, 40), ui::S(6));
    dl->AddText(ImVec2(badgePos.x + ui::S(8), badgePos.y + ui::S(3)), colAccent, "1.0");
    ImGui::PopFont();

    const Account* account = cfg.Active();
    ImGui::PushFont(uiFonts.smallFont);
    std::string status = gameRunning_.load() ? "Игра запущена" : (account ? account->name : "Гость");
    ImVec2 statusSize = ImGui::CalcTextSize(status.c_str());
    float statusX = display.x - ui::S(160) - statusSize.x;
    ImU32 dotColor = gameRunning_.load() ? colGreen : (account ? colGreen : colFaint);
    dl->AddCircleFilled(ImVec2(statusX - ui::S(12), h * 0.5f), ui::S(3.5f), dotColor);
    dl->AddText(ImVec2(statusX, (h - statusSize.y) * 0.5f), colMuted, status.c_str());
    ImGui::PopFont();

    struct ButtonDef {
        const char* id;
        ui::IconType icon;
        int action;
    };
    ButtonDef buttons[3] = {{"##win_min", ui::IconMin, 0},
                            {"##win_max", IsZoomed(hwnd_) ? ui::IconRestore : ui::IconMax, 1},
                            {"##win_close", ui::IconClose, 2}};
    float bw = ui::S(46);
    float bh = ui::S(32);
    float startX = display.x - bw * 3.0f - ui::S(10);
    float by = (h - bh) * 0.5f;
    for (int i = 0; i < 3; ++i) {
        ImGui::SetCursorScreenPos(ImVec2(startX + bw * i, by));
        ImGui::InvisibleButton(buttons[i].id, ImVec2(bw, bh));
        bool hovered = ImGui::IsItemHovered();
        bool active = ImGui::IsItemActive();
        if (hovered) {
            ImU32 bg = buttons[i].action == 2 ? ui::Alpha(colRed, active ? 235 : 190)
                                              : ui::Alpha(IM_COL32(255, 255, 255, 255), active ? 26 : 14);
            dl->AddRectFilled(ImVec2(startX + bw * i, by), ImVec2(startX + bw * (i + 1), by + bh), bg,
                              ui::S(8));
        }
        ImU32 iconColor = hovered && buttons[i].action == 2 ? IM_COL32(255, 255, 255, 255)
                                                           : (hovered ? colText : colMuted);
        ui::Icon(dl, buttons[i].icon,
                 ImVec2(startX + bw * i + bw * 0.5f, by + bh * 0.5f), ui::S(13), iconColor, 1.5f);
        if (ImGui::IsItemClicked()) {
            if (buttons[i].action == 0) ShowWindow(hwnd_, SW_MINIMIZE);
            else if (buttons[i].action == 1) ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
            else PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        }
    }

    dl->AddLine(ImVec2(0, h), ImVec2(display.x, h), ui::Alpha(colBorder, 150), 1.0f);
}

void App::DrawSidebar(ImVec2 display) {
    const float titleH = ui::S(56);
    const float width = ui::S(238);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddLine(ImVec2(width, titleH), ImVec2(width, display.y), ui::Alpha(colBorder, 120), 1.0f);

    struct NavDef {
        Page page;
        const char* label;
        ui::IconType icon;
    };
    NavDef items[] = {
        {Page::Home, "Главная", ui::IconHome},        {Page::Versions, "Версии", ui::IconCube},
        {Page::Mods, "Моды", ui::IconGrid},           {Page::Profile, "Профиль", ui::IconProfile},
        {Page::Accounts, "Аккаунты", ui::IconUsers},
        {Page::Downloads, "Загрузки", ui::IconDownload}, {Page::Server, "Сервер", ui::IconServer},
        {Page::Settings, "Настройки", ui::IconSettings},
    };

    float navWidth = width - ui::S(28);
    float y = titleH + ui::S(18);
    for (const auto& item : items) {
        ImGui::SetCursorScreenPos(ImVec2(ui::S(14), y));
        if (ui::NavItem(item.label, item.label, item.icon, page_ == item.page, navWidth)) {
            SetPage(item.page);
        }
        y += ui::S(48);
    }

    float cardH = ui::S(112);
    float cardY = display.y - cardH - ui::S(44);

    if (gameRunning_.load() && y + ui::S(44) < cardY) {
        ImVec2 p(ui::S(16), y + ui::S(4));
        dl->AddRectFilled(p, ImVec2(p.x + navWidth - ui::S(4), p.y + ui::S(40)), ui::Alpha(colGreen, 22),
                          ui::S(10));
        dl->AddCircleFilled(ImVec2(p.x + ui::S(18), p.y + ui::S(20)), ui::S(4), colGreen);
        ImGui::PushFont(uiFonts.smallFont);
        dl->AddText(ImVec2(p.x + ui::S(30), p.y + ui::S(10)), colGreen, "Minecraft запущен");
        ImGui::PopFont();
    }

    ImVec2 cmin(ui::S(14), cardY);
    ImVec2 cmax(ui::S(14) + navWidth, cardY + cardH);

    ImGui::SetCursorScreenPos(cmin);
    ImGui::InvisibleButton("##account_card", ImVec2(navWidth, cardH));
    bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked()) SetPage(Page::Accounts);

    dl->AddRectFilled(cmin, cmax, ui::Alpha(IM_COL32(255, 255, 255, 255), hovered ? 14 : 8), ui::S(14));
    dl->AddRect(cmin, cmax, hovered ? ui::Alpha(colAccent, 110) : colBorder, ui::S(14), 0, 1.0f);

    const Account* account = cfg.Active();
    float avatarSize = ui::S(42);
    ImVec2 avatarPos(cmin.x + ui::S(14), cmin.y + ui::S(14));
    DrawAvatar(account ? account->name : "", avatarPos, avatarSize);
    ImGui::PushFont(uiFonts.tiny);
    dl->AddText(ImVec2(avatarPos.x + avatarSize + ui::S(12), cmin.y + ui::S(16)), colMuted,
                account ? "АКТИВНЫЙ АККАУНТ" : "АККАУНТ");
    ImGui::PopFont();
    ImGui::PushFont(uiFonts.semibold);
    std::string name = account ? account->name : "Войти в аккаунт";
    dl->AddText(ImVec2(avatarPos.x + avatarSize + ui::S(12), cmin.y + ui::S(34)),
                account ? colText : colAccent, ui::Ellipsize(name, navWidth - avatarSize - ui::S(32),
                                                             uiFonts.semibold).c_str());
    ImGui::PopFont();
    ImGui::PushFont(uiFonts.smallFont);
    dl->AddText(ImVec2(avatarPos.x, cmin.y + ui::S(70)),
                account ? colGreen : colFaint,
                account ? (account->type == "msa" ? "Microsoft" : "Оффлайн-режим") : "Создайте оффлайн-аккаунт");
    ImGui::PopFont();

    dl->AddText(ImVec2(ui::S(18), display.y - ui::S(34)), ui::Alpha(colFaint, 180), "XpLauncher 1.0.0");
}

// ------------------------------------------------------------------ avatar / toasts

void App::DrawAvatar(const std::string& name, ImVec2 topLeft, float size, bool rounded) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 max(topLeft.x + size, topLeft.y + size);
    float rounding = rounded ? size * 0.30f : 0.0f;

    ImTextureID texture = name.empty() ? 0 : textures.Get(AvatarUrl(name));
    if (texture) {
        dl->AddImageRounded(texture, topLeft, max, ImVec2(0, 0), ImVec2(1, 1),
                            IM_COL32(255, 255, 255, 255), rounding);
    } else {
        dl->AddRectFilledMultiColor(topLeft, max, ui::Alpha(colAccent, 90), ui::Alpha(colCyan, 60),
                                    ui::Alpha(colCyan, 50), ui::Alpha(colAccent, 80));
        std::string letter = name.empty() ? "?" : std::string(1, (char)toupper((unsigned char)name[0]));
        ImGui::PushFont(uiFonts.h3);
        ImVec2 ts = ImGui::CalcTextSize(letter.c_str());
        dl->AddText(ImVec2(topLeft.x + (size - ts.x) * 0.5f, topLeft.y + (size - ts.y) * 0.5f),
                    IM_COL32(255, 255, 255, 240), letter.c_str());
        ImGui::PopFont();
    }
    dl->AddRect(topLeft, max, ui::Alpha(IM_COL32(255, 255, 255, 255), 30), rounding, 0, 1.0f);
}

void App::ToastMsg(const std::string& text, int kind) {
    if (toasts_.size() >= 4) toasts_.erase(toasts_.begin());
    Toast toast;
    toast.text = text;
    toast.kind = kind;
    toast.life = 4.5f;
    toasts_.push_back(toast);
}

void App::DrawToasts() {
    float dt = ImGui::GetIO().DeltaTime;
    for (auto& toast : toasts_) toast.life -= dt;
    while (!toasts_.empty() && toasts_.front().life <= 0.0f) toasts_.erase(toasts_.begin());
    if (toasts_.empty()) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 display = ImGui::GetIO().DisplaySize;
    float y = display.y - ui::S(20);
    for (int i = (int)toasts_.size() - 1; i >= 0; --i) {
        const Toast& toast = toasts_[i];
        ImU32 color = toast.kind == 2 ? colRed : (toast.kind == 1 ? colGreen : colAccent);
        ui::IconType icon = toast.kind == 2 ? ui::IconWarn : (toast.kind == 1 ? ui::IconCheck : ui::IconGlobe);
        ImGui::PushFont(uiFonts.body);
        ImVec2 textSize = ImGui::CalcTextSize(toast.text.c_str());
        float width = std::min(ui::S(460), textSize.x + ui::S(74));
        float height = ui::S(54);
        ImVec2 min(display.x - width - ui::S(20), y - height);
        ImVec2 max(display.x - ui::S(20), y);
        float alpha = std::clamp(toast.life / 0.7f, 0.0f, 1.0f);
        dl->AddRectFilled(min, max, ui::Alpha(ui::Hex("#151A2B"), (int)(242 * alpha)), ui::S(12));
        dl->AddRect(min, max, ui::Alpha(color, (int)(120 * alpha)), ui::S(12), 0, 1.0f);
        dl->AddCircleFilled(ImVec2(min.x + ui::S(27), min.y + height * 0.5f), ui::S(15),
                            ui::Alpha(color, (int)(46 * alpha)));
        ui::Icon(dl, icon, ImVec2(min.x + ui::S(27), min.y + height * 0.5f), ui::S(15),
                 ui::Alpha(color, (int)(255 * alpha)), 1.8f);
        dl->AddText(ImVec2(min.x + ui::S(52), min.y + (height - textSize.y) * 0.5f),
                    ui::Alpha(colText, (int)(255 * alpha)), toast.text.c_str());
        ImGui::PopFont();
        y = min.y - ui::S(10);
    }
}

// ------------------------------------------------------------------ async

void App::EnsureManifest(bool force) {
    if (mc.ManifestLoading()) return;
    if (force) {
        manifestRequested_ = true;
        mc.FetchManifestAsync(true);
        return;
    }
    if (!manifestRequested_) {
        manifestRequested_ = true;
        mc.FetchManifestAsync();
    }
}

void App::EnsureLoaderVersions() {
    if (loaderIdx_ <= 0 || selectedVersion_.empty()) return;
    std::string key = selectedVersion_ + "|" + std::to_string(loaderIdx_);
    if (key == loaderKey_) return;
    loaderKey_ = key;
    cfg.s.loaderVersion.clear();
    {
        std::lock_guard lock(loaders_.mutex);
        loaders_.loading = true;
        loaders_.error.clear();
        loaders_.versions.clear();
        loaders_.key = key;
    }
    std::string gameVersion = selectedVersion_;
    int kind = loaderIdx_;
    std::thread([this, key, gameVersion, kind]() {
        std::string error;
        std::vector<LoaderVersion> versions = mc.FetchLoaderVersions(gameVersion, kind, error);
        std::lock_guard lock(loaders_.mutex);
        if (loaders_.key != key) return;
        loaders_.versions = std::move(versions);
        loaders_.error = error;
        loaders_.loading = false;
    }).detach();
}

void App::EnsureJavaScan() {
    if (javaScanStarted_) return;
    javaScanStarted_ = true;
    javaLoading_ = true;
    std::thread([this]() {
        std::vector<JavaInstall> installs;
        try {
            installs = DetectJava();
        } catch (...) {
        }
        std::lock_guard lock(javaMutex_);
        javaList_ = std::move(installs);
        javaLoading_ = false;
    }).detach();
}

void App::StartJavaInstall(int major, bool launchAfter, const std::string& gameVersion, int loaderKind) {
    if (javaTask_ && javaTask_->IsRunning()) {
        ToastMsg("Java уже скачивается");
        return;
    }
    int feature = std::max(major, 8);
    javaLaunchAfter_ = launchAfter;
    javaPendingGame_ = gameVersion;
    javaPendingKind_ = loaderKind;
    ToastMsg("Java " + std::to_string(feature) + " не найдена — скачиваю автоматически...");
    LogLine("[Java] автоматическая установка Java " + std::to_string(feature));
    javaTask_ = tasks.Start("Java " + std::to_string(feature), [this, feature](Task& task) {
        std::string error;
        std::string path = EnsureJavaRuntime(feature, &task, &task.cancel, &error);
        if (path.empty()) {
            task.Fail(error.empty() ? "Не удалось установить Java" : error);
            return;
        }
        {
            std::lock_guard lock(javaMutex_);
            bool exists = false;
            for (const auto& install : javaList_) {
                if (install.path == path) exists = true;
            }
            if (!exists) javaList_.push_back({path, std::to_string(feature), feature});
        }
        task.Succeed("Java " + std::to_string(feature) + " установлена");
    });
}

// ------------------------------------------------------------------ config sync

void App::SaveConfig() {
    cfg.s.gameDir = Trim(setGameDir_);
    if (cfg.s.gameDir.empty()) cfg.s.gameDir = cfg.DefaultGameDir();
    cfg.s.jvmArgs = Trim(setJvmArgs_);
    cfg.s.cfApiKey = Trim(setCfKey_);
    cfg.s.serverMotd = Trim(setServerMotd_);
    if (cfg.s.serverMotd.empty()) cfg.s.serverMotd = "XpLauncher LAN";
    cfg.Save();
    ui::SetAccent(cfg.s.accent);
}

void App::SyncSettingsBuffers() {
    snprintf(setGameDir_, sizeof(setGameDir_), "%s",
             cfg.s.gameDir.empty() ? cfg.DefaultGameDir().c_str() : cfg.s.gameDir.c_str());
    snprintf(setJvmArgs_, sizeof(setJvmArgs_), "%s", cfg.s.jvmArgs.c_str());
    snprintf(setCfKey_, sizeof(setCfKey_), "%s", cfg.s.cfApiKey.c_str());
    snprintf(setServerMotd_, sizeof(setServerMotd_), "%s", cfg.s.serverMotd.c_str());
}

std::string App::GameDirString() {
    std::string dir = cfg.s.gameDir.empty() ? cfg.DefaultGameDir() : cfg.s.gameDir;
    return dir;
}

std::string App::GameVersion() {
    if (!selectedVersion_.empty()) return selectedVersion_;
    if (auto manifest = mc.Manifest()) return manifest->latestRelease;
    return cfg.s.selectedVersion;
}

std::string App::SelectedLoaderVersion() { return cfg.s.loaderVersion; }

bool App::AnyInstallRunning() { return installTask_ && installTask_->IsRunning(); }

bool App::IsReadyToPlay(const std::string& gameVersion, int loaderKind) {
    if (gameVersion.empty()) return false;
    if (loaderKind == 0) return mc.BaseInstalled(gameVersion);
    return mc.LoaderInstalled(gameVersion, loaderKind);
}

void App::StartInstall(const std::string& gameVersion, int loaderKind, const std::string& loaderVersion,
                       bool playAfter) {
    if (gameVersion.empty()) {
        ToastMsg("Сначала выберите версию", 2);
        return;
    }
    if (AnyInstallRunning()) {
        ToastMsg("Дождитесь завершения текущей загрузки", 2);
        return;
    }
    if (!cfg.Active()) {
        ToastMsg("Сначала создайте аккаунт", 2);
        SetPage(Page::Accounts);
        return;
    }
    installGame_ = gameVersion;
    installKind_ = loaderKind;
    installPlayAfter_ = playAfter;
    installTask_ = mc.Install(gameVersion, loaderKind, loaderVersion, playAfter);
    cfg.s.selectedVersion = gameVersion;
    cfg.s.loaderKind = loaderKind;
    cfg.Save();
    if (!playAfter) {
        ToastMsg("Загрузка началась");
        SetPage(Page::Downloads);
    } else {
        ToastMsg("Подготовка и запуск...");
    }
}

void App::StartPlay(const std::string& gameVersion, int loaderKind, const std::string& loaderVersion) {
    if (gameRunning_.load()) {
        ToastMsg("Minecraft уже запущен");
        return;
    }
    if (gameVersion.empty()) {
        ToastMsg("Выберите версию игры", 2);
        return;
    }
    if (!IsReadyToPlay(gameVersion, loaderKind)) {
        StartInstall(gameVersion, loaderKind, loaderVersion, true);
        return;
    }
    LaunchNow(gameVersion, loaderKind);
}

void App::LaunchNow(const std::string& gameVersion, int loaderKind) {
    Account* account = (cfg.activeAccount >= 0 && cfg.activeAccount < (int)cfg.accounts.size())
                           ? &cfg.accounts[cfg.activeAccount]
                           : nullptr;
    if (!account) {
        ToastMsg("Сначала создайте аккаунт", 2);
        SetPage(Page::Accounts);
        return;
    }
    if (account->type == "msa" && !account->refreshToken.empty() &&
        (account->accessToken.empty() ||
         (account->expiresAt > 0 && account->expiresAt - NowSeconds() < 180))) {
        StartMicrosoftRefresh(cfg.activeAccount, true, gameVersion, loaderKind);
        return;
    }
    std::string versionId = mc.EffectiveVersionId(gameVersion, loaderKind);
    if (versionId.empty()) versionId = gameVersion;
    fs::path versionJson = mc.VersionsDir() / versionId / (versionId + ".json");
    if (!FileExists(versionJson)) {
        ToastMsg("Версия не установлена. Нажмите «Установить»", 2);
        return;
    }

    int required = std::max(mc.RequiredJavaMajor(versionId), 8);
    bool javaReady = !cfg.s.javaPath.empty() && FileExists(cfg.s.javaPath);
    if (!javaReady) {
        javaReady =
            !FindJavaExe(AppDataDir() / "java" / ("temurin-" + std::to_string(required))).empty();
    }
    if (!javaReady) {
        auto installs = DetectJava();
        for (const auto& install : installs) {
            if (install.major >= required) {
                javaReady = true;
                break;
            }
        }
    }
    if (!javaReady) {
        StartJavaInstall(required, true, gameVersion, loaderKind);
        return;
    }

    std::string error;
    gameRunning_.store(true);
    gameWasRunning_ = true;
    gameRunningVersion_ = gameVersion;
    bool ok = mc.Launch(versionId, *account, cfg.s, error, [this](int) { gameRunning_.store(false); });
    if (!ok) {
        gameRunning_.store(false);
        gameWasRunning_ = false;
        if (error.find("Java") != std::string::npos) {
            StartJavaInstall(required, true, gameVersion, loaderKind);
            return;
        }
        ToastMsg(error.empty() ? "Не удалось запустить Minecraft" : error, 2);
    } else {
        launchStartUnix_ = NowSeconds();
        cfg.s.launchCount += 1;
        cfg.s.lastLaunch = NowDateTime();
        cfg.s.lastVersion = gameVersion;
        auto& recent = cfg.s.recentVersions;
        recent.erase(std::remove(recent.begin(), recent.end(), gameVersion), recent.end());
        recent.insert(recent.begin(), gameVersion);
        if (recent.size() > 6) recent.resize(6);
        cfg.Save();
        ToastMsg("Minecraft запускается...");
        if (cfg.s.closeOnLaunch) ShowWindow(hwnd_, SW_MINIMIZE);
    }
}

void App::OnInstallFinished() {
    if (!installTask_) return;
    int state = installTask_->state.load();
    std::string error = installTask_->Error();
    bool played = installPlayAfter_;
    std::string game = installGame_;
    int kind = installKind_;
    installTask_.reset();
    installPlayAfter_ = false;
    if (state == (int)TaskState::Done) {
        ToastMsg("Установка завершена", 1);
        cfg.Save();
        if (played) LaunchNow(game, kind);
    } else if (state == (int)TaskState::Failed) {
        ToastMsg(error.empty() ? "Установка не удалась" : error, 2);
        SetPage(Page::Downloads);
    } else if (state == (int)TaskState::Cancelled) {
        ToastMsg("Загрузка отменена");
    }
}

// ------------------------------------------------------------------ mods logic

void App::RefreshInstalledMods() {
    installedMods_ = ScanInstalledMods(mc.ModsDir());
    installedModsDirty_ = false;
}

void App::StartModSearch(bool append) {
    int provider = modsProvider_;
    std::string query = Trim(modSearch_);
    std::string gameVersion = modGameVersion_;
    int loaderKind = modLoaderKind_;
    std::string apiKey = cfg.s.cfApiKey;

    int generation = 0;
    int offset = 0;
    std::string key = std::to_string(provider) + "|" + query + "|" + gameVersion + "|" +
                      std::to_string(loaderKind);
    {
        std::lock_guard lock(modsAsync_.mutex);
        if (append && modsAsync_.key != key) return;
        offset = append ? (int)modsAsync_.hits.size() : 0;
        generation = ++modsAsync_.generation;
        modsAsync_.loading = true;
        modsAsync_.error.clear();
        modsAsync_.hasMore = false;
        if (!append) {
            modsAsync_.hits.clear();
            modsAsync_.total = 0;
        }
        modsAsync_.key = key;
    }

    std::thread([this, generation, provider, query, gameVersion, loaderKind, apiKey, offset]() {
        std::vector<ModHit> hits;
        int total = 0;
        std::string error;
        bool ok;
        if (provider == 0) {
            ok = modrinth.Search(query, gameVersion, loaderKind, offset, 24, hits, total, error);
        } else {
            ok = curse.Search(apiKey, query, gameVersion, loaderKind, offset, 24, hits, total, error);
        }
        size_t added = hits.size();
        std::lock_guard lock(modsAsync_.mutex);
        if (modsAsync_.generation != generation) return;
        modsAsync_.loading = false;
        if (!error.empty()) modsAsync_.error = error;
        if (ok) {
            if (offset == 0) modsAsync_.hits = std::move(hits);
            else modsAsync_.hits.insert(modsAsync_.hits.end(), hits.begin(), hits.end());
            modsAsync_.total = total;
            modsAsync_.hasMore = added > 0 && (int)modsAsync_.hits.size() < total;
        }
    }).detach();
}

void App::OpenModFiles(const ModHit& mod, int provider) {
    modFilesProject_ = mod.id;
    modFilesProvider_ = provider;
    modFilesTitle_ = mod.title;
    modFilesOpen_ = true;
    modFilesNeedOpen_ = true;

    std::string gameVersion = modGameVersion_;
    int loaderKind = modLoaderKind_;
    std::string apiKey = cfg.s.cfApiKey;
    std::string project = mod.id;
    int generation = 0;
    {
        std::lock_guard lock(modFilesAsync_.mutex);
        generation = ++modFilesAsync_.generation;
        modFilesAsync_.loading = true;
        modFilesAsync_.error.clear();
        modFilesAsync_.files.clear();
        modFilesAsync_.title = mod.title;
        modFilesAsync_.key = std::to_string(provider) + "|" + project;
    }
    std::thread([this, generation, provider, project, gameVersion, loaderKind, apiKey]() {
        std::vector<ModFile> files;
        std::string error;
        bool ok;
        if (provider == 0) {
            ok = modrinth.Files(project, gameVersion, loaderKind, files, error);
        } else {
            ok = curse.Files(apiKey, project, gameVersion, loaderKind, files, error);
        }
        std::lock_guard lock(modFilesAsync_.mutex);
        if (modFilesAsync_.generation != generation) return;
        modFilesAsync_.loading = false;
        if (!error.empty()) modFilesAsync_.error = error;
        if (ok) modFilesAsync_.files = std::move(files);
    }).detach();
}

void App::InstallMod(const ModFile& file, const std::string& title) {
    fs::path dir = mc.ModsDir();
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::string filename = SanitizeFilename(file.filename);
    fs::path dest = dir / filename;
    fs::path part = dest;
    part += L".part";
    if (FileExists(dest) || FileExists(part)) {
        ToastMsg("Файл уже скачивается или установлен: " + filename, 2);
        return;
    }
    lastModTask_ = tasks.Start("Мод: " + title, [file, dest](Task& task) {
        task.SetStatus("Скачивание " + file.filename);
        std::string error;
        bool ok = DownloadFile(
            file.url, dest,
            [&](int64_t received, int64_t total) {
                if (total > 0) {
                    task.SetProgress((float)received / (float)total,
                                     "Скачивание " + ui::FormatSize(received) + " / " +
                                         ui::FormatSize(total));
                }
            },
            &task.cancel, 3, &error);
        if (!ok) throw std::runtime_error(error.empty() ? "Не удалось скачать файл" : error);
        task.Log("Сохранено: " + dest.string());
        task.Succeed("Установлено");
    });
    ToastMsg("Скачивание мода...");
}

// ------------------------------------------------------------------ modals

void App::DrawModals() {
    if (modFilesNeedOpen_) {
        modFilesNeedOpen_ = false;
        ImGui::OpenPopup("Файлы мода");
    }
    if (deleteNeedOpen_) {
        deleteNeedOpen_ = false;
        ImGui::OpenPopup("Удалить мод");
    }

    ImVec2 display = ImGui::GetIO().DisplaySize;
    ImVec2 center(display.x * 0.5f, display.y * 0.5f);
    const ImGuiWindowFlags modalFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ui::S(780), ui::S(574)), ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui::S(24), ui::S(22)));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(ui::Hex("#141826")));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(colBorder));
    if (ImGui::BeginPopupModal("Файлы мода", nullptr, modalFlags)) {
        ImGui::PushFont(uiFonts.h2);
        ImGui::TextUnformatted(modFilesTitle_.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - ui::S(46));
        if (ui::IconButton("##close_modfiles", ui::IconClose, ImVec2(ui::S(32), ui::S(30)), false, 0,
                           "Закрыть")) {
            ImGui::CloseCurrentPopup();
        }
        std::string subtitle = modGameVersion_.empty() ? "Все версии игры" : modGameVersion_;
        subtitle += "  ·  ";
        subtitle += modLoaderKind_ < 0 ? "Любой загрузчик" : LoaderName(modLoaderKind_);
        ImGui::PushFont(uiFonts.smallFont);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
        ImGui::TextUnformatted(subtitle.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Separator();

        std::vector<ModFile> files;
        std::string error;
        bool loading = false;
        {
            std::lock_guard lock(modFilesAsync_.mutex);
            files = modFilesAsync_.files;
            error = modFilesAsync_.error;
            loading = modFilesAsync_.loading;
        }

        if (loading) {
            ImGui::Dummy(ImVec2(0, ui::S(60)));
            ImVec2 p = ImGui::GetCursorScreenPos();
            ui::Spinner(ImVec2(p.x + ui::S(14), p.y + ui::S(14)), ui::S(13), colAccent);
            ImGui::SameLine(0, ui::S(38));
            TextMuted("Загрузка списка файлов...");
            ImGui::Dummy(ImVec2(0, ui::S(60)));
        } else if (!error.empty() && files.empty()) {
            ImGui::Dummy(ImVec2(0, ui::S(20)));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colRed));
            ImGui::TextWrapped("%s", error.c_str());
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, ui::S(20)));
        } else {
            ImGui::BeginChild("##modfile_list", ImVec2(0, ui::S(396)), ImGuiChildFlags_None,
                              ImGuiWindowFlags_AlwaysVerticalScrollbar);
            for (size_t i = 0; i < files.size(); ++i) {
                const ModFile& file = files[i];
                ImGui::PushID((int)i);
                float rowW = ImGui::GetContentRegionAvail().x;
                ImGui::BeginGroup();
                ImGui::PushFont(uiFonts.semibold);
                std::string name = file.versionName.empty() ? file.filename : file.versionName;
                ImGui::TextUnformatted(ui::Ellipsize(name, rowW - ui::S(150), uiFonts.semibold).c_str());
                ImGui::PopFont();
                ImGui::PushFont(uiFonts.smallFont);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
                std::string meta;
                if (!file.gameVersions.empty()) meta += file.gameVersions;
                if (!file.loaders.empty()) meta += (meta.empty() ? "" : "  ·  ") + file.loaders;
                if (file.size > 0) meta += (meta.empty() ? "" : "  ·  ") + ui::FormatSize(file.size);
                if (!file.published.empty()) meta += "  ·  " + ui::FormatDate(file.published);
                ImGui::TextUnformatted(ui::Ellipsize(meta, rowW - ui::S(150), uiFonts.smallFont).c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
                ImGui::PushFont(uiFonts.tiny);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colFaint));
                ImGui::TextUnformatted(file.filename.c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
                ImGui::EndGroup();

                ImGui::SameLine(rowW - ui::S(134));
                if (ui::SoftButton("Скачать", ImVec2(ui::S(124), ui::S(34)), true, ui::IconDownload)) {
                    InstallMod(file, modFilesTitle_);
                }
                ImGui::PopID();
                ImGui::Separator();
            }
            if (!error.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colRed));
                ImGui::TextWrapped("%s", error.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();
        }

        ImGui::Dummy(ImVec2(0, ui::S(4)));
        if (ui::GhostButton("Закрыть", ImVec2(ui::S(150), ui::S(38)))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ui::S(500), ui::S(248)), ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui::S(24), ui::S(22)));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(ui::Hex("#141826")));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(colBorder));
    if (ImGui::BeginPopupModal("Удалить мод", nullptr, modalFlags)) {
        ImGui::PushFont(uiFonts.h2);
        ImGui::TextUnformatted("Удалить мод?");
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, ui::S(6)));
        ImGui::PushFont(uiFonts.semibold);
        ImGui::TextUnformatted(ui::Ellipsize(deleteTarget_.name, ui::S(420), uiFonts.semibold).c_str());
        ImGui::PopFont();
        TextMutedWrapped("Файл будет удалён из папки mods. Это действие нельзя отменить.");
        ImGui::Dummy(ImVec2(0, ui::S(14)));
        if (ui::GhostButton("Отмена", ImVec2(ui::S(140), ui::S(40)))) ImGui::CloseCurrentPopup();
        ImGui::SameLine(0, ui::S(10));
        if (ui::PrimaryButton("Удалить", ImVec2(ui::S(160), ui::S(40)), true, ui::IconTrash, false)) {
            if (DeleteInstalledMod(deleteTarget_)) {
                ToastMsg("Мод удалён", 1);
                installedModsDirty_ = true;
            } else {
                ToastMsg("Не удалось удалить файл мода", 2);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

// ------------------------------------------------------------------ pages switch

void App::SetPage(Page page) {
    if (page_ == page) return;
    page_ = page;
    pageAnim_ = cfg.s.animations ? 0.0f : 1.0f;
    pageScrollReset_ = true;
    cfg.s.lastPage = (int)page;
    cfg.Save();
}
