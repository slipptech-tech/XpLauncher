#include "app.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace {

bool ValidNickname(const std::string& name) {
    if (name.size() < 3 || name.size() > 16) return false;
    for (unsigned char c : name) {
        if (!(std::isalnum(c) || c == '_')) return false;
    }
    return true;
}

} // namespace

void App::DrawAccounts() {
    PageHeader("Аккаунты", "Профили для входа в игру и оффлайн-регистрация");

    float availW = ImGui::GetContentRegionAvail().x;
    float gap = ui::S(16);
    float leftW = std::max(ui::S(380), availW * 0.54f);
    float rightW = std::max(ui::S(300), availW - leftW - gap);
    float panelH = std::max(ui::S(430), ImGui::GetContentRegionAvail().y - ui::S(2));

    // ------------------------------------------------------------ accounts
    ImGui::BeginGroup();
    ui::BeginCard("##accounts_list", ImVec2(leftW, panelH));
    {
        ImGui::PushFont(uiFonts.h3);
        ImGui::TextUnformatted("Профили");
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::PushFont(uiFonts.smallFont);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colFaint));
        std::string count = std::to_string(cfg.accounts.size()) + " сохранено";
        ImGui::TextUnformatted(count.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Separator();

        if (cfg.accounts.empty()) {
            ImGui::Dummy(ImVec2(0, ui::S(40)));
            ImGui::PushFont(uiFonts.h2);
            ImGui::TextUnformatted("Пока нет аккаунтов");
            ImGui::PopFont();
            TextMutedWrapped("Создайте оффлайн-аккаунт справа. Он подойдёт для одиночной игры и "
                             "серверов без проверки лицензии.");
        }

        ImGui::BeginChild("##accounts_scroll", ImVec2(0, 0));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (size_t i = 0; i < cfg.accounts.size(); ++i) {
            Account& account = cfg.accounts[i];
            bool active = cfg.activeAccount == (int)i;
            ImGui::PushID((int)i);
            float rowW = ImGui::GetContentRegionAvail().x;
            float rowH = ui::S(76);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImVec2 pmax(p.x + rowW, p.y + rowH);
            dl->AddRectFilled(p, pmax, active ? ui::Alpha(colAccent, 24)
                                              : ui::Alpha(IM_COL32(255, 255, 255, 255), 7),
                              ui::S(12));
            dl->AddRect(p, pmax, active ? ui::Alpha(colAccent, 130) : colBorder, ui::S(12), 0, 1.0f);

            DrawAvatar(account.name, ImVec2(p.x + ui::S(13), p.y + ui::S(14)), ui::S(48));
            ImGui::PushFont(uiFonts.semibold);
            std::string name = ui::Ellipsize(account.name, rowW - ui::S(160), uiFonts.semibold);
            dl->AddText(ImVec2(p.x + ui::S(74), p.y + ui::S(14)), colText, name.c_str());
            ImGui::PopFont();
            ImGui::PushFont(uiFonts.tiny);
            std::string uuid = account.uuid.empty() ? "без UUID" : account.uuid.substr(0, 12) + "...";
            dl->AddText(ImVec2(p.x + ui::S(74), p.y + ui::S(37)), colFaint, uuid.c_str());
            ImGui::PopFont();
            ImGui::PushFont(uiFonts.smallFont);
            dl->AddText(ImVec2(p.x + ui::S(74), p.y + ui::S(53)),
                        account.type == "msa" ? colCyan : colGreen,
                        account.type == "msa" ? "Microsoft-аккаунт" : "Оффлайн-режим");
            ImGui::PopFont();

            ImGui::SetCursorScreenPos(ImVec2(p.x + rowW - ui::S(118), p.y + (rowH - ui::S(32)) * 0.5f));
            if (active) {
                ImVec2 pillPos = ImGui::GetCursorScreenPos();
                dl->AddRectFilled(pillPos, ImVec2(pillPos.x + ui::S(104), pillPos.y + ui::S(32)),
                                  ui::Alpha(colAccent, 40), ui::S(9));
                ImGui::PushFont(uiFonts.smallFont);
                ImVec2 ts = ImGui::CalcTextSize("Активен");
                dl->AddText(ImVec2(pillPos.x + (ui::S(104) - ts.x) * 0.5f,
                                   pillPos.y + (ui::S(32) - ts.y) * 0.5f),
                            colAccent, "Активен");
                ImGui::PopFont();
                ImGui::Dummy(ImVec2(ui::S(104), ui::S(32)));
            } else if (ui::GhostButton("Выбрать", ImVec2(ui::S(104), ui::S(32)), true, ui::IconUser)) {
                cfg.activeAccount = (int)i;
                cfg.Save();
                ToastMsg("Аккаунт выбран: " + account.name, 1);
            }
            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH));
            ImGui::Dummy(ImVec2(rowW, ui::S(8)));
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    ui::EndCard();
    ImGui::EndGroup();

    ImGui::SameLine(0, gap);

    // ------------------------------------------------------------ register
    ImGui::BeginGroup();
    ui::BeginCard("##account_register", ImVec2(rightW, ui::S(280)));
    {
        ImGui::PushFont(uiFonts.h3);
        ImGui::TextUnformatted("Новый оффлайн-аккаунт");
        ImGui::PopFont();
        TextMutedWrapped("Введите никнейм — лаунчер создаст профиль с оффлайн-UUID и сразу выберет его.");

        ImGui::Dummy(ImVec2(0, ui::S(10)));
        ImGui::PushFont(uiFonts.smallFont);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
        ImGui::TextUnformatted("Никнейм");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##nickname", "3–16 символов, буквы, цифры и _", nickname_,
                                 sizeof(nickname_));
        std::string nick = Trim(nickname_);
        bool valid = ValidNickname(nick);
        ImGui::Dummy(ImVec2(0, ui::S(4)));
        if (!nick.empty() && !valid) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colRed));
            ImGui::PushFont(uiFonts.smallFont);
            ImGui::TextUnformatted("Никнейм должен быть от 3 до 16 символов: буквы, цифры и _");
            ImGui::PopFont();
            ImGui::PopStyleColor();
        } else {
            ImGui::PushFont(uiFonts.smallFont);
            TextMuted("Регистрация не требует пароля и интернета");
            ImGui::PopFont();
        }
        ImGui::Dummy(ImVec2(0, ui::S(8)));
        if (ui::PrimaryButton("Создать аккаунт",
                              ImVec2(ImGui::GetContentRegionAvail().x, ui::S(42)), valid,
                              ui::IconPlus)) {
            int index = cfg.AddOfflineAccount(nick);
            cfg.Save();
            ToastMsg("Аккаунт создан: " + cfg.accounts[index].name, 1);
            nickname_[0] = 0;
        }
    }
    ui::EndCard();
    ImGui::Dummy(ImVec2(0, gap));

    ui::BeginCard("##account_msa",
                  ImVec2(rightW, std::max(ui::S(150),
                                          panelH - ui::S(280) - gap - ImGui::GetStyle().ItemSpacing.y * 2.0f)),
                  0, true);
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddCircleFilled(ImVec2(p.x + ui::S(6), p.y + ui::S(6)), ui::S(6), ui::Alpha(colCyan, 90));
        ImGui::Dummy(ImVec2(ui::S(16), ui::S(12)));
        ImGui::PushFont(uiFonts.h3);
        ImGui::TextUnformatted("Microsoft-аккаунт");
        ImGui::PopFont();

        const Account* active = cfg.Active();
        bool activeMsa = active && active->type == "msa";
        if (activeMsa) {
            ImGui::PushFont(uiFonts.smallFont);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colGreen));
            ImGui::Text("Подключён: %s", active->name.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
        } else {
            TextMutedWrapped("Лицензионная игра, скины и серверы с проверкой Mojang. Для входа "
                             "нужен Client ID приложения Azure.");
        }
        ImGui::Dummy(ImVec2(0, ui::S(6)));

        int state = msa_.state.load();
        std::string userCode;
        std::string status;
        std::string verifyUri;
        {
            std::lock_guard lock(msa_.mutex);
            userCode = msa_.userCode;
            status = msa_.status;
            verifyUri = msa_.verificationUri;
        }

        float innerW = ImGui::GetContentRegionAvail().x;
        if (state == 1 || state == 2) {
            ImGui::PushFont(uiFonts.smallFont);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
            ImGui::TextWrapped("%s", status.empty() ? "Подключаемся к Microsoft..." : status.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, ui::S(6)));

            if (!userCode.empty()) {
                ImVec2 boxMin = ImGui::GetCursorScreenPos();
                ImVec2 boxMax(boxMin.x + innerW, boxMin.y + ui::S(64));
                dl->AddRectFilled(boxMin, boxMax, ui::Alpha(colCyan, 20), ui::S(12));
                dl->AddRect(boxMin, boxMax, ui::Alpha(colCyan, 90), ui::S(12), 0, 1.0f);
                ImGui::PushFont(uiFonts.mono);
                ImVec2 ts = ImGui::CalcTextSize(userCode.c_str());
                dl->AddText(ImVec2((boxMin.x + boxMax.x - ts.x) * 0.5f,
                                   (boxMin.y + boxMax.y - ts.y) * 0.5f),
                            colCyan, userCode.c_str());
                ImGui::PopFont();
                ImGui::Dummy(ImVec2(innerW, ui::S(64)));
                ImGui::PushFont(uiFonts.tiny);
                TextMuted("Введите этот код на странице входа Microsoft");
                ImGui::PopFont();
                ImGui::Dummy(ImVec2(0, ui::S(6)));

                float halfW = (innerW - ui::S(8)) * 0.5f;
                if (ui::SoftButton("Открыть страницу",
                                   ImVec2(halfW, ui::S(36)), true, ui::IconGlobe)) {
                    ui::OpenUrl(verifyUri.empty() ? "https://www.microsoft.com/link" : verifyUri);
                }
                ImGui::SameLine(0, ui::S(8));
                if (ui::GhostButton("Копировать код", ImVec2(halfW, ui::S(36)))) {
                    ImGui::SetClipboardText(userCode.c_str());
                    ToastMsg("Код скопирован", 1);
                }
                ImGui::Dummy(ImVec2(0, ui::S(6)));
            }

            if (ui::GhostButton("Отменить вход", ImVec2(innerW, ui::S(34)))) {
                CancelMicrosoftFlow();
            }
        } else {
        ImGui::PushFont(uiFonts.smallFont);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
        ImGui::TextUnformatted("Client ID приложения Azure");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::SetNextItemWidth(-1);
        bool edited = ImGui::InputTextWithHint("##msa_client_id", "00000000-0000-0000-0000-000000000000",
                                               msaClientId_, sizeof(msaClientId_));
        (void)edited;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            cfg.s.msaClientId = Trim(msaClientId_);
            cfg.Save();
            ToastMsg("Client ID сохранён", 1);
        }

        std::string problem = MsaClientIdLooksValid(Trim(msaClientId_));
        bool clientValid = problem.empty();
        ImGui::PushFont(uiFonts.tiny);
        if (!clientValid && msaClientId_[0] != 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colRed));
            ImGui::TextWrapped("%s", problem.c_str());
            ImGui::PopStyleColor();
        } else {
            TextMutedWrapped("Client ID не секрет — он хранится в настройках лаунчера.");
        }
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, ui::S(6)));

        if (ui::PrimaryButton("Войти через Microsoft", ImVec2(innerW, ui::S(42)), clientValid,
                              ui::IconGlobe)) {
            cfg.s.msaClientId = Trim(msaClientId_);
            cfg.Save();
            StartMicrosoftLogin();
        }
        if (activeMsa) {
            ImGui::Dummy(ImVec2(0, ui::S(6)));
            if (ui::GhostButton("Обновить сессию Microsoft", ImVec2(innerW, ui::S(34)))) {
                cfg.s.msaClientId = Trim(msaClientId_);
                cfg.Save();
                StartMicrosoftRefresh(cfg.activeAccount, false, "", 0);
            }
        }

        ImGui::Dummy(ImVec2(0, ui::S(8)));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, ui::S(6)));

        if (!clientValid || msaClientEditing_) {
            ImGui::PushFont(uiFonts.semibold);
            ImGui::TextUnformatted("Как получить Client ID");
            ImGui::PopFont();
            ImGui::PushFont(uiFonts.tiny);
            TextMutedWrapped("1. Откройте Azure Portal → Microsoft Entra ID → App registrations.\n"
                             "2. New registration, тип аккаунтов — personal Microsoft accounts.\n"
                             "3. Authentication → Allow public client flows → Yes.\n"
                             "4. Скопируйте Application (client) ID и вставьте выше.");
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, ui::S(6)));
            float halfW = (innerW - ui::S(8)) * 0.5f;
            if (ui::SoftButton("Azure Portal", ImVec2(halfW, ui::S(32)), true, ui::IconGlobe)) {
                ui::OpenUrl("https://portal.azure.com/#view/Microsoft_AAD_RegisteredApps/ApplicationsListBlade");
            }
            ImGui::SameLine(0, ui::S(8));
            if (ui::GhostButton("Одобрение Mojang", ImVec2(halfW, ui::S(32)))) {
                ui::OpenUrl("https://aka.ms/mce-reviewappid");
            }
            ImGui::Dummy(ImVec2(0, ui::S(4)));
            ImGui::PushFont(uiFonts.tiny);
            TextMutedWrapped("Minecraft принимает вход только от приложений, одобренных Mojang. "
                             "Заявка отправляется по ссылке «Одобрение Mojang».");
            ImGui::PopFont();
        } else {
            if (ui::SoftButton("Инструкция по Client ID", ImVec2(innerW, ui::S(32)), true,
                               ui::IconShield)) {
                msaClientEditing_ = true;
            }
        }
        }
    }
    ui::EndCard();
    ImGui::EndGroup();
}
