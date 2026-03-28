#pragma once

#include "retro_skill_assets.h"
#include "retro_skill_state.h"
#include <d3d9.h>

struct RetroSkillBehaviorHooks;

void RenderRetroSkillPanel(RetroSkillRuntimeState& state, RetroSkillAssets& assets, LPDIRECT3DDEVICE9 device, float mainScale, const RetroSkillBehaviorHooks* hooks);
