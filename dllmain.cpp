#include <windows.h>
#include <d3d9.h>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <stdint.h> 

// 包含 ImGui 核心
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

// 引入原框架的各类头文件
#include "retro_skill_state.h"
#include "retro_skill_app.h"
#include "retro_skill_text_dwrite.h"

#pragma comment(lib, "d3d9.lib")

// ============================================================================
// 全局指针与内联 Hook 恢复数据
// ============================================================================
typedef long(__stdcall* Present_t)(struct IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
typedef long(__stdcall* Reset_t)(struct IDirect3DDevice9*, struct _D3DPRESENT_PARAMETERS_*);

Present_t oPresent = nullptr;
Reset_t oReset = nullptr;
WNDPROC oWndProc = nullptr;

HWND g_hWindow = nullptr;
bool g_bInitialized = false;

// 内联蹦床防线数据 (用于安全卸载还原)
uint8_t g_origPresent[5];
uint8_t g_origReset[5];
void* g_pTargetPresent = nullptr;
void* g_pTargetReset = nullptr;

RetroSkillRuntimeState  g_retroState;
RetroSkillAssets        g_retroAssets;
RetroSkillBehaviorHooks g_behaviorHooks;
ImFont* g_mainFont = nullptr;
float                   g_mainScale = 1.0f;

ULONGLONG               g_lastAcceptedLButtonDownTick = 0;
const ULONGLONG         g_minLButtonDownIntervalMs = 300;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ============================================================================
// [核心技术] 手写 32 位 Inline Trampoline Hook (绝对拦截)
// ============================================================================
void* TrampolineHook(void* pSource, void* pDestination, size_t length, uint8_t* outOriginalBytes)
{
    if (length < 5) return nullptr;

    memcpy(outOriginalBytes, pSource, length);
    void* gateway = VirtualAlloc(nullptr, length + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!gateway) return nullptr;

    DWORD oldProtect;
    VirtualProtect(pSource, length, PAGE_EXECUTE_READWRITE, &oldProtect);

    memcpy(gateway, pSource, length);
    uintptr_t gatewayRelativeAddr = ((uintptr_t)pSource + length) - ((uintptr_t)gateway + length) - 5;
    *(uint8_t*)((uintptr_t)gateway + length) = 0xE9; // JMP 指令
    *(uint32_t*)((uintptr_t)gateway + length + 1) = (uint32_t)gatewayRelativeAddr;

    uintptr_t relativeAddr = (uintptr_t)pDestination - (uintptr_t)pSource - 5;
    *(uint8_t*)pSource = 0xE9;
    *(uint32_t*)((uintptr_t)pSource + 1) = (uint32_t)relativeAddr;

    for (size_t i = 5; i < length; i++) *(uint8_t*)((uintptr_t)pSource + i) = 0x90;

    VirtualProtect(pSource, length, oldProtect, &oldProtect);
    return gateway; 
}

// ============================================================================
// Hook: 窗口消息处理 (精准过滤，防止穿透)
// ============================================================================
LRESULT CALLBACK hkWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) 
{
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK)
    {
        ULONGLONG nowTick = ::GetTickCount64();
        if (g_lastAcceptedLButtonDownTick != 0 && nowTick - g_lastAcceptedLButtonDownTick < g_minLButtonDownIntervalMs)
            return 0; 
        g_lastAcceptedLButtonDownTick = nowTick;
    }

    if (g_bInitialized) 
    {
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        ImGuiIO& io = ImGui::GetIO();
        // 精确拦截：如果鼠标在 UI 面板上，吞掉鼠标信号不传给游戏；打字同理
        if (io.WantCaptureMouse && (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST)) return true;
        if (io.WantCaptureKeyboard && (msg >= WM_KEYFIRST && msg <= WM_KEYLAST)) return true;
    }
    
    return CallWindowProc(oWndProc, hWnd, msg, wParam, lParam);
}

// ============================================================================
// Hook: D3D9 Reset (核弹级清理，彻底解决分辨率改变卡死问题)
// ============================================================================
long __stdcall hkReset(struct IDirect3DDevice9* pDevice, struct _D3DPRESENT_PARAMETERS_* pPresentationParameters) 
{
    // 【核心机制】：当游戏改变分辨率，DX9 会强制要求清理所有显存。
    if (g_bInitialized) 
    {
        // 1. 还原窗口事件句柄（防止游戏在此期间销毁重建窗口导致指针异常）
        if (g_hWindow && oWndProc) {
            SetWindowLongPtr(g_hWindow, GWLP_WNDPROC, (LONG_PTR)oWndProc);
            oWndProc = nullptr;
        }

        // 2. 将所有 UI 纹理、字体缓存、直接渲染层全部进行毁灭级卸载
        ShutdownRetroSkillApp(g_retroAssets);
        RetroSkillDWriteShutdown();
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        // 3. 将状态置空。这样等 Reset 完成后的下一帧，引擎会自动从零重建它，100%不会卡死！
        g_bInitialized = false;
    }

    return oReset(pDevice, pPresentationParameters);
}

