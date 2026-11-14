#include "minecraft.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_set>

namespace {

constexpr const char* kManifestUrl = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
constexpr const char* kResourcesBase = "https://resources.download.minecraft.net/";
constexpr const char* kLibrariesBase = "https://libraries.minecraft.net/";

struct CancelledError {};

std::string ReplaceAll(std::string text, const std::string& from, const std::string& to) {
    if (from.empty()) return text;
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

std::vector<std::string> Split(const std::string& text, char delimiter) {
    std::vector<std::string> parts;
    std::string current;
    std::stringstream ss(text);
    while (std::getline(ss, current, delimiter)) parts.push_back(current);
    return parts;
}

std::vector<std::string> SplitArgs(const std::string& line) {
    std::vector<std::string> args;
    std::string current;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') {
            quoted = !quoted;
        } else if (c == ' ' && !quoted) {
            if (!current.empty()) {
                args.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) args.push_back(current);
    return args;
}

std::string OsName() {
    return "windows";
}

std::string OsArch() {
    SYSTEM_INFO info{};
    GetNativeSystemInfo(&info);
    switch (info.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_ARM64:
            return "arm64";
        case PROCESSOR_ARCHITECTURE_AMD64:
            return "x86_64";
        default:
            return "x86";
    }
}

std::string OsVersion() {
    return "10.0.19045";
}

std::string ArchBits() {
    return OsArch() == "x86" ? "32" : "64";
}

std::string MavenPath(const std::string& name, const std::string& extraClassifier = {}) {
    std::string spec = name;
    std::string extension = "jar";
    size_t at = spec.find('@');
    if (at != std::string::npos) {
        extension = spec.substr(at + 1);
        spec = spec.substr(0, at);
    }
    auto parts = Split(spec, ':');
    if (parts.size() < 3) return {};
    std::string classifier = parts.size() > 3 ? parts[3] : extraClassifier;
    std::string result = parts[0];
    std::replace(result.begin(), result.end(), '.', '/');
    result += "/" + parts[1] + "/" + parts[2] + "/" + parts[1] + "-" + parts[2];
    if (!classifier.empty()) result += "-" + classifier;
    result += "." + extension;
    return result;
}

std::string LibKey(const std::string& name) {
    auto parts = Split(name, ':');
    if (parts.size() < 2) return name;
    std::string key = parts[0] + ":" + parts[1];
    if (parts.size() > 3) key += ":" + parts[3];
    return key;
}

std::vector<long long> VersionNumbers(const std::string& version) {
    std::vector<long long> numbers;
    long long current = -1;
    for (char c : version) {
        if (c >= '0' && c <= '9') {
            if (current < 0) current = 0;
            current = current * 10 + (c - '0');
        } else if (c == '.') {
            if (current >= 0) numbers.push_back(current);
            current = -1;
        } else {
            break;
        }
    }
    if (current >= 0) numbers.push_back(current);
    return numbers;
}

bool VersionGreater(const std::string& a, const std::string& b) {
    auto na = VersionNumbers(a);
    auto nb = VersionNumbers(b);
    size_t count = std::max(na.size(), nb.size());
    for (size_t i = 0; i < count; ++i) {
        long long va = i < na.size() ? na[i] : 0;
        long long vb = i < nb.size() ? nb[i] : 0;
        if (va != vb) return va > vb;
    }
    return a > b;
}

bool VersionIsStable(const std::string& version) {
    std::string lower = ToLower(version);
    return lower.find("beta") == std::string::npos && lower.find("alpha") == std::string::npos &&
           lower.find("rc") == std::string::npos && lower.find("snapshot") == std::string::npos;
}

std::map<std::string, bool> DefaultFeatures(const Settings& settings) {
    return {{"is_demo_user", false},
            {"has_custom_resolution", settings.winW > 0 && settings.winH > 0},
            {"has_quick_plays_support", false},
            {"is_quick_play_singleplayer", false},
            {"is_quick_play_multiplayer", false},
            {"is_quick_play_realms", false}};
}

std::wstring QuoteArgW(const std::string& arg) {
    std::wstring w = Wide(arg);
    if (w.empty()) return L"\"\"";
    bool needQuote = w.find_first_of(L" \t") != std::wstring::npos || w.find(L'"') != std::wstring::npos;
    if (!needQuote) return w;
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : w) {
        if (c == L'\\') {
            ++backslashes;
        } else if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
            backslashes = 0;
        } else {
            out.append(backslashes, L'\\');
            backslashes = 0;
            out.push_back(c);
        }
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

bool DownloadWithTask(const std::string& url, const fs::path& dest, Task& task,
                      const std::string& label, int64_t* expectedSize = nullptr) {
    std::error_code ec;
    if (FileExists(dest)) {
        auto size = fs::file_size(dest, ec);
        if (!ec && size > 0 && (!expectedSize || *expectedSize <= 0 || (int64_t)size == *expectedSize)) {
            return true;
        }
    }
    if (dest.has_parent_path()) fs::create_directories(dest.parent_path(), ec);
    std::string error;
    bool ok = DownloadFile(
        url, dest,
        [&](int64_t received, int64_t total) {
            if (total > 0) {
                task.SetProgress((float)received / (float)total,
                                 label + " " + std::to_string((int)(100.0 * received / total)) + "%");
            }
        },
        &task.cancel, 3, &error);
    if (!ok) {
        if (task.cancel.load()) throw CancelledError{};
        task.Log("Не удалось скачать: " + url + (error.empty() ? "" : " (" + error + ")"));
    }
    return ok;
}

} // namespace

const char* LoaderName(int kind) {
    switch (kind) {
        case 1: return "Fabric";
        case 2: return "Quilt";
        case 3: return "Forge";
        case 4: return "NeoForge";
        default: return "Vanilla";
    }
}

const char* LoaderShortName(int kind) {
    switch (kind) {
        case 1: return "Fabric";
        case 2: return "Quilt";
        case 3: return "Forge";
        case 4: return "NeoForge";
        default: return "Vanilla";
    }
}

const char* LoaderModrinthCategory(int kind) {
    switch (kind) {
        case 1: return "fabric";
        case 2: return "quilt";
        case 3: return "forge";
        case 4: return "neoforge";
        default: return "";
    }
}

Minecraft::Minecraft(Config& config, TaskManager& tasks) : cfg_(config), tasks_(tasks) {}

fs::path Minecraft::GameDir() {
    std::string dir = cfg_.s.gameDir.empty() ? cfg_.DefaultGameDir() : cfg_.s.gameDir;
    std::error_code ec;
    fs::create_directories(dir, ec);
    return fs::path(dir);
}

fs::path Minecraft::VersionsDir() { return GameDir() / "versions"; }
fs::path Minecraft::LibrariesDir() { return GameDir() / "libraries"; }
fs::path Minecraft::AssetsDir() { return GameDir() / "assets"; }
fs::path Minecraft::ModsDir() { return GameDir() / "mods"; }

std::shared_ptr<const VersionManifest> Minecraft::Manifest() {
    std::lock_guard lock(manifestMutex_);
    return manifest_;
}

bool Minecraft::ManifestReady() { return manifestReady_.load(); }
bool Minecraft::ManifestLoading() { return manifestLoading_.load(); }

std::string Minecraft::ManifestError() {
    std::lock_guard lock(manifestMutex_);
    return manifestError_;
}

void Minecraft::FetchManifestAsync(bool force) {
    if (manifestLoading_.exchange(true)) return;
    (void)force;
    std::thread([this]() {
        HttpResult response = HttpGet(kManifestUrl, {"Accept: application/json"}, 30000);
        fs::path cachePath = cfg_.dataDir / "cache" / "version_manifest_v2.json";
        std::string text;
        std::string error;
        if (response.ok() && !response.body.empty()) {
            text = response.body;
            WriteTextFile(cachePath, text);
        } else {
            text = ReadTextFile(cachePath);
            error = response.error.empty() ? ("HTTP " + std::to_string(response.status)) : response.error;
        }

        auto parsed = std::make_shared<VersionManifest>();
        bool ok = false;
        if (!text.empty()) {
            json j = json::parse(text, nullptr, false);
            if (!j.is_discarded() && j.is_object() && j.contains("versions")) {
                if (j.contains("latest")) {
                    parsed->latestRelease = j["latest"].value("release", "");
                    parsed->latestSnapshot = j["latest"].value("snapshot", "");
                }
                for (const auto& v : j["versions"]) {
                    VersionEntry entry;
                    entry.id = v.value("id", "");
                    entry.type = v.value("type", "release");
                    entry.url = v.value("url", "");
                    entry.releaseTime = v.value("releaseTime", "");
                    if (!entry.id.empty() && !entry.url.empty()) parsed->versions.push_back(entry);
                }
                ok = !parsed->versions.empty();
            } else {
                error = "Не удалось разобрать список версий";
            }
        } else {
            error = "Нет доступа к списку версий";
        }

        {
            std::lock_guard lock(manifestMutex_);
            if (ok) {
                manifest_ = parsed;
                manifestReady_ = true;
                manifestError_.clear();
            } else {
                manifestError_ = error;
            }
        }
        manifestLoading_ = false;
    }).detach();
}

json Minecraft::LoadVersionJson(const std::string& versionId) {
    fs::path path = VersionsDir() / versionId / (versionId + ".json");
    std::string text = ReadTextFile(path);
    if (text.empty()) throw std::runtime_error("Версия " + versionId + " не установлена");
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        throw std::runtime_error("Повреждён JSON версии " + versionId);
    }
    return j;
}

