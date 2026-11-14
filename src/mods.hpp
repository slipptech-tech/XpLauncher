#pragma once

#include "core.hpp"

#include <string>
#include <vector>

struct ModHit {
    std::string id;
    std::string slug;
    std::string title;
    std::string description;
    std::string author;
    std::string iconUrl;
    std::string website;
    long long downloads = 0;
    std::vector<std::string> categories;
};

struct ModFile {
    std::string versionName;
    std::string versionId;
    std::string filename;
    std::string url;
    std::string published;
    std::string gameVersions;
    std::string loaders;
    long long size = 0;
    bool primary = false;
};

struct InstalledMod {
    std::string file;
    std::string path;
    std::string name;
    std::string version;
    std::string id;
    long long size = 0;
    bool enabled = true;
};

class ModrinthClient {
public:
    bool Search(const std::string& query, const std::string& gameVersion, int loaderKind, int offset,
                int limit, std::vector<ModHit>& out, int& total, std::string& error) const;
    bool Files(const std::string& projectId, const std::string& gameVersion, int loaderKind,
               std::vector<ModFile>& out, std::string& error) const;
};

class CurseForgeClient {
public:
    static bool KeyLooksValid(const std::string& key);
    bool Search(const std::string& apiKey, const std::string& query, const std::string& gameVersion,
                int loaderKind, int offset, int limit, std::vector<ModHit>& out, int& total,
                std::string& error) const;
    bool Files(const std::string& apiKey, const std::string& modId, const std::string& gameVersion,
               int loaderKind, std::vector<ModFile>& out, std::string& error) const;
};

std::vector<InstalledMod> ScanInstalledMods(const fs::path& modsDir);
bool SetModEnabled(const InstalledMod& mod, bool enabled);
bool DeleteInstalledMod(const InstalledMod& mod);
std::string SanitizeFilename(const std::string& name);
