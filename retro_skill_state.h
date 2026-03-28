#pragma once

#include "imgui.h"
#include <vector>

struct SkillEntry {
    const char* name;
    int level;
    int maxLevel;
    ImU32 iconColor;
};

struct PanelMetrics {
    float shadow;
    float width;
    float height;
    float rounding;
    float titleHeight;
    float tabHeight;
    float tabWidth;
    float infoHeight;
    float rowHeight;
    float footerHeight;
    float outerPadding;
    float innerPadding;
    float iconSize;
};

inline PanelMetrics GetPanelMetrics(float mainScale)
{
    return {
        0.0f,
        174.0f * mainScale,
        299.0f * mainScale,
        0.0f,
        0.0f,
        20.0f * mainScale,
        76.0f * mainScale,
        24.0f * mainScale,
        36.0f * mainScale,
        24.0f * mainScale,
        0.0f,
        0.0f,
        32.0f * mainScale
    };
}

struct RetroSkillRuntimeState {
    std::vector<SkillEntry> passiveSkills;
    std::vector<SkillEntry> activeSkills;

    int activeTab = 0;
    float dragTitleHeight = 28.0f;

    bool isDraggingSkill = false;
    int dragSkillTab = -1;
    int dragSkillIndex = -1;
    ImVec2 dragSkillGrabOffset = ImVec2(0.0f, 0.0f);
    bool dragSkillStartedThisFrame = false;

    bool isPressingUiButton = false;
    bool isHoldingActionButton = false;
    bool isHoveringActionButtons = false;
    bool actionHoverStoppedByClick = false;
    bool actionHoverInstantUseNormal1 = false;
    double actionButtonsHoverStartTime = 0.0;

    double minClickIntervalSeconds = 0.3;
    double lastAcceptedClickTime = -1.0;

    float scrollOffset = 0.0f;
    bool isScrollDragging = false;
    float scrollDragStartY = 0.0f;
    float scrollDragStartOffset = 0.0f;

    bool titlePressLockedUntilRelease = false;
};

void ResetRetroSkillData(RetroSkillRuntimeState& state);
