#include "ui.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>

#ifndef IM_PI
#define IM_PI 3.14159265358979323846f
#endif

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core.hpp"

UiFonts uiFonts;

ImU32 colAccent = 0;
ImU32 colAccentSoft = 0;
ImU32 colCyan = 0;
ImU32 colText = 0;
ImU32 colMuted = 0;
ImU32 colFaint = 0;
ImU32 colCard = 0;
ImU32 colCard2 = 0;
ImU32 colBorder = 0;
ImU32 colBg0 = 0;
ImU32 colBg1 = 0;
ImU32 colGreen = 0;
ImU32 colRed = 0;
ImU32 colYellow = 0;

namespace ui {

float S(float v) { return v * uiFonts.scale; }

ImVec2 V(float x, float y) { return ImVec2(x, y); }

ImU32 Hex(const char* hex) {
    if (!hex) return IM_COL32(255, 255, 255, 255);
    std::string s = hex;
    if (!s.empty() && s[0] == '#') s = s.substr(1);
    unsigned int value = 0;
    if (s.size() == 6) value = (unsigned int)strtoul(s.c_str(), nullptr, 16) | 0xFF000000u;
    else if (s.size() == 8) value = (unsigned int)strtoul(s.c_str(), nullptr, 16);
    else return IM_COL32(255, 255, 255, 255);
    int r = (int)((value >> 16) & 0xFF);
    int g = (int)((value >> 8) & 0xFF);
    int b = (int)(value & 0xFF);
    int a = (int)((value >> 24) & 0xFF);
    return IM_COL32(r, g, b, a);
}

ImU32 Alpha(ImU32 color, int alpha) {
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
    c.w = std::clamp(alpha / 255.0f, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(c);
}

ImU32 Lighten(ImU32 color, float amount) {
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(color);
    if (amount >= 0) {
        c.x += (1.0f - c.x) * amount;
        c.y += (1.0f - c.y) * amount;
        c.z += (1.0f - c.z) * amount;
    } else {
        c.x *= (1.0f + amount);
        c.y *= (1.0f + amount);
        c.z *= (1.0f + amount);
    }
    return ImGui::ColorConvertFloat4ToU32(c);
}

namespace {

ImU32 TextOn(ImU32 background) {
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(background);
    float luma = 0.299f * c.x + 0.587f * c.y + 0.114f * c.z;
    return luma > 0.62f ? IM_COL32(14, 16, 24, 255) : IM_COL32(250, 251, 255, 255);
}

ImFont* LoadFontFile(const char* path, float pixels, bool mono = false) {
    ImGuiIO& io = ImGui::GetIO();
    if (!FileExists(path)) return nullptr;
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 1;
    config.PixelSnapH = true;
    if (mono) config.GlyphOffset.y = 0.5f;
    ImFont* font = io.Fonts->AddFontFromFileTTF(path, pixels, &config,
                                                io.Fonts->GetGlyphRangesCyrillic());
    return font;
}

ImVec2 IconPos(ImVec2 center, float size, float x, float y) {
    return ImVec2(center.x + x * size, center.y + y * size);
}

} // namespace

void LoadFonts(float scale) {
    uiFonts = UiFonts{};
    uiFonts.scale = scale;
    ImGuiIO& io = ImGui::GetIO();

    const char* regular = "C:/Windows/Fonts/segoeui.ttf";
    const char* semibold = "C:/Windows/Fonts/seguisb.ttf";
    const char* bold = "C:/Windows/Fonts/segoeuib.ttf";
    const char* mono = "C:/Windows/Fonts/consola.ttf";

    uiFonts.tiny = LoadFontFile(regular, 11.5f * scale);
    uiFonts.smallFont = LoadFontFile(regular, 13.0f * scale);
    uiFonts.body = LoadFontFile(regular, 15.0f * scale);
    uiFonts.semibold = LoadFontFile(semibold, 15.0f * scale);
    uiFonts.h3 = LoadFontFile(semibold, 18.0f * scale);
    uiFonts.h2 = LoadFontFile(bold, 22.0f * scale);
    uiFonts.h1 = LoadFontFile(bold, 30.0f * scale);
    uiFonts.logo = LoadFontFile(semibold, 18.0f * scale);
    uiFonts.mono = LoadFontFile(mono, 13.0f * scale, true);

    if (!uiFonts.body) {
        ImFont* fallback = io.Fonts->AddFontDefault();
        uiFonts.tiny = uiFonts.smallFont = uiFonts.body = uiFonts.semibold = uiFonts.h3 = uiFonts.h2 =
            uiFonts.h1 = uiFonts.logo = uiFonts.mono = fallback;
    }
    if (!uiFonts.tiny) uiFonts.tiny = uiFonts.body;
    if (!uiFonts.smallFont) uiFonts.smallFont = uiFonts.body;
    if (!uiFonts.semibold) uiFonts.semibold = uiFonts.body;
    if (!uiFonts.h3) uiFonts.h3 = uiFonts.semibold;
    if (!uiFonts.h2) uiFonts.h2 = uiFonts.h3;
    if (!uiFonts.h1) uiFonts.h1 = uiFonts.h2;
    if (!uiFonts.logo) uiFonts.logo = uiFonts.semibold;
    if (!uiFonts.mono) uiFonts.mono = uiFonts.body;
    io.FontDefault = uiFonts.body;
}

void ApplyStyle() {
    colText = Hex("#F2F4FF");
    colMuted = Hex("#98A2C0");
    colFaint = Hex("#636C89");
    colCard = Hex("#141726");
    colCard2 = Hex("#1C2032");
    colBorder = Hex("#272D42");
    colBg0 = Hex("#0A0B12");
    colBg1 = Hex("#0D1120");
    colGreen = Hex("#44DDA0");
    colRed = Hex("#FF5D7A");
    colYellow = Hex("#FFC857");
    colCyan = Hex("#3AD6F0");
    SetAccent("#7C5CFF");

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 14.0f;
    style.FrameRounding = 9.0f;
    style.PopupRounding = 14.0f;
    style.SelectableRounding = 8.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 7.0f;
    style.TabRounding = 9.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.WindowPadding = ImVec2(12, 10);
    style.FramePadding = ImVec2(13, 9);
    style.ItemSpacing = ImVec2(9, 9);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.ScrollbarSize = 9;
    style.GrabMinSize = 22;
    style.ScrollbarPadding = 4;
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextPadding = ImVec2(16, 8);
    style.DisplaySafeAreaPadding = ImVec2(0, 0);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(colText);
    colors[ImGuiCol_TextDisabled] = ImGui::ColorConvertU32ToFloat4(colFaint);
    colors[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(Hex("#0B0D16"));
    colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_PopupBg] = ImGui::ColorConvertU32ToFloat4(Hex("#181C2B"));
    colors[ImGuiCol_Border] = ImGui::ColorConvertU32ToFloat4(colBorder);
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg] = ImGui::ColorConvertU32ToFloat4(Hex("#1A1E2E"));
    colors[ImGuiCol_FrameBgHovered] = ImGui::ColorConvertU32ToFloat4(Hex("#22273B"));
    colors[ImGuiCol_FrameBgActive] = ImGui::ColorConvertU32ToFloat4(Hex("#2A3050"));
    colors[ImGuiCol_TitleBg] = ImGui::ColorConvertU32ToFloat4(Hex("#0B0D16"));
    colors[ImGuiCol_TitleBgActive] = ImGui::ColorConvertU32ToFloat4(Hex("#0B0D16"));
    colors[ImGuiCol_TitleBgCollapsed] = ImGui::ColorConvertU32ToFloat4(Hex("#0B0D16"));
    colors[ImGuiCol_MenuBarBg] = ImGui::ColorConvertU32ToFloat4(Hex("#121522"));
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ScrollbarGrab] = ImGui::ColorConvertU32ToFloat4(Hex("#3B4360"));
    colors[ImGuiCol_ScrollbarGrabHovered] =
        ImGui::ColorConvertU32ToFloat4(Hex("#4C5678"));
    colors[ImGuiCol_ScrollbarGrabActive] = ImGui::ColorConvertU32ToFloat4(Lighten(colAccent, 0.1f));
    colors[ImGuiCol_CheckMark] = ImGui::ColorConvertU32ToFloat4(colAccent);
    colors[ImGuiCol_SliderGrab] = ImGui::ColorConvertU32ToFloat4(colAccent);
    colors[ImGuiCol_SliderGrabActive] = ImGui::ColorConvertU32ToFloat4(Lighten(colAccent, 0.18f));
    colors[ImGuiCol_Button] = ImGui::ColorConvertU32ToFloat4(Hex("#1C2032"));
    colors[ImGuiCol_ButtonHovered] = ImGui::ColorConvertU32ToFloat4(Hex("#252B42"));
    colors[ImGuiCol_ButtonActive] = ImGui::ColorConvertU32ToFloat4(Hex("#2C3350"));
    colors[ImGuiCol_Header] = ImGui::ColorConvertU32ToFloat4(Alpha(colAccent, 38));
    colors[ImGuiCol_HeaderHovered] = ImGui::ColorConvertU32ToFloat4(Hex("#1F2437"));
    colors[ImGuiCol_HeaderActive] = ImGui::ColorConvertU32ToFloat4(Hex("#252B42"));
    colors[ImGuiCol_Separator] = ImGui::ColorConvertU32ToFloat4(colBorder);
    colors[ImGuiCol_SeparatorHovered] = ImGui::ColorConvertU32ToFloat4(Alpha(colAccent, 130));
    colors[ImGuiCol_SeparatorActive] = ImGui::ColorConvertU32ToFloat4(colAccent);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ResizeGripHovered] = ImGui::ColorConvertU32ToFloat4(Alpha(colAccent, 120));
    colors[ImGuiCol_ResizeGripActive] = ImGui::ColorConvertU32ToFloat4(colAccent);
    colors[ImGuiCol_Tab] = ImGui::ColorConvertU32ToFloat4(Hex("#151928"));
    colors[ImGuiCol_TabHovered] = ImGui::ColorConvertU32ToFloat4(Hex("#1F2437"));
    colors[ImGuiCol_TabSelected] = ImGui::ColorConvertU32ToFloat4(Alpha(colAccent, 45));
    colors[ImGuiCol_PlotLines] = ImGui::ColorConvertU32ToFloat4(colAccent);
    colors[ImGuiCol_PlotLinesHovered] = ImGui::ColorConvertU32ToFloat4(Lighten(colAccent, 0.2f));
    colors[ImGuiCol_PlotHistogram] = ImGui::ColorConvertU32ToFloat4(colAccent);
    colors[ImGuiCol_PlotHistogramHovered] = ImGui::ColorConvertU32ToFloat4(Lighten(colAccent, 0.2f));
    colors[ImGuiCol_TableHeaderBg] = ImGui::ColorConvertU32ToFloat4(Hex("#171B2A"));
    colors[ImGuiCol_TableBorderStrong] = ImGui::ColorConvertU32ToFloat4(colBorder);
    colors[ImGuiCol_TableBorderLight] = ImGui::ColorConvertU32ToFloat4(Hex("#1E2335"));
    colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.012f);
    colors[ImGuiCol_TextSelectedBg] = ImGui::ColorConvertU32ToFloat4(Alpha(colAccent, 90));
    colors[ImGuiCol_DragDropTarget] = ImGui::ColorConvertU32ToFloat4(colAccent);
    colors[ImGuiCol_NavCursor] = ImGui::ColorConvertU32ToFloat4(colAccent);
    colors[ImGuiCol_NavWindowingHighlight] = ImGui::ColorConvertU32ToFloat4(Alpha(colAccent, 180));
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.35f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.025f, 0.05f, 0.62f);

    style.ScaleAllSizes(uiFonts.scale);
}

