#include <windows.h>
#include <d3d9.h>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <stdint.h> // 引入 uintptr_t

// 包含 ImGui 核心
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

// 引入原框架的各类头文件（按照提供的 main.cpp）
#include "retro_skill_state.h"
#include "retro_skill_app.h"
#include "retro_skill_text_dwrite.h"

#pragma comment(lib, "d3d9.lib")

// ============================================================================
// 全局指针与函数定义
// ============================================================================
typedef long(__stdcall* EndScene_t)(struct IDirect3DDevice9*);
typedef long(__stdcall* Reset_t)(struct IDirect3DDevice9*, struct _D3DPRESENT_PARAMETERS_*);

EndScene_t oEndScene = nullptr;
Reset_t oReset = nullptr;
WNDPROC oWndProc = nullptr;

HWND g_hWindow = nullptr;
bool g_bInitialized = false;
uintptr_t* g_D3D9VTable = nullptr; // 改用 uintptr_t 提升跨平台指针安全性

// UI 框架实例与数据 (迁移自原 main.cpp)
RetroSkillRuntimeState  g_retroState;
RetroSkillAssets        g_retroAssets;
RetroSkillBehaviorHooks g_behaviorHooks;
ImFont* g_mainFont = nullptr;
float                   g_mainScale = 1.0f;

// 鼠标防抖控制
ULONGLONG               g_lastAcceptedLButtonDownTick = 0;
const ULONGLONG         g_minLButtonDownIntervalMs = 300;

// 声明 ImGui 的窗口消息处理函数
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ============================================================================
// Hook: 窗口消息处理 (用于拦截鼠标键盘给 ImGui，并包含防抖逻辑)
// ============================================================================
LRESULT CALLBACK hkWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) 
{
    // 鼠标左键防抖机制（同原 main.cpp）
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK)
    {
        ULONGLONG nowTick = ::GetTickCount64();
        if (g_lastAcceptedLButtonDownTick != 0 &&
            nowTick - g_lastAcceptedLButtonDownTick < g_minLButtonDownIntervalMs)
        {
            return 0; // 拦截过快点击
        }
        g_lastAcceptedLButtonDownTick = nowTick;
    }

    // 如果 ImGui 想要处理输入，拦截并消耗此消息
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) 
    {
        return true;
    }
    
    // 如果 ImGui 没有吃掉消息，原样还给游戏
    return CallWindowProc(oWndProc, hWnd, msg, wParam, lParam);
}

// ============================================================================
// Hook: D3D9 Reset (核心修复分辨率改变卡死 + 附加 DWrite 重置)
// ============================================================================
long __stdcall hkReset(struct IDirect3DDevice9* pDevice, struct _D3DPRESENT_PARAMETERS_* pPresentationParameters) 
{
    // [核查重点]: 当游戏调整分辨率或 Alt+Tab 切换时，必须释放所有外部关联资源
    if (g_bInitialized) 
    {
        ImGui_ImplDX9_InvalidateDeviceObjects();
        RetroSkillDWriteOnDeviceLost();
    }

    // 调用原始 Reset 函数
    long hr = oReset(pDevice, pPresentationParameters);

    // 如果原函数执行成功且已初始化，重新创建关联资源
    if (g_bInitialized && SUCCEEDED(hr)) 
    {
        ImGui_ImplDX9_CreateDeviceObjects();
        RetroSkillDWriteOnDeviceReset(pDevice);
    }

    return hr;
}

