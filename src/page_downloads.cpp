#include "app.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

ImU32 TaskStateColor(int state) {
    switch (state) {
        case (int)TaskState::Done: return colGreen;
        case (int)TaskState::Failed: return colRed;
        case (int)TaskState::Cancelled: return colYellow;
        default: return colAccent;
    }
}

const char* TaskStateText(int state) {
    switch (state) {
        case (int)TaskState::Done: return "Завершено";
        case (int)TaskState::Failed: return "Ошибка";
        case (int)TaskState::Cancelled: return "Отменено";
        default: return "Выполняется";
    }
}

} // namespace

void App::DrawDownloads() {
    PageHeader("Загрузки", "Активные задачи, прогресс и журналы установки");

    float availW = ImGui::GetContentRegionAvail().x;
    int activeCount = tasks.ActiveCount();

    ImGui::PushFont(uiFonts.smallFont);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
    std::string activeText = "Активных задач: " + std::to_string(activeCount);
    ImGui::TextUnformatted(activeText.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::SetCursorPosX(availW - ui::S(216));
    if (ui::GhostButton("Очистить завершённые", ImVec2(ui::S(216), ui::S(34)), true, ui::IconTrash)) {
        tasks.ClearFinished();
        expandedTasks_.clear();
        ToastMsg("История очищена");
    }
    ImGui::Dummy(ImVec2(0, ui::S(12)));

    auto snapshot = tasks.Snapshot();
    if (snapshot.empty()) {
        ui::BeginCard("##downloads_empty", ImVec2(availW, ui::S(220)));
        ImGui::Dummy(ImVec2(0, ui::S(24)));
        ImGui::PushFont(uiFonts.h2);
        ImGui::TextUnformatted("Загрузок пока нет");
        ImGui::PopFont();
        TextMutedWrapped("Установите версию игры или скачайте мод — прогресс появится здесь.");
        ImGui::Dummy(ImVec2(0, ui::S(12)));
        if (ui::PrimaryButton("К списку версий", ImVec2(ui::S(200), ui::S(40)), true, ui::IconCube)) {
            SetPage(Page::Versions);
        }
        ui::EndCard();
        return;
    }

    for (auto it = snapshot.rbegin(); it != snapshot.rend(); ++it) {
        const std::shared_ptr<Task>& task = *it;
        int state = task->state.load();
        bool expanded = expandedTasks_.count(task->id) > 0;
        float cardH = expanded ? ui::S(304) : ui::S(136);
        ImGui::PushID(task->id);
        ui::BeginCard("##task_card", ImVec2(availW, cardH));
        {
            float innerW = ImGui::GetContentRegionAvail().x;
            ImGui::PushFont(uiFonts.h3);
            std::string title = ui::Ellipsize(task->title, innerW - ui::S(150), uiFonts.h3);
            ImGui::TextUnformatted(title.c_str());
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::PushFont(uiFonts.tiny);
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImGui::ColorConvertU32ToFloat4(TaskStateColor(state)));
            ImGui::TextUnformatted(TaskStateText(state));
            ImGui::PopStyleColor();
            ImGui::PopFont();

            ImGui::Dummy(ImVec2(0, ui::S(2)));
            float percent = task->progress.load();
            char overlay[32];
            snprintf(overlay, sizeof(overlay), "%.0f%%", percent * 100.0f);
            ui::ProgressBar(percent, ImVec2(innerW, ui::S(8)), TaskStateColor(state), overlay);

            ImGui::Dummy(ImVec2(0, ui::S(2)));
            float journalW = task->IsRunning() ? ui::S(288) : ui::S(150);
            std::string status = task->Status();
            if (status.empty()) status = TaskStateText(state);
            ImGui::PushFont(uiFonts.smallFont);
            float statusH = ImGui::GetTextLineHeight();
            float statusY = ImGui::GetCursorPosY();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
            ImGui::TextUnformatted(
                ui::Ellipsize(status, innerW - journalW - ui::S(24), uiFonts.smallFont).c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();

            ImGui::SameLine(ui::S(20) + innerW - journalW);
            ImGui::SetCursorPosY(statusY + (statusH - ui::S(30)) * 0.5f);
            if (task->IsRunning()) {
                if (ui::GhostButton("Отменить", ImVec2(ui::S(130), ui::S(30)), true, ui::IconClose)) {
                    task->cancel.store(true);
                    ToastMsg("Отмена задачи...");
                }
                ImGui::SameLine(0, ui::S(8));
            }
            if (ui::GhostButton(expanded ? "Скрыть журнал" : "Журнал",
                                ImVec2(ui::S(150), ui::S(30)), true, ui::IconGrid)) {
                if (expanded) expandedTasks_.erase(task->id);
                else expandedTasks_.insert(task->id);
            }

            if (expanded) {
                ImGui::Dummy(ImVec2(0, ui::S(6)));
                ImGui::BeginChild("##task_log", ImVec2(0, 0), ImGuiChildFlags_None,
                                  ImGuiWindowFlags_AlwaysVerticalScrollbar);
                ImGui::PushFont(uiFonts.mono);
                auto logs = task->Logs();
                if (logs.empty()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colFaint));
                    ImGui::TextUnformatted("Журнал пуст.");
                    ImGui::PopStyleColor();
                }
                for (const auto& line : logs) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
                    ImGui::TextUnformatted(line.c_str());
                    ImGui::PopStyleColor();
                }
                ImGui::SetScrollHereY(1.0f);
                ImGui::PopFont();
                ImGui::EndChild();
            }
        }
        ui::EndCard();
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, ui::S(10)));
    }
}