void SetAccent(const std::string& hex) {
    colAccent = Hex(hex.empty() ? "#7C5CFF" : hex.c_str());
    colAccentSoft = Alpha(colAccent, 38);
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_CheckMark] = ImGui::ColorConvertU32ToFloat4(colAccent);
    style.Colors[ImGuiCol_SliderGrab] = ImGui::ColorConvertU32ToFloat4(colAccent);
    style.Colors[ImGuiCol_SliderGrabActive] = ImGui::ColorConvertU32ToFloat4(Lighten(colAccent, 0.18f));
    style.Colors[ImGuiCol_TextLink] = ImGui::ColorConvertU32ToFloat4(colAccent);
    style.Colors[ImGuiCol_NavCursor] = ImGui::ColorConvertU32ToFloat4(colAccent);
    style.Colors[ImGuiCol_Header] = ImGui::ColorConvertU32ToFloat4(Alpha(colAccent, 38));
}

std::string Ellipsize(const std::string& text, float maxWidth, ImFont* font) {
    if (!font || maxWidth <= 0) return text;
    if (font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0, text.c_str()).x <= maxWidth) return text;
    const char* ellipsis = "...";
    float ellipsisWidth = font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0, ellipsis).x;
    std::string result;
    std::string candidate;
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = (unsigned char)text[i];
        size_t length = 1;
        if ((c & 0x80) == 0) length = 1;
        else if ((c & 0xE0) == 0xC0) length = 2;
        else if ((c & 0xF0) == 0xE0) length = 3;
        else if ((c & 0xF8) == 0xF0) length = 4;
        candidate = result + text.substr(i, length);
        if (font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0, candidate.c_str()).x + ellipsisWidth >
            maxWidth) {
            break;
        }
        result = candidate;
        i += length;
    }
    return result + ellipsis;
}

