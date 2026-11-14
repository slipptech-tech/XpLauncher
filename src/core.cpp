#include "core.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <bcrypt.h>
#include <d3d11.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

#include "miniz.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

// ------------------------------------------------------------------ text
std::string Utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), len);
    return out;
}

std::string UrlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)::tolower(c); });
    return s;
}

bool StartsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool EndsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// ------------------------------------------------------------------ http
namespace {

struct WinHttpHandle {
    HINTERNET h = nullptr;
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET handle) : h(handle) {}
    ~WinHttpHandle() {
        if (h) WinHttpCloseHandle(h);
    }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    WinHttpHandle(WinHttpHandle&& other) noexcept : h(other.h) { other.h = nullptr; }
    WinHttpHandle& operator=(WinHttpHandle&& other) noexcept {
        if (this != &other) {
            if (h) WinHttpCloseHandle(h);
            h = other.h;
            other.h = nullptr;
        }
        return *this;
    }
    operator HINTERNET() const { return h; }
    bool valid() const { return h != nullptr; }
};

std::wstring HttpErrorMessage(DWORD code) {
    wchar_t* buffer = nullptr;
    DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&buffer, 0, nullptr);
    std::wstring result;
    if (len && buffer) result.assign(buffer, len);
    if (buffer) LocalFree(buffer);
    if (!result.empty()) result = Wide(Trim(Utf8(result)));
    return result;
}

struct UrlParts {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = 0;
    bool secure = false;
    bool valid = false;
};

UrlParts ParseUrl(const std::string& url) {
    UrlParts parts;
    std::wstring wurl = Wide(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwHostNameLength = (DWORD)-1;
    uc.dwUrlPathLength = (DWORD)-1;
    uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) return parts;
    parts.host.assign(uc.lpszHostName, uc.dwHostNameLength);
    parts.path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.dwExtraInfoLength > 0) parts.path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    parts.port = uc.nPort;
    parts.secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
    parts.valid = true;
    return parts;
}

const wchar_t* UserAgent() {
    return L"XpLauncher/1.0.0 (Windows NT 10.0; Win64; x64)";
}

