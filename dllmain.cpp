#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <stdint.h>

// 包含核心组件
#pragma comment(lib, "d3d9.lib")
#include <d3d9.h>
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

// 引入图片加载与 UI 框架
#include "stb_image.h"
#include "retro_skill_state.h"
#include "retro_skill_app.h"
#include "retro_skill_panel.h" 
#include "retro_skill_text_dwrite.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ============================================================================
// [全局配置与内存地址]
// ============================================================================
uintptr_t g_BaseAddr = 0;
const uintptr_t OFF_BASE = 0x00B61A68;
std::vector<uintptr_t> G_OFFSETS_X = { 0x18, 0x30, 0x40, 0x24, 0x18, 0x24, 0x1C };
std::vector<uintptr_t> G_OFFSETS_Y = { 0x18, 0x30, 0x40, 0x24, 0x18, 0x24, 0x20 };

HWND g_GameHwnd = nullptr;
WNDPROC g_OriginalWndProc = nullptr;
bool g_ImGuiInitialized = false;
bool g_ExpandPanel = false; // 超级技能栏展开状态

// 【修复：补全丢失的全局 UI 状态变量】
RetroSkillRuntimeState  g_retroState;
RetroSkillAssets        g_retroAssets;
RetroSkillBehaviorHooks g_behaviorHooks;
ImFont* g_mainFont = nullptr;
float                   g_mainScale = 1.0f;

// 贴图资源 (使用 Managed 内存池以提升稳定性)
IDirect3DTexture9 *g_texBtnNormal = nullptr, *g_texBtnHover = nullptr, *g_texBtnPressed = nullptr, *g_texBtnDisabled = nullptr;
int g_btnW = 0, g_btnH = 0;

// ============================================================================
// [工具函数] 独立的图片转 D3D 纹理加载器
// ============================================================================
bool LoadManagedTextureFromFile(IDirect3DDevice9* device, const char* filename, IDirect3DTexture9** out_texture, int* out_width, int* out_height) 
{
    int image_width = 0, image_height = 0, channels = 0;
    unsigned char* image_data = stbi_load(filename, &image_width, &image_height, &channels, 4);
    if (image_data == NULL) return false;

    HRESULT hr = device->CreateTexture(image_width, image_height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, out_texture, NULL);
    if (FAILED(hr)) { 
        stbi_image_free(image_data); 
        return false; 
    }

    D3DLOCKED_RECT locked_rect;
    (*out_texture)->LockRect(0, &locked_rect, NULL, 0);
    unsigned char* pDest = (unsigned char*)locked_rect.pBits;
    unsigned char* pSrc = image_data;
    
    // RGBA 转 BGRA
    for (int i = 0; i < image_width * image_height; i++) {
        pDest[i*4 + 0] = pSrc[i*4 + 2]; // B
        pDest[i*4 + 1] = pSrc[i*4 + 1]; // G
        pDest[i*4 + 2] = pSrc[i*4 + 0]; // R
        pDest[i*4 + 3] = pSrc[i*4 + 3]; // A
    }
    
    (*out_texture)->UnlockRect(0);
    stbi_image_free(image_data);

    if (out_width) *out_width = image_width;
    if (out_height) *out_height = image_height;
    return true;
}

// ============================================================================
// [核心逻辑] 安全解析多级指针
// ============================================================================
uintptr_t ResolvePtrChain(uintptr_t base, std::vector<uintptr_t> offsets) {
    uintptr_t addr = base;
    for (uintptr_t off : offsets) {
        if (addr == 0) return 0;
        if (IsBadReadPtr((void*)addr, sizeof(uintptr_t))) return 0;
        addr = *(uintptr_t*)addr;
        if (addr == 0) return 0;
        addr += off;
    }
    return addr;
}