std::string FormatSize(long long bytes) {
    const char* units[] = {"Б", "КБ", "МБ", "ГБ"};
    double value = (double)bytes;
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    char buffer[32];
    snprintf(buffer, sizeof(buffer), unit == 0 ? "%.0f %s" : "%.1f %s", value, units[unit]);
    return buffer;
}

std::string FormatCount(long long count) {
    char buffer[32];
    if (count >= 1000000)
        snprintf(buffer, sizeof(buffer), "%.1fM", count / 1000000.0);
    else if (count >= 1000)
        snprintf(buffer, sizeof(buffer), "%.1fK", count / 1000.0);
    else
        snprintf(buffer, sizeof(buffer), "%lld", count);
    return buffer;
}

std::string FormatDate(const std::string& iso) {
    if (iso.size() >= 10 && iso[4] == '-' && iso[7] == '-') {
        return iso.substr(8, 2) + "." + iso.substr(5, 2) + "." + iso.substr(0, 4);
    }
    return iso;
}

void Icon(ImDrawList* dl, IconType type, ImVec2 center, float size, ImU32 color, float thickness) {
    if (!dl) return;
    const float t = std::max(1.0f, thickness * uiFonts.scale);
    auto P = [&](float x, float y) { return IconPos(center, size, x, y); };
    auto line = [&](float x1, float y1, float x2, float y2) {
        dl->AddLine(P(x1, y1), P(x2, y2), color, t);
    };

    switch (type) {
        case IconHome: {
            ImVec2 roof[] = {P(-0.40f, 0.02f), P(0, -0.38f), P(0.40f, 0.02f)};
            dl->AddPolyline(roof, 3, color, 0, t);
            dl->AddRect(P(-0.28f, 0.02f), P(0.28f, 0.38f), color, S(2), 0, t);
            dl->AddLine(P(0.0f, 0.38f), P(0.0f, 0.10f), color, t);
            break;
        }
        case IconCube: {
            ImVec2 top[] = {P(0, -0.40f), P(0.38f, -0.19f), P(0, 0.02f), P(-0.38f, -0.19f), P(0, -0.40f)};
            dl->AddPolyline(top, 5, color, 0, t);
            line(0, 0.02f, 0, 0.40f);
            line(-0.38f, -0.19f, -0.38f, 0.20f);
            line(0.38f, -0.19f, 0.38f, 0.20f);
            line(-0.38f, 0.20f, 0, 0.40f);
            line(0.38f, 0.20f, 0, 0.40f);
            line(-0.38f, -0.19f, 0, 0.02f);
            line(0.38f, -0.19f, 0, 0.02f);
            break;
        }
        case IconGrid: {
            float r = S(4);
            dl->AddRectFilled(P(-0.38f, -0.38f), P(-0.06f, -0.06f), color, r);
            dl->AddRectFilled(P(0.06f, -0.38f), P(0.38f, -0.06f), color, r);
            dl->AddRectFilled(P(-0.38f, 0.06f), P(-0.06f, 0.38f), color, r);
            dl->AddRectFilled(P(0.06f, 0.06f), P(0.38f, 0.38f), color, r);
            break;
        }
        case IconUser: {
            dl->AddCircle(P(0, -0.18f), size * 0.17f, color, 0, t);
            dl->PathArcTo(P(0, 0.40f), size * 0.33f, IM_PI, IM_PI * 2.0f, 32);
            dl->PathStroke(color, 0, t);
            break;
        }
        case IconDownload: {
            line(0, -0.38f, 0, 0.10f);
            line(-0.20f, -0.10f, 0, 0.10f);
            line(0.20f, -0.10f, 0, 0.10f);
            ImVec2 tray[] = {P(-0.32f, 0.20f), P(-0.32f, 0.36f), P(0.32f, 0.36f), P(0.32f, 0.20f)};
            dl->AddPolyline(tray, 4, color, 0, t);
            break;
        }
        case IconSettings: {
            dl->AddCircle(P(0, 0), size * 0.16f, color, 0, t);
            for (int i = 0; i < 8; ++i) {
                float a = (float)i * IM_PI / 4.0f;
                line(cosf(a) * 0.27f, sinf(a) * 0.27f, cosf(a) * 0.40f, sinf(a) * 0.40f);
            }
            break;
        }
        case IconPlay: {
            dl->AddTriangleFilled(P(-0.24f, -0.34f), P(-0.24f, 0.34f), P(0.36f, 0), color);
            break;
        }
        case IconSearch: {
            dl->AddCircle(P(-0.06f, -0.06f), size * 0.27f, color, 0, t);
            line(0.15f, 0.15f, 0.38f, 0.38f);
            break;
        }
        case IconClose: {
            line(-0.28f, -0.28f, 0.28f, 0.28f);
            line(0.28f, -0.28f, -0.28f, 0.28f);
            break;
        }
        case IconMin: {
            line(-0.30f, 0.0f, 0.30f, 0.0f);
            break;
        }
        case IconMax: {
            dl->AddRect(P(-0.28f, -0.28f), P(0.28f, 0.28f), color, S(1), 0, t);
            break;
        }
        case IconRestore: {
            dl->AddRect(P(-0.32f, -0.18f), P(0.18f, 0.32f), color, S(1), 0, t);
            dl->AddRect(P(-0.18f, -0.32f), P(0.32f, 0.18f), color, S(1), 0, t);
            break;
        }
        case IconCheck: {
            ImVec2 check[] = {P(-0.36f, 0.02f), P(-0.08f, 0.30f), P(0.38f, -0.28f)};
            dl->AddPolyline(check, 3, color, 0, t);
            break;
        }
        case IconTrash: {
            line(-0.34f, -0.24f, 0.34f, -0.24f);
            line(-0.12f, -0.24f, -0.08f, -0.36f);
            line(0.12f, -0.24f, 0.08f, -0.36f);
            dl->AddRect(P(-0.25f, -0.18f), P(0.25f, 0.34f), color, S(3), 0, t);
            line(-0.10f, -0.08f, -0.08f, 0.24f);
            line(0.10f, -0.08f, 0.08f, 0.24f);
            break;
        }
        case IconFolder: {
            ImVec2 folder[] = {P(-0.38f, -0.28f), P(-0.08f, -0.28f), P(-0.01f, -0.16f),
                               P(0.38f, -0.16f),   P(0.38f, 0.32f),   P(-0.38f, 0.32f),
                               P(-0.38f, -0.28f)};
            dl->AddPolyline(folder, 7, color, 0, t);
            break;
        }
        case IconRefresh: {
            dl->PathArcTo(P(0, 0), size * 0.30f, -0.6f, 4.6f, 32);
            dl->PathStroke(color, 0, t);
            ImVec2 tip = P(cosf(4.6f) * 0.30f, sinf(4.6f) * 0.30f);
            dl->AddTriangleFilled(ImVec2(tip.x + size * 0.08f, tip.y),
                                  ImVec2(tip.x - size * 0.07f, tip.y - size * 0.10f),
                                  ImVec2(tip.x - size * 0.02f, tip.y + size * 0.10f), color);
            break;
        }
        case IconPlus: {
            line(-0.30f, 0.0f, 0.30f, 0.0f);
            line(0.0f, -0.30f, 0.0f, 0.30f);
            break;
        }
        case IconArrowRight: {
            line(-0.20f, 0.0f, 0.28f, 0.0f);
            line(0.06f, -0.20f, 0.28f, 0.0f);
            line(0.06f, 0.20f, 0.28f, 0.0f);
            break;
        }
        case IconLogout: {
            ImVec2 door[] = {P(-0.30f, -0.34f), P(-0.02f, -0.34f), P(-0.02f, 0.34f), P(-0.30f, 0.34f)};
            dl->AddPolyline(door, 4, color, 0, t);
            line(-0.04f, 0.0f, 0.36f, 0.0f);
            line(0.16f, -0.18f, 0.36f, 0.0f);
            line(0.16f, 0.18f, 0.36f, 0.0f);
            break;
        }
        case IconWarn: {
            ImVec2 triangle[] = {P(0, -0.36f), P(0.38f, 0.32f), P(-0.38f, 0.32f), P(0, -0.36f)};
            dl->AddPolyline(triangle, 4, color, 0, t);
            line(0, -0.12f, 0, 0.10f);
            dl->AddCircleFilled(P(0, 0.23f), std::max(1.0f, size * 0.03f), color);
            break;
        }
        case IconMod: {
            ImVec2 body[] = {P(-0.36f, -0.12f), P(-0.36f, 0.34f), P(0.36f, 0.34f), P(0.36f, -0.12f)};
            dl->AddPolyline(body, 4, color, 0, t);
            dl->AddCircle(P(0, -0.14f), size * 0.14f, color, 0, t);
            line(-0.36f, -0.30f, -0.20f, -0.30f);
            line(0.20f, -0.30f, 0.36f, -0.30f);
            break;
        }
        case IconGlobe: {
            dl->AddCircle(P(0, 0), size * 0.34f, color, 0, t);
            dl->AddEllipse(P(0, 0), ImVec2(size * 0.15f, size * 0.34f), color, 0, 0, t);
            line(-0.32f, 0.0f, 0.32f, 0.0f);
            break;
        }
        case IconProfile: {
            dl->AddCircle(P(0, 0), size * 0.38f, color, 0, t);
            dl->AddCircle(P(0, -0.10f), size * 0.11f, color, 0, t);
            dl->PathArcTo(P(0, 0.26f), size * 0.20f, IM_PI, IM_PI * 2.0f, 24);
            dl->PathStroke(color, 0, t);
            break;
        }
        case IconUsers: {
            dl->AddCircle(P(-0.14f, -0.12f), size * 0.12f, color, 0, t);
            dl->PathArcTo(P(-0.14f, 0.34f), size * 0.21f, IM_PI, IM_PI * 2.0f, 24);
            dl->PathStroke(color, 0, t);
            dl->AddCircle(P(0.20f, -0.06f), size * 0.10f, color, 0, t);
            dl->PathArcTo(P(0.20f, 0.34f), size * 0.17f, IM_PI, IM_PI * 2.0f, 24);
            dl->PathStroke(color, 0, t);
            break;
        }
        case IconClock: {
            dl->AddCircle(P(0, 0), size * 0.36f, color, 0, t);
            line(0.0f, 0.0f, 0.0f, -0.20f);
            line(0.0f, 0.0f, 0.16f, 0.06f);
            break;
        }
        case IconChart: {
            dl->AddRectFilled(P(-0.32f, 0.04f), P(-0.16f, 0.34f), color, S(2));
            dl->AddRectFilled(P(-0.08f, -0.14f), P(0.08f, 0.34f), color, S(2));
            dl->AddRectFilled(P(0.16f, -0.30f), P(0.32f, 0.34f), color, S(2));
            break;
        }
        case IconServer: {
            dl->AddRect(P(-0.36f, -0.36f), P(0.36f, -0.04f), color, S(3), 0, t);
            dl->AddRect(P(-0.36f, 0.06f), P(0.36f, 0.38f), color, S(3), 0, t);
            dl->AddCircleFilled(P(-0.24f, -0.20f), std::max(1.0f, size * 0.035f), color);
            dl->AddCircleFilled(P(-0.24f, 0.22f), std::max(1.0f, size * 0.035f), color);
            line(-0.10f, -0.20f, 0.24f, -0.20f);
            line(-0.10f, 0.22f, 0.24f, 0.22f);
            break;
        }
        case IconShield: {
            ImVec2 shield[] = {P(0, -0.38f), P(0.32f, -0.24f), P(0.30f, 0.12f), P(0, 0.38f),
                               P(-0.30f, 0.12f), P(-0.32f, -0.24f), P(0, -0.38f)};
            dl->AddPolyline(shield, 7, color, 0, t);
            ImVec2 check[] = {P(-0.14f, 0.0f), P(-0.03f, 0.12f), P(0.16f, -0.12f)};
            dl->AddPolyline(check, 3, color, 0, t);
            break;
        }
        case IconConsole: {
            dl->AddRect(P(-0.38f, -0.30f), P(0.38f, 0.32f), color, S(4), 0, t);
            line(-0.24f, -0.08f, -0.06f, 0.04f);
            line(-0.06f, 0.04f, -0.24f, 0.16f);
            line(0.02f, 0.16f, 0.22f, 0.16f);
            break;
        }
        case IconSignal: {
            dl->PathArcTo(P(0, 0.24f), size * 0.18f, -IM_PI * 0.75f, -IM_PI * 0.25f, 16);
            dl->PathStroke(color, 0, t);
            dl->PathArcTo(P(0, 0.24f), size * 0.32f, -IM_PI * 0.78f, -IM_PI * 0.22f, 16);
            dl->PathStroke(color, 0, t);
            dl->PathArcTo(P(0, 0.24f), size * 0.46f, -IM_PI * 0.80f, -IM_PI * 0.20f, 16);
            dl->PathStroke(color, 0, t);
            dl->AddCircleFilled(P(0, 0.26f), std::max(1.0f, size * 0.05f), color);
            break;
        }
        case IconShirt: {
            ImVec2 shirt[] = {P(-0.36f, -0.26f), P(-0.16f, -0.36f), P(0, -0.24f),
                              P(0.16f, -0.36f),   P(0.36f, -0.26f),   P(0.26f, -0.02f),
                              P(0.18f, -0.06f),   P(0.18f, 0.34f),    P(-0.18f, 0.34f),
                              P(-0.18f, -0.06f),  P(-0.26f, -0.02f),  P(-0.36f, -0.26f)};
            dl->AddPolyline(shirt, 12, color, 0, t);
            break;
        }
        case IconEye:
        case IconEyeOff: {
            dl->AddEllipse(P(0, 0), ImVec2(size * 0.38f, size * 0.24f), color, 0, 0, t);
            dl->AddCircleFilled(P(0, 0), std::max(1.0f, size * 0.11f), color);
            if (type == IconEyeOff) {
                line(-0.32f, -0.32f, 0.32f, 0.32f);
            }
            break;
        }
        default:
            break;
    }
}