HttpResult HttpRequest(const std::wstring& method, const std::string& url, const std::string& body,
                       const std::vector<std::string>& headers, int timeoutMs) {
    HttpResult result;
    UrlParts parts = ParseUrl(url);
    if (!parts.valid) {
        result.error = "Некорректный URL";
        return result;
    }

    WinHttpHandle session(WinHttpOpen(UserAgent(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.valid()) {
        session = WinHttpHandle(WinHttpOpen(UserAgent(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    }
    if (!session.valid()) {
        result.error = "WinHttpOpen: " + Utf8(HttpErrorMessage(GetLastError()));
        return result;
    }
    WinHttpSetTimeouts(session, 15000, 15000, 30000, timeoutMs);

    WinHttpHandle connection(WinHttpConnect(session, parts.host.c_str(), parts.port, 0));
    if (!connection.valid()) {
        result.error = "Нет соединения: " + Utf8(HttpErrorMessage(GetLastError()));
        return result;
    }

    DWORD flags = parts.secure ? WINHTTP_FLAG_SECURE : 0;
    WinHttpHandle request(WinHttpOpenRequest(connection, method.c_str(), parts.path.c_str(), nullptr,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request.valid()) {
        result.error = "WinHttpOpenRequest: " + Utf8(HttpErrorMessage(GetLastError()));
        return result;
    }

    for (const auto& h : headers) {
        std::wstring wh = Wide(h);
        WinHttpAddRequestHeaders(request, wh.c_str(), (DWORD)-1,
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    const void* bodyPtr = body.empty() ? WINHTTP_NO_REQUEST_DATA : body.data();
    DWORD bodyLen = (DWORD)body.size();
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)bodyPtr, bodyLen,
                            bodyLen, 0)) {
        result.error = "Ошибка отправки: " + Utf8(HttpErrorMessage(GetLastError()));
        return result;
    }
    if (!WinHttpReceiveResponse(request, nullptr)) {
        result.error = "Ошибка ответа: " + Utf8(HttpErrorMessage(GetLastError()));
        return result;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
    result.status = (long)status;

    DWORD available = 0;
    do {
        available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) break;
        if (available == 0) break;
        std::string chunk;
        chunk.resize(available);
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &read)) break;
        chunk.resize(read);
        result.body += chunk;
    } while (available > 0);

    if (result.status == 0) {
        result.error = "Пустой ответ сервера";
    }
    return result;
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

std::string ReadAllBytesFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

HttpResult HttpGet(const std::string& url, const std::vector<std::string>& headers, int timeoutMs) {
    return HttpRequest(L"GET", url, "", headers, timeoutMs);
}

HttpResult HttpPost(const std::string& url, const std::string& body,
                    const std::vector<std::string>& headers, int timeoutMs) {
    return HttpRequest(L"POST", url, body, headers, timeoutMs);
}

HttpResult HttpDelete(const std::string& url, const std::vector<std::string>& headers, int timeoutMs) {
    return HttpRequest(L"DELETE", url, "", headers, timeoutMs);
}

bool DownloadFile(const std::string& url, const fs::path& dest, const ProgressFn& progress,
                  std::atomic<bool>* cancel, int retries, std::string* error,
                  const std::vector<std::string>& headers) {
    std::error_code ec;
    if (dest.has_parent_path()) fs::create_directories(dest.parent_path(), ec);

    for (int attempt = 0; attempt < std::max(1, retries); ++attempt) {
        if (cancel && cancel->load()) {
            if (error) *error = "Отменено";
            return false;
        }
        if (retries > 1 && attempt > 0) Sleep(400 * attempt);

        UrlParts parts = ParseUrl(url);
        if (!parts.valid) {
            if (error) *error = "Некорректный URL";
            return false;
        }

        WinHttpHandle session(WinHttpOpen(UserAgent(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session.valid()) {
            session = WinHttpHandle(WinHttpOpen(UserAgent(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        }
        if (!session.valid()) {
            if (error) *error = "WinHttpOpen: " + Utf8(HttpErrorMessage(GetLastError()));
            continue;
        }
        WinHttpSetTimeouts(session, 15000, 15000, 30000, 60000);

        WinHttpHandle connection(WinHttpConnect(session, parts.host.c_str(), parts.port, 0));
        if (!connection.valid()) {
            if (error) *error = "Нет соединения: " + Utf8(HttpErrorMessage(GetLastError()));
            continue;
        }
        DWORD flags = parts.secure ? WINHTTP_FLAG_SECURE : 0;
        WinHttpHandle request(WinHttpOpenRequest(connection, L"GET", parts.path.c_str(), nullptr,
                                                 WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
        if (!request.valid()) {
            if (error) *error = "WinHttpOpenRequest: " + Utf8(HttpErrorMessage(GetLastError()));
            continue;
        }
        for (const auto& h : headers) {
            std::wstring wh = Wide(h);
            WinHttpAddRequestHeaders(request, wh.c_str(), (DWORD)-1,
                                     WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        }

        if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request, nullptr)) {
            if (error) *error = "Ошибка загрузки: " + Utf8(HttpErrorMessage(GetLastError()));
            continue;
        }

        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
        if (status != 200) {
            if (error) *error = "HTTP " + std::to_string(status);
            if (status == 404 || status == 403) return false;
            continue;
        }

        int64_t total = 0;
        DWORD len = 0;
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &len, (LPDWORD)&statusSize,
                                WINHTTP_NO_HEADER_INDEX)) {
            total = (int64_t)len;
        }

        fs::path partPath = dest;
        partPath += L".part";
        fs::remove(partPath, ec);

        std::ofstream out(partPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "Не удалось создать файл";
            return false;
        }

        if (progress) progress(0, total);

        int64_t received = 0;
        bool failed = false;
        std::string failError;
        for (;;) {
            if (cancel && cancel->load()) {
                failed = true;
                failError = "Отменено";
                break;
            }
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available)) {
                failed = true;
                failError = "Соединение прервано: " + Utf8(HttpErrorMessage(GetLastError()));
                break;
            }
            if (available == 0) break;
            std::string buffer;
            buffer.resize(available);
            DWORD read = 0;
            if (!WinHttpReadData(request, buffer.data(), available, &read)) {
                failed = true;
                failError = "Ошибка чтения: " + Utf8(HttpErrorMessage(GetLastError()));
                break;
            }
            if (read == 0) break;
            out.write(buffer.data(), read);
            received += read;
            if (progress) progress(received, total);
        }
        out.close();

        if (!failed && total > 0 && received != total) {
            failed = true;
            failError = "Загрузка неполная";
        }
        if (failed) {
            fs::remove(partPath, ec);
            if (error) *error = failError;
            if (cancel && cancel->load()) return false;
            continue;
        }

        fs::remove(dest, ec);
        fs::rename(partPath, dest, ec);
        if (ec) {
            if (error) *error = "Не удалось сохранить файл: " + ec.message();
            return false;
        }
        return true;
    }

    if (error && error->empty()) *error = "Не удалось скачать файл";
    return false;
}

// ------------------------------------------------------------------ files
namespace {
std::wstring g_dataDirOverride;
}

void SetDataDirOverride(const std::wstring& path) {
    g_dataDirOverride = path;
}

fs::path AppDataDir() {
    std::error_code ec;
    if (!g_dataDirOverride.empty()) {
        fs::path dir(g_dataDirOverride);
        fs::create_directories(dir, ec);
        return dir;
    }
    static fs::path cached;
    if (!cached.empty()) return cached;
    PWSTR raw = nullptr;
    fs::path base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw))) {
        base = raw;
        CoTaskMemFree(raw);
    } else {
        const char* appdata = getenv("APPDATA");
        base = appdata ? fs::path(appdata) : fs::current_path();
    }
    cached = base / "XpLauncher";
    fs::create_directories(cached, ec);
    return cached;
}

std::string ReadTextFile(const fs::path& path) {
    return ReadAllBytesFile(path);
}

std::string ReadBinaryFile(const fs::path& path) {
    return ReadAllBytesFile(path);
}

bool WriteTextFile(const fs::path& path, const std::string& text) {
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(text.data(), (std::streamsize)text.size());
    return (bool)out;
}

bool WriteBinaryFile(const fs::path& path, const std::string& bytes) {
    return WriteTextFile(path, bytes);
}

bool FileExists(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

bool DirectoryExists(const fs::path& path) {
    std::error_code ec;
    return fs::is_directory(path, ec);
}

// ------------------------------------------------------------------ zip
namespace {

struct ZipArchive {
    mz_zip_archive zip{};
    std::string data;
    bool open = false;
    ~ZipArchive() {
        if (open) mz_zip_reader_end(&zip);
    }
};

bool OpenZip(const fs::path& zipPath, ZipArchive& archive, std::string* error) {
    archive.data = ReadAllBytesFile(zipPath);
    if (archive.data.empty()) {
        if (error) *error = "Не удалось прочитать архив";
        return false;
    }
    if (!mz_zip_reader_init_mem(&archive.zip, archive.data.data(), archive.data.size(), 0)) {
        if (error) *error = "Повреждённый ZIP-архив";
        return false;
    }
    archive.open = true;
    return true;
}

} // namespace

bool ZipList(const fs::path& zipPath, std::vector<std::string>& names, std::string* error) {
    ZipArchive archive;
    if (!OpenZip(zipPath, archive, error)) return false;
    mz_uint count = mz_zip_reader_get_num_files(&archive.zip);
    names.clear();
    names.reserve(count);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&archive.zip, i, &stat)) continue;
        if (!stat.m_is_directory) names.emplace_back(stat.m_filename);
    }
    return true;
}

bool ZipReadFile(const fs::path& zipPath, const std::string& entry, std::string& out) {
    ZipArchive archive;
    if (!OpenZip(zipPath, archive, nullptr)) return false;
    int index = mz_zip_reader_locate_file(&archive.zip, entry.c_str(), nullptr, 0);
    if (index < 0) {
        std::string lower = ToLower(entry);
        mz_uint count = mz_zip_reader_get_num_files(&archive.zip);
        for (mz_uint i = 0; i < count; ++i) {
            mz_zip_archive_file_stat stat{};
            if (!mz_zip_reader_file_stat(&archive.zip, i, &stat)) continue;
            if (ToLower(stat.m_filename) == lower) {
                index = (int)i;
                break;
            }
        }
    }
    if (index < 0) return false;
    mz_zip_archive_file_stat stat{};
    if (!mz_zip_reader_file_stat(&archive.zip, (mz_uint)index, &stat)) return false;
    out.resize((size_t)stat.m_uncomp_size);
    if (out.empty()) return true;
    return mz_zip_reader_extract_to_mem(&archive.zip, (mz_uint)index, out.data(), out.size(), 0) != 0;
}

bool ZipExtract(const fs::path& zipPath, const fs::path& destDir,
                const std::function<bool(const std::string&)>& filter, const std::string& onlyExtension,
                bool flatten, std::string* error) {
    ZipArchive archive;
    if (!OpenZip(zipPath, archive, error)) return false;
    std::string ext = ToLower(onlyExtension);
    std::error_code ec;
    fs::create_directories(destDir, ec);

    mz_uint count = mz_zip_reader_get_num_files(&archive.zip);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&archive.zip, i, &stat)) continue;
        if (stat.m_is_directory) continue;
        std::string name = stat.m_filename;
        if (!ext.empty() && !EndsWith(ToLower(name), ext)) continue;
        if (filter && !filter(name)) continue;

        fs::path relative = flatten ? fs::path(name).filename() : fs::path(name).lexically_normal();
        for (const auto& part : relative) {
            if (part == L"..") {
                relative.clear();
                break;
            }
        }
        if (relative.empty()) continue;

        fs::path target = destDir / relative;
        fs::create_directories(target.parent_path(), ec);
        std::vector<char> buffer((size_t)stat.m_uncomp_size);
        if (buffer.empty()) {
            std::ofstream empty(target, std::ios::binary);
            continue;
        }
        if (!mz_zip_reader_extract_to_mem(&archive.zip, i, buffer.data(), buffer.size(), 0)) {
            if (error) *error = "Не удалось распаковать " + name;
            return false;
        }
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "Не удалось записать " + Utf8(target.wstring());
            return false;
        }
        out.write(buffer.data(), (std::streamsize)buffer.size());
    }
    return true;
}

