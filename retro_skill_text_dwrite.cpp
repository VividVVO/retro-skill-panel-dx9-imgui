#include "retro_skill_text_dwrite.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwrite.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    template <typename T>
    void SafeRelease(T*& ptr)
    {
        if (ptr)
        {
            ptr->Release();
            ptr = nullptr;
        }
    }

    struct TextCacheEntry
    {
        LPDIRECT3DTEXTURE9 texture = nullptr;
        int width = 0;
        int height = 0;
    };

    struct TextCacheKey
    {
        std::wstring text;
        int fontPx = 0;
        ImU32 color = 0;

        bool operator==(const TextCacheKey& other) const
        {
            return fontPx == other.fontPx && color == other.color && text == other.text;
        }
    };

    struct TextCacheKeyHasher
    {
        size_t operator()(const TextCacheKey& key) const
        {
            size_t h1 = std::hash<std::wstring>{}(key.text);
            size_t h2 = std::hash<int>{}(key.fontPx);
            size_t h3 = std::hash<unsigned int>{}(key.color);
            return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2)) ^ (h3 + 0x9e3779b9 + (h2 << 6) + (h2 >> 2));
        }
    };

    struct DWriteRuntime
    {
        LPDIRECT3DDEVICE9 device = nullptr;
        IDWriteFactory* factory = nullptr;
        IDWriteTextFormat* textFormat = nullptr;
        std::unordered_map<TextCacheKey, TextCacheEntry, TextCacheKeyHasher> cache;
    };

    DWriteRuntime g_runtime;

    std::wstring Utf8ToWide(const char* text)
    {
        if (!text || !text[0])
            return std::wstring();

        int needed = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
        if (needed <= 1)
        {
            needed = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
            if (needed <= 1)
                return std::wstring();

            std::wstring out((size_t)needed - 1, L'\0');
            MultiByteToWideChar(CP_ACP, 0, text, -1, &out[0], needed);
            return out;
        }

        std::wstring out((size_t)needed - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text, -1, &out[0], needed);
        return out;
    }

    void ClearTextureCache()
    {
        for (auto& pair : g_runtime.cache)
            SafeRelease(pair.second.texture);
        g_runtime.cache.clear();
    }

    TextCacheEntry* BuildTextEntry(const TextCacheKey& key)
    {
        if (!g_runtime.device || !g_runtime.factory || !g_runtime.textFormat)
            return nullptr;

        IDWriteTextLayout* textLayout = nullptr;
        const WCHAR* wideText = key.text.c_str();
        const UINT32 textLen = (UINT32)key.text.length();

        HRESULT hr = g_runtime.factory->CreateTextLayout(
            wideText,
            textLen,
            g_runtime.textFormat,
            1024.0f,
            256.0f,
            &textLayout);
        if (FAILED(hr) || !textLayout)
            return nullptr;

        textLayout->SetFontSize((FLOAT)key.fontPx, DWRITE_TEXT_RANGE{ 0, textLen });
        textLayout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

        DWRITE_TEXT_METRICS metrics = {};
        hr = textLayout->GetMetrics(&metrics);
        if (FAILED(hr))
        {
            textLayout->Release();
            return nullptr;
        }

        int width = (int)std::ceil(metrics.widthIncludingTrailingWhitespace) + 4;
        int height = (int)std::ceil(metrics.height) + 4;
        if (width < 1) width = 1;
        if (height < 1) height = 1;
        if (width > 2048) width = 2048;
        if (height > 512) height = 512;

        const int supersample = 2;
        int widthSS = width * supersample;
        int heightSS = height * supersample;

        HDC memDc = CreateCompatibleDC(nullptr);
        if (!memDc)
        {
            textLayout->Release();
            return nullptr;
        }

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = widthSS;
        bmi.bmiHeader.biHeight = -heightSS;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* dibBits = nullptr;
        HBITMAP dib = CreateDIBSection(memDc, &bmi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
        if (!dib || !dibBits)
        {
            if (dib)
                DeleteObject(dib);
            DeleteDC(memDc);
            textLayout->Release();
            return nullptr;
        }

        HGDIOBJ oldBitmap = SelectObject(memDc, dib);
        std::memset(dibBits, 0, (size_t)widthSS * (size_t)heightSS * 4);

        HFONT font = CreateFontW(
            -(key.fontPx * supersample),
            0,
            0,
            0,
            FW_MEDIUM,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Microsoft YaHei");

        HGDIOBJ oldFont = nullptr;
        if (font)
            oldFont = SelectObject(memDc, font);

        SetBkMode(memDc, TRANSPARENT);
        SetTextColor(memDc, RGB(255, 255, 255));
        TextOutW(memDc, 2 * supersample, supersample, wideText, (int)textLen);

        const unsigned char targetR = (unsigned char)((key.color >> IM_COL32_R_SHIFT) & 0xFFu);
        const unsigned char targetG = (unsigned char)((key.color >> IM_COL32_G_SHIFT) & 0xFFu);
        const unsigned char targetB = (unsigned char)((key.color >> IM_COL32_B_SHIFT) & 0xFFu);

        std::vector<uint32_t> pixels((size_t)width * (size_t)height, 0u);
        uint32_t* srcPixels = reinterpret_cast<uint32_t*>(dibBits);

        for (int dy = 0; dy < height; ++dy)
        {
            for (int dx = 0; dx < width; ++dx)
            {
                int srcX = dx * supersample;
                int srcY = dy * supersample;

                int accum = 0;
                for (int sy = 0; sy < supersample; ++sy)
                {
                    for (int sx = 0; sx < supersample; ++sx)
                    {
                        uint32_t src = srcPixels[(size_t)(srcY + sy) * (size_t)widthSS + (size_t)(srcX + sx)];
                        unsigned char b = (unsigned char)(src & 0xFFu);
                        unsigned char g = (unsigned char)((src >> 8) & 0xFFu);
                        unsigned char r = (unsigned char)((src >> 16) & 0xFFu);
                        accum += (int)((r + g + b) / 3);
                    }
                }

                int avg = accum / (supersample * supersample);
                float normalized = (float)avg / 255.0f;
                float contrasted = std::pow(normalized, 0.82f);
                int a = (int)(contrasted * 255.0f + 0.5f);
                if (a < 8)
                    a = 0;
                if (a > 255)
                    a = 255;

                if (a == 0)
                    pixels[(size_t)dy * (size_t)width + (size_t)dx] = 0u;
                else
                    pixels[(size_t)dy * (size_t)width + (size_t)dx] = ((uint32_t)a << 24) |
                                                                    ((uint32_t)targetR << 16) |
                                                                    ((uint32_t)targetG << 8) |
                                                                    ((uint32_t)targetB);
            }
        }

        if (oldFont)
            SelectObject(memDc, oldFont);
        if (font)
            DeleteObject(font);
        SelectObject(memDc, oldBitmap);
        DeleteObject(dib);
        DeleteDC(memDc);

        textLayout->Release();

        LPDIRECT3DTEXTURE9 texture = nullptr;
        hr = g_runtime.device->CreateTexture(
            width,
            height,
            1,
            D3DUSAGE_DYNAMIC,
            D3DFMT_A8R8G8B8,
            D3DPOOL_DEFAULT,
            &texture,
            nullptr);

        if (FAILED(hr) || !texture)
            return nullptr;

        D3DLOCKED_RECT locked = {};
        hr = texture->LockRect(0, &locked, nullptr, 0);
        if (FAILED(hr))
        {
            SafeRelease(texture);
            return nullptr;
        }

        for (int y = 0; y < height; ++y)
        {
            unsigned char* dstRow = (unsigned char*)locked.pBits + (size_t)y * (size_t)locked.Pitch;
            const unsigned char* srcRow = (const unsigned char*)pixels.data() + (size_t)y * (size_t)width * 4;
            std::memcpy(dstRow, srcRow, (size_t)width * 4);
        }

        texture->UnlockRect(0);

        auto inserted = g_runtime.cache.emplace(key, TextCacheEntry{ texture, width, height });
        if (!inserted.second)
        {
            SafeRelease(texture);
            return &inserted.first->second;
        }
        return &inserted.first->second;
    }
}

bool RetroSkillDWriteInitialize(LPDIRECT3DDEVICE9 device)
{
    RetroSkillDWriteShutdown();

    g_runtime.device = device;
    if (!g_runtime.device)
        return false;

    HRESULT hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&g_runtime.factory));
    if (FAILED(hr) || !g_runtime.factory)
    {
        RetroSkillDWriteShutdown();
        return false;
    }

    hr = g_runtime.factory->CreateTextFormat(
        L"Microsoft YaHei",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        14.0f,
        L"zh-cn",
        &g_runtime.textFormat);
    if (FAILED(hr) || !g_runtime.textFormat)
    {
        RetroSkillDWriteShutdown();
        return false;
    }

    g_runtime.textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    g_runtime.textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    g_runtime.textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    return true;
}