json Minecraft::ResolveVersion(const std::string& versionId, std::string* baseId) {
    json child = LoadVersionJson(versionId);
    std::string parentId = child.value("inheritsFrom", "");
    if (parentId.empty()) {
        if (baseId) *baseId = versionId;
        return child;
    }
    json parent = ResolveVersion(parentId, baseId);
    json merged = parent;
    for (auto& [key, value] : child.items()) {
        if (key == "libraries" && value.is_array()) {
            json libraries = parent.contains("libraries") && parent["libraries"].is_array()
                                 ? parent["libraries"]
                                 : json::array();
            for (const auto& lib : value) libraries.push_back(lib);
            merged["libraries"] = libraries;
        } else if (key == "arguments" && value.is_object()) {
            json arguments = parent.contains("arguments") && parent["arguments"].is_object()
                                 ? parent["arguments"]
                                 : json::object();
            if (!arguments.contains("game")) arguments["game"] = json::array();
            if (!arguments.contains("jvm")) arguments["jvm"] = json::array();
            if (value.contains("game") && value["game"].is_array()) {
                for (const auto& a : value["game"]) arguments["game"].push_back(a);
            }
            if (value.contains("jvm") && value["jvm"].is_array()) {
                for (const auto& a : value["jvm"]) arguments["jvm"].push_back(a);
            }
            merged["arguments"] = arguments;
            merged.erase("minecraftArguments");
        } else {
            merged[key] = value;
        }
    }
    return merged;
}

json Minecraft::EnsureVersionJson(const std::string& versionId, Task& task) {
    fs::path path = VersionsDir() / versionId / (versionId + ".json");
    if (FileExists(path)) {
        std::string text = ReadTextFile(path);
        json j = json::parse(text, nullptr, false);
        if (!j.is_discarded() && j.is_object()) return j;
    }
    auto manifest = Manifest();
    if (!manifest) throw std::runtime_error("Список версий ещё не загружен");
    const VersionEntry* found = nullptr;
    for (const auto& entry : manifest->versions) {
        if (entry.id == versionId) {
            found = &entry;
            break;
        }
    }
    if (!found) throw std::runtime_error("Версия " + versionId + " не найдена");
    task.SetStatus("Скачивание описания версии " + versionId);
    std::string error;
    if (!DownloadFile(found->url, path, nullptr, &task.cancel, 3, &error)) {
        if (task.cancel.load()) throw CancelledError{};
        throw std::runtime_error("Не удалось скачать описание версии: " + error);
    }
    json j = json::parse(ReadTextFile(path), nullptr, false);
    if (j.is_discarded() || !j.is_object()) throw std::runtime_error("Повреждён JSON версии " + versionId);
    return j;
}