// ------------------------------------------------------------------ logging
namespace {
std::mutex g_logMutex;
fs::path g_logPath;
} // namespace

void LogInit(const fs::path& file) {
    std::lock_guard lock(g_logMutex);
    g_logPath = file;
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
}

void LogLine(const std::string& line) {
    std::lock_guard lock(g_logMutex);
    std::string stamped = "[" + Timestamp() + "] " + line + "\n";
    OutputDebugStringA(stamped.c_str());
    if (!g_logPath.empty()) {
        std::ofstream out(g_logPath, std::ios::app | std::ios::binary);
        if (out) out << stamped;
    }
}

fs::path LogPath() {
    std::lock_guard lock(g_logMutex);
    return g_logPath;
}

// ------------------------------------------------------------------ process
ProcessResult RunProcess(const std::string& exe, const std::vector<std::string>& args,
                         std::atomic<bool>* cancel,
                         const std::function<void(const std::string&)>& onLine, int timeoutMs) {
    ProcessResult result;
    std::wstring commandLine = QuoteArgW(exe);
    for (const auto& a : args) {
        commandLine.push_back(L' ');
        commandLine += QuoteArgW(a);
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        result.output = "CreatePipe failed";
        return result;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = nullptr;

    PROCESS_INFORMATION pi{};
    std::wstring exeW = Wide(exe);
    BOOL ok = CreateProcessW(exeW.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &si, &pi);
    CloseHandle(writePipe);
    if (!ok) {
        CloseHandle(readPipe);
        result.output = "Не удалось запустить процесс: " + Utf8(HttpErrorMessage(GetLastError()));
        return result;
    }

    result.started = true;
    std::string pending;
    auto pump = [&]() -> bool {
        DWORD available = 0;
        if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr)) return false;
        if (available == 0) return true;
        std::string buffer(available, '\0');
        DWORD read = 0;
        if (!ReadFile(readPipe, buffer.data(), available, &read, nullptr)) return false;
        buffer.resize(read);
        result.output += buffer;
        pending += buffer;
        size_t pos;
        while ((pos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            pending.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (onLine) onLine(line);
        }
        return true;
    };

    bool cancelled = false;
    bool timedOut = false;
    ULONGLONG startTick = GetTickCount64();
    for (;;) {
        if (cancel && cancel->load() && !cancelled) {
            TerminateProcess(pi.hProcess, 1);
            cancelled = true;
        }
        if (!timedOut && timeoutMs > 0 &&
            GetTickCount64() - startTick > (ULONGLONG)timeoutMs) {
            TerminateProcess(pi.hProcess, 1);
            timedOut = true;
        }
        if (!pump()) break;
        DWORD wait = WaitForSingleObject(pi.hProcess, 25);
        if (wait == WAIT_OBJECT_0) {
            for (int i = 0; i < 8; ++i) {
                if (!pump()) break;
                Sleep(10);
            }
            break;
        }
    }
    if (!pending.empty() && onLine) onLine(pending);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    result.exitCode = (int)exitCode;
    if (cancelled) result.output += "\n[Отменено]";
    if (timedOut) result.output += "\n[Таймаут]";
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(readPipe);
    return result;
}

// ------------------------------------------------------------------ tasks
void Task::SetStatus(const std::string& text) {
    std::lock_guard lock(mutex_);
    status_ = text;
}

void Task::SetProgress(float value, const std::string& status) {
    progress.store(std::clamp(value, 0.0f, 1.0f));
    if (!status.empty()) SetStatus(status);
}

