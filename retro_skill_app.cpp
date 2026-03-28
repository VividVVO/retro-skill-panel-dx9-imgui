#include "retro_skill_app.h"

#include "imgui.h"
#include "retro_skill_panel.h"

#include <cmath>

namespace
{
    struct DefaultBehaviorController
    {
        RetroSkillRuntimeState* state = nullptr;
    };

    RetroSkillActionDecision OnDefaultTabAction(const RetroSkillActionContext& context, void* userData)
    {
        DefaultBehaviorController* controller = static_cast<DefaultBehaviorController*>(userData);
        if (!controller || !controller->state)
            return RetroSkill_UseDefault;

        RetroSkillRuntimeState& state = *controller->state;
        state.activeTab = context.targetTab;
        state.scrollOffset = 0.0f;
        state.isDraggingSkill = false;
        state.dragSkillTab = -1;
        state.dragSkillIndex = -1;
        return RetroSkill_SuppressDefault;
    }

    RetroSkillActionDecision OnDefaultPlusAction(const RetroSkillActionContext& context, void* userData)
    {
        DefaultBehaviorController* controller = static_cast<DefaultBehaviorController*>(userData);
        if (!controller || !controller->state)
            return RetroSkill_UseDefault;

        RetroSkillRuntimeState& state = *controller->state;
        std::vector<SkillEntry>& skills = (state.activeTab == 0) ? state.passiveSkills : state.activeSkills;
        if (context.skillIndex < 0 || context.skillIndex >= (int)skills.size())
            return RetroSkill_SuppressDefault;

        SkillEntry& skill = skills[(size_t)context.skillIndex];
        if (skill.level < skill.maxLevel)
            skill.level++;
        return RetroSkill_SuppressDefault;
    }

    RetroSkillActionDecision OnDefaultInitAction(const RetroSkillActionContext& context, void* userData)
    {
        DefaultBehaviorController* controller = static_cast<DefaultBehaviorController*>(userData);
        if (!controller || !controller->state)
            return RetroSkill_UseDefault;

        RetroSkillRuntimeState& state = *controller->state;
        std::vector<SkillEntry>& skills = (context.currentTab == 0) ? state.passiveSkills : state.activeSkills;
        for (SkillEntry& skill : skills)
            skill.level = 0;

        return RetroSkill_SuppressDefault;
    }

    static DefaultBehaviorController g_defaultBehaviorController;
}

enum MouseState { MS_NORMAL, MS_PRESSED, MS_DRAG, MS_HOVER_INSTANT, MS_HOVER_LOOP_A, MS_HOVER_LOOP_B };

void InitializeRetroSkillApp(RetroSkillRuntimeState& state, RetroSkillAssets& assets, LPDIRECT3DDEVICE9 device, const char* assetPath)
{
    ResetRetroSkillData(state);
    LoadAllRetroSkillAssets(assets, device, assetPath);
}

void ShutdownRetroSkillApp(RetroSkillAssets& assets)
{
    CleanupRetroSkillAssets(assets);
}

void ConfigureRetroSkillDefaultBehaviorHooks(RetroSkillBehaviorHooks& hooks, RetroSkillRuntimeState& state)
{
    g_defaultBehaviorController.state = &state;
    hooks.userData = &g_defaultBehaviorController;
    hooks.onTabAction = OnDefaultTabAction;
    hooks.onPlusAction = OnDefaultPlusAction;
    hooks.onInitAction = OnDefaultInitAction;
}

void RenderRetroSkillScene(RetroSkillRuntimeState& state, RetroSkillAssets& assets, LPDIRECT3DDEVICE9 device, float mainScale)
{
    RenderRetroSkillSceneEx(state, assets, device, mainScale, nullptr);
}

void RenderRetroSkillSceneEx(RetroSkillRuntimeState& state, RetroSkillAssets& assets, LPDIRECT3DDEVICE9 device, float mainScale, const RetroSkillBehaviorHooks* hooks)
{
    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("Scene", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoBackground);

    RenderRetroSkillPanel(state, assets, device, mainScale, hooks);

    if (state.isDraggingSkill && ImGui::IsMouseClicked(0) && !state.dragSkillStartedThisFrame)
    {
        state.isDraggingSkill = false;
        state.dragSkillTab = -1;
        state.dragSkillIndex = -1;
    }

    bool isMouseDown = io.MouseDown[0];
    MouseState mouseState = MS_NORMAL;
    if (state.isDraggingSkill)
    {
        mouseState = MS_DRAG;
    }
    else if (isMouseDown && state.isPressingUiButton)
    {
        mouseState = MS_PRESSED;
    }
    else if (state.isHoveringActionButtons && !state.actionHoverStoppedByClick)
    {
        double hoverElapsed = ImGui::GetTime() - state.actionButtonsHoverStartTime;
        if (hoverElapsed < 0.5)
            mouseState = state.actionHoverInstantUseNormal1 ? MS_HOVER_LOOP_A : MS_HOVER_LOOP_B;
        else
        {
            double loopTime = hoverElapsed - 0.5;
            int loopFrame = (int)floor(loopTime / 0.5);
            mouseState = (loopFrame % 2 == 0) ? MS_HOVER_LOOP_A : MS_HOVER_LOOP_B;
        }
    }

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 mousePos = io.MousePos;

    UITexture* mouseTex = nullptr;
    switch (mouseState)
    {
        case MS_NORMAL: mouseTex = GetRetroSkillTexture(assets, "mouse.normal"); break;
        case MS_PRESSED: mouseTex = GetRetroSkillTexture(assets, "mouse.pressed"); break;
        case MS_DRAG: mouseTex = GetRetroSkillTexture(assets, "mouse.drag"); break;
        case MS_HOVER_INSTANT: mouseTex = GetRetroSkillTexture(assets, "mouse.normal.2"); break;
        case MS_HOVER_LOOP_A: mouseTex = GetRetroSkillTexture(assets, "mouse.normal.1"); break;
        case MS_HOVER_LOOP_B: mouseTex = GetRetroSkillTexture(assets, "mouse.normal.2"); break;
    }

    if ((!mouseTex || !mouseTex->texture) && mouseState == MS_HOVER_INSTANT)
        mouseTex = GetRetroSkillTexture(assets, "mouse.normal");
    if ((!mouseTex || !mouseTex->texture) && mouseState == MS_HOVER_LOOP_A)
        mouseTex = GetRetroSkillTexture(assets, "mouse.normal");

    if (mouseTex && mouseTex->texture)
    {
        ImVec2 cursorMin(mousePos.x, mousePos.y);
        ImVec2 cursorMax(mousePos.x + mouseTex->width * mainScale,
                       mousePos.y + mouseTex->height * mainScale);
        dl->AddImage((ImTextureID)mouseTex->texture, cursorMin, cursorMax);
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
}
