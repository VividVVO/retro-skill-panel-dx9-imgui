#include "retro_skill_state.h"

void ResetRetroSkillData(RetroSkillRuntimeState& state)
{
    state.passiveSkills = {
        {"连击强化", 0, 1, IM_COL32(165, 108, 214, 255)},
        {"白眼",     1, 1, IM_COL32(148,  74, 194, 255)},
        {"额外攻击", 0, 1, IM_COL32(191, 107, 107, 255)},
        {"铃兰持续", 1, 1, IM_COL32(217, 138, 226, 255)},
        {"樱速灵",   0, 1, IM_COL32(226, 170, 170, 255)},
        {"铃兰减速", 1, 1, IM_COL32(180, 132, 232, 255)}
    };

    state.activeSkills = {
        {"五行归一", 0, 1, IM_COL32(203, 132,  67, 255)},
        {"灵峰召唤", 0, 1, IM_COL32( 92, 132, 194, 255)},
        {"破魔符",   0, 1, IM_COL32(120, 164, 120, 255)},
        {"元素爆发", 0, 1, IM_COL32(180, 140, 200, 255)},
        {"瞬移术",   0, 1, IM_COL32(150, 100, 180, 255)},
        {"护盾墙",   0, 1, IM_COL32(100, 150, 200, 255)},
        {"火焰风暴", 0, 1, IM_COL32(200, 100, 100, 255)},
        {"冰封千里", 0, 1, IM_COL32(100, 200, 220, 255)}
    };

    state.activeTab = 0;
    state.dragTitleHeight = 28.0f;

    state.isDraggingSkill = false;
    state.dragSkillTab = -1;
    state.dragSkillIndex = -1;
    state.dragSkillGrabOffset = ImVec2(0.0f, 0.0f);
    state.dragSkillStartedThisFrame = false;

    state.isPressingUiButton = false;
    state.isHoldingActionButton = false;
    state.isHoveringActionButtons = false;
    state.actionHoverStoppedByClick = false;
    state.actionHoverInstantUseNormal1 = false;
    state.actionButtonsHoverStartTime = 0.0;

    state.minClickIntervalSeconds = 0.3;
    state.lastAcceptedClickTime = -1.0;

    state.scrollOffset = 0.0f;
    state.isScrollDragging = false;
    state.scrollDragStartY = 0.0f;
    state.scrollDragStartOffset = 0.0f;

    state.titlePressLockedUntilRelease = false;
}