void Task::Log(const std::string& line) {
    LogLine("[" + title + "] " + line);
    std::lock_guard lock(mutex_);
    log_.push_back(line);
    if (log_.size() > 200) log_.erase(log_.begin(), log_.begin() + 100);
}

void Task::Succeed(const std::string& status) {
    if (!status.empty()) SetStatus(status);
    state.store(static_cast<int>(TaskState::Done));
    progress.store(1.0f);
}

void Task::Fail(const std::string& error) {
    {
        std::lock_guard lock(mutex_);
        error_ = error;
        status_ = error;
        log_.push_back("[Ошибка] " + error);
    }
    LogLine("[FAIL] " + title + ": " + error);
    state.store(static_cast<int>(TaskState::Failed));
}

std::string Task::Status() {
    std::lock_guard lock(mutex_);
    return status_;
}

std::string Task::Error() {
    std::lock_guard lock(mutex_);
    return error_;
}

std::vector<std::string> Task::Logs() {
    std::lock_guard lock(mutex_);
    return log_;
}

std::shared_ptr<Task> TaskManager::Start(const std::string& title, std::function<void(Task&)> fn) {
    auto task = std::make_shared<Task>();
    {
        std::lock_guard lock(mutex_);
        task->id = nextId_++;
        task->title = title;
        tasks_.push_back(task);
    }
    LogLine("Задача #" + std::to_string(task->id) + ": " + title);
    std::thread([task, fn = std::move(fn)]() {
        try {
            fn(*task);
        } catch (const std::exception& e) {
            if (task->IsRunning()) task->Fail(e.what());
        } catch (...) {
            if (task->IsRunning()) task->Fail("Неизвестная ошибка");
        }
        if (task->state.load() == static_cast<int>(TaskState::Running)) {
            if (task->cancel.load()) {
                task->state.store(static_cast<int>(TaskState::Cancelled));
                task->SetStatus("Отменено");
            } else {
                task->Succeed();
            }
        }
    }).detach();
    return task;
}

std::vector<std::shared_ptr<Task>> TaskManager::Snapshot() {
    std::lock_guard lock(mutex_);
    return tasks_;
}

int TaskManager::ActiveCount() {
    std::lock_guard lock(mutex_);
    int count = 0;
    for (auto& t : tasks_) {
        if (t->IsRunning()) ++count;
    }
    return count;
}

void TaskManager::ClearFinished() {
    std::lock_guard lock(mutex_);
    tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(),
                                [](const std::shared_ptr<Task>& t) { return !t->IsRunning(); }),
                 tasks_.end());
}

bool TaskManager::HasFinished() {
    std::lock_guard lock(mutex_);
    for (auto& t : tasks_) {
        if (!t->IsRunning()) return true;
    }
    return false;
}

// ------------------------------------------------------------------ config
void Config::Load() {
    dataDir = AppDataDir();
    fs::path path = dataDir / "config.json";
    if (!FileExists(path)) {
        Save();
        return;
    }
    json j = json::parse(ReadTextFile(path), nullptr, false);
    if (j.is_discarded() || !j.is_object()) return;

    if (j.contains("settings") && j["settings"].is_object()) {
        const json& js = j["settings"];
        auto getStr = [&](const char* key, std::string& target) {
            if (js.contains(key) && js[key].is_string()) target = js[key].get<std::string>();
        };
        auto getBool = [&](const char* key, bool& target) {
            if (js.contains(key) && js[key].is_boolean()) target = js[key].get<bool>();
        };
        auto getInt = [&](const char* key, int& target) {
            if (js.contains(key) && js[key].is_number_integer()) target = js[key].get<int>();
        };
        getInt("ramMb", s.ramMb);
        getStr("javaPath", s.javaPath);
        getStr("gameDir", s.gameDir);
        getStr("jvmArgs", s.jvmArgs);
        getInt("winW", s.winW);
        getInt("winH", s.winH);
        getBool("fullscreen", s.fullscreen);
        getBool("closeOnLaunch", s.closeOnLaunch);
        getStr("accent", s.accent);
        getStr("cfApiKey", s.cfApiKey);
        getStr("msaClientId", s.msaClientId);
        getBool("showSnapshots", s.showSnapshots);
        getStr("selectedVersion", s.selectedVersion);
        getInt("loaderKind", s.loaderKind);
        getStr("loaderVersion", s.loaderVersion);
        getStr("lastLaunch", s.lastLaunch);
        getStr("lastVersion", s.lastVersion);
        getInt("launchCount", s.launchCount);
        getBool("animations", s.animations);
        getBool("restoreAfterExit", s.restoreAfterExit);
        getBool("launchToLastPage", s.launchToLastPage);
        if (js.contains("playSeconds") && js["playSeconds"].is_number_integer()) {
            s.playSeconds = js["playSeconds"].get<long long>();
        }
        if (js.contains("recentVersions") && js["recentVersions"].is_array()) {
            for (const auto& v : js["recentVersions"]) {
                if (v.is_string()) s.recentVersions.push_back(v.get<std::string>());
            }
        }
        getStr("serverVersion", s.serverVersion);
        getInt("serverPort", s.serverPort);
        getInt("serverMaxPlayers", s.serverMaxPlayers);
        getStr("serverMotd", s.serverMotd);
        getInt("serverGamemode", s.serverGamemode);
        getInt("serverDifficulty", s.serverDifficulty);
        getBool("serverOnlineMode", s.serverOnlineMode);
        getInt("lastPage", s.lastPage);
        getBool("pendingServerStart", s.pendingServerStart);
    }
    if (j.contains("accounts") && j["accounts"].is_array()) {
        for (const auto& ja : j["accounts"]) {
            Account a;
            if (ja.contains("name") && ja["name"].is_string()) a.name = ja["name"];
            if (ja.contains("uuid") && ja["uuid"].is_string()) a.uuid = ja["uuid"];
            if (ja.contains("type") && ja["type"].is_string()) a.type = ja["type"];
            if (ja.contains("accessToken") && ja["accessToken"].is_string()) a.accessToken = ja["accessToken"];
            if (ja.contains("refreshToken") && ja["refreshToken"].is_string()) a.refreshToken = ja["refreshToken"];
            if (ja.contains("expiresAt") && ja["expiresAt"].is_number_integer()) a.expiresAt = ja["expiresAt"];
            if (!a.name.empty()) accounts.push_back(a);
        }
    }
    if (j.contains("activeAccount") && j["activeAccount"].is_number_integer()) {
        activeAccount = j["activeAccount"];
    }
    if (s.gameDir.empty()) s.gameDir = DefaultGameDir();
    if (s.ramMb < 1024) s.ramMb = 1024;
}