void DrawBackdrop(ImDrawList* dl, ImVec2 size, float alpha, ImVec2 origin) {
    if (!dl) return;
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    if (alpha <= 0.0f) return;
    auto fadeColor = [alpha](ImU32 color, int baseAlpha) {
        return Alpha(color, (int)std::lround(baseAlpha * alpha));
    };
    ImVec2 end(origin.x + size.x, origin.y + size.y);
    int bgAlpha = (int)std::lround(255.0f * alpha);
    dl->AddRectFilledMultiColor(origin, end, Alpha(colBg0, bgAlpha), Alpha(colBg0, bgAlpha),
                                Alpha(colBg1, bgAlpha), Alpha(colBg1, bgAlpha));

    struct Glow {
        ImVec2 center;
        float radius;
        ImU32 color;
    };
    Glow glows[] = {
        {ImVec2(size.x * 0.92f, -size.y * 0.18f), size.y * 0.95f, colCyan},
        {ImVec2(size.x * 0.10f, size.y * 1.05f), size.y * 0.9f, colAccent},
        {ImVec2(size.x * 0.62f, size.y * 0.25f), size.y * 0.55f, colAccent},
    };
    for (const auto& glow : glows) {
        ImVec2 center(origin.x + glow.center.x, origin.y + glow.center.y);
        for (int i = 0; i < 16; ++i) {
            float radius = glow.radius * (1.0f - (float)i * 0.055f);
            dl->AddCircleFilled(center, radius, fadeColor(glow.color, 2), 64);
        }
    }

    // Very subtle grid in the background.
    ImU32 grid = fadeColor(IM_COL32(255, 255, 255, 255), 4);
    for (float x = 0; x < size.x; x += S(68))
        dl->AddLine(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x, end.y), grid, 1.0f);
    for (float y = 0; y < size.y; y += S(68))
        dl->AddLine(ImVec2(origin.x, origin.y + y), ImVec2(end.x, origin.y + y), grid, 1.0f);
}

