#include <windows.h>
#include <vector>
#include <algorithm>
#include <string>

// ImGui Headers
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

// ============================================================================
// [核心配置] 内存地址常量 (基于索引)
// ============================================================================
const DWORD ADDR_CALLSITE       = 0x50009C1C;
const DWORD ADDR_DRAW_TEXT_MAIN = 0x5000E640;

const DWORD VTABLE_TEXTCTX_0    = 0x50025B14;
const DWORD VTABLE_TEXTCTX_4    = 0x50025B10;
const DWORD VTABLE_TARGET_0     = 0x50025888;
const DWORD VTABLE_TARGET_4     = 0x50025880;

// ============================================================================
// [全局共享数据] 用于 ASM 裸采样与渲染线程交互
// ============================================================================
DWORD g_Captured_TextCtx   = 0;
DWORD g_Captured_DrawState = 0;
DWORD g_Captured_Tick      = 0;
DWORD g_pGetTickCount      = (DWORD)GetTickCount; // 规避 ASM 中的外部链接解析问题
DWORD g_JmpTarget_E640     = ADDR_DRAW_TEXT_MAIN;

// 动态 Target 收集池 (任务 4)
std::vector<DWORD> g_TargetCandidates;
DWORD g_ActiveTarget = 0; // 当前选中的目标画布

// ============================================================================
// [任务1] 纯裸采样 Stub (严格遵守: 栈不偏移, 不用 pushad/popad, 不读对象深层)
// ============================================================================
__declspec(naked) void Callsite_Stub_50009C1C()
{
    __asm {
        // 此时栈顶 [esp] 是 50009C21 (原始函数调用返回地址)
        // [esp+4] 是 drawState, ecx 是 textCtx

        // 1. 裸捕获 TextCtx
        mov dword ptr [g_Captured_TextCtx], ecx

        // 2. 裸捕获 DrawState (用 eax 周转)
        push eax
        mov eax, dword ptr [esp + 8]  // push eax 导致原来 esp+4 的位置变成 esp+8
        mov dword ptr [g_Captured_DrawState], eax
        
        // 3. 裸捕获 Tick 时间戳 (安全压栈 ecx/edx 防止 GetTickCount 破坏)
        push ecx
        push edx
        call dword ptr [g_pGetTickCount]
        mov dword ptr [g_Captured_Tick], eax
        pop edx
        pop ecx

        // 恢复 eax
        pop eax

        // 4. Jmp 到原函数执行真实渲染 (完美保持原始 esp，让其正常 ret)
        jmp dword ptr [g_JmpTarget_E640]
    }
}

