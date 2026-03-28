#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "retro_skill_assets.h"

static bool LoadTextureInternal(LPDIRECT3DDEVICE9 device, const char* filename, UITexture& outTexture)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = stbi_load(filename, &width, &height, &channels, 4);
    if (!data)
        return false;

    HRESULT hr = device->CreateTexture(
        width,
        height,
        1,
        0,
        D3DFMT_A8R8G8B8,
        D3DPOOL_MANAGED,
        &outTexture.texture,
        nullptr
    );

    if (FAILED(hr))
    {
        stbi_image_free(data);
        return false;
    }

    D3DLOCKED_RECT lockedRect;
    hr = outTexture.texture->LockRect(0, &lockedRect, nullptr, 0);
    if (SUCCEEDED(hr))
    {
        for (int y = 0; y < height; y++)
        {
            unsigned char* dstRow = (unsigned char*)lockedRect.pBits + y * lockedRect.Pitch;
            const unsigned char* srcRow = data + y * width * 4;

            for (int x = 0; x < width; x++)
            {
                dstRow[x * 4 + 0] = srcRow[x * 4 + 2];
                dstRow[x * 4 + 1] = srcRow[x * 4 + 1];
                dstRow[x * 4 + 2] = srcRow[x * 4 + 0];
                dstRow[x * 4 + 3] = srcRow[x * 4 + 3];
            }
        }

        outTexture.texture->UnlockRect(0);
    }

    stbi_image_free(data);
    outTexture.width = width;
    outTexture.height = height;
    return SUCCEEDED(hr);
}

bool LoadRetroSkillTexture(LPDIRECT3DDEVICE9 device, const char* filename, UITexture& outTexture)
{
    return LoadTextureInternal(device, filename, outTexture);
}

bool LoadAllRetroSkillAssets(RetroSkillAssets& assets, LPDIRECT3DDEVICE9 device, const char* assetPath)
{
    auto loadOne = [&](const char* key, const char* fileName) {
        UITexture texture;
        std::string path = std::string(assetPath) + fileName;
        if (LoadTextureInternal(device, path.c_str(), texture))
            assets.textures[key] = texture;
    };

    loadOne("main", "SkillEx.main.png");
    loadOne("Passive.0", "SkillEx.Passive.0.png");
    loadOne("Passive.1", "SkillEx.Passive.1.png");
    loadOne("ActivePassive.0", "SkillEx.ActivePassive.0.png");
    loadOne("ActivePassive.1", "SkillEx.ActivePassive.1.png");
    loadOne("initial.normal", "SkillEx.initial.normal.png");
    loadOne("initial.mouseOver", "SkillEx.initial.mouseOver.png");
    loadOne("initial.pressed", "SkillEx.inital.pressed.png");
    loadOne("scroll", "OptionMenu.scroll.2.png");
    loadOne("scroll.pressed", "OptionMenu.scroll.1.png");
    loadOne("BtSpUp.normal", "SkillEx.main.BtSpUp.normal.0.png");
    loadOne("BtSpUp.disabled", "SkillEx.main.BtSpUp.disabled.0.png");
    loadOne("BtSpUp.pressed", "SkillEx.main.BtSpUp.pressed.0.png");
    loadOne("BtSpUp.mouseOver", "SkillEx.main.BtSpUp.mouseOver.0.png");
    loadOne("skill0", "Skill.main.skill0.png");
    loadOne("skill1", "Skill.main.skill1.png");
    loadOne("TypeIcon.0", "SkillEx.TypeIcon.0.png");
    loadOne("TypeIcon.2", "SkillEx.TypeIcon.2.png");
    loadOne("mouse.normal", "System.mouse.normal.png");
    loadOne("mouse.normal.1", "System.mouse.normal.1.png");
    loadOne("mouse.normal.2", "System.mouse.normal.2.png");
    loadOne("mouse.pressed", "System.mouse.pressed.png");
    loadOne("mouse.drag", "System.mouse.Drag.png");

    return true;
}

void CleanupRetroSkillAssets(RetroSkillAssets& assets)
{
    for (auto& pair : assets.textures)
    {
        if (pair.second.texture)
            pair.second.texture->Release();
    }
    assets.textures.clear();
}

UITexture* GetRetroSkillTexture(RetroSkillAssets& assets, const char* name)
{
    auto it = assets.textures.find(name);
    if (it != assets.textures.end())
        return &it->second;
    return nullptr;
}
