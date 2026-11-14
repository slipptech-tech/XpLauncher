cmake_minimum_required(VERSION 3.21)
project(XpLauncher VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(TP "${CMAKE_SOURCE_DIR}/third_party")

add_executable(XpLauncher WIN32
    src/main.cpp
    resources/app.rc
    src/app.cpp
    src/core.cpp
    src/minecraft.cpp
    src/mods.cpp
    src/msa.cpp
    src/app_msa.cpp
    src/ui.cpp
    src/page_home.cpp
    src/page_versions.cpp
    src/page_mods.cpp
    src/page_profile.cpp
    src/page_accounts.cpp
    src/page_downloads.cpp
    src/page_server.cpp
    src/page_settings.cpp
    ${TP}/imgui/imgui.cpp
    ${TP}/imgui/imgui_draw.cpp
    ${TP}/imgui/imgui_tables.cpp
    ${TP}/imgui/imgui_widgets.cpp
    ${TP}/imgui/backends/imgui_impl_win32.cpp
    ${TP}/imgui/backends/imgui_impl_dx11.cpp
    ${TP}/miniz.c
    ${TP}/miniz_tdef.c
    ${TP}/miniz_tinfl.c
    ${TP}/miniz_zip.c
)

target_include_directories(XpLauncher PRIVATE
    src
    ${TP}
    ${TP}/imgui
    ${TP}/imgui/backends
)

target_compile_definitions(XpLauncher PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

target_compile_options(XpLauncher PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:/utf-8 /W3 /MP>
    $<$<COMPILE_LANGUAGE:C>:/W3>
)

target_link_libraries(XpLauncher PRIVATE
    d3d11 dxgi d3dcompiler dwmapi winhttp bcrypt shlwapi shell32 ole32 comctl32 version uuid
)

set_target_properties(XpLauncher PROPERTIES
    VS_GLOBAL_CharacterSet "Unicode"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(XpLauncher PRIVATE /permissive-)
endif()
