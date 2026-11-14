#pragma once

#include "core.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

struct VersionEntry {
    std::string id;
    std::string type;
    std::string url;
    std::string releaseTime;
};

struct VersionManifest {
    std::string latestRelease;
    std::string latestSnapshot;
    std::vector<VersionEntry> versions;
};

struct LoaderVersion {
    std::string version;
    bool stable = true;
};

const char* LoaderName(int kind);
const char* LoaderShortName(int kind);
const char* LoaderModrinthCategory(int kind);

class Minecraft {
public:
    Minecraft(Config& config, TaskManager& tasks);

    std::shared_ptr<const VersionManifest> Manifest();
    void FetchManifestAsync(bool force = false);
    bool ManifestReady();
    bool ManifestLoading();
    std::string ManifestError();

    bool BaseInstalled(const std::string& versionId);
    bool LoaderInstalled(const std::string& gameVersion, int loaderKind);
    std::string FindLoaderId(const std::string& gameVersion, int loaderKind,
                             const std::string& loaderVersion = "");
    std::string EffectiveVersionId(const std::string& gameVersion, int loaderKind);

    std::vector<LoaderVersion> FetchLoaderVersions(const std::string& gameVersion, int loaderKind,
                                                   std::string& error);
    std::string LatestLoaderVersion(const std::string& gameVersion, int loaderKind);
    int RequiredJavaMajor(const std::string& versionId);

    std::shared_ptr<Task> Install(const std::string& gameVersion, int loaderKind,
                                  std::string loaderVersion, bool playAfter = false);

    bool Launch(const std::string& versionId, const Account& account, const Settings& settings,
                std::string& error, std::function<void(int)> onExit = {});

    fs::path GameDir();
    fs::path VersionsDir();
    fs::path LibrariesDir();
    fs::path AssetsDir();
    fs::path ModsDir();

private:
    struct LibRef {
        std::string name;
        std::string path;
        std::string url;
        bool native = false;
    };

    json LoadVersionJson(const std::string& versionId);
    json ResolveVersion(const std::string& versionId, std::string* baseId);
    json EnsureVersionJson(const std::string& versionId, Task& task);
    void EnsureBaseFiles(const std::string& gameVersion, Task& task);
    void EnsureLibraries(const std::string& launchId, Task& task, const json& resolved);
    void EnsureAssets(const json& resolved, Task& task);
    std::string InstallLoader(const std::string& gameVersion, int loaderKind,
                              const std::string& loaderVersion, Task& task);
    std::string ChooseJava(int requiredMajor, const std::string& configured);
    std::vector<LibRef> CollectLibraries(const json& resolved,
                                         const std::map<std::string, bool>& features) const;
    bool RulesAllow(const json& rules, const std::map<std::string, bool>& features) const;
    std::string FindGeneratedVersion(const std::string& gameVersion, const std::string& marker,
                                     const std::string& versionHint);
    bool RunInstaller(const fs::path& jar, const std::string& gameVersion, Task& task);

    Config& cfg_;
    TaskManager& tasks_;
    std::mutex manifestMutex_;
    std::shared_ptr<const VersionManifest> manifest_;
    std::atomic<bool> manifestLoading_{false};
    std::atomic<bool> manifestReady_{false};
    std::string manifestError_;

    std::mutex loaderCacheMutex_;
    std::map<std::string, std::vector<LoaderVersion>> loaderCache_;
};