void SectionLabel(const char* text) {
    ImGui::PushFont(uiFonts.smallFont);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colMuted));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void BadgeAtCursor(ImVec2 pos, const char* text, ImU32 color) {
    ImGui::PushFont(uiFonts.tiny);
    ImVec2 textSize = ImGui::CalcTextSize(text);
    float h = S(20);
    float w = textSize.x + S(14);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), Alpha(color, 34), S(6));
    dl->AddRect(pos, ImVec2(pos.x + w, pos.y + h), Alpha(color, 72), S(6), 0, 1.0f);
    dl->AddText(ImVec2(pos.x + S(7), pos.y + (h - textSize.y) * 0.5f), color, text);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(w, h));
}

void Badge(const char* text, ImU32 color) { BadgeAtCursor(ImGui::GetCursorScreenPos(), text, color); }

bool PrimaryButton(const char* label, ImVec2 size, bool enabled, IconType icon, bool drawGlow) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(label, size);
    bool hovered = ImGui::IsItemHovered() && enabled;
    bool active = ImGui::IsItemActive() && enabled;
    bool clicked = ImGui::IsItemClicked() && enabled;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rounding = std::min(S(12), size.y * 0.32f);
    ImU32 base = enabled ? colAccent : Hex("#2A2F45");
    ImU32 fill = active ? Lighten(base, -0.12f) : (hovered ? Lighten(base, 0.10f) : base);

    if (drawGlow && enabled) {
        dl->AddRectFilled(ImVec2(pos.x + S(2), pos.y + S(6)), ImVec2(pos.x + size.x - S(2), pos.y + size.y + S(6)),
                          Alpha(colAccent, 70), rounding);
        dl->AddRectFilled(ImVec2(pos.x + S(6), pos.y + S(10)), ImVec2(pos.x + size.x - S(6), pos.y + size.y + S(12)),
                          Alpha(colAccent, 28), rounding);
    }
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), fill, rounding);
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), Alpha(IM_COL32(255, 255, 255, 255), enabled ? 30 : 15),
                rounding, 0, 1.0f);

    ImU32 textColor = enabled ? TextOn(base) : Alpha(colMuted, 160);
    ImGui::PushFont(uiFonts.semibold);
    ImVec2 textSize = ImGui::CalcTextSize(label);
    float iconSize = S(17);
    float gap = icon == IconNone ? 0.0f : S(10);
    float totalWidth = textSize.x + (icon == IconNone ? 0.0f : iconSize + gap);
    float startX = pos.x + (size.x - totalWidth) * 0.5f;
    float centerY = pos.y + size.y * 0.5f;
    if (icon != IconNone) {
        Icon(dl, icon, ImVec2(startX + iconSize * 0.5f, centerY), iconSize, textColor, 1.8f);
        startX += iconSize + gap;
    }
    dl->AddText(ImVec2(startX, centerY - textSize.y * 0.5f), textColor, label);
    ImGui::PopFont();
    return clicked;
}