bool Minecraft::RulesAllow(const json& rules, const std::map<std::string, bool>& features) const {
    if (!rules.is_array()) return true;
    bool allowed = false;
    for (const auto& rule : rules) {
        if (!rule.is_object()) continue;
        bool match = true;
        if (rule.contains("os") && rule["os"].is_object()) {
            const json& os = rule["os"];
            if (os.contains("name") && os["name"].is_string() && os["name"] != OsName()) match = false;
            if (os.contains("arch") && os["arch"].is_string() && os["arch"] != OsArch()) match = false;
            if (os.contains("version") && os["version"].is_string() && match) {
                try {
                    if (!std::regex_search(OsVersion(), std::regex(os["version"].get<std::string>())))
                        match = false;
                } catch (...) {
                }
            }
        }
        if (rule.contains("features") && rule["features"].is_object()) {
            for (auto& [key, expected] : rule["features"].items()) {
                auto it = features.find(key);
                bool actual = it != features.end() ? it->second : false;
                if (expected.is_boolean() && actual != expected.get<bool>()) match = false;
            }
        }
        if (!match) continue;
        std::string action = rule.value("action", "allow");
        allowed = action == "allow";
    }
    return allowed;
}

std::vector<Minecraft::LibRef> Minecraft::CollectLibraries(
    const json& resolved, const std::map<std::string, bool>& features) const {
    std::vector<LibRef> refs;
    if (!resolved.contains("libraries") || !resolved["libraries"].is_array()) return refs;

    for (const auto& lib : resolved["libraries"]) {
        if (!lib.is_object()) continue;
        if (lib.contains("rules") && !RulesAllow(lib["rules"], features)) continue;
        std::string name = lib.value("name", "");
        if (name.empty()) continue;

        std::string path;
        std::string url;
        bool hasArtifact = false;
        if (lib.contains("downloads") && lib["downloads"].is_object()) {
            const json& downloads = lib["downloads"];
            if (downloads.contains("artifact") && downloads["artifact"].is_object()) {
                const json& artifact = downloads["artifact"];
                path = artifact.value("path", "");
                url = artifact.value("url", "");
                hasArtifact = !path.empty();
            }
        }
        bool nativeOnly = name.find(":natives-") != std::string::npos;
        if (!hasArtifact) {
            path = MavenPath(name);
            if (path.empty()) continue;
            std::string base = lib.value("url", kLibrariesBase);
            if (!base.empty() && base.back() != '/') base.push_back('/');
            url = base + path;
        }

        if (!nativeOnly && !path.empty()) {
            refs.push_back({name, path, url, false});
        }

        std::string classifier;
        if (lib.contains("natives") && lib["natives"].is_object()) {
            auto it = lib["natives"].find("windows");
            if (it != lib["natives"].end() && it->is_string()) {
                classifier = it->get<std::string>();
                classifier = ReplaceAll(classifier, "${arch}", ArchBits());
            }
        }
        if (!classifier.empty()) {
            std::string nativePath;
            std::string nativeUrl;
            if (lib.contains("downloads") && lib["downloads"].is_object() &&
                lib["downloads"].contains("classifiers") &&
                lib["downloads"]["classifiers"].is_object()) {
                const json& classifiers = lib["downloads"]["classifiers"];
                if (classifiers.contains(classifier)) {
                    nativePath = classifiers[classifier].value("path", "");
                    nativeUrl = classifiers[classifier].value("url", "");
                }
            }
            if (nativePath.empty()) {
                nativePath = MavenPath(name, classifier);
                std::string base = lib.value("url", kLibrariesBase);
                if (!base.empty() && base.back() != '/') base.push_back('/');
                nativeUrl = base + nativePath;
            }
            if (!nativePath.empty()) refs.push_back({name + ":" + classifier, nativePath, nativeUrl, true});
        }
        if (nativeOnly) {
            // У современных версий natives-артефакт — обычный jar в classpath: LWJGL
            // извлекает из него DLL сам. Распаковка нужна только старым сборкам без artifact.
            refs.push_back({name, path, url, !hasArtifact});
        }
    }
    return refs;
}

void Minecraft::EnsureLibraries(const std::string& launchId, Task& task, const json& resolved) {
    task.SetStatus("Библиотеки");
    fs::path nativesDir = VersionsDir() / launchId / "natives";
    std::error_code ec;
    fs::create_directories(nativesDir, ec);
    if (fs::exists(nativesDir, ec)) {
        for (const auto& entry : fs::directory_iterator(nativesDir, ec)) {
            if (ec) break;
            fs::remove_all(entry.path(), ec);
        }
    }

    auto refs = CollectLibraries(resolved, DefaultFeatures(cfg_.s));
    std::unordered_set<std::string> uniquePaths;
    std::vector<LibRef> unique;
    for (const auto& ref : refs) {
        if (uniquePaths.insert(ref.path).second) unique.push_back(ref);
    }

    int total = (int)unique.size();
    int done = 0;
    for (const auto& ref : unique) {
        if (task.cancel.load()) throw CancelledError{};
        fs::path dest = LibrariesDir() / ref.path;
        DownloadWithTask(ref.url, dest, task, "Библиотеки: " + std::to_string(done + 1) + " / " +
                                                       std::to_string(total));
        if (ref.native && FileExists(dest)) {
            std::string err;
            if (!ZipExtract(dest, nativesDir, {}, ".dll", true, &err)) {
                task.Log("Не удалось распаковать натив: " + err);
            }
        }
        ++done;
        task.SetProgress(total > 0 ? (float)done / (float)total : 1.0f,
                         "Библиотеки: " + std::to_string(done) + " / " + std::to_string(total));
    }
}

