#pragma once

#include "core.hpp"

#include <atomic>
#include <functional>
#include <string>

struct MsaDeviceCode {
    std::string deviceCode;
    std::string userCode;
    std::string verificationUri;
    std::string message;
    int interval = 5;
    int expiresIn = 900;
};

bool MsaStartDeviceCode(const std::string& clientId, MsaDeviceCode& out, std::string& error);
bool MsaPollDeviceCode(const std::string& clientId, const MsaDeviceCode& code,
                       std::atomic<bool>* cancel, std::string& msAccess, std::string& msRefresh,
                       std::string& error,
                       const std::function<void(const std::string&)>& onStatus = {});
bool MsaLoginWithMsToken(const std::string& msAccess, const std::string& msRefresh, Account& account,
                         std::string& error);
bool MsaRefreshAccount(const std::string& clientId, Account& account, std::string& error);
bool MsaUploadSkin(const std::string& mcAccessToken, const std::string& pngBytes, bool slim,
                   std::string& error);
bool MsaResetSkin(const std::string& mcAccessToken, std::string& error);
std::string MsaClientIdLooksValid(const std::string& clientId);