bool GhostButton(const char* label, ImVec2 size, bool enabled, IconType icon) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(label, size);
    bool hovered = ImGui::IsItemHovered() && enabled;
    bool active = ImGui::IsItemActive() && enabled;
    bool clicked = ImGui::IsItemClicked() && enabled;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rounding = std::min(S(11), size.y * 0.32f);
    ImU32 border = hovered ? Alpha(colAccent, 160) : colBorder;
    if (hovered) {
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                          active ? Alpha(colAccent, 36) : Alpha(colAccent, 18), rounding);
    }
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), border, rounding, 0, 1.0f);

    ImU32 textColor = enabled ? (hovered ? colText : colMuted) : Alpha(colMuted, 120);
    ImGui::PushFont(uiFonts.semibold);
    ImVec2 textSize = ImGui::CalcTextSize(label);
    float iconSize = S(15);
    float gap = icon == IconNone ? 0.0f : S(8);
    float total = textSize.x + (icon == IconNone ? 0.0f : iconSize + gap);
    float x = pos.x + (size.x - total) * 0.5f;
    float cy = pos.y + size.y * 0.5f;
    if (icon != IconNone) {
        Icon(dl, icon, ImVec2(x + iconSize * 0.5f, cy), iconSize, textColor, 1.6f);
        x += iconSize + gap;
    }
    dl->AddText(ImVec2(x, cy - textSize.y * 0.5f), textColor, label);
    ImGui::PopFont();
    return clicked;
}

bool SoftButton(const char* label, ImVec2 size, bool enabled, IconType icon) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(label, size);
    bool hovered = ImGui::IsItemHovered() && enabled;
    bool active = ImGui::IsItemActive() && enabled;
    bool clicked = ImGui::IsItemClicked() && enabled;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rounding = std::min(S(10), size.y * 0.32f);
    ImU32 bg = active ? Alpha(IM_COL32(255, 255, 255, 255), 26)
                      : (hovered ? Alpha(IM_COL32(255, 255, 255, 255), 18)
                                 : Alpha(IM_COL32(255, 255, 255, 255), 10));
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg, rounding);

    ImU32 textColor = enabled ? colText : Alpha(colMuted, 120);
    ImGui::PushFont(uiFonts.semibold);
    ImVec2 textSize = ImGui::CalcTextSize(label);
    float iconSize = S(15);
    float gap = icon == IconNone ? 0.0f : S(8);
    float total = textSize.x + (icon == IconNone ? 0.0f : iconSize + gap);
    float x = pos.x + (size.x - total) * 0.5f;
    float cy = pos.y + size.y * 0.5f;
    if (icon != IconNone) {
        Icon(dl, icon, ImVec2(x + iconSize * 0.5f, cy), iconSize, textColor, 1.6f);
        x += iconSize + gap;
    }
    dl->AddText(ImVec2(x, cy - textSize.y * 0.5f), textColor, label);
    ImGui::PopFont();
    return clicked;
}

