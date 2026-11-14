#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "core.hpp"
#include "minecraft.hpp"
#include "mods.hpp"
#include "msa.hpp"
#include "ui.hpp"

#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

enum class Page { Home = 0, Versions, Mods, Profile, Accounts, Downloads, Server, Settings };

struct Toast {
    std::string text;
    int kind = 0; // 0 info, 1 success, 2 error
    float life = 4.0f;
};

struct AsyncLoaders {
    std::mutex mutex;
    std::vector<LoaderVersion> versions;
    std::string error;
    bool loading = false;
    std::string key;
};

struct AsyncMods {
    std::mutex mutex;
    std::vector<ModHit> hits;
    int total = 0;
    std::string error;
    bool loading = false;
    bool hasMore = false;
    int generation = 0;
    std::string key;
};

struct AsyncFiles {
    std::mutex mutex;
    std::vector<ModFile> files;
    std::string error;
    bool loading = false;
    int generation = 0;
    std::string key;
    std::string title;
};

struct MsaFlow {
    std::mutex mutex;
    std::atomic<int> state{0}; // 0 idle, 1 starting, 2 waiting, 3 success, 4 failed, 5 cancelled
    std::atomic<bool> cancel{false};
    std::string userCode;
    std::string verificationUri;
    std::string status;
    std::string error;
    Account account;
    int mode = 0; // 0 login, 1 refresh
    int accountIndex = -1;
    std::thread worker;
};

class App {
public:
    Config cfg;
    TaskManager tasks;
    TextureCache textures;
    Minecraft mc{cfg, tasks};
    ModrinthClient modrinth;
    CurseForgeClient curse;

    void Init(void* hwnd, void* d3dDevice);
    void Frame();
    void Shutdown();

    void* Window() const { return hwnd_; }

private:
    void DrawTitleBar(ImVec2 display);
    void DrawSidebar(ImVec2 display);
    void DrawHome();
    void DrawVersions();
    void DrawMods();
    void DrawProfile();
    void DrawAccounts();
    void DrawDownloads();
    void DrawServer();
    void DrawSettings();
    void DrawModals();
    void DrawToasts();
    void AnimatePopups();

    void ToastMsg(const std::string& text, int kind = 0);
    void SaveConfig();
    void SyncSettingsBuffers();
    void EnsureManifest(bool force = false);
    void EnsureLoaderVersions();
    void EnsureJavaScan();
    void StartJavaInstall(int major, bool launchAfter, const std::string& gameVersion, int loaderKind);

    std::string GameVersion();
    std::string SelectedLoaderVersion();
    bool AnyInstallRunning();
    bool IsReadyToPlay(const std::string& gameVersion, int loaderKind);
    void StartPlay(const std::string& gameVersion, int loaderKind, const std::string& loaderVersion);
    void StartInstall(const std::string& gameVersion, int loaderKind, const std::string& loaderVersion,
                      bool playAfter);
    void LaunchNow(const std::string& gameVersion, int loaderKind);
    void OnInstallFinished();

    void RefreshInstalledMods();
    void StartModSearch(bool append);
    void OpenModFiles(const ModHit& mod, int provider);
    void InstallMod(const ModFile& file, const std::string& title);

    // Microsoft account
    void StartMicrosoftLogin();
    void StartMicrosoftRefresh(int accountIndex, bool launchAfter, const std::string& gameVersion,
                               int loaderKind);
    void CancelMicrosoftFlow();
    void PollMicrosoftFlow();
    void JoinMicrosoftWorker();

    void SetPage(Page page);
    void RelaunchElevated(Page page);
    bool IsElevated() const;

    // Server
    fs::path ServerDir() const;
    void PrepareServer(bool startAfter);
    void StartServerProcess();
    void StopServer();
    void RequestServerStart();
    void AppendServerLog(const std::string& line);
    std::string ServerAddress() const;

    std::string GameDirString();
    void DrawAvatar(const std::string& name, ImVec2 topLeft, float size, bool rounded = true);

