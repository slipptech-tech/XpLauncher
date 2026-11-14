#include <windows.h>
#include <shellapi.h>

#include "app.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace {

const char* kGamemodeNames[] = {"Выживание", "Творческий", "Приключение", "Наблюдатель"};
const char* kGamemodeValues[] = {"survival", "creative", "adventure", "spectator"};
const char* kDifficultyNames[] = {"Мирный", "Лёгкий", "Обычный", "Сложный"};
const char* kDifficultyValues[] = {"peaceful", "easy", "normal", "hard"};

bool OptionCombo(const char* id, int* value, const char* const* items, int count, float width) {
    bool changed = false;
    int clamped = std::clamp(*value, 0, count - 1);
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, items[clamped])) {
        for (int i = 0; i < count; ++i) {
            if (ImGui::Selectable(items[i], clamped == i)) {
                *value = i;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

std::string EscapeProperty(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (size_t i = 0; i < text.size();) {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x80) {
            if (c == '\\') out += "\\\\";
            else if (c == '\n' || c == '\r') out += ' ';
            else out += (char)c;
            ++i;
            continue;
        }
        uint32_t cp = 0;
        int len = 0;
        if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        if (len == 0 || i + (size_t)len > text.size()) {
            ++i;
            continue;
        }
        bool ok = true;
        for (int k = 1; k < len; ++k) {
            unsigned char cc = (unsigned char)text[i + k];
            if ((cc & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) {
            ++i;
            continue;
        }
        char buf[20];
        if (cp > 0xFFFF) {
            cp -= 0x10000;
            snprintf(buf, sizeof(buf), "\\u%04X\\u%04X", 0xD800 + (cp >> 10), 0xDC00 + (cp & 0x3FF));
        } else {
            snprintf(buf, sizeof(buf), "\\u%04X", cp);
        }
        out += buf;
        i += (size_t)len;
    }
    return out;
}

std::string BuildServerProperties(const Settings& s) {
    int gamemode = std::clamp(s.serverGamemode, 0, 3);
    int difficulty = std::clamp(s.serverDifficulty, 0, 3);
    std::string p;
    p += "server-port=" + std::to_string(std::clamp(s.serverPort, 1024, 65535)) + "\n";
    p += "max-players=" + std::to_string(std::clamp(s.serverMaxPlayers, 1, 200)) + "\n";
    p += "motd=" + EscapeProperty(s.serverMotd.empty() ? "XpLauncher LAN" : s.serverMotd) + "\n";
    p += std::string("gamemode=") + kGamemodeValues[gamemode] + "\n";
    p += std::string("difficulty=") + kDifficultyValues[difficulty] + "\n";
    p += std::string("online-mode=") + (s.serverOnlineMode ? "true" : "false") + "\n";
    p += "level-name=world\n";
    p += "spawn-protection=0\n";
    p += "view-distance=10\n";
    p += "simulation-distance=8\n";
    p += "enable-command-block=true\n";
    p += "white-list=false\n";
    p += "allow-flight=false\n";
    return p;
}

std::string FormatClock(int64_t seconds) {
    if (seconds < 0) seconds = 0;
    int64_t h = seconds / 3600;
    int64_t m = (seconds % 3600) / 60;
    int64_t s = seconds % 60;
    char buf[32];
    if (h > 0) snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", (long long)h, (long long)m, (long long)s);
    else snprintf(buf, sizeof(buf), "%02lld:%02lld", (long long)m, (long long)s);
    return buf;
}

const char* kIpMask = "•••.•••.•••.•••";

std::string MaskAddress(const std::string& address) {
    size_t colon = address.rfind(':');
    if (colon == std::string::npos) return kIpMask;
    return std::string(kIpMask) + address.substr(colon);
}

std::string HideIp(const std::string& text) {
    std::string ip = LocalIPv4();
    if (ip.empty()) return text;
    std::string out = text;
    size_t pos = 0;
    while ((pos = out.find(ip, pos)) != std::string::npos) {
        out.replace(pos, ip.size(), kIpMask);
        pos += strlen(kIpMask);
    }
    return out;
}

std::wstring QuoteWide(const std::wstring& value) {
    std::wstring out = L"\"";
    for (wchar_t c : value) {
        if (c == L'"') out += L'\\';
        out += c;
    }
    out += L"\"";
    return out;
}

} // namespace

// ------------------------------------------------------------------ state

fs::path App::ServerDir() const {
    std::string version = cfg.s.serverVersion.empty() ? "latest" : cfg.s.serverVersion;
    return cfg.dataDir / "server" / version;
}

std::string App::ServerAddress() const {
    return LocalIPv4() + ":" + std::to_string(std::clamp(cfg.s.serverPort, 1024, 65535));
}

void App::AppendServerLog(const std::string& line) {
    if (line.empty()) return;
    std::lock_guard lock(serverLogMutex_);
    serverLog_.push_back(line);
    if (serverLog_.size() > 300) {
        serverLog_.erase(serverLog_.begin(), serverLog_.begin() + (serverLog_.size() - 300));
    }
}

// ------------------------------------------------------------------ elevation

bool App::IsElevated() const {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    bool elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) &&
                    elevation.TokenIsElevated != 0;
    CloseHandle(token);
    return elevated;
}

void App::RelaunchElevated(Page page) {
    wchar_t exePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        ToastMsg("Не удалось определить путь лаунчера", 2);
        return;
    }
    cfg.s.lastPage = (int)page;
    cfg.Save();

    std::wstring params = L"--data-dir \"" + cfg.dataDir.wstring() + L"\"";
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.lpParameters = params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) {
        DWORD err = GetLastError();
        cfg.s.pendingServerStart = false;
        cfg.Save();
        ToastMsg(err == ERROR_CANCELLED ? "Запуск от имени администратора отменён"
                                        : "Не удалось перезапустить лаунчер с правами администратора",
                 2);
        return;
    }
    if (sei.hProcess) CloseHandle(sei.hProcess);
    DestroyWindow(hwnd_);
}

