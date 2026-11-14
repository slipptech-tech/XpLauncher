#pragma once

#include "imgui.h"
#include "json.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ---------------------------------------------------------------- text utils
std::string Utf8(const std::wstring& s);
std::wstring Wide(const std::string& s);
std::string UrlEncode(const std::string& s);
std::string Trim(const std::string& s);
std::string ToLower(std::string s);
bool StartsWith(const std::string& s, const std::string& prefix);
bool EndsWith(const std::string& s, const std::string& suffix);

// ---------------------------------------------------------------- http
struct HttpResult {
    long status = 0;
    std::string body;
    std::string error;
    bool ok() const { return status >= 200 && status < 300 && error.empty(); }
};

HttpResult HttpGet(const std::string& url, const std::vector<std::string>& headers = {},
                   int timeoutMs = 30000);
HttpResult HttpPost(const std::string& url, const std::string& body,
                    const std::vector<std::string>& headers = {}, int timeoutMs = 30000);
HttpResult HttpDelete(const std::string& url, const std::vector<std::string>& headers = {},
                      int timeoutMs = 30000);

using ProgressFn = std::function<void(int64_t received, int64_t total)>;
bool DownloadFile(const std::string& url, const fs::path& dest, const ProgressFn& progress,
                  std::atomic<bool>* cancel = nullptr, int retries = 3,
                  std::string* error = nullptr, const std::vector<std::string>& headers = {});

// ---------------------------------------------------------------- files
fs::path AppDataDir();
void SetDataDirOverride(const std::wstring& path);
std::string ReadTextFile(const fs::path& path);
std::string ReadBinaryFile(const fs::path& path);
bool WriteTextFile(const fs::path& path, const std::string& text);
bool WriteBinaryFile(const fs::path& path, const std::string& bytes);
bool FileExists(const fs::path& path);
bool DirectoryExists(const fs::path& path);

// ---------------------------------------------------------------- zip
bool ZipList(const fs::path& zipPath, std::vector<std::string>& names, std::string* error = nullptr);
bool ZipReadFile(const fs::path& zipPath, const std::string& entry, std::string& out);
bool ZipExtract(const fs::path& zipPath, const fs::path& destDir,
                const std::function<bool(const std::string&)>& filter = {},
                const std::string& onlyExtension = "", bool flatten = false,
                std::string* error = nullptr);

// ---------------------------------------------------------------- logging
void LogInit(const fs::path& file);
void LogLine(const std::string& line);
fs::path LogPath();

// ---------------------------------------------------------------- process
struct ProcessResult {
    bool started = false;
    int exitCode = -1;
    std::string output;
};

ProcessResult RunProcess(const std::string& exe, const std::vector<std::string>& args,
                         std::atomic<bool>* cancel = nullptr,
                         const std::function<void(const std::string&)>& onLine = {},
                         int timeoutMs = 0);

// ---------------------------------------------------------------- tasks
enum class TaskState { Running = 0, Done = 1, Failed = 2, Cancelled = 3 };

struct Task {
    int id = 0;
    std::string title;
    std::atomic<int> state{static_cast<int>(TaskState::Running)};
    std::atomic<float> progress{0.0f};
    std::atomic<bool> cancel{false};

    void SetStatus(const std::string& text);
    void SetProgress(float value, const std::string& status = "");
    void Log(const std::string& line);
    void Succeed(const std::string& status = "");
    void Fail(const std::string& error);

    std::string Status();
    std::string Error();
    std::vector<std::string> Logs();
    bool IsRunning() const { return state.load() == static_cast<int>(TaskState::Running); }

private:
    std::mutex mutex_;
    std::string status_;
    std::string error_;
    std::vector<std::string> log_;
};

class TaskManager {
public:
    std::shared_ptr<Task> Start(const std::string& title, std::function<void(Task&)> fn);
    std::vector<std::shared_ptr<Task>> Snapshot();
    int ActiveCount();
    void ClearFinished();
    bool HasFinished();

private:
    std::mutex mutex_;
    std::vector<std::shared_ptr<Task>> tasks_;
    int nextId_ = 1;
};

// ---------------------------------------------------------------- config
struct Account {
    std::string name;
    std::string uuid;
    std::string type = "offline";
    std::string accessToken;
    std::string refreshToken;
    int64_t expiresAt = 0;
};

struct Settings {
    int ramMb = 4096;
    std::string javaPath;
    std::string gameDir;
    std::string jvmArgs;
    int winW = 1280;
    int winH = 720;
    bool fullscreen = false;
    bool closeOnLaunch = true;
    std::string accent = "#7C5CFF";
    std::string cfApiKey;
    std::string msaClientId;
    bool showSnapshots = false;
    std::string selectedVersion;
    int loaderKind = 0;
    std::string loaderVersion;
    std::string lastLaunch;
    std::string lastVersion;
    int launchCount = 0;
    long long playSeconds = 0;
    std::vector<std::string> recentVersions;
    // Interface
    bool animations = true;
    bool restoreAfterExit = true;
    bool launchToLastPage = true;
    // Server tab
    std::string serverVersion;
    int serverPort = 25565;
    int serverMaxPlayers = 10;
    std::string serverMotd = "XpLauncher LAN";
    int serverGamemode = 0;
    int serverDifficulty = 1;
    bool serverOnlineMode = false;
    int lastPage = 0;
    bool pendingServerStart = false;
};

class Config {
public:
    Settings s;
    std::vector<Account> accounts;
    int activeAccount = -1;
    fs::path dataDir;

    void Load();
    void Save();
    const Account* Active() const;
    std::string DefaultGameDir() const;
    int AddOfflineAccount(const std::string& name);
};

// ---------------------------------------------------------------- textures
class TextureCache {
public:
    TextureCache();
    ~TextureCache();
    ImTextureID Get(const std::string& url);
    ImTextureID GetFile(const fs::path& path);
    void Pump(void* d3dDevice);
    void Shutdown();

private:
    struct Entry {
        std::atomic<int> state{0}; // 0 queued, 1 loading, 2 ready, 3 failed
        std::vector<unsigned char> bytes;
        ImTextureID texture = 0;
    };
    void WorkerLoop();

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::string> queue_;
    std::unordered_map<std::string, std::shared_ptr<Entry>> map_;
    std::vector<std::thread> workers_;
    bool stop_ = false;
};

// ---------------------------------------------------------------- java
struct JavaInstall {
    std::string path;
    std::string version;
    int major = 0;
};

std::vector<JavaInstall> DetectJava();
void InvalidateJavaCache();
// Ищет java.exe в каталоге или в его подкаталогах (до 4 уровней).
std::string FindJavaExe(const fs::path& root);
// Скачивает и распаковывает Temurin JRE нужной версии в dataDir/java.
std::string EnsureJavaRuntime(int major, Task* task = nullptr, std::atomic<bool>* cancel = nullptr,
                              std::string* error = nullptr);
std::string OfflineUuid(const std::string& name);
std::string MinecraftUuid(const std::string& uuidWithDashes);
int64_t NowSeconds();
std::string Timestamp();
std::string NowDateTime();
std::string LocalIPv4();