// ============================================================================
// Hook: D3D9 EndScene (核心渲染逻辑，每帧调用)
// ============================================================================
long __stdcall hkEndScene(struct IDirect3DDevice9* pDevice) 
{
    // 用于首次渲染的测试声音
    static bool s_firstHit = false;
    if (!s_firstHit) {
        Beep(1000, 200); // 发出高音 Beep 证明渲染循环已被成功劫持！
        s_firstHit = true;
    }

    // 1. 初始化阶段 (仅运行一次)
    if (!g_bInitialized) 
    {
        D3DDEVICE_CREATION_PARAMETERS params;
        if (SUCCEEDED(pDevice->GetCreationParameters(&params))) 
        {
            g_hWindow = params.hFocusWindow;
            // 防御性编程：如果游戏隐藏了句柄，强制获取当前前台窗口
            if (!g_hWindow) g_hWindow = GetForegroundWindow();

            // 拦截窗口消息
            if (g_hWindow) {
                oWndProc = (WNDPROC)SetWindowLongPtr(g_hWindow, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
            }

            // 获取显示器 DPI 缩放并初始化
            ImGui_ImplWin32_EnableDpiAwareness();
            g_mainScale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));
            if (g_mainScale <= 0.0f) g_mainScale = 1.0f; // 安全托底

            // 初始化 ImGui 上下文
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

            // 应用原框架风格配置
            ImGui::StyleColorsDark();
            ImGuiStyle& style = ImGui::GetStyle();
            style.ScaleAllSizes(g_mainScale);
            style.FontScaleDpi = 1.0f;
            style.WindowRounding = 0.0f;
            style.FrameRounding = 2.0f * g_mainScale;
            style.ScrollbarRounding = 2.0f * g_mainScale;
            style.WindowBorderSize = 0.0f;
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
            style.Colors[ImGuiCol_Border] = ImVec4(0, 0, 0, 0);
            style.Colors[ImGuiCol_Button] = ImVec4(0, 0, 0, 0);

            // [目标实现]: 替换/注入 UI 框架字体
            char winDir[MAX_PATH] = {};
            GetWindowsDirectoryA(winDir, MAX_PATH);
            std::string fontPath = std::string(winDir) + "\\Fonts\\msyh.ttc";

            io.FontGlobalScale = 1.0f;
            ImFontConfig fontConfig;
            fontConfig.OversampleH = 1;
            fontConfig.OversampleV = 1;
            fontConfig.PixelSnapH = true;

            g_mainFont = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 14.0f * g_mainScale, &fontConfig, io.Fonts->GetGlyphRangesChineseFull());
            if (!g_mainFont) 
            {
                io.Fonts->AddFontDefault();
            }

            ImGui_ImplWin32_Init(g_hWindow);
            ImGui_ImplDX9_Init(pDevice);
            
            // 初始化原应用核心环境
            RetroSkillDWriteInitialize(pDevice);

            // 【注意】这里是极易导致报错/不显示的地方，资源路径如果不存在可能会挂起
            // 请确保 G:\code\UI\SkillEx\ 在你的电脑上存在，或者改成 "./" 相对路径
            const char* assetPath = "G:\\code\\UI\\SkillEx\\";
            InitializeRetroSkillApp(g_retroState, g_retroAssets, pDevice, assetPath);
            ConfigureRetroSkillDefaultBehaviorHooks(g_behaviorHooks, g_retroState);

            g_bInitialized = true;
        }
    }

    // 2. 渲染阶段
    if (g_bInitialized) 
    {
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (g_mainFont) 
            ImGui::PushFont(g_mainFont);

        // 调用 UI 框架主绘制入口
        RenderRetroSkillSceneEx(g_retroState, g_retroAssets, pDevice, g_mainScale, &g_behaviorHooks);

        if (g_mainFont) 
            ImGui::PopFont();

        // [极其重要的测试机制]：单独绘制一个调试窗口！
        // 这样即使 RenderRetroSkillSceneEx 因为找不到图片彻底失效了，
        // 左上角也能显示出这个 Debug 窗口，证明注入和 Hook 逻辑是 100% 存活的。
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::Begin("Hook Debug Status", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("D3D9 Hook Active & Rendering!");
        ImGui::Text("If you only see this, it means RetroSkill failed to load Assets.");
        ImGui::End();

        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }

    // 调用原始 EndScene 返回游戏画面
    return oEndScene(pDevice);
}