void Minecraft::EnsureAssets(const json& resolved, Task& task) {
    if (!resolved.contains("assetIndex") || !resolved["assetIndex"].is_object()) {
        task.Log("В версии нет индекса ресурсов — пропускаю ассеты");
        return;
    }
    const json& indexInfo = resolved["assetIndex"];
    std::string indexId = indexInfo.value("id", "");
    std::string indexUrl = indexInfo.value("url", "");
    if (indexId.empty()) return;

    fs::path indexDir = AssetsDir() / "indexes";
    fs::path indexFile = indexDir / (indexId + ".json");
    if (!FileExists(indexFile)) {
        task.SetStatus("Индекс ресурсов");
        std::string error;
        if (!DownloadFile(indexUrl, indexFile, nullptr, &task.cancel, 3, &error)) {
            if (task.cancel.load()) throw CancelledError{};
            throw std::runtime_error("Не удалось скачать индекс ресурсов: " + error);
        }
    }

    json index = json::parse(ReadTextFile(indexFile), nullptr, false);
    if (index.is_discarded() || !index.is_object() || !index.contains("objects")) {
        throw std::runtime_error("Повреждён индекс ресурсов");
    }

    const json& objects = index["objects"];
    std::vector<std::pair<std::string, int64_t>> items;
    items.reserve(objects.size());
    for (auto it = objects.begin(); it != objects.end(); ++it) {
        if (!it.value().is_object()) continue;
        std::string hash = it.value().value("hash", "");
        int64_t size = it.value().value("size", (int64_t)0);
        if (hash.size() < 4) continue;
        items.emplace_back(hash, size);
    }
    if (items.empty()) return;

    fs::path objectsDir = AssetsDir() / "objects";
    fs::create_directories(objectsDir);
    std::atomic<size_t> nextIndex{0};
    std::atomic<int> completed{0};
    std::atomic<int> failed{0};
    std::mutex errorMutex;
    std::string firstError;

    auto worker = [&]() {
        for (;;) {
            size_t i = nextIndex.fetch_add(1);
            if (i >= items.size()) break;
            if (task.cancel.load()) break;
            const auto& [hash, size] = items[i];
            fs::path dest = objectsDir / hash.substr(0, 2) / hash;
            std::error_code ec;
            if (FileExists(dest)) {
                if (size <= 0 || (int64_t)fs::file_size(dest, ec) == size) {
                    int done = completed.fetch_add(1) + 1;
                    task.SetProgress((float)done / (float)items.size(),
                                     "Ассеты: " + std::to_string(done) + " / " +
                                         std::to_string(items.size()));
                    continue;
                }
            }
            fs::create_directories(dest.parent_path(), ec);
            std::string url = std::string(kResourcesBase) + hash.substr(0, 2) + "/" + hash;
            std::string error;
            bool ok = DownloadFile(url, dest, nullptr, &task.cancel, 2, &error);
            if (!ok) {
                if (!task.cancel.load()) {
                    failed.fetch_add(1);
                    std::lock_guard lock(errorMutex);
                    if (firstError.empty()) firstError = url + " (" + error + ")";
                }
                if (task.cancel.load()) break;
            }
            int done = completed.fetch_add(1) + 1;
            task.SetProgress((float)done / (float)items.size(),
                             "Ассеты: " + std::to_string(done) + " / " + std::to_string(items.size()));
        }
    };

    int workerCount = (int)std::min<unsigned>(10, std::max(2u, std::thread::hardware_concurrency()));
    std::vector<std::thread> workers;
    for (int i = 0; i < workerCount; ++i) workers.emplace_back(worker);
    for (auto& t : workers) t.join();
    if (task.cancel.load()) throw CancelledError{};
    if (failed.load() > 0) {
        throw std::runtime_error("Не удалось скачать ассеты (" + std::to_string(failed.load()) +
                                 "), например: " + firstError);
    }

    bool virtualAssets = index.value("virtual", false) || index.value("map_to_resources", false);
    if (virtualAssets) {
        std::error_code ec;
        fs::path virtualDir = AssetsDir() / "virtual" / indexId;
        for (auto it = objects.begin(); it != objects.end(); ++it) {
            std::string hash = it.value().value("hash", "");
            if (hash.empty()) continue;
            fs::path source = objectsDir / hash.substr(0, 2) / hash;
            fs::path target = virtualDir / it.key();
            if (!FileExists(source) || FileExists(target)) continue;
            fs::create_directories(target.parent_path(), ec);
            fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
        }
    }
}

void Minecraft::EnsureBaseFiles(const std::string& gameVersion, Task& task) {
    json version = EnsureVersionJson(gameVersion, task);
    fs::path versionDir = VersionsDir() / gameVersion;
    std::error_code ec;
    fs::create_directories(versionDir, ec);

    if (version.contains("downloads") && version["downloads"].contains("client")) {
        const json& client = version["downloads"]["client"];
        std::string url = client.value("url", "");
        int64_t size = client.value("size", (int64_t)0);
        if (!url.empty()) {
            task.SetStatus("Клиент Minecraft " + gameVersion);
            DownloadWithTask(url, versionDir / (gameVersion + ".jar"), task, "Клиент", &size);
        }
    } else {
        throw std::runtime_error("В описании версии нет клиента");
    }

    if (version.contains("logging") && version["logging"].contains("client")) {
        const json& client = version["logging"]["client"];
        if (client.contains("file") && client["file"].is_object()) {
            std::string id = client["file"].value("id", "");
            std::string url = client["file"].value("url", "");
            if (!id.empty() && !url.empty()) {
                fs::path dest = AssetsDir() / "log_configs" / id;
                if (!FileExists(dest)) DownloadWithTask(url, dest, task, "Конфигурация логирования");
            }
        }
    }

    EnsureAssets(version, task);
}

std::string Minecraft::ChooseJava(int requiredMajor, const std::string& configured) {
    if (!configured.empty() && FileExists(configured)) return configured;
    int required = std::max(requiredMajor, 8);

    std::string launcherJava = FindJavaExe(AppDataDir() / "java" / ("temurin-" + std::to_string(required)));
    if (!launcherJava.empty()) return launcherJava;

    auto installs = DetectJava();
    if (installs.empty()) {
        throw std::runtime_error("Java " + std::to_string(required) +
                                 " не найдена. Лаунчер установит её автоматически при следующем запуске");
    }
    const JavaInstall* best = nullptr;
    for (const auto& install : installs) {
        if (install.major >= required && (!best || install.major < best->major)) best = &install;
    }
    if (!best) {
        throw std::runtime_error("Нужна Java " + std::to_string(required) + "+, найдена только " +
                                 std::to_string(installs.back().major) +
                                 ". Лаунчер установит нужную версию автоматически");
    }
    return best->path;
}

int Minecraft::RequiredJavaMajor(const std::string& versionId) {
    try {
        json j = ResolveVersion(versionId, nullptr);
        if (j.contains("javaVersion") && j["javaVersion"].is_object()) {
            return j["javaVersion"].value("majorVersion", 8);
        }
    } catch (...) {
    }
    return 8;
}

