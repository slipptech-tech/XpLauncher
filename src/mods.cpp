#include "mods.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>

namespace {

const char* kUserAgent = "XpLauncher/1.0.0 (Windows; local launcher)";

std::string Str(const json& j, const char* key) {
    if (j.contains(key) && j[key].is_string()) return j[key].get<std::string>();
    return {};
}

long long Number(const json& j, const char* key) {
    if (j.contains(key) && j[key].is_number()) return j[key].get<long long>();
    return 0;
}

std::string JsonScalar(const json& j) {
    if (j.is_string()) return j.get<std::string>();
    if (j.is_number_integer()) return std::to_string(j.get<long long>());
    if (j.is_number_float()) return std::to_string(j.get<double>());
    return {};
}

std::string JoinArray(const json& value, const char* separator = ", ") {
    if (!value.is_array()) return {};
    std::string result;
    bool first = true;
    for (const auto& item : value) {
        if (!item.is_string()) continue;
        if (!first) result += separator;
        result += item.get<std::string>();
        first = false;
    }
    return result;
}

int CurseLoaderType(int loaderKind) {
    switch (loaderKind) {
        case 1: return 4;
        case 2: return 5;
        case 3: return 1;
        case 4: return 6;
        default: return 0;
    }
}

std::string CurseLoaderName(int type) {
    switch (type) {
        case 1: return "Forge";
        case 4: return "Fabric";
        case 5: return "Quilt";
        case 6: return "NeoForge";
        default: return "";
    }
}

std::string ModrinthFacets(const std::string& gameVersion, int loaderKind) {
    std::string facets = "[[\"project_type:mod\"]";
    if (!gameVersion.empty()) facets += ",[\"versions:" + gameVersion + "\"]";
    const char* loader = nullptr;
    switch (loaderKind) {
        case 1: loader = "fabric"; break;
        case 2: loader = "quilt"; break;
        case 3: loader = "forge"; break;
        case 4: loader = "neoforge"; break;
        default: break;
    }
    if (loader) facets += std::string(",[\"categories:") + loader + "\"]";
    facets += "]";
    return facets;
}

void ReadMetaFromZip(const fs::path& path, InstalledMod& mod) {
    std::string body;
    if (ZipReadFile(path, "fabric.mod.json", body)) {
        json j = json::parse(body, nullptr, false);
        if (!j.is_discarded() && j.is_object()) {
            mod.id = Str(j, "id");
            mod.name = Str(j, "name");
            mod.version = JsonScalar(j.contains("version") ? j["version"] : json());
        }
    }
    if (mod.name.empty() && ZipReadFile(path, "quilt.mod.json", body)) {
        json j = json::parse(body, nullptr, false);
        if (!j.is_discarded() && j.is_object()) {
            if (j.contains("quilt_loader") && j["quilt_loader"].is_object()) {
                mod.id = Str(j["quilt_loader"], "id");
                mod.version = JsonScalar(j["quilt_loader"].contains("version")
                                             ? j["quilt_loader"]["version"]
                                             : json());
            }
            if (j.contains("metadata") && j["metadata"].is_object()) {
                mod.name = Str(j["metadata"], "name");
            }
        }
    }
    if (mod.name.empty() &&
        (ZipReadFile(path, "META-INF/mods.toml", body) ||
         ZipReadFile(path, "META-INF/neoforge.mods.toml", body))) {
        std::regex re(R"((?:^|\n)\s*(modId|displayName|version)\s*=\s*["']([^"']*)["'])");
        for (std::sregex_iterator it(body.begin(), body.end(), re), end; it != end; ++it) {
            std::string key = (*it)[1].str();
            std::string value = (*it)[2].str();
            if (key == "modId" && mod.id.empty()) mod.id = value;
            if (key == "displayName" && mod.name.empty()) mod.name = value;
            if (key == "version" && mod.version.empty()) mod.version = value;
        }
    }
}

} // namespace

