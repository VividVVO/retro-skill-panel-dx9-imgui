#include "retro_skill_panel.h"

#include "retro_skill_app.h"
#include "retro_skill_text_dwrite.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <string>

static void DrawOutlinedText(ImDrawList* dl, const ImVec2& pos, ImU32 color, const char* text, float mainScale, float fontSize = 0.0f)
{
    const ImVec2 alignedPos(floorf(pos.x), floorf(pos.y));
    (void)mainScale;

    const float resolvedFontSize = (fontSize > 0.0f) ? fontSize : ImGui::GetFontSize();
    if (RetroSkillDWriteDrawText(dl, alignedPos, color, text, resolvedFontSize))
        return;

    if (fontSize > 0.0f)
    {
        ImFont* font = ImGui::GetFont();
        dl->AddText(font, fontSize, alignedPos, color, text);
        return;
    }

    dl->AddText(alignedPos, color, text);
}

void RenderRetroSkillPanel(RetroSkillRuntimeState& state, RetroSkillAssets& assets, LPDIRECT3DDEVICE9 device, float mainScale, const RetroSkillBehaviorHooks* hooks)
{
    ImGuiIO& io = ImGui::GetIO();
    const PanelMetrics m = GetPanelMetrics(mainScale);
    state.dragTitleHeight = 20.0f * mainScale;

    state.dragSkillStartedThisFrame = false;
    state.isPressingUiButton = false;

    bool isHoveringActionButtonsThisFrame = false;
    bool actionButtonsClickedThisFrame = false;

    auto shouldSuppressTabAction = [&](int targetTab) {
        if (!hooks || !hooks->onTabAction)
            return false;
        RetroSkillActionContext context;
        context.currentTab = state.activeTab;
        context.targetTab = targetTab;
        return hooks->onTabAction(context, hooks->userData) == RetroSkill_SuppressDefault;
    };

    auto shouldSuppressPlusAction = [&](int skillIndex, const SkillEntry& skill) {
        if (!hooks || !hooks->onPlusAction)
            return false;
        RetroSkillActionContext context;
        context.currentTab = state.activeTab;
        context.targetTab = state.activeTab;
        context.skillIndex = skillIndex;
        context.currentLevel = skill.level;
        context.maxLevel = skill.maxLevel;
        return hooks->onPlusAction(context, hooks->userData) == RetroSkill_SuppressDefault;
    };

    auto shouldSuppressInitAction = [&]() {
        if (!hooks || !hooks->onInitAction)
            return false;
        RetroSkillActionContext context;
        context.currentTab = state.activeTab;
        context.targetTab = state.activeTab;
        return hooks->onInitAction(context, hooks->userData) == RetroSkill_SuppressDefault;
    };

    auto canAcceptActionClick = [&]() {
        double now = ImGui::GetTime();
        if (state.lastAcceptedClickTime < 0.0)
            return true;
        return (now - state.lastAcceptedClickTime) >= state.minClickIntervalSeconds;
    };

    auto markActionClickAccepted = [&]() {
        state.lastAcceptedClickTime = ImGui::GetTime();
    };

    ImGui::SetNextWindowSize(ImVec2(m.width, m.height), ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoMove;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("Super Skill Panel", nullptr, flags))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 panelPos = wp;

        ImGui::SetCursorScreenPos(panelPos);
        ImGui::PushID("title_drag_zone");
        ImGui::InvisibleButton("title_drag_zone", ImVec2(m.width, state.dragTitleHeight));
        bool titleHovered = ImGui::IsItemHovered();
        bool titleHeld = ImGui::IsItemActive() && io.MouseDown[0];
        ImGui::PopID();

        if (!io.MouseDown[0])
            state.titlePressLockedUntilRelease = false;

        if (titleHeld && !titleHovered)
            state.titlePressLockedUntilRelease = true;

        if (titleHeld && titleHovered && !state.titlePressLockedUntilRelease)
            state.isPressingUiButton = true;

        UITexture* mainTex = GetRetroSkillTexture(assets, "main");
        if (mainTex && mainTex->texture)
        {
            dl->AddImage((ImTextureID)mainTex->texture,
                        panelPos,
                        ImVec2(panelPos.x + mainTex->width * mainScale,
                               panelPos.y + mainTex->height * mainScale));
        }

        ImGui::SetCursorScreenPos(ImVec2(panelPos.x, panelPos.y + state.dragTitleHeight));
        ImGui::PushID("panel_body_drag_zone");
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("panel_body_drag_zone", ImVec2(m.width, m.height - state.dragTitleHeight));
        bool panelBodyHovered = ImGui::IsItemHovered();
        ImGui::PopID();

        if (panelBodyHovered)
            state.isPressingUiButton = state.isPressingUiButton || io.MouseDown[0];

        ImVec2 passiveTabPos(panelPos.x + 10.0f * mainScale, panelPos.y + 27.0f * mainScale);
        ImVec2 activeTabPos(panelPos.x + 89.0f * mainScale, panelPos.y + 27.0f * mainScale);

        ImGui::SetCursorScreenPos(passiveTabPos);
        ImGui::PushID("passive_tab");
        ImGui::InvisibleButton("passive_tab", ImVec2(76.0f * mainScale, 20.0f * mainScale));
        bool passiveClicked = ImGui::IsItemClicked();
        bool passiveHeld = ImGui::IsItemActive() && io.MouseDown[0];
        ImGui::PopID();

        ImGui::SetCursorScreenPos(activeTabPos);
        ImGui::PushID("active_tab");
        ImGui::InvisibleButton("active_tab", ImVec2(76.0f * mainScale, 20.0f * mainScale));
        bool activeClicked = ImGui::IsItemClicked();
        bool activeHeld = ImGui::IsItemActive() && io.MouseDown[0];
        ImGui::PopID();

        if (passiveHeld || activeHeld)
            state.isPressingUiButton = true;

        if (passiveClicked)
        {
            if (canAcceptActionClick())
            {
                bool suppressDefault = shouldSuppressTabAction(0);
                if (!suppressDefault)
                {
                    state.activeTab = 0;
                    state.scrollOffset = 0.0f;
                    state.isDraggingSkill = false;
                    state.dragSkillTab = -1;
                    state.dragSkillIndex = -1;
                }
                markActionClickAccepted();
            }
        }

        if (activeClicked)
        {
            if (canAcceptActionClick())
            {
                bool suppressDefault = shouldSuppressTabAction(1);
                if (!suppressDefault)
                {
                    state.activeTab = 1;
                    state.scrollOffset = 0.0f;
                    state.isDraggingSkill = false;
                    state.dragSkillTab = -1;
                    state.dragSkillIndex = -1;
                }
                markActionClickAccepted();
            }
        }

        if (state.activeTab == 0)
        {
            UITexture* passiveActiveTex = GetRetroSkillTexture(assets, "Passive.1");
            if (passiveActiveTex && passiveActiveTex->texture)
            {
                dl->AddImage((ImTextureID)passiveActiveTex->texture,
                            passiveTabPos,
                            ImVec2(passiveTabPos.x + passiveActiveTex->width * mainScale,
                                   passiveTabPos.y + passiveActiveTex->height * mainScale));
            }
        }
        else
        {
            UITexture* passiveNormalTex = GetRetroSkillTexture(assets, "Passive.0");
            if (passiveNormalTex && passiveNormalTex->texture)
            {
                dl->AddImage((ImTextureID)passiveNormalTex->texture,
                            passiveTabPos,
                            ImVec2(passiveTabPos.x + passiveNormalTex->width * mainScale,
                                   passiveTabPos.y + passiveNormalTex->height * mainScale));
            }
        }

        if (state.activeTab == 1)
        {
            UITexture* activeActiveTex = GetRetroSkillTexture(assets, "ActivePassive.1");
            if (activeActiveTex && activeActiveTex->texture)
            {
                ImVec2 activeActivePos(activeTabPos.x - 1.0f * mainScale, activeTabPos.y);
                dl->AddImage((ImTextureID)activeActiveTex->texture,
                            activeActivePos,
                            ImVec2(activeActivePos.x + activeActiveTex->width * mainScale,
                                   activeActivePos.y + activeActiveTex->height * mainScale));
            }
        }
        else
        {
            UITexture* activeNormalTex = GetRetroSkillTexture(assets, "ActivePassive.0");
            if (activeNormalTex && activeNormalTex->texture)
            {
                ImVec2 activeNormalPos(activeTabPos.x - 1.0f * mainScale, activeTabPos.y);
                dl->AddImage((ImTextureID)activeNormalTex->texture,
                            activeNormalPos,
                            ImVec2(activeNormalPos.x + activeNormalTex->width * mainScale,
                                   activeNormalPos.y + activeNormalTex->height * mainScale));
            }
        }

        auto& skills = (state.activeTab == 0) ? state.passiveSkills : state.activeSkills;
        float listStartY = panelPos.y + 93.0f * mainScale;
        float listEndY = panelPos.y + 248.0f * mainScale;
        float listX = panelPos.x + 9.0f * mainScale;
        float skillBoxHeight = 35.0f * mainScale;
        float skillGap = 5.0f * mainScale;

        int totalSkills = (int)skills.size();
        int visibleSkills = 4;
        float totalSkillHeight = skillBoxHeight + skillGap;
        float maxScrollOffset = (float)((totalSkills > visibleSkills) ? (totalSkills - visibleSkills) * totalSkillHeight : 0.0f);
        int maxScrollIndex = (totalSkills > visibleSkills) ? (totalSkills - visibleSkills) : 0;

        if (state.scrollOffset < 0.0f) state.scrollOffset = 0.0f;
        if (state.scrollOffset > maxScrollOffset) state.scrollOffset = maxScrollOffset;

        bool isMouseOverList = io.MousePos.x >= listX && io.MousePos.x <= (listX + 140.0f * mainScale) &&
                               io.MousePos.y >= listStartY && io.MousePos.y <= listEndY;
        if (isMouseOverList && !state.isScrollDragging && io.MouseWheel != 0.0f && maxScrollIndex > 0)
        {
            int scrollIndex = (int)round(state.scrollOffset / totalSkillHeight);
            if (io.MouseWheel > 0.0f)
                scrollIndex--;
            else if (io.MouseWheel < 0.0f)
                scrollIndex++;

            if (scrollIndex < 0) scrollIndex = 0;
            if (scrollIndex > maxScrollIndex) scrollIndex = maxScrollIndex;
            state.scrollOffset = (float)scrollIndex * totalSkillHeight;
        }

        for (size_t i = 0; i < skills.size(); ++i)
        {
            SkillEntry& skill = skills[i];
            float skillY = listStartY + i * totalSkillHeight - state.scrollOffset;

            if (skillY + skillBoxHeight < listStartY || skillY > listEndY)
                continue;

            ImVec2 skillBoxPos(listX, skillY);
            bool canLearn = skill.level < skill.maxLevel;
            const char* skillFrameKey = canLearn ? "skill1" : "skill0";

            UITexture* skillBoxTex = GetRetroSkillTexture(assets, skillFrameKey);
            if (skillBoxTex && skillBoxTex->texture)
            {
                dl->AddImage((ImTextureID)skillBoxTex->texture,
                            skillBoxPos,
                            ImVec2(skillBoxPos.x + skillBoxTex->width * mainScale,
                                   skillBoxPos.y + skillBoxTex->height * mainScale));
            }

            ImVec2 iconPos(skillBoxPos.x + 3.0f * mainScale, skillBoxPos.y + 2.0f * mainScale);
            ImVec2 iconSize(32.0f * mainScale, 32.0f * mainScale);

            if (!state.isScrollDragging)
            {
                ImGui::SetCursorScreenPos(iconPos);
                ImGui::PushID(("skill_drag_" + std::to_string(i)).c_str());
                ImGui::InvisibleButton("skill_drag", iconSize);

                if (ImGui::IsItemClicked())
                {
                    if (state.isDraggingSkill)
                    {
                        if (!(state.dragSkillStartedThisFrame && state.dragSkillTab == state.activeTab && state.dragSkillIndex == (int)i))
                        {
                            state.isDraggingSkill = false;
                            state.dragSkillTab = -1;
                            state.dragSkillIndex = -1;
                        }
                    }
                    else
                    {
                        state.isDraggingSkill = true;
                        state.dragSkillTab = state.activeTab;
                        state.dragSkillIndex = (int)i;
                        state.dragSkillGrabOffset = ImVec2(io.MousePos.x - iconPos.x, io.MousePos.y - iconPos.y);
                        state.dragSkillStartedThisFrame = true;
                    }
                }

                ImGui::PopID();
            }

            bool isThisSkillDragging = state.isDraggingSkill && (state.dragSkillTab == state.activeTab) && (state.dragSkillIndex == (int)i);
            if (!isThisSkillDragging)
            {
                dl->AddRectFilled(iconPos, ImVec2(iconPos.x + iconSize.x, iconPos.y + iconSize.y),
                                 skill.iconColor, 4.0f * mainScale);
                dl->AddRect(iconPos, ImVec2(iconPos.x + iconSize.x, iconPos.y + iconSize.y),
                           IM_COL32(25, 27, 31, 255), 4.0f * mainScale, 0, 1.0f * mainScale);
            }

            ImVec2 textPos(skillBoxPos.x + 41.0f * mainScale, skillBoxPos.y + 1.0f * mainScale);
            DrawOutlinedText(dl, textPos, IM_COL32(34, 34, 34, 255), skill.name, mainScale);

            char levelText[32];
            sprintf_s(levelText, "%d", skill.level);
            DrawOutlinedText(dl, ImVec2(floorf(textPos.x), floorf(textPos.y + 19.0f * mainScale)), IM_COL32(34, 34, 34, 255), levelText, mainScale, floorf(9.0f * mainScale));

            const char* typeIconKey = (state.activeTab == 0) ? "TypeIcon.2" : "TypeIcon.0";
            UITexture* typeIconTex = GetRetroSkillTexture(assets, typeIconKey);
            if (typeIconTex && typeIconTex->texture)
            {
                ImVec2 typeIconPos(panelPos.x + 15.0f * mainScale, panelPos.y + 55.0f * mainScale);
                dl->AddImage((ImTextureID)typeIconTex->texture,
                            typeIconPos,
                            ImVec2(typeIconPos.x + typeIconTex->width * mainScale,
                                   typeIconPos.y + typeIconTex->height * mainScale));
            }

            ImVec2 plusPos(skillBoxPos.x + 125.0f * mainScale, skillBoxPos.y + 20.0f * mainScale);
            ImGui::SetCursorScreenPos(plusPos);
            ImGui::PushID(("plus" + std::to_string(i)).c_str());
            ImGui::InvisibleButton("plus", ImVec2(12.0f * mainScale, 12.0f * mainScale));
            bool plusHovered = ImGui::IsItemHovered();
            bool plusClicked = ImGui::IsItemClicked();
            bool plusHeld = ImGui::IsItemActive() && plusHovered;
            bool plusReleased = ImGui::IsItemDeactivated();
            bool plusReleasedWithClick = plusReleased && plusHovered;
            ImGui::PopID();

            isHoveringActionButtonsThisFrame = isHoveringActionButtonsThisFrame || plusHovered;
            actionButtonsClickedThisFrame = actionButtonsClickedThisFrame || plusClicked;

            if (plusHeld)
                state.isPressingUiButton = true;

            UITexture* plusTex = nullptr;
            if (skill.level >= skill.maxLevel)
                plusTex = GetRetroSkillTexture(assets, "BtSpUp.disabled");
            else if (plusHeld)
                plusTex = GetRetroSkillTexture(assets, "BtSpUp.pressed");
            else if (plusHovered)
                plusTex = GetRetroSkillTexture(assets, "BtSpUp.mouseOver");
            else
                plusTex = GetRetroSkillTexture(assets, "BtSpUp.normal");

            if (plusTex && plusTex->texture)
            {
                dl->AddImage((ImTextureID)plusTex->texture,
                            plusPos,
                            ImVec2(plusPos.x + plusTex->width * mainScale,
                                   plusPos.y + plusTex->height * mainScale));
            }

            if (plusReleasedWithClick && skill.level < skill.maxLevel)
            {
                if (canAcceptActionClick())
                {
                    bool suppressDefault = shouldSuppressPlusAction((int)i, skill);
                    if (!suppressDefault)
                        skill.level++;
                    markActionClickAccepted();
                }
            }

            float lineY = skillY + skillBoxHeight + skillGap / 2.0f;
            if (i < skills.size() - 1 && lineY < listEndY)
            {
                ImVec2 lineStart(listX, lineY);
                ImVec2 lineEnd(listX + 140.0f * mainScale, lineY);
                dl->AddLine(lineStart, lineEnd, IM_COL32(153, 153, 153, 255), 1.0f * mainScale);
            }
        }

        float scrollbarTrackTop = panelPos.y + 104.0f * mainScale;
        float scrollbarTrackHeight = 133.0f * mainScale;

        UITexture* scrollCheckTex = GetRetroSkillTexture(assets, "scroll");
        if (scrollCheckTex && scrollCheckTex->texture)
        {
            float scrollThumbHeight = (float)scrollCheckTex->height;
            float scrollRatio = 0.0f;
            if (maxScrollOffset > 0.0f)
                scrollRatio = state.scrollOffset / maxScrollOffset;

            if (scrollRatio < 0.0f) scrollRatio = 0.0f;
            if (scrollRatio > 1.0f) scrollRatio = 1.0f;

            float availableTrackHeight = scrollbarTrackHeight - scrollThumbHeight;
            float thumbY = scrollbarTrackTop + scrollRatio * availableTrackHeight;
            ImVec2 scrollPos(panelPos.x + 159.0f * mainScale, thumbY);

            ImVec2 scrollMin(scrollPos.x - (float)scrollCheckTex->width / 2.0f, thumbY);
            ImVec2 scrollMax(scrollPos.x + (float)scrollCheckTex->width / 2.0f, thumbY + scrollThumbHeight);

            bool isMouseOverThumb = io.MousePos.x >= scrollMin.x && io.MousePos.x <= scrollMax.x &&
                                    io.MousePos.y >= scrollMin.y && io.MousePos.y <= scrollMax.y;

            if (isMouseOverThumb && ImGui::IsMouseClicked(0))
            {
                state.isScrollDragging = true;
                state.scrollDragStartY = io.MousePos.y;
                state.scrollDragStartOffset = state.scrollOffset;
            }

            if (!io.MouseDown[0])
                state.isScrollDragging = false;

            if (state.isScrollDragging)
            {
                float mouseDelta = io.MousePos.y - state.scrollDragStartY;

                if (availableTrackHeight > 0.0f && maxScrollOffset > 0.0f)
                {
                    float offsetDelta = (mouseDelta / availableTrackHeight) * maxScrollOffset;
                    float newScrollOffset = state.scrollDragStartOffset + offsetDelta;
                    int scrollIndex = (int)round(newScrollOffset / totalSkillHeight);

                    if (scrollIndex < 0) scrollIndex = 0;
                    if (scrollIndex > maxScrollIndex) scrollIndex = maxScrollIndex;

                    state.scrollOffset = (float)scrollIndex * totalSkillHeight;
                }
            }

            bool isPressed = state.isScrollDragging || (isMouseOverThumb && io.MouseDown[0]);
            UITexture* scrollTex = nullptr;

            if (isPressed)
            {
                scrollTex = GetRetroSkillTexture(assets, "scroll.pressed");
                if (!scrollTex || !scrollTex->texture)
                    scrollTex = GetRetroSkillTexture(assets, "scroll");
            }
            else
            {
                scrollTex = GetRetroSkillTexture(assets, "scroll");
            }

            if (scrollTex && scrollTex->texture)
            {
                device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
                device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
                device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);

                ImVec2 roundedMin(floor(scrollMin.x), floor(scrollMin.y));
                ImVec2 roundedMax(floor(scrollMax.x), floor(scrollMax.y));

                dl->AddImage((ImTextureID)scrollTex->texture, roundedMin, roundedMax);

                device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
                device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
                device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
            }
        }

        ImVec2 initPos(panelPos.x + m.width - 60.0f * mainScale,
                      panelPos.y + m.height - 26.0f * mainScale);

        ImGui::SetCursorScreenPos(initPos);
        ImGui::PushID("init_button");
        ImGui::InvisibleButton("init_button", ImVec2(50.0f * mainScale, 16.0f * mainScale));
        bool initHovered = ImGui::IsItemHovered();
        bool initClicked = ImGui::IsItemClicked();
        bool initHeld = ImGui::IsItemActive() && initHovered;
        bool initReleased = ImGui::IsItemDeactivated();
        bool initReleasedWithClick = initReleased && initHovered;
        ImGui::PopID();

        isHoveringActionButtonsThisFrame = isHoveringActionButtonsThisFrame || initHovered;
        actionButtonsClickedThisFrame = actionButtonsClickedThisFrame || initClicked;

        if (initHeld)
            state.isPressingUiButton = true;

        UITexture* initTex;
        if (initHeld)
            initTex = GetRetroSkillTexture(assets, "initial.pressed");
        else if (initHovered)
            initTex = GetRetroSkillTexture(assets, "initial.mouseOver");
        else
            initTex = GetRetroSkillTexture(assets, "initial.normal");

        if (initTex && initTex->texture)
        {
            device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
            device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);

            dl->AddImage((ImTextureID)initTex->texture,
                        initPos,
                        ImVec2(initPos.x + initTex->width * mainScale,
                               initPos.y + initTex->height * mainScale));

            device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        }

        if (initReleasedWithClick)
        {
            if (canAcceptActionClick())
            {
                bool suppressDefault = shouldSuppressInitAction();
                if (!suppressDefault)
                {
                    for (SkillEntry& skill : skills)
                        skill.level = 0;
                }
                markActionClickAccepted();
            }
        }

        if (state.isDraggingSkill && state.dragSkillTab == state.activeTab && state.dragSkillIndex >= 0 && state.dragSkillIndex < (int)skills.size())
        {
            const SkillEntry& draggedSkill = skills[(size_t)state.dragSkillIndex];
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            ImVec2 dragIconPos(io.MousePos.x - state.dragSkillGrabOffset.x, io.MousePos.y - state.dragSkillGrabOffset.y);
            ImVec2 dragIconSize(32.0f * mainScale, 32.0f * mainScale);
            fg->AddRectFilled(dragIconPos, ImVec2(dragIconPos.x + dragIconSize.x, dragIconPos.y + dragIconSize.y),
                              draggedSkill.iconColor, 4.0f * mainScale);
            fg->AddRect(dragIconPos, ImVec2(dragIconPos.x + dragIconSize.x, dragIconPos.y + dragIconSize.y),
                        IM_COL32(25, 27, 31, 255), 4.0f * mainScale, 0, 1.0f * mainScale);
        }

        if (!isHoveringActionButtonsThisFrame)
        {
            state.isHoveringActionButtons = false;
            state.actionHoverStoppedByClick = false;
            state.actionButtonsHoverStartTime = 0.0;
        }
        else
        {
            if (!state.isHoveringActionButtons)
            {
                state.actionButtonsHoverStartTime = ImGui::GetTime();
                state.actionHoverInstantUseNormal1 = ((GetTickCount64() & 1ULL) != 0ULL);
            }
            state.isHoveringActionButtons = true;
            if (actionButtonsClickedThisFrame)
            {
                state.actionHoverStoppedByClick = true;
                state.isHoldingActionButton = true;
            }
        }

        if (!io.MouseDown[0])
            state.isHoldingActionButton = false;

        if (state.isHoldingActionButton && !isHoveringActionButtonsThisFrame)
            state.isPressingUiButton = false;
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
}