bool Minecraft::BaseInstalled(const std::string& versionId) {
    fs::path dir = VersionsDir() / versionId;
    return FileExists(dir / (versionId + ".json")) && FileExists(dir / (versionId + ".jar"));
}

std::string Minecraft::FindLoaderId(const std::string& gameVersion, int loaderKind,
                                    const std::string& loaderVersion) {
    std::error_code ec;
    fs::path root = VersionsDir();
    if (!fs::is_directory(root, ec)) return {};
    std::string best;
    for (const auto& entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (!entry.is_directory(ec)) continue;
        std::string name = entry.path().filename().string();
        if (!FileExists(entry.path() / (name + ".json"))) continue;
        bool matches = false;
        switch (loaderKind) {
            case 1:
                matches = StartsWith(name, "fabric-loader-") && EndsWith(name, "-" + gameVersion);
                break;
            case 2:
                matches = StartsWith(name, "quilt-loader-") && EndsWith(name, "-" + gameVersion);
                break;
            case 3:
                matches = name.find("forge") != std::string::npos &&
                          name.find("neoforge") == std::string::npos &&
                          name.find(gameVersion) != std::string::npos;
                break;
            case 4:
                matches = StartsWith(name, "neoforge-") || name.find("neoforge") != std::string::npos;
                break;
            default:
                break;
        }
        if (!matches) continue;
        if (!loaderVersion.empty() && name.find(loaderVersion) == std::string::npos) continue;
        if (name.size() > best.size()) best = name;
    }
    return best;
}

bool Minecraft::LoaderInstalled(const std::string& gameVersion, int loaderKind) {
    if (loaderKind == 0) return BaseInstalled(gameVersion);
    return !FindLoaderId(gameVersion, loaderKind).empty();
}

std::string Minecraft::EffectiveVersionId(const std::string& gameVersion, int loaderKind) {
    if (loaderKind == 0) return gameVersion;
    std::string id = FindLoaderId(gameVersion, loaderKind);
    return id.empty() ? gameVersion : id;
}

std::vector<LoaderVersion> Minecraft::FetchLoaderVersions(const std::string& gameVersion, int loaderKind,
                                                          std::string& error) {
    error.clear();
    if (loaderKind == 0) return {{gameVersion, true}};
    std::string cacheKey = std::to_string(loaderKind) + "|" + gameVersion;
    {
        std::lock_guard lock(loaderCacheMutex_);
        auto it = loaderCache_.find(cacheKey);
        if (it != loaderCache_.end()) return it->second;
    }

    std::vector<LoaderVersion> result;
    if (loaderKind == 1) {
        HttpResult r = HttpGet("https://meta.fabricmc.net/v2/versions/loader/" + gameVersion,
                               {"Accept: application/json"});
        if (!r.ok()) {
            error = "Fabric API: " + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error);
            return result;
        }
        json j = json::parse(r.body, nullptr, false);
        if (!j.is_array()) {
            error = "Некорректный ответ Fabric API";
            return result;
        }
        for (const auto& item : j) {
            if (!item.contains("loader")) continue;
            LoaderVersion v;
            v.version = item["loader"].value("version", "");
            v.stable = item["loader"].value("stable", true);
            if (!v.version.empty()) result.push_back(v);
        }
    } else if (loaderKind == 2) {
        HttpResult r = HttpGet("https://meta.quiltmc.org/v3/versions/loader/" + gameVersion,
                               {"Accept: application/json"});
        if (!r.ok()) {
            error = "Quilt API: " + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error);
            return result;
        }
        json j = json::parse(r.body, nullptr, false);
        if (!j.is_array()) {
            error = "Некорректный ответ Quilt API";
            return result;
        }
        for (const auto& item : j) {
            if (!item.contains("loader")) continue;
            LoaderVersion v;
            v.version = item["loader"].value("version", "");
            v.stable = VersionIsStable(v.version);
            if (!v.version.empty()) result.push_back(v);
        }
    } else if (loaderKind == 3) {
        HttpResult r = HttpGet("https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml",
                               {}, 30000);
        std::string recommended;
        HttpResult promos = HttpGet(
            "https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json",
            {"Accept: application/json"});
        if (promos.ok()) {
            json p = json::parse(promos.body, nullptr, false);
            if (!p.is_discarded() && p.contains("promos")) {
                recommended = p["promos"].value(gameVersion + "-recommended", "");
            }
        }
        std::string prefix = gameVersion + "-";
        if (r.ok()) {
            std::regex re("<version>([^<]+)</version>");
            for (std::sregex_iterator it(r.body.begin(), r.body.end(), re), end; it != end; ++it) {
                std::string full = (*it)[1].str();
                if (!StartsWith(full, prefix)) continue;
                std::string version = full.substr(prefix.size());
                if (version.find("-mdk") != std::string::npos ||
                    version.find("-userdev") != std::string::npos)
                    continue;
                result.push_back({version, true});
            }
        } else if (promos.ok()) {
            json p = json::parse(promos.body, nullptr, false);
            if (!p.is_discarded() && p.contains("promos")) {
                std::string latest = p["promos"].value(gameVersion + "-latest", "");
                if (!recommended.empty()) result.push_back({recommended, true});
                if (!latest.empty() && latest != recommended) result.push_back({latest, true});
            }
        }
        if (result.empty()) error = "Не удалось получить список версий Forge";
    } else if (loaderKind == 4) {
        HttpResult r = HttpGet(
            "https://maven.neoforged.net/api/maven/versions/releases/net/neoforged/neoforge",
            {"Accept: application/json"});
        if (!r.ok()) {
            error = "NeoForge API: " + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error);
            return result;
        }
        json j = json::parse(r.body, nullptr, false);
        if (j.is_discarded() || !j.contains("versions") || !j["versions"].is_array()) {
            error = "Некорректный ответ NeoForge API";
            return result;
        }
        std::string prefix;
        int maj = 0, min = 0, patch = 0;
        if (sscanf(gameVersion.c_str(), "%d.%d.%d", &maj, &min, &patch) >= 2) {
            if (gameVersion == "1.20.1") {
                prefix = "47.1.";
            } else {
                prefix = std::to_string(maj) + "." + std::to_string(min) + ".";
            }
        }
        for (const auto& v : j["versions"]) {
            if (!v.is_string()) continue;
            std::string version = v.get<std::string>();
            if (!prefix.empty() && !StartsWith(version, prefix)) continue;
            result.push_back({version, VersionIsStable(version)});
        }
        if (result.empty()) error = "Нет сборок NeoForge для " + gameVersion;
    }

    std::sort(result.begin(), result.end(), [](const LoaderVersion& a, const LoaderVersion& b) {
        bool sa = a.stable, sb = b.stable;
        if (sa != sb) return sa;
        return VersionGreater(a.version, b.version);
    });
    {
        std::lock_guard lock(loaderCacheMutex_);
        loaderCache_[cacheKey] = result;
    }
    return result;
}