bool ModrinthClient::Search(const std::string& query, const std::string& gameVersion, int loaderKind,
                            int offset, int limit, std::vector<ModHit>& out, int& total,
                            std::string& error) const {
    out.clear();
    total = 0;
    std::string url = "https://api.modrinth.com/v2/search?query=" + UrlEncode(query) +
                      "&limit=" + std::to_string(limit) + "&offset=" + std::to_string(offset) +
                      "&index=relevance&facets=" + UrlEncode(ModrinthFacets(gameVersion, loaderKind));
    HttpResult r = HttpGet(url, {std::string("User-Agent: ") + kUserAgent, "Accept: application/json"});
    if (!r.ok()) {
        error = "Modrinth: " + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error);
        return false;
    }
    json j = json::parse(r.body, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("hits")) {
        error = "Некорректный ответ Modrinth";
        return false;
    }
    total = (int)Number(j, "total_hits");
    for (const auto& hit : j["hits"]) {
        if (!hit.is_object()) continue;
        ModHit mod;
        mod.id = Str(hit, "project_id");
        mod.slug = Str(hit, "slug");
        mod.title = Str(hit, "title");
        mod.description = Str(hit, "description");
        mod.author = Str(hit, "author");
        mod.iconUrl = Str(hit, "icon_url");
        // Modrinth serves resized WebP thumbnails which stb_image cannot decode;
        // the original PNG is available at the stable project icon endpoint.
        if (!mod.iconUrl.empty() && EndsWith(ToLower(mod.iconUrl), ".webp") && !mod.id.empty()) {
            mod.iconUrl = "https://cdn.modrinth.com/data/" + mod.id + "/icon.png";
        }
        mod.downloads = Number(hit, "downloads");
        if (hit.contains("categories") && hit["categories"].is_array()) {
            for (const auto& c : hit["categories"]) {
                if (c.is_string()) mod.categories.push_back(c.get<std::string>());
            }
        }
        if (!mod.id.empty()) out.push_back(std::move(mod));
    }
    return true;
}

bool ModrinthClient::Files(const std::string& projectId, const std::string& gameVersion, int loaderKind,
                           std::vector<ModFile>& out, std::string& error) const {
    out.clear();
    std::string url = "https://api.modrinth.com/v2/project/" + UrlEncode(projectId) + "/version";
    std::string params;
    if (!gameVersion.empty()) params += "&game_versions=" + UrlEncode("[\"" + gameVersion + "\"]");
    const char* loader = nullptr;
    switch (loaderKind) {
        case 1: loader = "fabric"; break;
        case 2: loader = "quilt"; break;
        case 3: loader = "forge"; break;
        case 4: loader = "neoforge"; break;
        default: break;
    }
    if (loader) params += std::string("&loaders=") + UrlEncode(std::string("[\"") + loader + "\"]");
    if (!params.empty()) url += "?" + params.substr(1);

    HttpResult r = HttpGet(url, {std::string("User-Agent: ") + kUserAgent, "Accept: application/json"});
    if (!r.ok()) {
        error = "Modrinth: " + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error);
        return false;
    }
    json j = json::parse(r.body, nullptr, false);
    if (j.is_discarded() || !j.is_array()) {
        error = "Некорректный ответ Modrinth";
        return false;
    }
    for (const auto& version : j) {
        if (!version.is_object()) continue;
        if (!version.contains("files") || !version["files"].is_array()) continue;
        const json* primary = nullptr;
        for (const auto& file : version["files"]) {
            if (!file.is_object()) continue;
            if (file.value("primary", false)) {
                primary = &file;
                break;
            }
        }
        if (!primary) {
            for (const auto& file : version["files"]) {
                if (file.is_object()) {
                    primary = &file;
                    break;
                }
            }
        }
        if (!primary) continue;
        ModFile file;
        file.versionName = Str(version, "name");
        file.versionId = Str(version, "version_number");
        file.filename = Str(*primary, "filename");
        file.url = Str(*primary, "url");
        file.published = Str(version, "date_published");
        file.size = Number(*primary, "size");
        file.gameVersions = version.contains("game_versions") ? JoinArray(version["game_versions"])
                                                              : std::string();
        file.loaders = version.contains("loaders") ? JoinArray(version["loaders"]) : std::string();
        file.primary = primary->value("primary", false);
        if (!file.url.empty() && !file.filename.empty()) out.push_back(std::move(file));
    }
    if (out.empty()) error = "Для этой версии игры нет файлов мода";
    return !out.empty();
}

