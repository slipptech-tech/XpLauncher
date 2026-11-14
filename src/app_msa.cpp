#include "app.hpp"

#include <algorithm>
#include <cctype>

// ------------------------------------------------------------------ microsoft

void App::JoinMicrosoftWorker() {
    if (msa_.worker.joinable()) msa_.worker.join();
}

void App::StartMicrosoftLogin() {
    std::string clientId = Trim(cfg.s.msaClientId);
    std::string problem = MsaClientIdLooksValid(clientId);
    if (!problem.empty()) {
        ToastMsg(problem, 2);
        msaClientEditing_ = true;
        return;
    }
    int activeState = msa_.state.load();
    if (activeState == 1 || activeState == 2) {
        ToastMsg("Вход через Microsoft уже выполняется");
        return;
    }
    JoinMicrosoftWorker();
    {
        std::lock_guard lock(msa_.mutex);
        msa_.userCode.clear();
        msa_.verificationUri.clear();
        msa_.status = "Подключаемся к Microsoft...";
        msa_.error.clear();
        msa_.mode = 0;
        msa_.accountIndex = -1;
    }
    msa_.cancel.store(false);
    msa_.state.store(1);
    msaReported_ = 1;
    msaLaunchAfter_ = false;
    msaPendingGame_.clear();
    msaPendingKind_ = 0;

    msa_.worker = std::thread([this, clientId]() {
        MsaDeviceCode code;
        std::string error;
        if (!MsaStartDeviceCode(clientId, code, error)) {
            std::lock_guard lock(msa_.mutex);
            msa_.error = error;
            msa_.state.store(4);
            return;
        }
        {
            std::lock_guard lock(msa_.mutex);
            msa_.userCode = code.userCode;
            msa_.verificationUri = code.verificationUri;
            msa_.status = "Откройте ссылку и введите код";
            msa_.mode = 0;
        }
        msa_.state.store(2);

        std::string access;
        std::string refresh;
        bool ok = MsaPollDeviceCode(clientId, code, &msa_.cancel, access, refresh, error,
                                    [this](const std::string& text) {
                                        std::lock_guard lock(msa_.mutex);
                                        msa_.status = text;
                                    });
        if (!ok) {
            if (msa_.cancel.load()) {
                msa_.state.store(5);
            } else {
                std::lock_guard lock(msa_.mutex);
                msa_.error = error;
                msa_.state.store(4);
            }
            return;
        }

        {
            std::lock_guard lock(msa_.mutex);
            msa_.status = "Получаем профиль Minecraft...";
        }
        Account account;
        if (!MsaLoginWithMsToken(access, refresh, account, error)) {
            std::lock_guard lock(msa_.mutex);
            msa_.error = error;
            msa_.state.store(4);
            return;
        }
        {
            std::lock_guard lock(msa_.mutex);
            msa_.account = account;
            msa_.mode = 0;
        }
        msa_.state.store(3);
    });
}

void App::StartMicrosoftRefresh(int accountIndex, bool launchAfter, const std::string& gameVersion,
                                int loaderKind) {
    if (accountIndex < 0 || accountIndex >= (int)cfg.accounts.size()) return;
    int activeState = msa_.state.load();
    if (activeState == 1 || activeState == 2) {
        ToastMsg("Дождитесь завершения входа в Microsoft");
        return;
    }
    std::string clientId = Trim(cfg.s.msaClientId);
    std::string problem = MsaClientIdLooksValid(clientId);
    if (!problem.empty()) {
        ToastMsg("Для Microsoft-аккаунта укажите Client ID", 2);
        msaClientEditing_ = true;
        SetPage(Page::Accounts);
        return;
    }
    Account snapshot = cfg.accounts[accountIndex];
    if (snapshot.refreshToken.empty()) {
        ToastMsg("Сессия Microsoft истекла — войдите в аккаунт заново", 2);
        SetPage(Page::Accounts);
        return;
    }
    JoinMicrosoftWorker();
    {
        std::lock_guard lock(msa_.mutex);
        msa_.status = "Обновляем сессию Microsoft...";
        msa_.error.clear();
        msa_.userCode.clear();
        msa_.account = snapshot;
        msa_.mode = 1;
        msa_.accountIndex = accountIndex;
    }
    msa_.cancel.store(false);
    msa_.state.store(1);
    msaReported_ = 1;
    msaLaunchAfter_ = launchAfter;
    msaPendingGame_ = launchAfter ? gameVersion : "";
    msaPendingKind_ = launchAfter ? loaderKind : 0;

    msa_.worker = std::thread([this, clientId, snapshot]() {
        Account account = snapshot;
        std::string error;
        if (!MsaRefreshAccount(clientId, account, error)) {
            std::lock_guard lock(msa_.mutex);
            msa_.error = error;
            msa_.state.store(4);
            return;
        }
        {
            std::lock_guard lock(msa_.mutex);
            msa_.account = account;
            msa_.mode = 1;
        }
        msa_.state.store(3);
    });
}