std::string Minecraft::LatestLoaderVersion(const std::string& gameVersion, int loaderKind) {
    std::string error;
    auto list = FetchLoaderVersions(gameVersion, loaderKind, error);
    for (const auto& v : list) {
        if (v.stable) return v.version;
    }
    return list.empty() ? std::string() : list.front().version;
}

std::string Minecraft::FindGeneratedVersion(const std::string& gameVersion, const std::string& marker,
                                            const std::string& versionHint) {
    std::error_code ec;
    fs::path root = VersionsDir();
    if (!fs::is_directory(root, ec)) return {};
    std::string best;
    std::string fallback;
    for (const auto& entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        std::string name = entry.path().filename().string();
        if (!FileExists(entry.path() / (name + ".json"))) continue;
        if (name.find(marker) == std::string::npos) continue;
        if (marker == "forge" && name.find("neoforge") != std::string::npos) continue;
        if (!versionHint.empty() && name.find(versionHint) != std::string::npos) best = name;
        fallback = name;
    }
    return best.empty() ? fallback : best;
}

bool Minecraft::RunInstaller(const fs::path& jar, const std::string& gameVersion, Task& task) {
    fs::path gameDir = GameDir();
    fs::path profiles = gameDir / "launcher_profiles.json";
    if (!FileExists(profiles)) {
        WriteTextFile(profiles,
                      "{\n  \"profiles\": {},\n  \"settings\": {\"language\": \"ru_ru\", \"showMenu\": false},\n  "
                      "\"version\": 3\n}\n");
    }
    std::string java = ChooseJava(RequiredJavaMajor(gameVersion), cfg_.s.javaPath);
    task.Log("Java: " + java);
    task.Log("Запуск установщика (это может занять несколько минут)...");
    int exitCode = 0;
    ProcessResult result = RunProcess(
        java, {"-jar", jar.string(), "--installClient", gameDir.string()}, &task.cancel,
        [&](const std::string& line) {
            if (line.empty()) return;
            task.Log(line);
            std::string trimmed = Trim(line);
            if (!trimmed.empty() && trimmed.size() < 160) task.SetStatus(trimmed);
        });
    if (!result.started) {
        task.Log(result.output);
        throw std::runtime_error("Не удалось запустить установщик загрузчика");
    }
    exitCode = result.exitCode;
    if (task.cancel.load()) throw CancelledError{};
    if (exitCode != 0) {
        throw std::runtime_error("Установщик завершился с кодом " + std::to_string(exitCode));
    }
    return true;
}

std::string Minecraft::InstallLoader(const std::string& gameVersion, int loaderKind,
                                     const std::string& loaderVersion, Task& task) {
    if (loaderKind == 1 || loaderKind == 2) {
        std::string base = loaderKind == 1 ? "https://meta.fabricmc.net/v2/versions/loader/"
                                           : "https://meta.quiltmc.org/v3/versions/loader/";
        std::string url = base + gameVersion + "/" + loaderVersion + "/profile/json";
        task.SetStatus("Профиль " + std::string(LoaderName(loaderKind)));
        HttpResult r = HttpGet(url, {"Accept: application/json"});
        if (!r.ok()) {
            throw std::runtime_error("Не удалось получить профиль " + std::string(LoaderName(loaderKind)) +
                                     " (HTTP " + std::to_string(r.status) + ")");
        }
        json profile = json::parse(r.body, nullptr, false);
        if (profile.is_discarded() || !profile.is_object()) {
            throw std::runtime_error("Некорректный профиль загрузчика");
        }
        std::string id = profile.value("id", "");
        if (id.empty()) throw std::runtime_error("В профиле загрузчика нет идентификатора");
        fs::path path = VersionsDir() / id / (id + ".json");
        WriteTextFile(path, profile.dump(2));
        task.Log("Установлен профиль " + id);
        return id;
    }

    if (loaderKind == 3) {
        std::string existing = FindLoaderId(gameVersion, 3, loaderVersion);
        if (!existing.empty()) {
            task.Log("Forge уже установлен: " + existing);
            return existing;
        }
        std::string fullVersion = gameVersion + "-" + loaderVersion;
        std::string url = "https://maven.minecraftforge.net/net/minecraftforge/forge/" + fullVersion +
                          "/forge-" + fullVersion + "-installer.jar";
        fs::path jar = cfg_.dataDir / "cache" / "installers" / ("forge-" + fullVersion + "-installer.jar");
        task.SetStatus("Скачивание установщика Forge");
        DownloadWithTask(url, jar, task, "Установщик Forge");
        RunInstaller(jar, gameVersion, task);
        std::string id = FindGeneratedVersion(gameVersion, "forge", loaderVersion);
        if (id.empty()) throw std::runtime_error("Forge установлен, но профиль версии не найден");
        task.Log("Установлен Forge: " + id);
        return id;
    }

    if (loaderKind == 4) {
        std::string existing = FindLoaderId(gameVersion, 4, loaderVersion);
        if (!existing.empty()) {
            task.Log("NeoForge уже установлен: " + existing);
            return existing;
        }
        std::string url = "https://maven.neoforged.net/releases/net/neoforged/neoforge/" + loaderVersion +
                          "/neoforge-" + loaderVersion + "-installer.jar";
        fs::path jar = cfg_.dataDir / "cache" / "installers" / ("neoforge-" + loaderVersion + "-installer.jar");
        task.SetStatus("Скачивание установщика NeoForge");
        DownloadWithTask(url, jar, task, "Установщик NeoForge");
        RunInstaller(jar, gameVersion, task);
        std::string id = FindGeneratedVersion(gameVersion, "neoforge", loaderVersion);
        if (id.empty()) throw std::runtime_error("NeoForge установлен, но профиль версии не найден");
        task.Log("Установлен NeoForge: " + id);
        return id;
    }

    return gameVersion;
}