void Config::Save() {
    if (dataDir.empty()) dataDir = AppDataDir();
    json j;
    j["settings"] = {
        {"ramMb", s.ramMb},
        {"javaPath", s.javaPath},
        {"gameDir", s.gameDir},
        {"jvmArgs", s.jvmArgs},
        {"winW", s.winW},
        {"winH", s.winH},
        {"fullscreen", s.fullscreen},
        {"closeOnLaunch", s.closeOnLaunch},
        {"accent", s.accent},
        {"cfApiKey", s.cfApiKey},
        {"msaClientId", s.msaClientId},
        {"showSnapshots", s.showSnapshots},
        {"selectedVersion", s.selectedVersion},
        {"loaderKind", s.loaderKind},
        {"loaderVersion", s.loaderVersion},
        {"lastLaunch", s.lastLaunch},
        {"lastVersion", s.lastVersion},
        {"launchCount", s.launchCount},
        {"playSeconds", s.playSeconds},
        {"recentVersions", s.recentVersions},
        {"animations", s.animations},
        {"restoreAfterExit", s.restoreAfterExit},
        {"launchToLastPage", s.launchToLastPage},
        {"serverVersion", s.serverVersion},
        {"serverPort", s.serverPort},
        {"serverMaxPlayers", s.serverMaxPlayers},
        {"serverMotd", s.serverMotd},
        {"serverGamemode", s.serverGamemode},
        {"serverDifficulty", s.serverDifficulty},
        {"serverOnlineMode", s.serverOnlineMode},
        {"lastPage", s.lastPage},
        {"pendingServerStart", s.pendingServerStart},
    };
    json accountsJson = json::array();
    for (const auto& a : accounts) {
        accountsJson.push_back({{"name", a.name},
                                {"uuid", a.uuid},
                                {"type", a.type},
                                {"accessToken", a.accessToken},
                                {"refreshToken", a.refreshToken},
                                {"expiresAt", a.expiresAt}});
    }
    j["accounts"] = accountsJson;
    j["activeAccount"] = activeAccount;
    WriteTextFile(dataDir / "config.json", j.dump(2));
}

const Account* Config::Active() const {
    if (activeAccount < 0 || activeAccount >= (int)accounts.size()) return nullptr;
    return &accounts[activeAccount];
}

std::string Config::DefaultGameDir() const {
    return (dataDir / "game").string();
}

int Config::AddOfflineAccount(const std::string& name) {
    for (size_t i = 0; i < accounts.size(); ++i) {
        if (ToLower(accounts[i].name) == ToLower(name)) {
            activeAccount = (int)i;
            Save();
            return activeAccount;
        }
    }
    Account a;
    a.name = name;
    a.uuid = OfflineUuid(name);
    a.type = "offline";
    accounts.push_back(a);
    activeAccount = (int)accounts.size() - 1;
    Save();
    return activeAccount;
}

// ------------------------------------------------------------------ textures
TextureCache::TextureCache() {
    for (int i = 0; i < 4; ++i) {
        workers_.emplace_back([this]() { WorkerLoop(); });
    }
}

TextureCache::~TextureCache() {
    Shutdown();
}

void TextureCache::Shutdown() {
    {
        std::lock_guard lock(mutex_);
        if (stop_) return;
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
}

ImTextureID TextureCache::Get(const std::string& url) {
    if (url.empty()) return 0;
    std::lock_guard lock(mutex_);
    auto it = map_.find(url);
    if (it != map_.end()) {
        Entry& e = *it->second;
        return e.state.load() == 2 ? e.texture : 0;
    }
    auto entry = std::make_shared<Entry>();
    map_[url] = entry;
    queue_.push_back(url);
    cv_.notify_one();
    return 0;
}

ImTextureID TextureCache::GetFile(const fs::path& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return 0;
    std::string key = "file:" + path.string();
    std::lock_guard lock(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
        Entry& e = *it->second;
        return e.state.load() == 2 ? e.texture : 0;
    }
    auto entry = std::make_shared<Entry>();
    map_[key] = entry;
    queue_.push_back(key);
    cv_.notify_one();
    return 0;
}

void TextureCache::WorkerLoop() {
    for (;;) {
        std::string url;
        std::shared_ptr<Entry> entry;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this]() { return stop_ || !queue_.empty(); });
            if (stop_ && queue_.empty()) return;
            url = queue_.front();
            queue_.pop_front();
            auto it = map_.find(url);
            if (it == map_.end()) continue;
            entry = it->second;
            entry->state.store(1);
        }
        std::string bytes;
        if (StartsWith(url, "file:")) {
            bytes = ReadAllBytesFile(fs::path(url.substr(5)));
        } else {
            HttpResult res = HttpGet(url, {"Accept: image/png,image/jpeg,image/webp,*/*"}, 20000);
            if (res.ok()) bytes = std::move(res.body);
        }
        {
            std::lock_guard lock(mutex_);
            if (!bytes.empty() && !stop_) {
                entry->bytes.assign(bytes.begin(), bytes.end());
                entry->state.store(2);
            } else {
                entry->state.store(3);
            }
        }
    }
}