bool CurseForgeClient::KeyLooksValid(const std::string& key) {
    return key.size() >= 20 && key.find_first_of(" \t\r\n") == std::string::npos;
}

bool CurseForgeClient::Search(const std::string& apiKey, const std::string& query,
                              const std::string& gameVersion, int loaderKind, int offset, int limit,
                              std::vector<ModHit>& out, int& total, std::string& error) const {
    out.clear();
    total = 0;
    if (!KeyLooksValid(apiKey)) {
        error = "Укажите API-ключ CurseForge в настройках";
        return false;
    }
    std::string url = "https://api.curseforge.com/v1/mods/search?gameId=432&classId=6&sortField=2"
                      "&sortOrder=desc&pageSize=" + std::to_string(limit) + "&index=" +
                      std::to_string(offset) + "&searchFilter=" + UrlEncode(query);
    if (!gameVersion.empty()) url += "&gameVersion=" + UrlEncode(gameVersion);
    int loaderType = CurseLoaderType(loaderKind);
    if (loaderType > 0) url += "&modLoaderType=" + std::to_string(loaderType);

    HttpResult r = HttpGet(url, {"x-api-key: " + apiKey, "Accept: application/json"});
    if (!r.ok()) {
        error = "CurseForge: " + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error);
        if (r.status == 403) error += " — проверьте API-ключ";
        return false;
    }
    json j = json::parse(r.body, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("data")) {
        error = "Некорректный ответ CurseForge";
        return false;
    }
    total = (int)Number(j, "pagination");
    if (j.contains("pagination") && j["pagination"].is_object()) {
        total = (int)Number(j["pagination"], "totalCount");
    }
    for (const auto& item : j["data"]) {
        if (!item.is_object()) continue;
        ModHit mod;
        long long id = Number(item, "id");
        mod.id = id > 0 ? std::to_string(id) : "";
        mod.title = Str(item, "name");
        mod.description = Str(item, "summary");
        mod.downloads = Number(item, "downloadCount");
        if (item.contains("logo") && item["logo"].is_object()) {
            mod.iconUrl = Str(item["logo"], "thumbnailUrl");
            if (mod.iconUrl.empty()) mod.iconUrl = Str(item["logo"], "url");
        }
        if (item.contains("links") && item["links"].is_object()) {
            mod.website = Str(item["links"], "websiteUrl");
        }
        if (item.contains("authors") && item["authors"].is_array() && !item["authors"].empty()) {
            mod.author = Str(item["authors"][0], "name");
        }
        if (item.contains("categories") && item["categories"].is_array()) {
            for (const auto& c : item["categories"]) {
                if (c.is_object() && c.contains("name") && c["name"].is_string())
                    mod.categories.push_back(c["name"].get<std::string>());
            }
        }
        if (!mod.id.empty()) out.push_back(std::move(mod));
    }
    return true;
}

