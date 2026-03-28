#pragma once

#include "imgui.h"

#include <d3d9.h>

bool RetroSkillDWriteInitialize(LPDIRECT3DDEVICE9 device);
void RetroSkillDWriteShutdown();
void RetroSkillDWriteOnDeviceLost();
void RetroSkillDWriteOnDeviceReset(LPDIRECT3DDEVICE9 device);
bool RetroSkillDWriteDrawText(ImDrawList* drawList, const ImVec2& pos, ImU32 color, const char* text, float fontSize);