std::shared_ptr<Task> Minecraft::Install(const std::string& gameVersion, int loaderKind,
                                         std::string loaderVersion, bool playAfter) {
    std::string title = std::string(loaderKind == 0 ? "Установка Minecraft " : "Установка ") +
                        (loaderKind == 0 ? gameVersion : (std::string(LoaderName(loaderKind)) + " " + gameVersion));
    return tasks_.Start(title, [this, gameVersion, loaderKind, loaderVersion, playAfter](Task& task) {
        try {
            EnsureBaseFiles(gameVersion, task);
            std::string launchId = gameVersion;
            if (loaderKind != 0) {
                std::string version = loaderVersion;
                if (version.empty()) {
                    task.SetStatus("Поиск версии " + std::string(LoaderName(loaderKind)));
                    version = LatestLoaderVersion(gameVersion, loaderKind);
                }
                if (version.empty()) {
                    throw std::runtime_error("Не удалось найти версию " + std::string(LoaderName(loaderKind)) +
                                             " для " + gameVersion);
                }
                task.Log(std::string("Загрузчик: ") + LoaderName(loaderKind) + " " + version);
                cfg_.s.loaderVersion = version;
                launchId = InstallLoader(gameVersion, loaderKind, version, task);
            }
            if (task.cancel.load()) throw CancelledError{};
            json resolved = ResolveVersion(launchId, nullptr);
            EnsureLibraries(launchId, task, resolved);
            if (task.cancel.load()) throw CancelledError{};

            cfg_.s.selectedVersion = gameVersion;
            cfg_.s.loaderKind = loaderKind;
            if (loaderKind == 0) cfg_.s.loaderVersion.clear();
            cfg_.Save();

            if (playAfter) {
                const Account* account = cfg_.Active();
                if (!account) throw std::runtime_error("Сначала создайте аккаунт");
                std::string error;
                task.SetStatus("Запуск игры...");
                task.Log("Запуск " + launchId);
                if (!Launch(launchId, *account, cfg_.s, error)) throw std::runtime_error(error);
            }
            task.Succeed("Готово");
        } catch (const CancelledError&) {
            task.state.store(static_cast<int>(TaskState::Cancelled));
            task.SetStatus("Отменено");
        } catch (const std::exception& e) {
            task.Fail(e.what());
        }
    });
}