bool IconButton(const char* id, IconType icon, ImVec2 size, bool active, ImU32 color, const char* tooltip) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    bool hovered = ImGui::IsItemHovered();
    bool pressed = ImGui::IsItemActive();
    bool clicked = ImGui::IsItemClicked();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rounding = std::min(S(10), size.y * 0.3f);

    ImU32 bg = active ? Alpha(colAccent, 40)
                      : (pressed ? Alpha(IM_COL32(255, 255, 255, 255), 24)
                                 : (hovered ? Alpha(IM_COL32(255, 255, 255, 255), 14) : 0));
    if (bg) dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg, rounding);
    ImU32 iconColor = color ? color : (active ? colAccent : (hovered ? colText : colMuted));
    Icon(dl, icon, ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f), size.y * 0.48f, iconColor, 1.7f);
    if (tooltip && hovered) {
        ImGui::BeginTooltip();
        ImGui::PushFont(uiFonts.smallFont);
        ImGui::TextUnformatted(tooltip);
        ImGui::PopFont();
        ImGui::EndTooltip();
    }
    return clicked;
}

bool NavItem(const char* id, const char* label, IconType icon, bool active, float width) {
    float height = S(44);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(width, height));
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rounding = S(11);
    ImVec2 min = ImVec2(pos.x, pos.y + S(2));
    ImVec2 max = ImVec2(pos.x + width, pos.y + height - S(2));

    if (active) {
        dl->AddRectFilled(min, max, Alpha(colAccent, 30), rounding);
        dl->AddRectFilled(ImVec2(min.x, min.y + S(11)), ImVec2(min.x + S(3), max.y - S(11)), colAccent,
                          S(2));
    } else if (hovered) {
        dl->AddRectFilled(min, max, Alpha(IM_COL32(255, 255, 255, 255), 9), rounding);
    }

    ImU32 iconColor = active ? colAccent : (hovered ? colText : colMuted);
    ImU32 textColor = active ? colText : (hovered ? colText : colMuted);
    Icon(dl, icon, ImVec2(pos.x + S(24), pos.y + height * 0.5f), S(19), iconColor, 1.7f);
    ImGui::PushFont(active ? uiFonts.semibold : uiFonts.body);
    dl->AddText(ImVec2(pos.x + S(46), pos.y + height * 0.5f - ImGui::GetFontSize() * 0.5f), textColor, label);
    ImGui::PopFont();
    if (active) {
        ImGui::PushFont(uiFonts.tiny);
        dl->AddCircleFilled(ImVec2(pos.x + width - S(18), pos.y + height * 0.5f), S(3), colAccent);
        ImGui::PopFont();
    }
    return clicked;
}

void BeginCard(const char* id, ImVec2 size, ImU32 bg, bool scrollable) {
    if (size.x <= 0) size.x = ImGui::GetContentRegionAvail().x;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rounding = S(16);
    ImU32 background = bg ? bg : colCard;
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), background, rounding);
    dl->AddRectFilledMultiColor(ImVec2(pos.x + 1, pos.y + 1), ImVec2(pos.x + size.x - 1, pos.y + size.y * 0.55f),
                                Alpha(IM_COL32(255, 255, 255, 255), 7), Alpha(IM_COL32(255, 255, 255, 255), 7),
                                IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), colBorder, rounding, 0, 1.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20), S(18)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild(id, size, ImGuiChildFlags_AlwaysUseWindowPadding,
                      scrollable ? ImGuiWindowFlags_None
                                 : (ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse));
}

void EndCard() {
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

bool Toggle(const char* label, bool* value) {
    float height = S(30);
    float width = ImGui::GetContentRegionAvail().x;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(label, ImVec2(width, height));
    bool hovered = ImGui::IsItemHovered();
    bool changed = ImGui::IsItemClicked();
    if (changed) *value = !*value;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::PushFont(uiFonts.body);
    ImU32 textColor = hovered ? colText : colText;
    dl->AddText(ImVec2(pos.x, pos.y + height * 0.5f - ImGui::GetFontSize() * 0.5f), textColor, label);
    ImGui::PopFont();

    float switchW = S(42);
    float switchH = S(23);
    ImVec2 sw0 = ImVec2(pos.x + width - switchW, pos.y + (height - switchH) * 0.5f);
    ImVec2 sw1 = ImVec2(sw0.x + switchW, sw0.y + switchH);
    ImU32 track = *value ? colAccent : Hex("#2A3047");
    dl->AddRectFilled(sw0, sw1, hovered ? Lighten(track, 0.08f) : track, switchH * 0.5f);
    float knobR = switchH * 0.5f - S(3);
    float knobX = *value ? sw1.x - knobR - S(3) : sw0.x + knobR + S(3);
    dl->AddCircleFilled(ImVec2(knobX, (sw0.y + sw1.y) * 0.5f), knobR, IM_COL32(255, 255, 255, 248));
    return changed;
}

bool Segmented(const char* id, const char* const* items, int count, int* selected, float width) {
    if (count <= 0) return false;
    float height = S(38);
    float totalWidth = width > 0 ? width : ImGui::GetContentRegionAvail().x;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + totalWidth, pos.y + height), Hex("#151928"), height * 0.5f);
    dl->AddRect(pos, ImVec2(pos.x + totalWidth, pos.y + height), colBorder, height * 0.5f, 0, 1.0f);

    float segmentWidth = totalWidth / (float)count;
    bool changed = false;
    bool anyHovered = false;
    for (int i = 0; i < count; ++i) {
        ImGui::PushID(i);
        ImGui::SetCursorScreenPos(ImVec2(pos.x + segmentWidth * i, pos.y));
        ImGui::InvisibleButton(id, ImVec2(segmentWidth, height));
        bool hovered = ImGui::IsItemHovered();
        anyHovered |= hovered;
        if (ImGui::IsItemClicked() && *selected != i) {
            *selected = i;
            changed = true;
        }
        if (*selected == i) {
            ImVec2 segMin = ImVec2(pos.x + segmentWidth * i + S(3), pos.y + S(3));
            ImVec2 segMax = ImVec2(segMin.x + segmentWidth - S(6), pos.y + height - S(3));
            dl->AddRectFilled(segMin, segMax, Alpha(colAccent, 42), (height - S(6)) * 0.5f);
            dl->AddRect(segMin, segMax, Alpha(colAccent, 90), (height - S(6)) * 0.5f, 0, 1.0f);
        } else if (hovered) {
            ImVec2 segMin = ImVec2(pos.x + segmentWidth * i + S(3), pos.y + S(3));
            ImVec2 segMax = ImVec2(segMin.x + segmentWidth - S(6), pos.y + height - S(3));
            dl->AddRectFilled(segMin, segMax, Alpha(IM_COL32(255, 255, 255, 255), 8),
                              (height - S(6)) * 0.5f);
        }
        ImGui::PushFont(*selected == i ? uiFonts.semibold : uiFonts.body);
        ImVec2 textSize = ImGui::CalcTextSize(items[i]);
        ImU32 color = *selected == i ? colText : (hovered ? colText : colMuted);
        dl->AddText(ImVec2(pos.x + segmentWidth * i + (segmentWidth - textSize.x) * 0.5f,
                           pos.y + (height - textSize.y) * 0.5f),
                    color, items[i]);
        ImGui::PopFont();
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + height));
    ImGui::Dummy(ImVec2(totalWidth, 0));
    return changed;
}