void TextureCache::Pump(void* d3dDevice) {
    if (!d3dDevice) return;
    ID3D11Device* device = static_cast<ID3D11Device*>(d3dDevice);
    std::lock_guard lock(mutex_);
    for (auto& [url, entryPtr] : map_) {
        Entry& e = *entryPtr;
        if (e.state.load() != 2 || e.texture != 0) continue;

        int w = 0, h = 0, channels = 0;
        unsigned char* pixels = stbi_load_from_memory(e.bytes.data(), (int)e.bytes.size(), &w, &h,
                                                      &channels, 4);
        std::vector<unsigned char>().swap(e.bytes);
        if (!pixels || w <= 0 || h <= 0) {
            if (pixels) stbi_image_free(pixels);
            e.state.store(3);
            continue;
        }

        // Legacy skins (64x32) share the modern layout for the top half; pad them to 64x64 so all
        // UV math on skins can assume a square texture.
        std::vector<unsigned char> expanded;
        const unsigned char* source = pixels;
        if (w == 64 && h == 32) {
            expanded.assign((size_t)64 * 64 * 4, 0);
            for (int y = 0; y < 32; ++y) {
                std::memcpy(expanded.data() + (size_t)y * 64 * 4, pixels + (size_t)y * 64 * 4, 64 * 4);
            }
            source = expanded.data();
            w = 64;
            h = 64;
        }

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = (UINT)w;
        desc.Height = (UINT)h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = source;
        data.SysMemPitch = (UINT)w * 4;
        ID3D11Texture2D* texture = nullptr;
        if (SUCCEEDED(device->CreateTexture2D(&desc, &data, &texture)) && texture) {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = desc.Format;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;
            ID3D11ShaderResourceView* srv = nullptr;
            if (SUCCEEDED(device->CreateShaderResourceView(texture, &srvDesc, &srv)) && srv) {
                e.texture = (ImTextureID)srv;
            }
            texture->Release();
        }
        stbi_image_free(pixels);
        if (!e.texture) e.state.store(3);
    }
}

// ------------------------------------------------------------------ java
namespace {

void AddJavaCandidate(std::vector<fs::path>& candidates, const fs::path& home) {
    if (home.empty()) return;
    fs::path exe = home / "bin" / "java.exe";
    if (FileExists(exe)) candidates.push_back(exe);
}

void ScanJavaDir(std::vector<fs::path>& candidates, const fs::path& root, int depth = 0) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return;
    AddJavaCandidate(candidates, root);
    if (depth >= 3) return;
    for (const auto& entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (entry.is_directory(ec)) ScanJavaDir(candidates, entry.path(), depth + 1);
    }
}

void ReadJavaRegistry(std::vector<fs::path>& candidates, HKEY root, const wchar_t* subkey) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) return;
    wchar_t name[256];
    DWORD index = 0;
    for (;;) {
        DWORD nameLen = 256;
        if (RegEnumKeyExW(key, index++, name, &nameLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;
        HKEY child = nullptr;
        if (RegOpenKeyExW(key, name, 0, KEY_READ, &child) == ERROR_SUCCESS) {
            for (const wchar_t* valueName : {L"JavaHome", L"Path", L"InstallationPath"}) {
                wchar_t value[1024];
                DWORD size = sizeof(value);
                DWORD type = 0;
                if (RegQueryValueExW(child, valueName, nullptr, &type, (LPBYTE)value, &size) == ERROR_SUCCESS &&
                    (type == REG_SZ || type == REG_EXPAND_SZ) && size > 2) {
                    AddJavaCandidate(candidates, fs::path(value));
                    break;
                }
            }
            RegCloseKey(child);
        }
    }
    RegCloseKey(key);
}

int ParseJavaMajor(const std::string& version) {
    if (version.empty()) return 0;
    std::string v = version;
    if (StartsWith(v, "1.")) v = v.substr(2);
    int major = 0;
    for (char c : v) {
        if (c >= '0' && c <= '9') major = major * 10 + (c - '0');
        else break;
    }
    return major;
}

std::string ParseJavaVersionString(const std::string& output) {
    size_t pos = output.find("version \"");
    if (pos == std::string::npos) return {};
    pos += 9;
    size_t end = output.find('"', pos);
    if (end == std::string::npos) return {};
    return output.substr(pos, end - pos);
}

void GatherJavaCandidates(std::vector<fs::path>& candidates) {
    wchar_t buffer[32768];
    DWORD len = GetEnvironmentVariableW(L"JAVA_HOME", buffer, 32768);
    if (len > 0) AddJavaCandidate(candidates, fs::path(buffer));

    wchar_t found[MAX_PATH];
    if (SearchPathW(nullptr, L"java.exe", nullptr, MAX_PATH, found, nullptr) > 0) {
        candidates.emplace_back(found);
    }

    for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        ReadJavaRegistry(candidates, root, L"SOFTWARE\\JavaSoft\\JDK");
        ReadJavaRegistry(candidates, root, L"SOFTWARE\\JavaSoft\\Java Development Kit");
        ReadJavaRegistry(candidates, root, L"SOFTWARE\\JavaSoft\\Java Runtime Environment");
        ReadJavaRegistry(candidates, root, L"SOFTWARE\\Eclipse Adoptium");
        ReadJavaRegistry(candidates, root, L"SOFTWARE\\Eclipse Foundation\\JDK");
        ReadJavaRegistry(candidates, root, L"SOFTWARE\\Microsoft\\JDK");
        ReadJavaRegistry(candidates, root, L"SOFTWARE\\Azul Systems\\Zulu");
        ReadJavaRegistry(candidates, root, L"SOFTWARE\\BellSoft");
    }

    wchar_t programFiles[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"ProgramFiles", programFiles, MAX_PATH) > 0) {
        fs::path pf(programFiles);
        for (const wchar_t* dir : {L"Java", L"Eclipse Adoptium", L"Microsoft", L"BellSoft", L"Zulu",
                                   L"Amazon Corretto", L"JetBrains\\Runtime", L"Semeru",
                                   L"Eclipse Foundation"}) {
            ScanJavaDir(candidates, pf / dir);
        }
    }
    wchar_t programFilesX86[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"ProgramFiles(x86)", programFilesX86, MAX_PATH) > 0) {
        ScanJavaDir(candidates, fs::path(programFilesX86) / L"Java");
    }
    wchar_t programData[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"ProgramData", programData, MAX_PATH) > 0) {
        ScanJavaDir(candidates, fs::path(programData) / L"chocolatey" / L"lib");
    }

    // Java, скачанная самим лаунчером.
    fs::path dataDir = AppDataDir();
    ScanJavaDir(candidates, dataDir / L"java");

    // Профиль пользователя, из которого лаунчер был запущен (важно после relaunch as admin).
    fs::path roaming = dataDir.parent_path();
    ScanJavaDir(candidates, roaming / L".minecraft" / L"runtime");
    if (roaming.filename() == L"Roaming") {
        fs::path userRoot = roaming.parent_path().parent_path();
        fs::path local = userRoot / L"AppData" / L"Local";
        ScanJavaDir(candidates, local / L"Programs" / L"Eclipse Adoptium");
        ScanJavaDir(candidates, local / L"Programs" / L"Microsoft");
        ScanJavaDir(candidates, local / L"Programs" / L"Java");
        ScanJavaDir(candidates, local / L"Programs" / L"BellSoft");
        ScanJavaDir(candidates, local / L"Programs" / L"Zulu");
        ScanJavaDir(candidates, userRoot / L".jdks");
        ScanJavaDir(candidates, userRoot / L".gradle" / L"jdks");
        ScanJavaDir(candidates, userRoot / L"scoop" / L"apps");
    }

    wchar_t appData[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) > 0 &&
        fs::path(appData) != roaming) {
        ScanJavaDir(candidates, fs::path(appData) / L".minecraft" / L"runtime");
    }
    wchar_t localAppData[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH) > 0) {
        fs::path local(localAppData);
        ScanJavaDir(candidates, local / L"Programs" / L"Eclipse Adoptium");
        ScanJavaDir(candidates, local / L"Programs" / L"Microsoft");
        ScanJavaDir(candidates, local / L"Programs" / L"Java");
        ScanJavaDir(candidates, local / L"Programs" / L"BellSoft");
        ScanJavaDir(candidates, local / L"Programs" / L"Zulu");
        ScanJavaDir(candidates, local / L"JetBrains" / L"Toolbox" / L"apps");
    }
    wchar_t userProfile[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH) > 0) {
        fs::path profile(userProfile);
        ScanJavaDir(candidates, profile / L".jdks");
        ScanJavaDir(candidates, profile / L".gradle" / L"jdks");
        ScanJavaDir(candidates, profile / L"scoop" / L"apps");
    }
}

