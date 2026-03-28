#pragma once

#include <d3d9.h>
#include <map>
#include <string>

struct UITexture {
    LPDIRECT3DTEXTURE9 texture = nullptr;
    int width = 0;
    int height = 0;
};

struct RetroSkillAssets {
    std::map<std::string, UITexture> textures;
};

bool LoadRetroSkillTexture(LPDIRECT3DDEVICE9 device, const char* filename, UITexture& outTexture);
bool LoadAllRetroSkillAssets(RetroSkillAssets& assets, LPDIRECT3DDEVICE9 device, const char* assetPath);
void CleanupRetroSkillAssets(RetroSkillAssets& assets);
UITexture* GetRetroSkillTexture(RetroSkillAssets& assets, const char* name);
