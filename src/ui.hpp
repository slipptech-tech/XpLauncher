#pragma once

#include "imgui.h"

#include <string>

struct UiFonts {
    ImFont* tiny = nullptr;
    ImFont* smallFont = nullptr;
    ImFont* body = nullptr;
    ImFont* semibold = nullptr;
    ImFont* h3 = nullptr;
    ImFont* h2 = nullptr;
    ImFont* h1 = nullptr;
    ImFont* logo = nullptr;
    ImFont* mono = nullptr;
    float scale = 1.0f;
};

extern UiFonts uiFonts;

extern ImU32 colAccent;
extern ImU32 colAccentSoft;
extern ImU32 colCyan;
extern ImU32 colText;
extern ImU32 colMuted;
extern ImU32 colFaint;
extern ImU32 colCard;
extern ImU32 colCard2;
extern ImU32 colBorder;
extern ImU32 colBg0;
extern ImU32 colBg1;
extern ImU32 colGreen;
extern ImU32 colRed;
extern ImU32 colYellow;

namespace ui {

enum IconType {
    IconNone = -1,
    IconHome,
    IconCube,
    IconGrid,
    IconUser,
    IconDownload,
    IconSettings,
    IconPlay,
    IconSearch,
    IconClose,
    IconMin,
    IconMax,
    IconRestore,
    IconCheck,
    IconTrash,
    IconFolder,
    IconRefresh,
    IconPlus,
    IconArrowRight,
    IconLogout,
    IconWarn,
    IconMod,
    IconGlobe,
    IconProfile,
    IconUsers,
    IconClock,
    IconChart,
    IconServer,
    IconShield,
    IconConsole,
    IconSignal,
    IconShirt,
    IconEye,
    IconEyeOff
};

float S(float v);
ImVec2 V(float x, float y);
ImU32 Hex(const char* hex);
ImU32 Lighten(ImU32 color, float amount);
ImU32 Alpha(ImU32 color, int alpha);

void LoadFonts(float scale);
void ApplyStyle();
void SetAccent(const std::string& hex);
std::string Ellipsize(const std::string& text, float maxWidth, ImFont* font);
std::string FormatSize(long long bytes);
std::string FormatCount(long long count);
std::string FormatDate(const std::string& iso);

void Icon(ImDrawList* dl, IconType type, ImVec2 center, float size, ImU32 color, float thickness = 1.6f);
void DrawBackdrop(ImDrawList* dl, ImVec2 size, float alpha = 1.0f,
                  ImVec2 origin = ImVec2(0, 0));

void SectionLabel(const char* text);
void Badge(const char* text, ImU32 color);
void BadgeAtCursor(ImVec2 pos, const char* text, ImU32 color);

bool PrimaryButton(const char* label, ImVec2 size, bool enabled = true, IconType icon = IconPlay,
                   bool drawGlow = true);
bool GhostButton(const char* label, ImVec2 size, bool enabled = true, IconType icon = IconNone);
bool SoftButton(const char* label, ImVec2 size, bool enabled = true, IconType icon = IconNone);
bool IconButton(const char* id, IconType icon, ImVec2 size, bool active = false,
                ImU32 color = 0, const char* tooltip = nullptr);
bool NavItem(const char* id, const char* label, IconType icon, bool active, float width);

void BeginCard(const char* id, ImVec2 size, ImU32 bg = 0, bool scrollable = false);
void EndCard();

bool Toggle(const char* label, bool* value);
bool Segmented(const char* id, const char* const* items, int count, int* selected, float width = 0.0f);
void ProgressBar(float fraction, ImVec2 size, ImU32 color, const char* overlay = nullptr);
bool SearchInput(const char* id, char* buffer, int bufferSize, const char* hint, float width);
bool BrowseFolder(const std::string& title, std::string& outPath);
void OpenPath(const std::string& path);
void OpenUrl(const std::string& url);
void Tooltip(const char* text);
void Spinner(ImVec2 center, float radius, ImU32 color, float speed = 4.0f);
bool CheckboxStyled(const char* label, bool* value);

} // namespace ui