void ScanJavaInstalls(std::vector<JavaInstall>& out) {
    std::vector<fs::path> candidates;
    GatherJavaCandidates(candidates);

    std::set<std::string> seen;
    for (const auto& exe : candidates) {
        std::error_code ec;
        if (!fs::is_regular_file(exe, ec)) continue;
        fs::path canonical = fs::weakly_canonical(exe, ec);
        std::string key = ToLower(Utf8((ec ? exe : canonical).wstring()));
        if (!seen.insert(key).second) continue;
        ProcessResult r = RunProcess(exe.string(), {"-version"}, nullptr, {}, 8000);
        if (!r.started) continue;
        std::string version = ParseJavaVersionString(r.output);
        if (version.empty()) continue;
        JavaInstall install;
        install.path = exe.string();
        install.version = version;
        install.major = ParseJavaMajor(version);
        if (install.major > 0) out.push_back(install);
    }
    std::sort(out.begin(), out.end(), [](const JavaInstall& a, const JavaInstall& b) {
        if (a.major != b.major) return a.major < b.major;
        return a.path < b.path;
    });
    LogLine("[Java] обнаружено установок: " + std::to_string(out.size()));
    for (const auto& install : out) {
        LogLine("[Java] " + std::to_string(install.major) + " — " + install.path);
    }
}

std::mutex g_javaCacheMutex;
std::vector<JavaInstall> g_javaCache;
bool g_javaCacheValid = false;

} // namespace

std::vector<JavaInstall> DetectJava() {
    std::lock_guard lock(g_javaCacheMutex);
    if (!g_javaCacheValid) {
        std::vector<JavaInstall> installs;
        ScanJavaInstalls(installs);
        g_javaCache = std::move(installs);
        g_javaCacheValid = true;
    }
    return g_javaCache;
}

void InvalidateJavaCache() {
    std::lock_guard lock(g_javaCacheMutex);
    g_javaCacheValid = false;
}

std::string FindJavaExe(const fs::path& root) {
    if (root.empty()) return {};
    std::error_code ec;
    if (fs::is_regular_file(root, ec) && root.filename() == L"java.exe") {
        return root.string();
    }
    fs::path direct = root / "bin" / "java.exe";
    if (FileExists(direct)) return direct.string();
    if (!fs::is_directory(root, ec)) return {};

    std::vector<fs::path> dirs{root};
    for (int depth = 0; depth < 4 && !dirs.empty(); ++depth) {
        std::vector<fs::path> next;
        for (const auto& dir : dirs) {
            fs::path exe = dir / "bin" / "java.exe";
            if (FileExists(exe)) return exe.string();
            fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
            for (; !ec && it != fs::directory_iterator(); it.increment(ec)) {
                if (it->is_directory(ec)) next.push_back(it->path());
            }
            ec.clear();
        }
        dirs = std::move(next);
    }
    return {};
}

