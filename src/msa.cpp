#include "msa.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>

namespace {

const char* kScope = "XboxLive.signin offline_access";
const char* kDeviceCodeUrl = "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode";
const char* kTokenUrl = "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";
const char* kXblUrl = "https://user.auth.xboxlive.com/user/authenticate";
const char* kXstsUrl = "https://xsts.auth.xboxlive.com/xsts/authorize";
const char* kMcLoginUrl = "https://api.minecraftservices.com/authentication/login_with_xbox";
const char* kEntitlementsUrl = "https://api.minecraftservices.com/entitlements/mcstore";
const char* kProfileUrl = "https://api.minecraftservices.com/minecraft/profile";
const char* kSkinUrl = "https://api.minecraftservices.com/minecraft/profile/skins";

const std::vector<std::string> kJsonHeaders = {"Content-Type: application/json",
                                               "Accept: application/json"};
const std::vector<std::string> kFormHeaders = {
    "Content-Type: application/x-www-form-urlencoded", "Accept: application/json"};

json ParseJson(const std::string& text) {
    json j = json::parse(text, nullptr, false);
    return j;
}

std::string TrimErrorText(std::string text) {
    for (const char* marker : {" Trace ID:", "\r\n", "\n"}) {
        size_t pos = text.find(marker);
        if (pos != std::string::npos) text.resize(pos);
    }
    text = Trim(text);
    if (text.size() > 320) text.resize(320);
    return text;
}

std::string Describe(const HttpResult& res) {
    if (!res.error.empty()) return res.error;
    json j = ParseJson(res.body);
    if (!j.is_discarded() && j.is_object()) {
        for (const char* key : {"error_description", "errorMessage", "message", "error"}) {
            if (j.contains(key) && j[key].is_string()) {
                std::string text = TrimErrorText(j[key].get<std::string>());
                if (!text.empty()) return text;
            }
        }
    }
    if (res.status != 0) return "Сервер вернул HTTP " + std::to_string(res.status);
    return "Неизвестная ошибка сети";
}

std::string FormField(const std::string& key, const std::string& value) {
    return key + "=" + UrlEncode(value);
}

bool CheckCancel(std::atomic<bool>* cancel, std::string& error) {
    if (cancel && cancel->load()) {
        error = "Вход отменён";
        return true;
    }
    return false;
}

void SleepInterruptible(int ms, std::atomic<bool>* cancel) {
    const int step = 100;
    for (int elapsed = 0; elapsed < ms; elapsed += step) {
        if (cancel && cancel->load()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(step));
    }
}

std::string XstsAdvice(const json& j) {
    long long code = 0;
    if (j.contains("XErr")) {
        if (j["XErr"].is_number_integer()) code = j["XErr"].get<long long>();
        else if (j["XErr"].is_string()) {
            try {
                code = std::stoll(j["XErr"].get<std::string>());
            } catch (...) {
            }
        }
    }
    switch (code) {
        case 2148916233:
            return "У аккаунта Microsoft нет профиля Xbox. Создайте его на xbox.com и повторите вход.";
        case 2148916235:
            return "Xbox Live недоступен в вашей стране или регионе.";
        case 2148916236:
        case 2148916237:
            return "Для входа требуется проверка возраста (Южная Корея).";
        case 2148916238:
            return "Это детский аккаунт. Взрослый член семьи должен добавить его в семейную группу.";
        default:
            break;
    }
    if (code != 0) return "Xbox Live отклонил вход (код " + std::to_string(code) + ")";
    return "Xbox Live отклонил вход. Проверьте профиль Xbox и повторите попытку.";
}

bool LooksLikeGuid(const std::string& id) {
    if (id.size() < 32 || id.size() > 38) return false;
    for (char c : id) {
        if (!(std::isxdigit((unsigned char)c) || c == '-')) return false;
    }
    return true;
}

} // namespace

std::string MsaClientIdLooksValid(const std::string& clientId) {
    if (clientId.empty()) return "Укажите Client ID приложения Azure";
    if (!LooksLikeGuid(clientId)) return "Client ID должен быть в формате UUID, например 00000000-0000-0000-0000-000000000000";
    return {};
}

bool MsaStartDeviceCode(const std::string& clientId, MsaDeviceCode& out, std::string& error) {
    std::string body = FormField("client_id", clientId) + "&" + FormField("scope", kScope);
    HttpResult res = HttpPost(kDeviceCodeUrl, body, kFormHeaders, 20000);
    if (!res.ok()) {
        error = "Не удалось начать вход Microsoft: " + Describe(res);
        return false;
    }
    json j = ParseJson(res.body);
    if (j.is_discarded() || !j.is_object() || !j.contains("device_code") ||
        !j.contains("user_code")) {
        error = "Некорректный ответ Microsoft при выдаче кода";
        return false;
    }
    out.deviceCode = j.value("device_code", "");
    out.userCode = j.value("user_code", "");
    out.verificationUri = j.value("verification_uri", "https://www.microsoft.com/link");
    out.message = j.value("message", "");
    out.interval = std::clamp(j.value("interval", 5), 2, 30);
    out.expiresIn = std::clamp(j.value("expires_in", 900), 60, 1800);
    if (out.deviceCode.empty() || out.userCode.empty()) {
        error = "Microsoft не выдал код устройства";
        return false;
    }
    return true;
}

