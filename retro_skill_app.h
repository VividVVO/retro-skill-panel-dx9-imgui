#pragma once

#include "retro_skill_assets.h"
#include "retro_skill_state.h"

#include <d3d9.h>

enum RetroSkillActionDecision {
    RetroSkill_UseDefault = 0,
    RetroSkill_SuppressDefault = 1
};

struct RetroSkillActionContext {
    int currentTab = 0;
    int targetTab = -1;
    int skillIndex = -1;
    int currentLevel = 0;
    int maxLevel = 0;
};

typedef RetroSkillActionDecision (*RetroSkillTabActionCallback)(const RetroSkillActionContext& context, void* userData);
typedef RetroSkillActionDecision (*RetroSkillPlusActionCallback)(const RetroSkillActionContext& context, void* userData);
typedef RetroSkillActionDecision (*RetroSkillInitActionCallback)(const RetroSkillActionContext& context, void* userData);

struct RetroSkillBehaviorHooks {
    RetroSkillTabActionCallback onTabAction = nullptr;
    RetroSkillPlusActionCallback onPlusAction = nullptr;
    RetroSkillInitActionCallback onInitAction = nullptr;
    void* userData = nullptr;
};

void InitializeRetroSkillApp(RetroSkillRuntimeState& state, RetroSkillAssets& assets, LPDIRECT3DDEVICE9 device, const char* assetPath);
void ShutdownRetroSkillApp(RetroSkillAssets& assets);
void ConfigureRetroSkillDefaultBehaviorHooks(RetroSkillBehaviorHooks& hooks, RetroSkillRuntimeState& state);
void RenderRetroSkillScene(RetroSkillRuntimeState& state, RetroSkillAssets& assets, LPDIRECT3DDEVICE9 device, float mainScale);
void RenderRetroSkillSceneEx(RetroSkillRuntimeState& state, RetroSkillAssets& assets, LPDIRECT3DDEVICE9 device, float mainScale, const RetroSkillBehaviorHooks* hooks);