std::string EnsureJavaRuntime(int major, Task* task, std::atomic<bool>* cancel,
                              std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error) *error = message;
        if (task) task->Log(message);
        return std::string();
    };

    int feature = std::max(major, 8);
    fs::path root = AppDataDir() / "java";
    fs::path dir = root / ("temurin-" + std::to_string(feature));
    std::string existing = FindJavaExe(dir);
    if (!existing.empty()) {
        if (task) task->Log("Java " + std::to_string(feature) + " уже установлена: " + existing);
        return existing;
    }

    std::error_code ec;
    fs::create_directories(root, ec);

    if (task) task->SetStatus("Получение Java " + std::to_string(feature) + " из Adoptium");
    std::string api = "https://api.adoptium.net/v3/assets/latest/" + std::to_string(feature) +
                      "/hotspot?architecture=x64&image_type=jre&os=windows&vendor=eclipse";
    HttpResult res = HttpGet(api, {"Accept: application/json"}, 30000);
    if (!res.ok()) {
        return fail("Не удалось получить список сборок Java: " +
                    (res.error.empty() ? "HTTP " + std::to_string(res.status) : res.error));
    }
    json assets = json::parse(res.body, nullptr, false);
    if (!assets.is_array() || assets.empty()) {
        return fail("Adoptium не предложил сборку Java " + std::to_string(feature));
    }

    std::string downloadUrl;
    std::string fileName;
    long long expectedSize = 0;
    for (const auto& asset : assets) {
        const json* pkg = nullptr;
        if (asset.contains("binary") && asset["binary"].is_object() &&
            asset["binary"].contains("package")) {
            pkg = &asset["binary"]["package"];
        } else if (asset.contains("package")) {
            pkg = &asset["package"];
        }
        if (!pkg || !pkg->is_object()) continue;
        std::string link = pkg->value("link", "");
        if (link.empty()) continue;
        downloadUrl = link;
        fileName = pkg->value("name", "");
        expectedSize = pkg->value("size", 0LL);
        break;
    }
    if (downloadUrl.empty()) {
        return fail("Не найдена сборка JRE Java " + std::to_string(feature) + " для Windows x64");
    }
    if (fileName.empty()) fileName = "temurin-" + std::to_string(feature) + ".zip";
    fs::path zipPath = root / fileName;

    if (task) task->SetStatus("Загрузка Java " + std::to_string(feature));
    if (task) task->Log("Скачивание " + downloadUrl);
    std::string dlError;
    bool ok = DownloadFile(
        downloadUrl, zipPath,
        [&](int64_t received, int64_t total) {
            if (!task) return;
            total = total > 0 ? total : expectedSize;
            if (total <= 0) return;
            float fraction = (float)((double)received / (double)total);
            task->SetProgress(fraction * 0.85f,
                              "Загрузка Java " + std::to_string(feature) + " " +
                                  std::to_string((int)(fraction * 100)) + "%");
        },
        cancel, 3, &dlError);
    if (!ok) {
        fs::remove(zipPath, ec);
        return fail("Не удалось скачать Java: " + (dlError.empty() ? "ошибка сети" : dlError));
    }
    if (cancel && cancel->load()) {
        fs::remove(zipPath, ec);
        return fail("Загрузка Java отменена");
    }

    if (task) task->SetProgress(0.9f, "Распаковка Java " + std::to_string(feature));
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    std::string zipError;
    if (!ZipExtract(zipPath, dir, {}, "", false, &zipError)) {
        fs::remove_all(dir, ec);
        fs::remove(zipPath, ec);
        return fail("Не удалось распаковать Java: " +
                    (zipError.empty() ? "повреждённый архив" : zipError));
    }
    fs::remove(zipPath, ec);

    std::string java = FindJavaExe(dir);
    if (java.empty()) return fail("В архиве Java не найден java.exe");
    InvalidateJavaCache();
    if (task) {
        task->Log("Java " + std::to_string(feature) + ": " + java);
        task->SetProgress(1.0f, "Java " + std::to_string(feature) + " готова");
    }
    return java;
}

std::string OfflineUuid(const std::string& name) {
    std::string input = "OfflinePlayer:" + name;
    BCRYPT_ALG_HANDLE alg = nullptr;
    unsigned char hash[16] = {};
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM, nullptr, 0) == 0) {
        BCryptHash(alg, nullptr, 0, (PUCHAR)input.data(), (ULONG)input.size(), hash, 16);
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    hash[6] = (unsigned char)((hash[6] & 0x0F) | 0x30);
    hash[8] = (unsigned char)((hash[8] & 0x3F) | 0x80);
    char out[37];
    snprintf(out, sizeof(out),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", hash[0],
             hash[1], hash[2], hash[3], hash[4], hash[5], hash[6], hash[7], hash[8], hash[9], hash[10],
             hash[11], hash[12], hash[13], hash[14], hash[15]);
    return out;
}

std::string MinecraftUuid(const std::string& uuidWithDashes) {
    std::string out;
    out.reserve(32);
    for (char c : uuidWithDashes) {
        if (c != '-') out.push_back(c);
    }
    return out;
}

int64_t NowSeconds() {
    return (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string Timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    tm local{};
    localtime_s(&local, &t);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
    return buffer;
}

std::string NowDateTime() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    tm local{};
    localtime_s(&local, &t);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local);
    return buffer;
}

std::string LocalIPv4() {
    ULONG size = 0;
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                          GAA_FLAG_SKIP_DNS_SERVER,
                             nullptr, nullptr, &size) != ERROR_BUFFER_OVERFLOW) {
        return "127.0.0.1";
    }
    std::vector<unsigned char> buffer(size);
    auto* addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                          GAA_FLAG_SKIP_DNS_SERVER,
                             nullptr, addresses, &size) != NO_ERROR) {
        return "127.0.0.1";
    }
    for (auto* adapter = addresses; adapter; adapter = adapter->Next) {
        if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        if (adapter->OperStatus != IfOperStatusUp) continue;
        for (auto* uni = adapter->FirstUnicastAddress; uni; uni = uni->Next) {
            if (!uni->Address.lpSockaddr || uni->Address.lpSockaddr->sa_family != AF_INET) continue;
            auto* sin = reinterpret_cast<sockaddr_in*>(uni->Address.lpSockaddr);
            unsigned char* b = reinterpret_cast<unsigned char*>(&sin->sin_addr.s_addr);
            if (b[0] == 127) continue;
            char text[24];
            snprintf(text, sizeof(text), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
            return text;
        }
    }
    return "127.0.0.1";
}
