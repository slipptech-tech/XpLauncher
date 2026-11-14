#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>

#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <cwctype>
#include <string>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "app.hpp"
#include "ui.hpp"

static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_context = nullptr;
static IDXGISwapChain* g_swapChain = nullptr;
static ID3D11RenderTargetView* g_renderTarget = nullptr;
static UINT g_width = 0;
static UINT g_height = 0;
static bool g_occluded = false;

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void CreateRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) && backBuffer) {
        g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTarget);
        backBuffer->Release();
    }
}

static void CleanupRenderTarget() {
    if (g_renderTarget) {
        g_renderTarget->Release();
        g_renderTarget = nullptr;
    }
}

static bool CreateDeviceD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createFlags = 0;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags, levels, 2, D3D11_SDK_VERSION, &sd,
        &g_swapChain, &g_device, &featureLevel, &g_context);
    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createFlags, levels,
                                           2, D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device,
                                           &featureLevel, &g_context);
    }
    if (FAILED(hr)) return false;
    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_swapChain) {
        g_swapChain->Release();
        g_swapChain = nullptr;
    }
    if (g_context) {
        g_context->Release();
        g_context = nullptr;
    }
    if (g_device) {
        g_device->Release();
        g_device = nullptr;
    }
}

static LRESULT HandleNcCalcSize(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    if (wParam == TRUE) {
        if (IsZoomed(hwnd) && lParam) {
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            int frameX = GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
            int frameY = GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
            params->rgrc[0].left += frameX;
            params->rgrc[0].right -= frameX;
            params->rgrc[0].top += frameY;
            params->rgrc[0].bottom -= frameY;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, WM_NCCALCSIZE, wParam, lParam);
}

static LRESULT HandleNcHitTest(HWND hwnd, LPARAM lParam) {
    if (IsZoomed(hwnd)) return HTCLIENT;
    POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    RECT rect{};
    GetWindowRect(hwnd, &rect);
    int border = MulDiv(6, (int)GetDpiForWindow(hwnd), 96);
    bool left = pt.x >= rect.left && pt.x < rect.left + border;
    bool right = pt.x < rect.right && pt.x >= rect.right - border;
    bool top = pt.y >= rect.top && pt.y < rect.top + border;
    bool bottom = pt.y < rect.bottom && pt.y >= rect.bottom - border;
    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
    return HTCLIENT;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
        return true;
    }

    switch (msg) {
        case WM_NCCALCSIZE:
            return HandleNcCalcSize(hwnd, wParam, lParam);
        case WM_NCHITTEST:
            return HandleNcHitTest(hwnd, lParam);
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            UINT dpi = GetDpiForWindow(hwnd);
            info->ptMinTrackSize.x = MulDiv(1100, (int)dpi, 96);
            info->ptMinTrackSize.y = MulDiv(720, (int)dpi, 96);
            return 0;
        }
        case WM_SIZE:
            if (g_swapChain && wParam != SIZE_MINIMIZED) {
                g_width = LOWORD(lParam);
                g_height = HIWORD(lParam);
                if (g_width > 0 && g_height > 0) {
                    CleanupRenderTarget();
                    g_swapChain->ResizeBuffers(0, g_width, g_height, DXGI_FORMAT_UNKNOWN, 0);
                    CreateRenderTarget();
                }
            }
            g_occluded = (wParam == SIZE_MINIMIZED);
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_KEYMENU) return 0;
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DPICHANGED: {
            auto* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void EnableFramelessEffects(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    const int cornerPreference = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33, &cornerPreference, sizeof(cornerPreference));
    MARGINS margins = {1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
}

static std::wstring ParseDataDirArg(PWSTR commandLine) {
    std::wstring args = commandLine ? commandLine : L"";
    size_t pos = args.find(L"--data-dir");
    if (pos == std::wstring::npos) return {};
    pos += 10;
    while (pos < args.size() && iswspace(args[pos])) ++pos;
    if (pos >= args.size()) return {};
    if (args[pos] == L'"') {
        ++pos;
        size_t end = args.find(L'"', pos);
        if (end == std::wstring::npos) end = args.size();
        return args.substr(pos, end - pos);
    }
    size_t end = pos;
    while (end < args.size() && !iswspace(args[end])) ++end;
    return args.substr(pos, end - pos);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::wstring dataDir = ParseDataDirArg(commandLine);
    if (!dataDir.empty()) SetDataDirOverride(dataDir);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                 GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"XpLauncherWindow";
    RegisterClassExW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int windowW = 1360;
    int windowH = 860;
    int posX = (screenW - windowW) / 2;
    int posY = (screenH - windowH) / 2;

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"XpLauncher", WS_POPUP | WS_THICKFRAME |
                                    WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN,
                                posX, posY, windowW, windowH, nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 1;

    EnableFramelessEffects(hwnd);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, instance);
        MessageBoxW(nullptr, L"Не удалось создать устройство Direct3D 11.", L"XpLauncher",
                    MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    float scale = (float)GetDpiForWindow(hwnd) / 96.0f;
    if (scale < 1.0f) scale = 1.0f;
    ui::LoadFonts(scale);
    ui::ApplyStyle();
    ui::SetAccent("#7C5CFF");

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    static App app;
    app.Init(hwnd, g_device);

    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
        }
        if (!running) break;

        if (g_occluded) {
            Sleep(10);
            continue;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        app.Frame();
        ImGui::Render();

        const float clear[4] = {0.035f, 0.040f, 0.065f, 1.0f};
        g_context->OMSetRenderTargets(1, &g_renderTarget, nullptr);
        g_context->ClearRenderTargetView(g_renderTarget, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Keep a sane frame time when vsync is unavailable.
        g_swapChain->Present(1, 0);
    }

    app.Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, instance);
    return 0;
}