// ============================================================================
// [IAT Hook] 劫持系统 API 达到彻底“致盲”游戏鼠标的效果
// ============================================================================
void* HookIAT(const char* mod, const char* api, void* dst) {
    HMODULE h = GetModuleHandleA(NULL);
    if (!h) return nullptr;
    
    PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)h;
    if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;

    PIMAGE_NT_HEADERS pNtHeaders = (PIMAGE_NT_HEADERS)((BYTE*)h + pDosHeader->e_lfanew);
    if (pNtHeaders->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    IMAGE_DATA_DIRECTORY& importDir = pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (importDir.VirtualAddress == 0) return nullptr;

    PIMAGE_IMPORT_DESCRIPTOR pImp = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)h + importDir.VirtualAddress);
    while (pImp && pImp->Name) {
        if (!_stricmp((const char*)((BYTE*)h + pImp->Name), mod)) {
            PIMAGE_THUNK_DATA pT = (PIMAGE_THUNK_DATA)((BYTE*)h + pImp->FirstThunk);
            PIMAGE_THUNK_DATA pOT = (PIMAGE_THUNK_DATA)((BYTE*)h + pImp->OriginalFirstThunk);
            if (!pOT) pOT = pT;
            while (pOT->u1.Function) {
                if (!(pOT->u1.Ordinal & IMAGE_ORDINAL_FLAG) && !strcmp((const char*)((PIMAGE_IMPORT_BY_NAME)((BYTE*)h + pOT->u1.AddressOfData))->Name, api)) {
                    void* old = (void*)pT->u1.Function;
                    DWORD op; VirtualProtect(&pT->u1.Function, 4, PAGE_READWRITE, &op);
                    pT->u1.Function = (uintptr_t)dst;
                    VirtualProtect(&pT->u1.Function, 4, op, &op);
                    return old;
                }
                pT++; pOT++;
            }
        }
        pImp++;
    }
    return nullptr;
}

// 【修复：统一定义原系统 API 函数指针的命名】
typedef BOOL(WINAPI* tGetCursorPos)(LPPOINT);
typedef BOOL(WINAPI* tScreenToClient)(HWND, LPPOINT);
typedef SHORT(WINAPI* tGetAsyncKeyState)(int);
typedef SHORT(WINAPI* tGetKeyState)(int);

tGetCursorPos oGetCursorPos = nullptr; 
tScreenToClient oScreenToClient = nullptr;
tGetAsyncKeyState oGetAsyncKeyState = nullptr;
tGetKeyState oGetKeyState = nullptr;

bool g_SpoofWin32Input = true; 

BOOL WINAPI hkGetCursorPos(LPPOINT p) {
    BOOL r = oGetCursorPos ? oGetCursorPos(p) : GetCursorPos(p);
    if (r && g_ImGuiInitialized && g_SpoofWin32Input && ImGui::GetIO().WantCaptureMouse) { p->x = -10000; p->y = -10000; }
    return r;
}
BOOL WINAPI hkScreenToClient(HWND hWnd, LPPOINT lpPoint) {
    BOOL res = oScreenToClient ? oScreenToClient(hWnd, lpPoint) : ScreenToClient(hWnd, lpPoint);
    if (res && g_ImGuiInitialized && g_SpoofWin32Input && ImGui::GetIO().WantCaptureMouse) {
        lpPoint->x = -10000; lpPoint->y = -10000;
    }
    return res;
}
SHORT WINAPI hkGetAsyncKeyState(int v) {
    if (g_ImGuiInitialized && g_SpoofWin32Input && ImGui::GetIO().WantCaptureMouse && (v == VK_LBUTTON || v == VK_RBUTTON || v == VK_MBUTTON)) return 0;
    return oGetAsyncKeyState ? oGetAsyncKeyState(v) : GetAsyncKeyState(v);
}
SHORT WINAPI hkGetKeyState(int vKey) {
    if (g_ImGuiInitialized && g_SpoofWin32Input && ImGui::GetIO().WantCaptureMouse && (vKey == VK_LBUTTON || vKey == VK_RBUTTON || vKey == VK_MBUTTON)) return 0;
    return oGetKeyState ? oGetKeyState(vKey) : GetKeyState(vKey);
}