bool CurseForgeClient::Files(const std::string& apiKey, const std::string& modId,
                             const std::string& gameVersion, int loaderKind,
                             std::vector<ModFile>& out, std::string& error) const {
    out.clear();
    if (!KeyLooksValid(apiKey)) {
        error = "Укажите API-ключ CurseForge в настройках";
        return false;
    }
    std::string url = "https://api.curseforge.com/v1/mods/" + UrlEncode(modId) +
                      "/files?pageSize=50";
    if (!gameVersion.empty()) url += "&gameVersion=" + UrlEncode(gameVersion);
    int loaderType = CurseLoaderType(loaderKind);
    if (loaderType > 0) url += "&modLoaderType=" + std::to_string(loaderType);

    HttpResult r = HttpGet(url, {"x-api-key: " + apiKey, "Accept: application/json"});
    if (!r.ok()) {
        error = "CurseForge: " + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error);
        if (r.status == 403) error += " — проверьте API-ключ";
        return false;
    }
    json j = json::parse(r.body, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("data") || !j["data"].is_array()) {
        error = "Некорректный ответ CurseForge";
        return false;
    }
    for (const auto& item : j["data"]) {
        if (!item.is_object()) continue;
        ModFile file;
        file.filename = Str(item, "fileName");
        file.url = Str(item, "downloadUrl");
        file.size = Number(item, "fileLength");
        file.published = Str(item, "fileDate");
        file.versionName = Str(item, "displayName");
        file.gameVersions = item.contains("gameVersions") ? JoinArray(item["gameVersions"]) : "";
        file.loaders = CurseLoaderName((int)Number(item, "modLoaderType"));
        if (file.url.empty() || file.filename.empty()) continue;
        out.push_back(std::move(file));
    }
    std::sort(out.begin(), out.end(), [](const ModFile& a, const ModFile& b) {
        return a.published > b.published;
    });
    if (out.empty()) error = "Для этой версии игры нет доступных файлов";
    return !out.empty();
}

std::vector<InstalledMod> ScanInstalledMods(const fs::path& modsDir) {
    std::vector<InstalledMod> mods;
    std::error_code ec;
    if (!fs::is_directory(modsDir, ec)) return mods;
    for (const auto& entry : fs::directory_iterator(modsDir, fs::directory_options::skip_permission_denied,
                                                   ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        std::string filename = entry.path().filename().string();
        bool disabled = EndsWith(filename, ".disabled");
        std::string baseName = disabled ? filename.substr(0, filename.size() - 9) : filename;
        if (!EndsWith(ToLower(baseName), ".jar")) continue;
        InstalledMod mod;
        mod.file = filename;
        mod.path = entry.path().string();
        mod.enabled = !disabled;
        mod.size = (long long)fs::file_size(entry.path(), ec);
        ReadMetaFromZip(entry.path(), mod);
        if (mod.name.empty()) {
            mod.name = baseName.substr(0, baseName.size() - 4);
        }
        mods.push_back(std::move(mod));
    }
    std::sort(mods.begin(), mods.end(), [](const InstalledMod& a, const InstalledMod& b) {
        if (a.enabled != b.enabled) return a.enabled;
        return ToLower(a.name) < ToLower(b.name);
    });
    return mods;
}

bool SetModEnabled(const InstalledMod& mod, bool enabled) {
    fs::path path = mod.path;
    std::error_code ec;
    if (enabled) {
        if (!EndsWith(path.string(), ".disabled")) return true;
        fs::path target = path;
        target.replace_extension("");
        fs::rename(path, target, ec);
        return !ec;
    }
    if (EndsWith(path.string(), ".disabled")) return true;
    fs::path target = path;
    target += ".disabled";
    fs::rename(path, target, ec);
    return !ec;
}

bool DeleteInstalledMod(const InstalledMod& mod) {
    std::error_code ec;
    fs::remove(mod.path, ec);
    return !ec;
}

std::string SanitizeFilename(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (unsigned char c : name) {
        if (c < 32 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' ||
            c == '?' || c == '*') {
            out.push_back('_');
        } else {
            out.push_back((char)c);
        }
    }
    out = Trim(out);
    if (out.empty()) out = "mod.jar";
    return out;
}