void App::RequestServerStart() {
    if (serverRunning_.load()) {
        ToastMsg("Сервер уже запущен");
        return;
    }
    if (cfg.s.serverVersion.empty()) {
        ToastMsg("Выберите версию сервера", 2);
        return;
    }
    SaveConfig();
    if (!IsElevated()) {
        cfg.s.pendingServerStart = true;
        ToastMsg("Перезапуск с правами администратора...");
        RelaunchElevated(Page::Server);
        return;
    }
    PrepareServer(true);
}

// ------------------------------------------------------------------ prepare + process

void App::PrepareServer(bool startAfter) {
    if (serverTask_ && serverTask_->IsRunning()) {
        ToastMsg("Подготовка сервера уже идёт");
        return;
    }
    std::string version = cfg.s.serverVersion;
    if (version.empty()) {
        ToastMsg("Выберите версию сервера", 2);
        return;
    }
    SaveConfig();
    serverStartAfterPrepare_ = startAfter;

    fs::path dir = ServerDir();
    Settings snapshot = cfg.s;
    serverTask_ = tasks.Start("Сервер " + version, [this, version, dir, snapshot](Task& task) {
        task.SetStatus("Получение списка версий");
        task.Log("Подготовка сервера " + version);

        std::string versionUrl;
        std::string error;
        if (auto manifest = mc.Manifest()) {
            for (const auto& entry : manifest->versions) {
                if (entry.id == version) {
                    versionUrl = entry.url;
                    break;
                }
            }
        }
        if (versionUrl.empty()) {
            HttpResult res = HttpGet("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json");
            if (!res.ok()) {
                throw std::runtime_error(res.error.empty() ? "Нет доступа к списку версий Mojang" : res.error);
            }
            json manifest = json::parse(res.body, nullptr, false);
            if (manifest.is_discarded() || !manifest.contains("versions")) {
                throw std::runtime_error("Не удалось разобрать список версий");
            }
            for (const auto& entry : manifest["versions"]) {
                if (entry.value("id", "") == version) {
                    versionUrl = entry.value("url", "");
                    break;
                }
            }
        }
        if (versionUrl.empty()) {
            throw std::runtime_error("Версия " + version + " не найдена у Mojang");
        }
        if (task.cancel.load()) throw std::runtime_error("Отменено");

        task.SetStatus("Загрузка описания версии");
        HttpResult vres = HttpGet(versionUrl);
        if (!vres.ok()) {
            throw std::runtime_error(vres.error.empty() ? "Не удалось скачать описание версии" : vres.error);
        }
        json vjson = json::parse(vres.body, nullptr, false);
        if (vjson.is_discarded() || !vjson.contains("downloads") ||
            !vjson["downloads"].contains("server")) {
            throw std::runtime_error("Для версии " + version + " нет серверного файла");
        }
        const json& serverJson = vjson["downloads"]["server"];
        std::string url = serverJson.value("url", "");
        long long size = serverJson.value("size", 0LL);
        int javaMajor = 8;
        if (vjson.contains("javaVersion") && vjson["javaVersion"].is_object()) {
            javaMajor = vjson["javaVersion"].value("majorVersion", 8);
        }
        if (url.empty()) {
            throw std::runtime_error("Для версии " + version + " нет серверного файла");
        }

        std::error_code ec;
        fs::create_directories(dir, ec);
        fs::path jarPath = dir / "server.jar";
        bool needDownload = true;
        if (FileExists(jarPath) && size > 0) {
            auto actual = fs::file_size(jarPath, ec);
            needDownload = ec || (long long)actual != size;
        }
        if (needDownload) {
            task.SetStatus("Загрузка server.jar (" + ui::FormatSize(size) + ")");
            task.Log("Скачивание " + url);
            int64_t lastPercent = -1;
            std::string dlError;
            bool ok = DownloadFile(
                url, jarPath,
                [&](int64_t received, int64_t total) {
                    total = total > 0 ? total : size;
                    float fraction = total > 0 ? (float)((double)received / (double)total) : 0.0f;
                    task.SetProgress(fraction, "Загрузка server.jar " + std::to_string((int)(fraction * 100)) + "%");
                    int64_t percent = (int64_t)(fraction * 100);
                    if (percent != lastPercent && percent % 5 == 0) {
                        lastPercent = percent;
                        task.Log("Загрузка: " + std::to_string((int)percent) + "%");
                    }
                },
                &task.cancel, 3, &dlError);
            if (!ok) {
                throw std::runtime_error(dlError.empty() ? "Не удалось скачать server.jar" : dlError);
            }
        } else {
            task.Log("server.jar уже загружен");
            task.SetProgress(0.72f, "Проверка файлов");
        }
        if (task.cancel.load()) throw std::runtime_error("Отменено");

        task.SetStatus("Запись конфигурации");
        WriteTextFile(dir / "eula.txt",
                      "# Принято через XpLauncher\n"
                      "eula=true\n");
        WriteTextFile(dir / "server.properties", BuildServerProperties(snapshot));

        task.SetStatus("Поиск Java " + std::to_string(javaMajor) + "+");
        std::string java;
        if (!snapshot.javaPath.empty() && FileExists(snapshot.javaPath)) {
            java = snapshot.javaPath;
        } else {
            java = FindJavaExe(AppDataDir() / "java" /
                               ("temurin-" + std::to_string(std::max(javaMajor, 8))));
        }
        if (java.empty()) {
            auto installs = DetectJava();
            int bestMajor = -1;
            for (const auto& install : installs) {
                if (install.major >= javaMajor && install.major > bestMajor) {
                    bestMajor = install.major;
                    java = install.path;
                }
            }
        }
        if (java.empty()) {
            task.Log("Java " + std::to_string(javaMajor) + " не найдена — скачиваю автоматически");
            std::string javaError;
            java = EnsureJavaRuntime(javaMajor, &task, &task.cancel, &javaError);
            if (java.empty()) {
                throw std::runtime_error(
                    javaError.empty() ? "Не удалось установить Java " + std::to_string(javaMajor)
                                      : javaError);
            }
            InvalidateJavaCache();
        }
        if (!FileExists(java)) {
            throw std::runtime_error("Java " + std::to_string(javaMajor) + " не найдена");
        }
        serverJavaPath_ = java;
        serverPreparedVersion_ = version;
        task.Log("Java: " + java);
        task.Log("Путь: " + (dir / "server.jar").string());
        task.SetProgress(1.0f, "Сервер готов к запуску");
        task.Succeed("Сервер готов к запуску");
    });
}