bool MsaPollDeviceCode(const std::string& clientId, const MsaDeviceCode& code,
                       std::atomic<bool>* cancel, std::string& msAccess, std::string& msRefresh,
                       std::string& error,
                       const std::function<void(const std::string&)>& onStatus) {
    int interval = code.interval;
    int64_t deadline = NowSeconds() + code.expiresIn;
    if (onStatus) onStatus("Ожидаем подтверждение входа в браузере");

    while (NowSeconds() < deadline) {
        SleepInterruptible(interval * 1000, cancel);
        if (CheckCancel(cancel, error)) return false;

        std::string body = FormField("grant_type", "urn:ietf:params:oauth:grant-type:device_code") +
                           "&" + FormField("client_id", clientId) + "&" +
                           FormField("device_code", code.deviceCode);
        HttpResult res = HttpPost(kTokenUrl, body, kFormHeaders, 20000);
        if (res.ok()) {
            json j = ParseJson(res.body);
            if (!j.is_discarded() && j.is_object()) {
                msAccess = j.value("access_token", "");
                msRefresh = j.value("refresh_token", "");
                if (!msAccess.empty()) return true;
            }
            error = "Microsoft не вернул токен доступа";
            return false;
        }

        json j = ParseJson(res.body);
        std::string err = (j.is_object() && j.contains("error") && j["error"].is_string())
                              ? j["error"].get<std::string>()
                              : "";
        if (err == "authorization_pending") continue;
        if (err == "slow_down") {
            interval += 5;
            continue;
        }
        if (err == "authorization_declined") {
            error = "Вход через Microsoft отклонён";
            return false;
        }
        if (err == "expired_token" || err == "bad_verification_code") {
            error = "Код истёк. Начните вход заново";
            return false;
        }
        error = Describe(res);
        return false;
    }
    error = "Время ожидания входа истекло — начните заново";
    return false;
}

bool MsaLoginWithMsToken(const std::string& msAccess, const std::string& msRefresh, Account& account,
                         std::string& error) {
    json xblBody = {
        {"Properties",
         {{"AuthMethod", "RPS"},
          {"SiteName", "user.auth.xboxlive.com"},
          {"RpsTicket", "d=" + msAccess}}},
        {"RelyingParty", "http://auth.xboxlive.com"},
        {"TokenType", "JWT"}};
    HttpResult xblRes = HttpPost(kXblUrl, xblBody.dump(), kJsonHeaders, 20000);
    if (!xblRes.ok()) {
        error = "Xbox Live: " + Describe(xblRes);
        return false;
    }
    json xbl = ParseJson(xblRes.body);
    std::string xblToken = xbl.value("Token", "");
    std::string uhs;
    if (xbl.contains("DisplayClaims") && xbl["DisplayClaims"].is_object() &&
        xbl["DisplayClaims"].contains("xui") && xbl["DisplayClaims"]["xui"].is_array() &&
        !xbl["DisplayClaims"]["xui"].empty()) {
        uhs = xbl["DisplayClaims"]["xui"][0].value("uhs", "");
    }
    if (xblToken.empty() || uhs.empty()) {
        error = "Xbox Live вернул пустой токен";
        return false;
    }

    json xstsBody = {{"Properties",
                      {{"SandboxId", "RETAIL"}, {"UserTokens", json::array({xblToken})}}},
                     {"RelyingParty", "rp://api.minecraftservices.com/"},
                     {"TokenType", "JWT"}};
    HttpResult xstsRes = HttpPost(kXstsUrl, xstsBody.dump(), kJsonHeaders, 20000);
    if (!xstsRes.ok()) {
        json xsts = ParseJson(xstsRes.body);
        if (xsts.is_object() && xsts.contains("XErr")) {
            error = XstsAdvice(xsts);
        } else {
            error = "XSTS: " + Describe(xstsRes);
        }
        return false;
    }
    json xsts = ParseJson(xstsRes.body);
    std::string xstsToken = xsts.value("Token", "");
    if (xstsToken.empty()) {
        error = "XSTS вернул пустой токен";
        return false;
    }

    json mcBody = {{"identityToken", "XBL3.0 x=" + uhs + ";" + xstsToken}};
    HttpResult mcRes = HttpPost(kMcLoginUrl, mcBody.dump(), kJsonHeaders, 20000);
    if (!mcRes.ok()) {
        if (mcRes.status == 403) {
            std::string detail = Describe(mcRes);
            error = "Mojang отклонил приложение: " + detail +
                    ". Заявку на одобрение Client ID можно отправить на https://aka.ms/mce-reviewappid";
        } else {
            error = "Minecraft: " + Describe(mcRes);
        }
        return false;
    }
    json mc = ParseJson(mcRes.body);
    std::string mcToken = mc.value("access_token", "");
    int expiresIn = std::clamp(mc.value("expires_in", 86400), 300, 172800);
    if (mcToken.empty()) {
        error = "Minecraft не вернул токен доступа";
        return false;
    }

    std::vector<std::string> auth{"Authorization: Bearer " + mcToken, "Accept: application/json"};
    HttpResult ent = HttpGet(kEntitlementsUrl, auth, 20000);
    if (ent.ok()) {
        json ej = ParseJson(ent.body);
        bool owned = false;
        if (ej.is_object() && ej.contains("items") && ej["items"].is_array()) {
            for (const auto& item : ej["items"]) {
                std::string name = item.value("name", "");
                if (name == "game_minecraft" || name == "product_minecraft") {
                    owned = true;
                    break;
                }
            }
        }
        if (!owned) {
            error = "Аккаунт Microsoft не владеет Minecraft: Java Edition (или Game Pass)";
            return false;
        }
    }

    HttpResult profileRes = HttpGet(kProfileUrl, auth, 20000);
    if (!profileRes.ok()) {
        if (profileRes.status == 404) {
            error = "У аккаунта нет профиля Minecraft. Запустите игру один раз в официальном "
                    "лаунчере и повторите вход.";
        } else {
            error = "Профиль Minecraft: " + Describe(profileRes);
        }
        return false;
    }
    json profile = ParseJson(profileRes.body);
    std::string uuid = profile.value("id", "");
    std::string name = profile.value("name", "");
    if (uuid.empty() || name.empty()) {
        error = "Minecraft не вернул профиль игрока";
        return false;
    }

    account.type = "msa";
    account.name = name;
    account.uuid = uuid;
    account.accessToken = mcToken;
    account.refreshToken = msRefresh;
    account.expiresAt = NowSeconds() + expiresIn;
    LogLine("MSA: вход выполнен для " + name);
    return true;
}