// ============================================================================
// Hook: D3D9 Present (底层核心渲染拦截)
// ============================================================================
long __stdcall hkPresent(struct IDirect3DDevice9* pDevice, const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion) 
{
    // 1. 初始化 / 自动重建阶段
    if (!g_bInitialized) 
    {
        D3DDEVICE_CREATION_PARAMETERS params;
        if (SUCCEEDED(pDevice->GetCreationParameters(&params))) g_hWindow = params.hFocusWindow;
        if (!g_hWindow) g_hWindow = FindWindowA("MapleStoryClass", NULL); 
        if (!g_hWindow) g_hWindow = GetForegroundWindow(); 

        if (g_hWindow) 
        {
            oWndProc = (WNDPROC)SetWindowLongPtr(g_hWindow, GWLP_WNDPROC, (LONG_PTR)hkWndProc);

            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            g_mainScale = 1.0f; 

            ImGui::StyleColorsDark();
            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0); // 维持背景透明度
            style.Colors[ImGuiCol_Border] = ImVec4(0, 0, 0, 0);

            // 加载微软雅黑，保证全中文替换覆盖
            char winDir[MAX_PATH] = {};
            GetWindowsDirectoryA(winDir, MAX_PATH);
            std::string fontPath = std::string(winDir) + "\\Fonts\\msyh.ttc";

            ImFontConfig fontConfig;
            fontConfig.OversampleH = 1; fontConfig.OversampleV = 1; fontConfig.PixelSnapH = true;
            g_mainFont = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 14.0f, &fontConfig, io.Fonts->GetGlyphRangesChineseFull());
            if (!g_mainFont) io.Fonts->AddFontDefault();

            ImGui_ImplWin32_Init(g_hWindow);
            ImGui_ImplDX9_Init(pDevice);
            RetroSkillDWriteInitialize(pDevice);
            
            const char* assetPath = "G:\\code\\UI\\SkillEx\\";
            InitializeRetroSkillApp(g_retroState, g_retroAssets, pDevice, assetPath);
            ConfigureRetroSkillDefaultBehaviorHooks(g_behaviorHooks, g_retroState);

            g_bInitialized = true;
        }
    }

    // 2. 渲染绘制阶段
    if (g_bInitialized) 
    {
        // 状态块捕获护城河，保护游戏原有画面不被 UI 污染闪烁
        IDirect3DStateBlock9* pStateBlock = nullptr;
        if (SUCCEEDED(pDevice->CreateStateBlock(D3DSBT_ALL, &pStateBlock)) && pStateBlock != nullptr)
        {
            pStateBlock->Capture();

            ImGui_ImplDX9_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            if (g_mainFont) ImGui::PushFont(g_mainFont);

            // 核心：唯独只呼出你这套单独的技能面板
            RenderRetroSkillSceneEx(g_retroState, g_retroAssets, pDevice, g_mainScale, &g_behaviorHooks);
            
            if (g_mainFont) ImGui::PopFont();

            ImGui::EndFrame();
            ImGui::Render();

            // 手动开启闭合引擎流绘制 (Present拦截特供)
            if (SUCCEEDED(pDevice->BeginScene())) {
                ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
                pDevice->EndScene();
            }

            pStateBlock->Apply();
            pStateBlock->Release();
        }
    }

    return oPresent(pDevice, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

// ============================================================================
// 初始化引擎：创建伪设备以获得底层模块地址，并执行底层篡改
// ============================================================================
DWORD WINAPI InitThread(LPVOID lpReserved) 
{
    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return FALSE;

    WNDCLASSEXA wc = { sizeof(WNDCLASSEXA), CS_CLASSDC, DefWindowProc, 0L, 0L, GetModuleHandleA(NULL), NULL, NULL, NULL, NULL, "SkillHookDummy", NULL };
    RegisterClassExA(&wc);
    HWND hDummyWnd = CreateWindowA("SkillHookDummy", "Dummy", WS_OVERLAPPEDWINDOW, 0, 0, 10, 10, NULL, NULL, wc.hInstance, NULL);

    if (!hDummyWnd) { pD3D->Release(); return FALSE; }

    D3DPRESENT_PARAMETERS d3dpp = {};
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = hDummyWnd;
    d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;

    IDirect3DDevice9* pDummyDevice = nullptr;
    HRESULT hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hDummyWnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDummyDevice);

    if (FAILED(hr) || !pDummyDevice) 
    {
        DestroyWindow(hDummyWnd);
        pD3D->Release();
        return FALSE;
    }

    // 获取系统 d3d9.dll 中真实的函数地址
    uintptr_t* pVTable = *(uintptr_t**)pDummyDevice;
    g_pTargetReset   = (void*)pVTable[16];
    g_pTargetPresent = (void*)pVTable[17];

    // 执行内联蹦床注入
    oReset   = (Reset_t)TrampolineHook(g_pTargetReset, (void*)hkReset, 5, g_origReset);
    oPresent = (Present_t)TrampolineHook(g_pTargetPresent, (void*)hkPresent, 5, g_origPresent);

    pDummyDevice->Release();
    pD3D->Release();
    DestroyWindow(hDummyWnd);

    return TRUE;
}

// ============================================================================
// 卸载与复原逻辑 (安全地将 d3d9.dll 模块的代码改回原样)
// ============================================================================
void UnhookAndCleanup()
{
    // 将原本的 5 字节汇编代码原封不动地写回去，恢复游戏的原生渲染通道
    if (g_pTargetReset)
    {
        DWORD oldProtect;
        if (VirtualProtect(g_pTargetReset, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            memcpy(g_pTargetReset, g_origReset, 5);
            VirtualProtect(g_pTargetReset, 5, oldProtect, &oldProtect);
        }
    }
    if (g_pTargetPresent)
    {
        DWORD oldProtect;
        if (VirtualProtect(g_pTargetPresent, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            memcpy(g_pTargetPresent, g_origPresent, 5);
            VirtualProtect(g_pTargetPresent, 5, oldProtect, &oldProtect);
        }
    }

    if (g_hWindow && oWndProc)
    {
        SetWindowLongPtr(g_hWindow, GWLP_WNDPROC, (LONG_PTR)oWndProc);
    }

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

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) 
{
    switch (ul_reason_for_call) 
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, InitThread, hModule, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        UnhookAndCleanup();
        break;
    }
    return TRUE;
}