void App::StartServerProcess() {
    if (serverRunning_.load()) return;
    std::string version = cfg.s.serverVersion;
    fs::path dir = ServerDir();
    fs::path jar = dir / "server.jar";
    if (version.empty() || !FileExists(jar)) {
        ToastMsg("Сначала подготовьте сервер", 2);
        PrepareServer(true);
        return;
    }
    std::string java = serverJavaPath_;
    if (java.empty() || !FileExists(java)) {
        int required = 8;
        if (auto manifest = mc.Manifest()) {
            for (const auto& entry : manifest->versions) {
                if (entry.id != version) continue;
                HttpResult res = HttpGet(entry.url);
                if (res.ok()) {
                    json j = json::parse(res.body, nullptr, false);
                    if (!j.is_discarded() && j.contains("javaVersion")) {
                        required = j["javaVersion"].value("majorVersion", 8);
                    }
                }
                break;
            }
        }
        java = FindJavaExe(AppDataDir() / "java" /
                           ("temurin-" + std::to_string(std::max(required, 8))));
        if (java.empty()) {
            int bestMajor = -1;
            auto installs = DetectJava();
            for (const auto& install : installs) {
                if (install.major >= required && install.major > bestMajor) {
                    bestMajor = install.major;
                    java = install.path;
                }
            }
        }
    }
    if (java.empty() || !FileExists(java)) {
        ToastMsg("Java для сервера не найдена — установлю автоматически", 1);
        PrepareServer(true);
        return;
    }
    serverJavaPath_ = java;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE stdinRead = nullptr, stdinWrite = nullptr;
    HANDLE stdoutRead = nullptr, stdoutWrite = nullptr;
    if (!CreatePipe(&stdinRead, &stdinWrite, &sa, 0) ||
        !CreatePipe(&stdoutRead, &stdoutWrite, &sa, 0)) {
        ToastMsg("Не удалось создать каналы сервера", 2);
        return;
    }
    SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmd = QuoteWide(Wide(java)) +
                       L" -Xms1G -Xmx2G -Dfile.encoding=UTF-8 -jar server.jar nogui";
    std::vector<wchar_t> cmdLine(cmd.begin(), cmd.end());
    cmdLine.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = stdinRead;
    si.hStdOutput = stdoutWrite;
    si.hStdError = stdoutWrite;
    PROCESS_INFORMATION pi{};

    std::wstring cwd = dir.wstring();
    BOOL created = CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW, nullptr, cwd.c_str(), &si, &pi);
    CloseHandle(stdinRead);
    CloseHandle(stdoutWrite);
    if (!created) {
        CloseHandle(stdinWrite);
        CloseHandle(stdoutRead);
        ToastMsg("Не удалось запустить сервер (код " + std::to_string(GetLastError()) + ")", 2);
        return;
    }
    if (serverStdin_) CloseHandle(serverStdin_);
    if (serverProcess_) CloseHandle(serverProcess_);
    CloseHandle(pi.hThread);
    serverStdin_ = stdinWrite;
    serverProcess_ = pi.hProcess;
    serverStartedAt_ = NowSeconds();
    serverRunning_ = true;
    serverOnline_ = false;
    serverPlayers_ = 0;
    AppendServerLog("=== Запуск сервера " + version + " ===");
    AppendServerLog("Адрес: " + ServerAddress());
    ToastMsg("Сервер запускается...");

    HANDLE process = pi.hProcess;
    std::thread([this, process, stdoutRead]() {
        char buffer[4096];
        DWORD got = 0;
        std::string pending;
        while (ReadFile(stdoutRead, buffer, sizeof(buffer), &got, nullptr) && got > 0) {
            pending.append(buffer, got);
            size_t pos = 0;
            while ((pos = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, pos);
                pending.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                AppendServerLog(line);
                if (line.find("Done (") != std::string::npos) {
                    serverOnline_ = true;
                    serverStartedAt_ = NowSeconds();
                    AppendServerLog("=== Сервер готов: " + ServerAddress() + " ===");
                } else if (line.find("joined the game") != std::string::npos) {
                    int players = serverPlayers_.load() + 1;
                    serverPlayers_ = players < 0 ? 0 : players;
                } else if (line.find("left the game") != std::string::npos) {
                    int players = serverPlayers_.load() - 1;
                    serverPlayers_ = players < 0 ? 0 : players;
                }
            }
            if (pending.size() > 8192) pending.clear();
        }
        if (!pending.empty()) AppendServerLog(pending);
        CloseHandle(stdoutRead);
        WaitForSingleObject(process, 3000);
        DWORD code = 0;
        GetExitCodeProcess(process, &code);
        serverRunning_ = false;
        serverOnline_ = false;
        serverPlayers_ = 0;
        AppendServerLog("=== Сервер остановлен (код " + std::to_string(code) + ") ===");
        serverExitNotify_ = true;
    }).detach();
}