// ============================================================================
// [WndProc] 消息过滤 (解决任务栏闪烁)
// ============================================================================
LRESULT CALLBACK GameWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (g_ImGuiInitialized) {
        ImGui_ImplWin32_WndProcHandler(h, m, w, l);
        if (ImGui::GetIO().WantCaptureMouse) {
            if (m == WM_SETCURSOR) { SetCursor(NULL); return TRUE; }
            if (m == WM_MOUSEMOVE) { CallWindowProc(g_OriginalWndProc, h, m, w, MAKELPARAM((WORD)-10000, (WORD)-10000)); return 0; }
            if (m >= WM_LBUTTONDOWN && m <= WM_MOUSEWHEEL) return 0; 
        }
        if (ImGui::GetIO().WantCaptureKeyboard && m >= WM_KEYFIRST && m <= WM_KEYLAST) return 0;
    }
    return CallWindowProc(g_OriginalWndProc, h, m, w, l);
}

// ============================================================================
// 获取真正的游戏主窗口
// ============================================================================
HWND GetRealGameWindow()
{
    struct Param { HWND hwnd; DWORD pid; };
    Param p = {NULL, GetCurrentProcessId()};

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL
    {
        Param* p = (Param*)lParam;
        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == p->pid && IsWindowVisible(hwnd)) {
            char className[256];
            GetClassNameA(hwnd, className, sizeof(className));
            if (strcmp(className, "ConsoleWindowClass") != 0) {
                p->hwnd = hwnd;
                return FALSE; 
            }
        }
        return TRUE; 
    }, (LPARAM)&p);
    return p.hwnd;
}