void ProgressBar(float fraction, ImVec2 size, ImU32 color, const char* overlay) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rounding = size.y * 0.5f;
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), Hex("#23283D"), rounding);
    float clamped = std::clamp(fraction, 0.0f, 1.0f);
    if (clamped > 0.0f) {
        dl->PushClipRect(pos, ImVec2(pos.x + size.x * clamped, pos.y + size.y), true);
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), color, rounding);
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y * 0.55f),
                          Alpha(IM_COL32(255, 255, 255, 255), 35), rounding);
        dl->PopClipRect();
    }
    if (overlay && *overlay) {
        ImGui::PushFont(uiFonts.tiny);
        ImVec2 textSize = ImGui::CalcTextSize(overlay);
        dl->AddText(ImVec2(pos.x + (size.x - textSize.x) * 0.5f, pos.y + (size.y - textSize.y) * 0.5f),
                    colText, overlay);
        ImGui::PopFont();
    }
    ImGui::Dummy(size);
}

bool SearchInput(const char* id, char* buffer, int bufferSize, const char* hint, float width) {
    if (width <= 0) width = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(37), S(9)));
    ImGui::SetNextItemWidth(width);
    bool changed = ImGui::InputTextWithHint(id, hint, buffer, (size_t)bufferSize);
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    ImGui::PopStyleVar();
    ui::Icon(ImGui::GetWindowDrawList(), ui::IconSearch,
             ImVec2(min.x + S(17), (min.y + max.y) * 0.5f), S(15), colMuted, 1.5f);
    return changed;
}

void Tooltip(const char* text) {
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushFont(uiFonts.smallFont);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(colText));
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::EndTooltip();
    }
}

void Spinner(ImVec2 center, float radius, ImU32 color, float speed) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float time = (float)ImGui::GetTime() * speed;
    int segments = 24;
    for (int i = 0; i < segments; ++i) {
        float a0 = time + (float)i / segments * IM_PI * 2.0f;
        float a1 = a0 + 0.10f;
        int alpha = (int)(255.0f * (float)i / segments);
        dl->AddLine(ImVec2(center.x + cosf(a0) * radius, center.y + sinf(a0) * radius),
                    ImVec2(center.x + cosf(a1) * radius, center.y + sinf(a1) * radius), Alpha(color, alpha),
                    std::max(1.5f, radius * 0.14f));
    }
}

bool CheckboxStyled(const char* label, bool* value) {
    float height = S(26);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float box = S(18);
    ImGui::PushID(label);
    ImGui::InvisibleButton("##checkbox", ImVec2(ImGui::GetContentRegionAvail().x, height));
    bool hovered = ImGui::IsItemHovered();
    bool changed = ImGui::IsItemClicked();
    if (changed) *value = !*value;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 boxMin = ImVec2(pos.x, pos.y + (height - box) * 0.5f);
    ImVec2 boxMax = ImVec2(boxMin.x + box, boxMin.y + box);
    ImU32 fill = *value ? colAccent : (hovered ? Hex("#262C42") : Hex("#1C2032"));
    dl->AddRectFilled(boxMin, boxMax, fill, S(5));
    dl->AddRect(boxMin, boxMax, *value ? Alpha(IM_COL32(255, 255, 255, 255), 50) : colBorder, S(5), 0, 1.0f);
    if (*value) {
        Icon(dl, IconCheck, ImVec2((boxMin.x + boxMax.x) * 0.5f, (boxMin.y + boxMax.y) * 0.5f), S(14),
             TextOn(colAccent), 2.0f);
    }
    ImGui::PushFont(uiFonts.body);
    dl->AddText(ImVec2(boxMax.x + S(10), pos.y + (height - ImGui::GetFontSize()) * 0.5f),
                hovered ? colText : colText, label);
    ImGui::PopFont();
    ImGui::PopID();
    return changed;
}

bool BrowseFolder(const std::string& title, std::string& outPath) {
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) return false;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    std::wstring wtitle = Wide(title);
    dialog->SetTitle(wtitle.c_str());
    bool ok = false;
    if (SUCCEEDED(dialog->Show(nullptr))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                outPath = Utf8(path);
                CoTaskMemFree(path);
                ok = true;
            }
            item->Release();
        }
    }
    dialog->Release();
    return ok;
}

void OpenPath(const std::string& path) {
    std::wstring wpath = Wide(path);
    ShellExecuteW(nullptr, L"open", wpath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void OpenUrl(const std::string& url) {
    std::wstring wurl = Wide(url);
    ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

} // namespace ui