bool Minecraft::Launch(const std::string& versionId, const Account& account, const Settings& settings,
                       std::string& error, std::function<void(int)> onExit) {
    try {
        if (account.type == "msa" && account.accessToken.empty()) {
            error = "Токен Microsoft-аккаунта отсутствует — войдите заново";
            return false;
        }

        std::string baseId;
        json resolved = ResolveVersion(versionId, &baseId);
        int requiredJava = 8;
        if (resolved.contains("javaVersion") && resolved["javaVersion"].is_object()) {
            requiredJava = resolved["javaVersion"].value("majorVersion", 8);
        }
        std::string java = ChooseJava(requiredJava, settings.javaPath);
        fs::path gameDir = GameDir();
        fs::create_directories(gameDir);
        fs::create_directories(ModsDir());

        auto features = DefaultFeatures(settings);
        auto refs = CollectLibraries(resolved, features);
        fs::path librariesDir = LibrariesDir();
        std::vector<std::string> classpath;
        std::map<std::string, int> classpathIndex;
        for (const auto& ref : refs) {
            if (ref.native) continue;
            fs::path path = librariesDir / ref.path;
            if (!FileExists(path)) {
                error = "Не хватает библиотеки " + ref.path + ". Переустановите версию.";
                return false;
            }
            std::string key = LibKey(ref.name);
            auto it = classpathIndex.find(key);
            if (it != classpathIndex.end()) {
                classpath[it->second] = path.string();
            } else {
                classpathIndex[key] = (int)classpath.size();
                classpath.push_back(path.string());
            }
        }

        std::string jarId = resolved.value("jar", baseId);
        fs::path clientJar = VersionsDir() / jarId / (jarId + ".jar");
        if (!FileExists(clientJar)) {
            error = "Клиент версии не установлен. Нажмите «Установить».";
            return false;
        }
        classpath.push_back(clientJar.string());

        fs::path nativesDir = VersionsDir() / versionId / "natives";
        fs::path nativesJavaDir = nativesDir / "java";
        {
            std::error_code ec;
            fs::create_directories(nativesJavaDir, ec);
        }
        auto hasDllIn = [](const fs::path& dir) {
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(dir, ec)) {
                if (ec) break;
                if (entry.path().extension() == ".dll") return true;
            }
            return false;
        };
        bool rootHasDll = hasDllIn(nativesDir);
        bool javaHasDll = hasDllIn(nativesJavaDir);
        if (!rootHasDll || !javaHasDll) {
            // Современные версии (LWJGL 3.4) ищут нативы в natives/java, старые — в корне natives.
            for (const auto& ref : refs) {
                if (!ref.native) continue;
                fs::path nativeJar = librariesDir / ref.path;
                if (!FileExists(nativeJar)) continue;
                std::string err;
                if (!rootHasDll && !ZipExtract(nativeJar, nativesDir, {}, ".dll", true, &err)) {
                    LogLine("Натив " + ref.path + ": " + err);
                }
                if (!javaHasDll &&
                    !ZipExtract(nativeJar, nativesJavaDir, {}, ".dll", true, &err)) {
                    LogLine("Натив (java) " + ref.path + ": " + err);
                }
            }
        }

        std::string assetIndexId = "legacy";
        if (resolved.contains("assetIndex") && resolved["assetIndex"].is_object()) {
            assetIndexId = resolved["assetIndex"].value("id", "legacy");
        } else if (resolved.contains("assets")) {
            assetIndexId = resolved.value("assets", "legacy");
        }
        fs::path assetsRoot = AssetsDir();
        fs::path indexFile = assetsRoot / "indexes" / (assetIndexId + ".json");
        if (!FileExists(indexFile) && assetIndexId != "legacy") {
            error = "Ресурсы версии не установлены. Нажмите «Установить».";
            return false;
        }

        fs::path virtualAssets = assetsRoot / "virtual" / assetIndexId;
        bool useVirtual = DirectoryExists(virtualAssets);
        std::string classpathStr;
        for (size_t i = 0; i < classpath.size(); ++i) {
            if (i) classpathStr.push_back(';');
            classpathStr += classpath[i];
        }

        std::map<std::string, std::string> vars;
        vars["auth_player_name"] = account.name;
        vars["version_name"] = versionId;
        vars["game_directory"] = gameDir.string();
        vars["assets_root"] = assetsRoot.string();
        vars["assets_index_name"] = assetIndexId;
        vars["auth_uuid"] = MinecraftUuid(account.uuid);
        vars["auth_access_token"] = account.type == "msa" ? account.accessToken : "0";
        vars["user_type"] = account.type == "msa" ? "msa" : "legacy";
        vars["clientid"] = "";
        vars["auth_xuid"] = "";
        vars["user_properties"] = "{}";
        vars["version_type"] = resolved.value("type", "release");
        vars["natives_directory"] = nativesDir.string();
        vars["launcher_name"] = "XpLauncher";
        vars["launcher_version"] = "1.0.0";
        vars["classpath"] = classpathStr;
        vars["classpath_separator"] = ";";
        vars["library_directory"] = librariesDir.string();
        vars["resolution_width"] = std::to_string(settings.winW);
        vars["resolution_height"] = std::to_string(settings.winH);
        vars["game_assets"] = useVirtual ? virtualAssets.string() : assetsRoot.string();
        vars["quickPlayPath"] = "";
        vars["quickPlaySingleplayer"] = "";
        vars["quickPlayMultiplayer"] = "";
        vars["quickPlayRealms"] = "";

        auto substitute = [&](std::string text) {
            for (const auto& [key, value] : vars) text = ReplaceAll(text, "${" + key + "}", value);
            return text;
        };

        std::vector<std::string> jvmArgs;
        jvmArgs.push_back("-Xmx" + std::to_string(settings.ramMb) + "M");
        jvmArgs.push_back("-Xms" + std::to_string(std::min(settings.ramMb / 2, 1024)) + "M");
        jvmArgs.push_back("-Dfile.encoding=UTF-8");

        if (resolved.contains("logging") && resolved["logging"].contains("client") &&
            resolved["logging"]["client"].is_object()) {
            const json& clientLog = resolved["logging"]["client"];
            if (clientLog.contains("file") && clientLog["file"].is_object()) {
                std::string id = clientLog["file"].value("id", "");
                std::string argument = clientLog.value("argument", "");
                fs::path cfgPath = assetsRoot / "log_configs" / id;
                if (!id.empty() && !argument.empty() && FileExists(cfgPath)) {
                    jvmArgs.push_back(ReplaceAll(argument, "${path}", cfgPath.string()));
                }
            }
        }

        std::vector<std::string> gameArgs;
        auto appendArgs = [&](const json& list, std::vector<std::string>& target) {
            if (!list.is_array()) return;
            for (const auto& entry : list) {
                if (entry.is_string()) {
                    std::string value = entry.get<std::string>();
                    value = substitute(value);
                    if (!value.empty()) target.push_back(value);
                } else if (entry.is_object()) {
                    if (entry.contains("rules") && !RulesAllow(entry["rules"], features)) continue;
                    if (entry.contains("value")) {
                        const json& value = entry["value"];
                        if (value.is_string()) {
                            target.push_back(substitute(value.get<std::string>()));
                        } else if (value.is_array()) {
                            for (const auto& v : value) {
                                if (v.is_string()) target.push_back(substitute(v.get<std::string>()));
                            }
                        }
                    }
                }
            }
        };

        if (resolved.contains("arguments") && resolved["arguments"].is_object()) {
            if (resolved["arguments"].contains("jvm"))
                appendArgs(resolved["arguments"]["jvm"], jvmArgs);
            if (resolved["arguments"].contains("game"))
                appendArgs(resolved["arguments"]["game"], gameArgs);
        } else if (resolved.contains("minecraftArguments")) {
            for (auto& arg : SplitArgs(resolved.value("minecraftArguments", ""))) {
                gameArgs.push_back(substitute(arg));
            }
        }
        // Перекрываем значение из описания версии: каталоги с распакованными нативами существуют.
        jvmArgs.push_back("-Djava.library.path=" + nativesJavaDir.string() + ";" + nativesDir.string());

        if (!settings.jvmArgs.empty()) {
            for (auto& arg : SplitArgs(settings.jvmArgs)) jvmArgs.push_back(substitute(arg));
        }
        if (settings.fullscreen) gameArgs.push_back("--fullscreen");

        std::string mainClass = resolved.value("mainClass", "");
        if (mainClass.empty()) {
            error = "В версии не указан главный класс";
            return false;
        }

        std::vector<std::string> args = jvmArgs;
        args.push_back("-cp");
        args.push_back(classpathStr);
        args.push_back(mainClass);
        for (const auto& arg : gameArgs) args.push_back(arg);

        std::wstring commandLine = QuoteArgW(java);
        for (const auto& arg : args) {
            commandLine.push_back(L' ');
            commandLine += QuoteArgW(arg);
        }

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::wstring javaW = Wide(java);
        std::wstring cwdW = gameDir.wstring();
        if (!CreateProcessW(javaW.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, cwdW.c_str(), &si, &pi)) {
            error = "Не удалось запустить Minecraft (код " + std::to_string(GetLastError()) + ")";
            return false;
        }
        CloseHandle(pi.hThread);

        LogLine("Запущена игра: " + versionId + " (PID " + std::to_string(pi.dwProcessId) + ")");
        std::thread([process = pi.hProcess, onExit = std::move(onExit)]() {
            WaitForSingleObject(process, INFINITE);
            DWORD code = 0;
            GetExitCodeProcess(process, &code);
            CloseHandle(process);
            LogLine("Игра завершилась с кодом " + std::to_string(code));
            if (onExit) onExit((int)code);
        }).detach();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}