// ============================================================================
// [安装 Hook] 手动修改 50009C1C 为 call Stub
// ============================================================================
void InstallTextCallsiteHook()
{
    DWORD oldProtect;
    VirtualProtect((void*)ADDR_CALLSITE, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    
    // 写入 E8 (CALL) 和相对偏移量
    *(BYTE*)ADDR_CALLSITE = 0xE8;
    *(DWORD*)(ADDR_CALLSITE + 1) = (DWORD)Callsite_Stub_50009C1C - ADDR_CALLSITE - 5;
    
    VirtualProtect((void*)ADDR_CALLSITE, 5, oldProtect, &oldProtect);
}

// ============================================================================
// [校验层] (任务2) ValidateTextCtxAndTarget
// ============================================================================
bool IsValidTarget(DWORD targetPtr) {
    if (!targetPtr || IsBadReadPtr((void*)targetPtr, 0x40)) return false;
    return *(DWORD*)targetPtr == VTABLE_TARGET_0 && *(DWORD*)(targetPtr + 4) == VTABLE_TARGET_4;
}

bool IsValidTextCtx(DWORD ctxPtr) {
    if (!ctxPtr || IsBadReadPtr((void*)ctxPtr, 0x40)) return false;
    return *(DWORD*)ctxPtr == VTABLE_TEXTCTX_0 && *(DWORD*)(ctxPtr + 4) == VTABLE_TEXTCTX_4;
}

// ============================================================================
// [接管层] 核心绘制引擎 (任务 3 & 5 & 6)
// ============================================================================
void RenderNativeText(const wchar_t* text, int absX, int absY)
{
    // [时效校验]: 超过 200ms 未抓到游戏文字调用，回退到 ImGui 防奔溃
    if (GetTickCount() - g_Captured_Tick > 200) {
        ImGui::GetForegroundDrawList()->AddText(ImVec2(absX, absY), IM_COL32(255,0,0,255), "Fallback: TextCtx Expired");
        return;
    }

    // [对象头校验]: 每次 draw 前现读现验
    if (!IsValidTextCtx(g_Captured_TextCtx)) return;

    DWORD originalTarget = *(DWORD*)(g_Captured_TextCtx + 0x38);
    if (!IsValidTarget(originalTarget)) return;

    // 收集 Target 作为候选 (任务 4)
    if (std::find(g_TargetCandidates.begin(), g_TargetCandidates.end(), originalTarget) == g_TargetCandidates.end()) {
        g_TargetCandidates.push_back(originalTarget);
        if (g_TargetCandidates.size() > 20) g_TargetCandidates.erase(g_TargetCandidates.begin()); // 保持上限
    }

    // 确定本次绘制使用的活跃目标 (Active Target)
    DWORD activeTarget = g_ActiveTarget; 
    if (!IsValidTarget(activeTarget)) {
        activeTarget = originalTarget; // 没选或者选的无效，回退用自带的
    }

    // [核心算子]: 基于 activeTarget 提取裁剪区基准，重算坐标 (防一坨)
    int targetBaseX = *(int*)(activeTarget + 0x28);
    int targetBaseY = *(int*)(activeTarget + 0x2C);
    int localX = absX - targetBaseX;
    int localY = absY - targetBaseY;

    // [神之一手]: 临时劫持 textCtx 的目标指针
    *(DWORD*)(g_Captured_TextCtx + 0x38) = activeTarget;

    // [原班人马]: 借用 5000E640 函数提交到游戏排版
    typedef void(__thiscall* Sub5000E640_t)(DWORD pThis, DWORD drawState, int x, int y, const wchar_t* text, int alpha, DWORD a7);
    Sub5000E640_t pDrawOutlinedText = (Sub5000E640_t)ADDR_DRAW_TEXT_MAIN;

    pDrawOutlinedText(g_Captured_TextCtx, g_Captured_DrawState, localX, localY, text, 255, 0);

    // [清理战场]: 完璧归赵，防止游戏底层其它系统读取出错
    *(DWORD*)(g_Captured_TextCtx + 0x38) = originalTarget;
}

// ============================================================================
// [UI 层] 演示 UI 和目标选择器
// ============================================================================
void RenderSkillPanelDemo()
{
    ImGui::Begin("Native Text Takeover System", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::Text("TextCtx Captured: %s", (GetTickCount() - g_Captured_Tick < 200) ? "Active (OK)" : "Timeout/Expired");
    ImGui::Text("Current TextCtx : 0x%08X", g_Captured_TextCtx);
    
    ImGui::Separator();
    ImGui::Text("Target Candidate Selector (任务 4)");
    
    // 下拉框选择拦截到的全屏候选 Target
    if (ImGui::BeginCombo("Active Target", g_ActiveTarget == 0 ? "Default" : std::to_string(g_ActiveTarget).c_str()))
    {
        for (DWORD candidate : g_TargetCandidates) {
            bool is_selected = (g_ActiveTarget == candidate);
            char label[64];
            sprintf(label, "Target: 0x%08X", candidate);
            if (ImGui::Selectable(label, is_selected)) {
                g_ActiveTarget = candidate; 
            }
            if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::End();

    // 在屏幕某处绘制游戏原汁原味的文字！(完美测试用例)
    RenderNativeText(L"【接管成功】这行字与游戏100%一致，不发糊！", 200, 200);
    RenderNativeText(L"【接管成功】魔法抗性 Lv.20", 200, 230);
}

// (补充：在此之前依然保留您基于 DX9 EndScene 的 ImGui 主渲染框架，并在里面调用 RenderSkillPanelDemo())