void App::StopServer() {
    if (!serverRunning_.load()) return;
    AppendServerLog("=== Остановка сервера ===");
    if (serverStdin_) {
        const char command[] = "stop\n";
        DWORD written = 0;
        WriteFile(serverStdin_, command, (DWORD)strlen(command), &written, nullptr);
        FlushFileBuffers(serverStdin_);
        ToastMsg("Останавливаем сервер...");
    } else if (serverProcess_) {
        TerminateProcess(serverProcess_, 0);
        ToastMsg("Сервер остановлен", 1);
    }
}

// ------------------------------------------------------------------ page

void App::DrawServer() {
    PageHeader("Сервер", "Поднимите сервер Minecraft для игры по локальной сети");

    float availW = ImGui::GetContentRegionAvail().x;
    float gap = ui::S(16);
    float leftW = std::max(ui::S(400), availW * 0.55f);
    float rightW = std::max(ui::S(300), availW - leftW - gap);
    float panelH = std::max(ui::S(460), ImGui::GetContentRegionAvail().y - ui::S(2));
    bool running = serverRunning_.load();
    bool online = serverOnline_.load();

    if (cfg.s.serverVersion.empty()) {
        if (auto manifest = mc.Manifest()) {
            cfg.s.serverVersion = manifest->latestRelease;
            cfg.Save();
        }
    }

    // ------------------------------------------------------------ settings
    ImGui::BeginGroup();
    ui::BeginCard("##server_settings", ImVec2(leftW, panelH));
    {
        float innerW = ImGui::GetContentRegionAvail().x;
        ImGui::BeginDisabled(running);

        ImGui::PushFont(uiFonts.h3);
        ImGui::TextUnformatted("Настройки сервера");
        ImGui::PopFont();
        TextMutedWrapped("Сервер запускается локально на вашем компьютере. Друзья подключаются по "
                         "адресу из карточки состояния.");

        ImGui::Dummy(ImVec2(0, ui::S(6)));
        ui::SectionLabel("Версия игры");
        std::string version = cfg.s.serverVersion;
        if (VersionCombo("##server_version", version, mc.Manifest().get(), cfg.s.showSnapshots,
                         innerW, "Выберите версию")) {
            cfg.s.serverVersion = version;
            cfg.Save();
        }

        ImGui::Dummy(ImVec2(0, ui::S(4)));
        float colW = (innerW - ui::S(12)) * 0.5f;
        float colX = ImGui::GetCursorPosX();
        float col2X = colX + colW + ui::S(12);
        float rowY = ImGui::GetCursorPosY();
        auto columnLabel = [&](float x, const char* label) {
            ImGui::SetCursorPos(ImVec2(x, rowY));
            ui::SectionLabel(label);
            ImGui::SetCursorPosX(x);
        };

        columnLabel(colX, "Порт");
        ImGui::SetNextItemWidth(colW);
        if (ImGui::InputInt("##server_port", &cfg.s.serverPort, 0, 0)) {
            cfg.s.serverPort = std::clamp(cfg.s.serverPort, 1024, 65535);
            cfg.Save();
        }
        columnLabel(col2X, "Максимум игроков");
        ImGui::SetNextItemWidth(colW);
        if (ImGui::InputInt("##server_players", &cfg.s.serverMaxPlayers, 0, 0)) {
            cfg.s.serverMaxPlayers = std::clamp(cfg.s.serverMaxPlayers, 1, 100);
            cfg.Save();
        }

        ImGui::Dummy(ImVec2(0, ui::S(2)));
        ui::SectionLabel("Название (MOTD)");
        ImGui::SetNextItemWidth(innerW);
        if (ImGui::InputTextWithHint("##server_motd", "XpLauncher LAN", setServerMotd_,
                                     sizeof(setServerMotd_))) {
            cfg.s.serverMotd = Trim(setServerMotd_);
            if (cfg.s.serverMotd.empty()) cfg.s.serverMotd = "XpLauncher LAN";
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) cfg.Save();

        ImGui::Dummy(ImVec2(0, ui::S(2)));
        rowY = ImGui::GetCursorPosY();
        columnLabel(colX, "Режим игры");
        if (OptionCombo("##server_gamemode", &cfg.s.serverGamemode, kGamemodeNames, 4, colW)) {
            cfg.Save();
        }
        columnLabel(col2X, "Сложность");
        if (OptionCombo("##server_difficulty", &cfg.s.serverDifficulty, kDifficultyNames, 4, colW)) {
            cfg.Save();
        }

        ImGui::Dummy(ImVec2(0, ui::S(2)));
        if (ui::CheckboxStyled("Проверка лицензии (online-mode)", &cfg.s.serverOnlineMode)) {
            cfg.Save();
        }
        ImGui::PushFont(uiFonts.tiny);
        TextMutedWrapped(cfg.s.serverOnlineMode
                             ? "Подключаться смогут только лицензионные аккаунты."
                             : "Игроки без лицензии смогут зайти. Подходит для локальной игры.");
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0, ui::S(10)));
        ui::SectionLabel("Как подключиться");
        ImGui::PushFont(uiFonts.tiny);
        TextMutedWrapped("1. Запустите сервер и дождитесь строки «Done» в консоли.");
        TextMutedWrapped("2. На другом компьютере откройте Minecraft → Сетевая игра.");
        TextMutedWrapped("3. Введите адрес из карточки состояния и подключитесь.");
        ImGui::PopFont();

        ImGui::EndDisabled();

        float actionY = panelH - ui::S(18) - ui::S(42);
        ImGui::SetCursorPosY(actionY);
        if (running) {
            if (ui::PrimaryButton("Остановить сервер",
                                  ImVec2(innerW - ui::S(54), ui::S(42)), true, ui::IconClose, false)) {
                StopServer();
            }
        } else {
            float startW = std::max(ui::S(180), innerW - ui::S(54));
            if (ui::PrimaryButton("Запустить сервер", ImVec2(startW, ui::S(42)), true,
                                  ui::IconPlay, true)) {
                RequestServerStart();
            }
        }
        ImGui::SameLine(0, ui::S(10));
        if (ui::IconButton("##server_folder", ui::IconFolder, ImVec2(ui::S(44), ui::S(42)), false,
                           colText, "Открыть папку сервера")) {
            std::error_code ec;
            fs::create_directories(ServerDir(), ec);
            ui::OpenPath(ServerDir().string());
        }
    }
    ui::EndCard();
    ImGui::EndGroup();

    ImGui::SameLine(0, gap);

    // ------------------------------------------------------------ status + console
    ImGui::BeginGroup();
    ui::BeginCard("##server_status", ImVec2(rightW, ui::S(316)));
    {
        float innerW = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::PushFont(uiFonts.h3);
        ImGui::TextUnformatted("Состояние");
        ImGui::PopFont();
        ImGui::SameLine(0, ui::S(10));
        ui::Badge(online ? "В сети" : (running ? "Запуск..." : "Остановлен"),
                  online ? colGreen : (running ? colYellow : colMuted));

        ImGui::Dummy(ImVec2(0, ui::S(6)));
        ImGui::PushFont(uiFonts.smallFont);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colFaint));
        ImGui::TextUnformatted("Адрес для подключения");
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::PushFont(uiFonts.h2);
        ImVec2 addrPos = ImGui::GetCursorScreenPos();
        std::string address = ServerAddress();
        ImGui::TextUnformatted(showServerIp_ ? address.c_str() : MaskAddress(address).c_str());
        ImGui::PopFont();
        ImVec2 addrSize = ImGui::GetItemRectSize();
        ImVec2 nextLine = ImGui::GetCursorScreenPos();
        float btnSize = ui::S(32);
        ImVec2 btnPos(addrPos.x + innerW - btnSize * 2.0f - ui::S(8),
                      addrPos.y + (addrSize.y - btnSize) * 0.5f);
        ImGui::SetCursorScreenPos(btnPos);
        if (ui::IconButton("##toggle_ip", showServerIp_ ? ui::IconEyeOff : ui::IconEye,
                           ImVec2(btnSize, btnSize), showServerIp_, colMuted,
                           showServerIp_ ? "Скрыть IP" : "Показать IP")) {
            showServerIp_ = !showServerIp_;
        }
        ImGui::SetCursorScreenPos(ImVec2(btnPos.x + btnSize + ui::S(8), btnPos.y));
        if (ui::IconButton("##copy_addr", ui::IconGlobe, ImVec2(btnSize, btnSize), false,
                           colMuted, "Скопировать адрес")) {
            ImGui::SetClipboardText(address.c_str());
            ToastMsg("Адрес скопирован", 1);
        }
        ImGui::SetCursorScreenPos(nextLine);
        ImGui::PushFont(uiFonts.tiny);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colFaint));
        ImGui::TextUnformatted(showServerIp_ ? "IP виден — нажмите на глаз, чтобы скрыть"
                                             : "IP скрыт — нажмите на глаз, чтобы показать");
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0, ui::S(4)));
        auto infoRow = [&](const char* label, const std::string& value, ImU32 color) {
            float rowW = ImGui::GetContentRegionAvail().x;
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::PushFont(uiFonts.smallFont);
            dl->AddText(ImVec2(p.x, p.y + ui::S(3)), colFaint, label);
            ImGui::PopFont();
            ImGui::PushFont(uiFonts.body);
            ImVec2 vs = ImGui::CalcTextSize(value.c_str());
            dl->AddText(ImVec2(p.x + rowW - vs.x, p.y), color, value.c_str());
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(rowW, ImGui::GetTextLineHeight() + ui::S(4)));
        };
        infoRow("Версия", cfg.s.serverVersion.empty() ? "—" : cfg.s.serverVersion, colText);
        infoRow("Игроков",
                std::to_string(serverPlayers_.load()) + " / " +
                    std::to_string(std::clamp(cfg.s.serverMaxPlayers, 1, 100)),
                colText);
        infoRow("Время работы",
                running ? FormatClock(NowSeconds() - serverStartedAt_.load()) : "—", colText);

        if (serverTask_ && serverTask_->IsRunning()) {
            ImGui::Dummy(ImVec2(0, ui::S(4)));
            ui::ProgressBar(serverTask_->progress.load(), ImVec2(innerW, ui::S(8)), colAccent);
            ImGui::PushFont(uiFonts.tiny);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
            ImGui::TextUnformatted(serverTask_->Status().c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }

        ImGui::PushFont(uiFonts.tiny);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colFaint));
        ImGui::TextUnformatted(IsElevated() ? "Лаунчер запущен с правами администратора"
                                            : "Для запуска нужны права администратора");
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ui::EndCard();

    ImGui::Dummy(ImVec2(0, gap));

    float consoleH = std::max(ui::S(180),
                              panelH - ui::S(316) - gap - ImGui::GetStyle().ItemSpacing.y * 2.0f);
    ui::BeginCard("##server_console", ImVec2(rightW, consoleH));
    {
        float innerW = ImGui::GetContentRegionAvail().x;
        float headerY = ImGui::GetCursorPosY();
        ImGui::PushFont(uiFonts.h3);
        float titleH = ImGui::GetTextLineHeight();
        ImGui::TextUnformatted("Консоль");
        ImGui::PopFont();
        ImGui::SameLine(ui::S(20) + innerW - ui::S(28));
        ImGui::SetCursorPosY(headerY + (titleH - ui::S(28)) * 0.5f);
        if (ui::IconButton("##console_clear", ui::IconTrash, ImVec2(ui::S(28), ui::S(28)), false,
                           colFaint, "Очистить консоль")) {
            std::lock_guard lock(serverLogMutex_);
            serverLog_.clear();
        }
        ImGui::Separator();

        std::vector<std::string> lines;
        {
            std::lock_guard lock(serverLogMutex_);
            lines = serverLog_;
        }
        ImGui::BeginChild("##console_scroll", ImVec2(0, 0), ImGuiChildFlags_None);
        if (lines.empty()) {
            ImGui::Dummy(ImVec2(0, ui::S(16)));
            ImGui::PushFont(uiFonts.smallFont);
            TextMutedWrapped(running ? "Сервер запускается, вывод появится здесь..."
                                     : "Здесь появится вывод сервера после запуска.");
            ImGui::PopFont();
        } else {
            static size_t seenLines = 0;
            bool stick = lines.size() != seenLines;
            seenLines = lines.size();
            ImGui::PushFont(uiFonts.mono);
            for (const std::string& line : lines) {
                ImU32 color = colMuted;
                if (line.find("ERROR") != std::string::npos ||
                    line.find("SEVERE") != std::string::npos) {
                    color = colRed;
                } else if (line.find("WARN") != std::string::npos) {
                    color = colYellow;
                } else if (line.find("Done (") != std::string::npos ||
                           line.find("===") != std::string::npos) {
                    color = line.find("===") != std::string::npos ? colAccent : colGreen;
                }
                std::string shown = showServerIp_ ? line : HideIp(line);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color));
                ImGui::TextUnformatted(shown.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::PopFont();
            if (stick) ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }
    ui::EndCard();
    ImGui::EndGroup();
}