void RetroSkillDWriteShutdown()
{
    ClearTextureCache();
    SafeRelease(g_runtime.textFormat);
    SafeRelease(g_runtime.factory);
    g_runtime.device = nullptr;
}

void RetroSkillDWriteOnDeviceLost()
{
    ClearTextureCache();
}

void RetroSkillDWriteOnDeviceReset(LPDIRECT3DDEVICE9 device)
{
    g_runtime.device = device;
    ClearTextureCache();
}

bool RetroSkillDWriteDrawText(ImDrawList* drawList, const ImVec2& pos, ImU32 color, const char* text, float fontSize)
{
    if (!drawList || !text || !text[0])
        return true;

    if (!g_runtime.device || !g_runtime.factory || !g_runtime.textFormat)
        return false;

    std::wstring wide = Utf8ToWide(text);
    if (wide.empty())
        return false;

    int fontPx = (int)std::round((fontSize > 0.0f) ? fontSize : ImGui::GetFontSize());
    if (fontPx < 1)
        fontPx = 1;

    TextCacheKey key{ wide, fontPx, color };

    TextCacheEntry* entry = nullptr;
    auto it = g_runtime.cache.find(key);
    if (it != g_runtime.cache.end())
        entry = &it->second;
    else
        entry = BuildTextEntry(key);

    if (!entry || !entry->texture || entry->width <= 0 || entry->height <= 0)
        return false;

    ImVec2 dstMin(pos.x, pos.y);
    ImVec2 dstMax(pos.x + (float)entry->width, pos.y + (float)entry->height);
    drawList->AddImage((ImTextureID)entry->texture, dstMin, dstMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
    return true;
}