    HWND hwnd_ = nullptr;
    void* device_ = nullptr;
    Page page_ = Page::Home;
    float pageAnim_ = 1.0f;
    bool pageScrollReset_ = false;
    double time_ = 0.0;

    std::string selectedVersion_;
    int loaderIdx_ = 0;
    AsyncLoaders loaders_;
    std::string loaderKey_;
    bool manifestRequested_ = false;

    std::shared_ptr<Task> installTask_;
    bool installPlayAfter_ = false;
    std::string installGame_;
    int installKind_ = 0;
    std::shared_ptr<Task> lastModTask_;
    int lastModTaskId_ = 0;

    std::atomic<bool> gameRunning_{false};
    bool gameWasRunning_ = false;
    std::string gameRunningVersion_;
    int64_t launchStartUnix_ = 0;

    // Mods page
    int modsProvider_ = 0;
    char modSearch_[128] = {};
    std::string modSearchLast_;
    std::string modGameVersion_;
    int modLoaderKind_ = -1;
    double modSearchDebounce_ = 0.0;
    bool modSearchDirty_ = false;
    bool modsInitialized_ = false;
    AsyncMods modsAsync_;
    std::vector<InstalledMod> installedMods_;
    bool installedModsDirty_ = true;
    bool modFilesOpen_ = false;
    bool modFilesNeedOpen_ = false;
    std::string modFilesProject_;
    int modFilesProvider_ = 0;
    std::string modFilesTitle_;
    AsyncFiles modFilesAsync_;
    bool deleteModOpen_ = false;
    bool deleteNeedOpen_ = false;
    InstalledMod deleteTarget_;

    // Accounts
    char nickname_[32] = {};
    char msaClientId_[40] = {};
    bool msaClientEditing_ = false;
    MsaFlow msa_;
    int msaReported_ = 0;
    bool msaLaunchAfter_ = false;
    std::string msaPendingGame_;
    int msaPendingKind_ = 0;

    // Settings
    char setGameDir_[512] = {};
    char setJvmArgs_[512] = {};
    char setCfKey_[256] = {};
    char setServerMotd_[80] = {};
    double confirmUntil_ = 0.0;
    int confirmAction_ = 0;
    std::vector<JavaInstall> javaList_;
    std::mutex javaMutex_;
    std::atomic<bool> javaLoading_{false};
    bool javaScanStarted_ = false;
    std::shared_ptr<Task> javaTask_;
    bool javaLaunchAfter_ = false;
    std::string javaPendingGame_;
    int javaPendingKind_ = 0;

    // Server runtime
    std::shared_ptr<Task> serverTask_;
    bool serverAutoStart_ = false;
    bool serverStartAfterPrepare_ = false;
    std::atomic<bool> serverRunning_{false};
    std::atomic<bool> serverOnline_{false};
    std::atomic<bool> serverExitNotify_{false};
    std::atomic<int> serverPlayers_{0};
    HANDLE serverProcess_ = nullptr;
    HANDLE serverStdin_ = nullptr;
    std::mutex serverLogMutex_;
    std::vector<std::string> serverLog_;
    bool showServerIp_ = false;
    std::string serverPreparedVersion_;
    std::string serverJavaPath_;
    std::atomic<int64_t> serverStartedAt_{0};

    std::set<int> expandedTasks_;
    std::vector<Toast> toasts_;
    std::unordered_map<ImGuiID, double> popupAnimStart_;
    std::set<ImGuiID> popupAnimSeen_;
};

bool VersionCombo(const char* id, std::string& value, const VersionManifest* manifest,
                  bool showSnapshots, float width, const char* emptyLabel = nullptr,
                  bool allowAny = false);
bool LoaderCombo(const char* id, int& kind, float width, bool allowAny = false);
std::string AvatarUrl(const std::string& name);
void PageHeader(const char* title, const char* subtitle);
void TextMuted(const char* text);
void TextMutedWrapped(const char* text);