void App::CancelMicrosoftFlow() {
    int state = msa_.state.load();
    if (state != 1 && state != 2) return;
    msa_.cancel.store(true);
    ToastMsg("Отменяем вход...");
}

void App::PollMicrosoftFlow() {
    int state = msa_.state.load();
    if (state <= 2) return;
    if (state == msaReported_ && state != 3 && state != 4 && state != 5) return;
    if (state == 0) return;

    if (state == 3) {
        Account account;
        int mode = 0;
        int accountIndex = -1;
        {
            std::lock_guard lock(msa_.mutex);
            account = msa_.account;
            mode = msa_.mode;
            accountIndex = msa_.accountIndex;
        }
        JoinMicrosoftWorker();
        msaReported_ = 3;
        msa_.state.store(0);
        msaReported_ = 0;

        int index = -1;
        if (mode == 1) {
            if (accountIndex >= 0 && accountIndex < (int)cfg.accounts.size()) {
                cfg.accounts[accountIndex] = account;
                index = accountIndex;
            } else {
                for (size_t i = 0; i < cfg.accounts.size(); ++i) {
                    if (!account.uuid.empty() &&
                        ToLower(cfg.accounts[i].uuid) == ToLower(account.uuid)) {
                        cfg.accounts[i] = account;
                        index = (int)i;
                        break;
                    }
                }
            }
        } else {
            for (size_t i = 0; i < cfg.accounts.size(); ++i) {
                if (!account.uuid.empty() &&
                    ToLower(cfg.accounts[i].uuid) == ToLower(account.uuid)) {
                    cfg.accounts[i] = account;
                    index = (int)i;
                    break;
                }
            }
            if (index < 0) {
                cfg.accounts.push_back(account);
                index = (int)cfg.accounts.size() - 1;
            }
        }
        if (index >= 0) cfg.activeAccount = index;
        cfg.Save();

        if (mode == 1) {
            ToastMsg("Сессия Microsoft обновлена", 1);
        } else {
            ToastMsg("Вы вошли как " + account.name, 1);
        }

        bool launch = msaLaunchAfter_;
        std::string game = msaPendingGame_;
        int kind = msaPendingKind_;
        msaLaunchAfter_ = false;
        msaPendingGame_.clear();
        msaPendingKind_ = 0;
        if (launch && !game.empty()) LaunchNow(game, kind);
        return;
    }

    if (state == 4) {
        std::string error;
        {
            std::lock_guard lock(msa_.mutex);
            error = msa_.error;
        }
        JoinMicrosoftWorker();
        msaReported_ = 4;
        msa_.state.store(0);
        msaReported_ = 0;
        msaLaunchAfter_ = false;
        msaPendingGame_.clear();
        msaPendingKind_ = 0;
        ToastMsg(error.empty() ? "Не удалось войти через Microsoft" : error, 2);
        return;
    }

    if (state == 5) {
        JoinMicrosoftWorker();
        msaReported_ = 5;
        msa_.state.store(0);
        msaReported_ = 0;
        msaLaunchAfter_ = false;
        msaPendingGame_.clear();
        msaPendingKind_ = 0;
        ToastMsg("Вход через Microsoft отменён");
    }
}