bool MsaRefreshAccount(const std::string& clientId, Account& account, std::string& error) {
    if (account.refreshToken.empty()) {
        error = "Нет refresh-токена Microsoft — войдите заново";
        return false;
    }
    std::string body = FormField("client_id", clientId) + "&" +
                       FormField("grant_type", "refresh_token") + "&" +
                       FormField("refresh_token", account.refreshToken) + "&" +
                       FormField("scope", kScope);
    HttpResult res = HttpPost(kTokenUrl, body, kFormHeaders, 20000);
    if (!res.ok()) {
        json j = ParseJson(res.body);
        std::string err = (j.is_object() && j.contains("error") && j["error"].is_string())
                              ? j["error"].get<std::string>()
                              : "";
        if (err == "invalid_grant") {
            error = "Сессия Microsoft истекла — войдите в аккаунт заново";
        } else {
            error = "Не удалось обновить токен: " + Describe(res);
        }
        return false;
    }
    json j = ParseJson(res.body);
    std::string msAccess = j.value("access_token", "");
    std::string newRefresh = j.value("refresh_token", account.refreshToken);
    if (msAccess.empty()) {
        error = "Microsoft не вернул токен доступа";
        return false;
    }
    Account updated = account;
    if (!MsaLoginWithMsToken(msAccess, newRefresh, updated, error)) return false;
    account = updated;
    return true;
}

bool MsaUploadSkin(const std::string& mcAccessToken, const std::string& pngBytes, bool slim,
                   std::string& error) {
    if (mcAccessToken.empty()) {
        error = "Нет токена Minecraft — войдите в аккаунт заново";
        return false;
    }
    if (pngBytes.empty()) {
        error = "Файл скина пуст";
        return false;
    }
    std::string boundary = "----XpLauncherBoundary" + std::to_string(NowSeconds());
    std::string body;
    body.reserve(pngBytes.size() + 512);
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"variant\"\r\n\r\n";
    body += slim ? "slim" : "classic";
    body += "\r\n--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"file\"; filename=\"skin.png\"\r\n";
    body += "Content-Type: image/png\r\n\r\n";
    body += pngBytes;
    body += "\r\n--" + boundary + "--\r\n";

    std::vector<std::string> headers = {"Authorization: Bearer " + mcAccessToken,
                                        "Accept: application/json",
                                        "Content-Type: multipart/form-data; boundary=" + boundary};
    HttpResult res = HttpPost(kSkinUrl, body, headers, 30000);
    if (!res.ok()) {
        if (res.status == 401) {
            error = "Сессия Minecraft истекла — обновите аккаунт и повторите";
        } else {
            error = "Не удалось применить скин: " + Describe(res);
        }
        return false;
    }
    return true;
}

bool MsaResetSkin(const std::string& mcAccessToken, std::string& error) {
    HttpResult res = HttpDelete(kSkinUrl,
                                {"Authorization: Bearer " + mcAccessToken,
                                 "Accept: application/json"},
                                20000);
    if (!res.ok()) {
        error = "Не удалось сбросить скин: " + Describe(res);
        return false;
    }
    return true;
}