// ============================================================================
// 初始化引擎：创建伪设备获取真实 VTable
// ============================================================================
DWORD WINAPI InitThread(LPVOID lpReserved) 
{
    Beep(500, 100); // 注入提示：听到第一声低音，说明 DLL 已进入游戏进程

    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (pD3D == nullptr) 
    {
        MessageBoxA(NULL, "Direct3DCreate9 失败！请确认游戏是否为 DX9 环境。", "Hook 错误", MB_OK | MB_ICONERROR | MB_TOPMOST);
        return FALSE;
    }

    // 使用显式类名创建 Dummy Window，防止与游戏本身冲突
    WNDCLASSEXA wc = { sizeof(WNDCLASSEXA), CS_CLASSDC, DefWindowProc, 0L, 0L, GetModuleHandleA(NULL), NULL, NULL, NULL, NULL, "SkillHookDummy", NULL };
    RegisterClassExA(&wc);
    HWND hDummyWnd = CreateWindowA("SkillHookDummy", "Dummy", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);

    if (hDummyWnd == nullptr) 
    {
        MessageBoxA(NULL, "创建伪装窗口 (Dummy Window) 失败！", "Hook 错误", MB_OK | MB_ICONERROR | MB_TOPMOST);
        pD3D->Release();
        return FALSE;
    }

    D3DPRESENT_PARAMETERS d3dpp = {};
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = hDummyWnd;
    d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;

    IDirect3DDevice9* pDummyDevice = nullptr;
    HRESULT hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hDummyWnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDummyDevice);

    if (FAILED(hr) || pDummyDevice == nullptr) 
    {
        MessageBoxA(NULL, "创建伪装设备 (Dummy Device) 失败！游戏可能处于独占全屏或抗注入保护中。", "Hook 错误", MB_OK | MB_ICONERROR | MB_TOPMOST);
        DestroyWindow(hDummyWnd);
        pD3D->Release();
        return FALSE;
    }

    // [核心 Hook 机制]: 获取 VTable
    // 强制转换为指针数组获取 D3D9 虚函数表 (使用 uintptr_t 安全转换)
    g_D3D9VTable = *(uintptr_t**)pDummyDevice;

    oReset     = (Reset_t)g_D3D9VTable[16];
    oEndScene  = (EndScene_t)g_D3D9VTable[42];

    // 修改 VTable 内存属性，进行覆盖
    DWORD oldProtect;
    if (VirtualProtect(&g_D3D9VTable[16], sizeof(uintptr_t), PAGE_READWRITE, &oldProtect)) 
    {
        g_D3D9VTable[16] = (uintptr_t)hkReset;
        VirtualProtect(&g_D3D9VTable[16], sizeof(uintptr_t), oldProtect, &oldProtect);
    }

    if (VirtualProtect(&g_D3D9VTable[42], sizeof(uintptr_t), PAGE_READWRITE, &oldProtect)) 
    {
        g_D3D9VTable[42] = (uintptr_t)hkEndScene;
        VirtualProtect(&g_D3D9VTable[42], sizeof(uintptr_t), oldProtect, &oldProtect);
    }

    // 释放伪设备，真实设备(共用同一VTable)已受到感染
    pDummyDevice->Release();
    pD3D->Release();
    DestroyWindow(hDummyWnd);

    Beep(750, 100); // 挂载提示：听到第二声中音，说明 VTable 替换成功！
    return TRUE;
}

// ============================================================================
// 卸载与复原逻辑
// ============================================================================
void UnhookAndCleanup()
{
    if (g_D3D9VTable != nullptr)
    {
        DWORD oldProtect;
        // 恢复 Reset (索引16)
        if (oReset && VirtualProtect(&g_D3D9VTable[16], sizeof(uintptr_t), PAGE_READWRITE, &oldProtect)) {
            g_D3D9VTable[16] = (uintptr_t)oReset;
            VirtualProtect(&g_D3D9VTable[16], sizeof(uintptr_t), oldProtect, &oldProtect);
        }
        // 恢复 EndScene (索引42)
        if (oEndScene && VirtualProtect(&g_D3D9VTable[42], sizeof(uintptr_t), PAGE_READWRITE, &oldProtect)) {
            g_D3D9VTable[42] = (uintptr_t)oEndScene;
            VirtualProtect(&g_D3D9VTable[42], sizeof(uintptr_t), oldProtect, &oldProtect);
        }
    }

    // 恢复窗口过程
    if (g_hWindow && oWndProc)
    {
        SetWindowLongPtr(g_hWindow, GWLP_WNDPROC, (LONG_PTR)oWndProc);
    }

    // 释放应用与渲染引擎内存
    if (g_bInitialized)
    {
        ShutdownRetroSkillApp(g_retroAssets);
        RetroSkillDWriteShutdown();

        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_bInitialized = false;
    }
}

// ============================================================================
// DLL 入口
// ============================================================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) 
{
    switch (ul_reason_for_call) 
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        // 新开线程进行 Hook，防止卡死主线程
        CreateThread(nullptr, 0, InitThread, hModule, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        // 当 DLL 被卸载时执行清理与复原
        UnhookAndCleanup();
        break;
    }
    return TRUE;
}