// ============================================================================
// [底层 Hook 蹦床]
// ============================================================================
void *SmartSafeHook(DWORD targetFunc, void *myFunc)
{
    BYTE *pTarget = (BYTE *)targetFunc;

    while (pTarget[0] == 0xE9 || pTarget[0] == 0xEB)
    {
        if (pTarget[0] == 0xE9) pTarget = pTarget + 5 + *(DWORD *)(pTarget + 1);
        else pTarget = pTarget + 2 + (char)pTarget[1];
    }

    if (pTarget[0] == 0x8B && pTarget[1] == 0xFF && pTarget[2] == 0x55 && pTarget[3] == 0x8B && pTarget[4] == 0xEC)
    {
        void *trampoline = VirtualAlloc(NULL, 14, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!trampoline) return nullptr;

        memcpy(trampoline, pTarget, 5);
        *(BYTE *)((DWORD)trampoline + 5) = 0xE9;
        *(DWORD *)((DWORD)trampoline + 6) = (DWORD)(pTarget + 5) - ((DWORD)trampoline + 5) - 5;

        DWORD oldProtect;
        VirtualProtect(pTarget, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
        pTarget[0] = 0xE9;
        *(DWORD *)(pTarget + 1) = (DWORD)myFunc - (DWORD)pTarget - 5;
        VirtualProtect(pTarget, 5, oldProtect, &oldProtect);

        return trampoline;
    }
    return nullptr;
}

// ============================================================================
// [内部渲染核心]
// ============================================================================
typedef HRESULT(APIENTRY *tPresent)(IDirect3DDevice9 *pDevice, const RECT *, const RECT *, HWND, const RGNDATA *);
typedef HRESULT(APIENTRY *tReset)(IDirect3DDevice9 *pDevice, D3DPRESENT_PARAMETERS *);
tPresent oPresent = nullptr;
tReset oReset = nullptr;

// 【核弹级修复】：彻底解决游戏切换全屏或修改分辨率导致的死锁卡死问题
HRESULT APIENTRY hkReset(IDirect3DDevice9 *pDevice, D3DPRESENT_PARAMETERS *pPresentationParameters)
{
    if (g_ImGuiInitialized) 
    {
        ShutdownRetroSkillApp(g_retroAssets);
        RetroSkillDWriteShutdown(); 
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        // 释放按钮图像
        if (g_texBtnNormal) { g_texBtnNormal->Release(); g_texBtnNormal = nullptr; }
        if (g_texBtnHover) { g_texBtnHover->Release(); g_texBtnHover = nullptr; }
        if (g_texBtnPressed) { g_texBtnPressed->Release(); g_texBtnPressed = nullptr; }
        if (g_texBtnDisabled) { g_texBtnDisabled->Release(); g_texBtnDisabled = nullptr; }
        
        g_ImGuiInitialized = false; 
    }
    
    return oReset(pDevice, pPresentationParameters);
}

HRESULT APIENTRY hkPresent(IDirect3DDevice9 *pDevice, const RECT *pSourceRect, const RECT *pDestRect, HWND hDestWindowOverride, const RGNDATA *pDirtyRegion)
{
    if (!g_ImGuiInitialized && g_GameHwnd != nullptr)
    {
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0); 
        style.Colors[ImGuiCol_Border] = ImVec4(0, 0, 0, 0);

        char winDir[MAX_PATH];
        GetWindowsDirectoryA(winDir, MAX_PATH);
        std::string msyhPath = std::string(winDir) + "\\Fonts\\msyh.ttc";
        std::string simheiPath = std::string(winDir) + "\\Fonts\\simhei.ttf";

        ImFontConfig fontConfig;
        fontConfig.OversampleH = 1; fontConfig.OversampleV = 1; fontConfig.PixelSnapH = true;

        g_mainFont = io.Fonts->AddFontFromFileTTF(msyhPath.c_str(), 14.0f, &fontConfig, io.Fonts->GetGlyphRangesChineseFull());
        if (!g_mainFont) g_mainFont = io.Fonts->AddFontFromFileTTF(simheiPath.c_str(), 14.0f, &fontConfig, io.Fonts->GetGlyphRangesChineseFull());
        if (!g_mainFont) io.Fonts->AddFontDefault();

        ImGui_ImplWin32_Init(g_GameHwnd);
        ImGui_ImplDX9_Init(pDevice);
        
        RetroSkillDWriteInitialize(pDevice);
        
        const char* assetPath = "G:\\code\\UI\\SkillEx\\";
        InitializeRetroSkillApp(g_retroState, g_retroAssets, pDevice, assetPath);
        ConfigureRetroSkillDefaultBehaviorHooks(g_behaviorHooks, g_retroState);

        // 加载展开/收缩按钮的四个状态贴图
        LoadManagedTextureFromFile(pDevice, "G:\\code\\UI\\SkillEx\\SkillEx.surpe.normal.png", &g_texBtnNormal, &g_btnW, &g_btnH);
        LoadManagedTextureFromFile(pDevice, "G:\\code\\UI\\SkillEx\\SkillEx.surpe.mouseOver.png", &g_texBtnHover, NULL, NULL);
        LoadManagedTextureFromFile(pDevice, "G:\\code\\UI\\SkillEx\\SkillEx.surpe.pressed.png", &g_texBtnPressed, NULL, NULL);
        LoadManagedTextureFromFile(pDevice, "G:\\code\\UI\\SkillEx\\SkillEx.surpe.disabled.png", &g_texBtnDisabled, NULL, NULL);

        g_ImGuiInitialized = true;
    }

    if (g_ImGuiInitialized)
    {
        ImGuiIO &io = ImGui::GetIO();
        io.MouseDrawCursor = false; // 关闭原生光标绘制，全手动接管

        g_SpoofWin32Input = false; 
        
        IDirect3DStateBlock9* pStateBlock = nullptr;
        if (SUCCEEDED(pDevice->CreateStateBlock(D3DSBT_ALL, &pStateBlock)) && pStateBlock != nullptr)
        {
            pStateBlock->Capture();

            ImGui_ImplDX9_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            
            g_SpoofWin32Input = true; 

            // --- 内存吸附同步逻辑 ---
            int gameUiX = 0, gameUiY = 0;
            bool gameUiVisible = false;

            if (g_BaseAddr != 0 && OFF_BASE != 0) {
                uintptr_t baseVal = *(uintptr_t*)(g_BaseAddr + OFF_BASE);
                if (baseVal != 0) {
                    uintptr_t xAddr = ResolvePtrChain(g_BaseAddr + OFF_BASE, G_OFFSETS_X);
                    uintptr_t yAddr = ResolvePtrChain(g_BaseAddr + OFF_BASE, G_OFFSETS_Y);
                    
                    if (xAddr && yAddr) {
                        gameUiX = *(int*)xAddr;
                        gameUiY = *(int*)yAddr;
                        gameUiVisible = true; // 成功解析到最后，认为窗体是打开状态
                    }
                }
            }

            if (g_mainFont) ImGui::PushFont(g_mainFont);

            // 只有原版技能栏显示时，才渲染我们的超级技能面板和按钮
            if (gameUiVisible) 
            {
                // ========================================================================
                // 【1. 绘制独立的超级技能展开/收缩按钮】(自动内存吸附)
                // ========================================================================
                ImVec2 btnPos((float)gameUiX + 11, (float)gameUiY + 256);
                ImGui::SetNextWindowPos(btnPos);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::Begin("SuperSkillToggleButton", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove);

                ImVec2 size(g_btnW > 0 ? g_btnW * g_mainScale : 32 * g_mainScale, 
                            g_btnH > 0 ? g_btnH * g_mainScale : 32 * g_mainScale);

                ImGui::InvisibleButton("##ToggleBtn", size);
                bool is_hovered = ImGui::IsItemHovered();
                bool is_held = ImGui::IsItemActive();

                if (ImGui::IsItemClicked()) {
                    g_ExpandPanel = !g_ExpandPanel;
                }

                IDirect3DTexture9* tex = g_texBtnNormal;
                if (g_ExpandPanel) tex = g_texBtnPressed;
                else {
                    if (is_held) tex = g_texBtnPressed;
                    else if (is_hovered) tex = g_texBtnHover;
                }

                if (tex) ImGui::GetWindowDrawList()->AddImage((ImTextureID)tex, btnPos, ImVec2(btnPos.x + size.x, btnPos.y + size.y));
                else ImGui::GetWindowDrawList()->AddRectFilled(btnPos, ImVec2(btnPos.x + size.x, btnPos.y + size.y), IM_COL32(255, 0, 0, 100));

                ImGui::End();
                ImGui::PopStyleVar();

                // ========================================================================
                // 【2. 根据开关状态，控制超级技能面板的绘制】(自动内存吸附)
                // ========================================================================
                if (g_ExpandPanel)
                {
                    float panelWidth = 174.0f * g_mainScale; 
                    ImGui::SetNextWindowPos(ImVec2((float)gameUiX - panelWidth - 5, (float)gameUiY));
                    RenderRetroSkillPanel(g_retroState, g_retroAssets, pDevice, g_mainScale, &g_behaviorHooks);
                }
            }

            // 【3. 专属复古鼠标接管系统】
            if (io.WantCaptureMouse)
            {
                UITexture* mouseTex = nullptr;
                if (g_retroState.isDraggingSkill) {
                    mouseTex = GetRetroSkillTexture(g_retroAssets, "mouse.drag");
                } else if (io.MouseDown[0]) {
                    mouseTex = GetRetroSkillTexture(g_retroAssets, "mouse.pressed");
                } else {
                    mouseTex = GetRetroSkillTexture(g_retroAssets, "mouse.normal");
                }

                if (!mouseTex || !mouseTex->texture) {
                    mouseTex = GetRetroSkillTexture(g_retroAssets, "System.mouse");
                }

                if (mouseTex && mouseTex->texture)
                {
                    ImDrawList* fg_dl = ImGui::GetForegroundDrawList();
                    
                    ImVec2 mPos = io.MousePos;
                    mPos.y -= 3.0f * g_mainScale; 
                    
                    ImVec2 mMax(mPos.x + mouseTex->width * g_mainScale, mPos.y + mouseTex->height * g_mainScale);
                    fg_dl->AddImage((ImTextureID)mouseTex->texture, mPos, mMax);
                }
            }

            if (g_mainFont) ImGui::PopFont();

            ImGui::EndFrame();
            ImGui::Render();

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

bool SetupInternalD3D9Hook()
{
    WNDCLASSEXA wc = {sizeof(WNDCLASSEX), CS_CLASSDC, DefWindowProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, "DXDummy", NULL};
    RegisterClassExA(&wc);
    HWND hWnd = CreateWindowA("DXDummy", "", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);

    IDirect3D9 *pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return false;

    D3DPRESENT_PARAMETERS d3dpp = {};
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = hWnd;

    IDirect3DDevice9 *pDevice = nullptr;
    if (FAILED(pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDevice)))
    {
        pD3D->Release();
        return false;
    }

    void **vTable = *(void ***)pDevice;

    oPresent = (tPresent)SmartSafeHook((DWORD)vTable[17], (void *)hkPresent);
    oReset = (tReset)SmartSafeHook((DWORD)vTable[16], (void *)hkReset);

    if (!oPresent)
    {
        DWORD oldProtect;
        VirtualProtect(&vTable[17], sizeof(void *), PAGE_EXECUTE_READWRITE, &oldProtect);
        oPresent = (tPresent)vTable[17];
        vTable[17] = (void *)hkPresent;
        VirtualProtect(&vTable[17], sizeof(void *), oldProtect, &oldProtect);

        VirtualProtect(&vTable[16], sizeof(void *), PAGE_EXECUTE_READWRITE, &oldProtect);
        oReset = (tReset)vTable[16];
        vTable[16] = (void *)hkReset;
        VirtualProtect(&vTable[16], sizeof(void *), oldProtect, &oldProtect);
    }

    pDevice->Release();
    pD3D->Release();
    DestroyWindow(hWnd);
    UnregisterClassA("DXDummy", wc.hInstance);

    // ========================================================================
    // 执行 IAT 挂钩，致盲游戏底层轮询
    // ========================================================================
    oGetCursorPos = (tGetCursorPos)HookIAT("user32.dll", "GetCursorPos", (void*)hkGetCursorPos);
    oScreenToClient = (tScreenToClient)HookIAT("user32.dll", "ScreenToClient", (void*)hkScreenToClient);
    oGetAsyncKeyState = (tGetAsyncKeyState)HookIAT("user32.dll", "GetAsyncKeyState", (void*)hkGetAsyncKeyState);
    oGetKeyState = (tGetKeyState)HookIAT("user32.dll", "GetKeyState", (void*)hkGetKeyState);

    return true;
}

// ============================================================================
// [主线程控制]
// ============================================================================
DWORD WINAPI MainControlThread(LPVOID lpParam)
{
    Sleep(3000);

    g_BaseAddr = (uintptr_t)GetModuleHandleA(NULL);

    g_GameHwnd = GetRealGameWindow();
    if (g_GameHwnd)
    {
        g_OriginalWndProc = (WNDPROC)SetWindowLongPtrA(g_GameHwnd, GWL_WNDPROC, (LONG_PTR)GameWndProc);
    }

    SetupInternalD3D9Hook();

    return 0;
}

void UnhookAndCleanup()
{
    if (g_GameHwnd && g_OriginalWndProc)
    {
        SetWindowLongPtrA(g_GameHwnd, GWL_WNDPROC, (LONG_PTR)g_OriginalWndProc);
    }

    if (oGetCursorPos) HookIAT("user32.dll", "GetCursorPos", (void*)oGetCursorPos);
    if (oScreenToClient) HookIAT("user32.dll", "ScreenToClient", (void*)oScreenToClient);
    if (oGetAsyncKeyState) HookIAT("user32.dll", "GetAsyncKeyState", (void*)oGetAsyncKeyState);
    if (oGetKeyState) HookIAT("user32.dll", "GetKeyState", (void*)oGetKeyState);

    if (g_ImGuiInitialized)
    {
        ShutdownRetroSkillApp(g_retroAssets);
        RetroSkillDWriteShutdown();
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_ImGuiInitialized = false;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)MainControlThread, NULL, 0, NULL);
        if (hThread) CloseHandle(hThread);
    }
    else if (ul_reason_for_call == DLL_PROCESS_DETACH)
    {
        UnhookAndCleanup();
    }
    return TRUE;
}