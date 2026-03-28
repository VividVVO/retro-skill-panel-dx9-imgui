# 集成指南（integration-guide）

## 1. 你需要负责什么

本模块只负责“技能面板业务 UI + 交互状态 + 贴图管理”。
你仍需在宿主工程负责：

- Win32 窗口
- DirectX9 设备生命周期
- ImGui 上下文与后端初始化
- 主循环与 Present

## 2. 头文件与状态对象

```cpp
#include "retro_skill_app.h"

RetroSkillRuntimeState state;
RetroSkillAssets assets;
```

## 3. 初始化阶段

在 DX9 设备创建成功后调用：

```cpp
InitializeRetroSkillApp(state, assets, g_pd3dDevice, assetPath);
```

行为：
- 重置技能与交互状态（`ResetRetroSkillData`）
- 加载所有面板与鼠标贴图（`LoadAllRetroSkillAssets`）

## 4. 每帧调用顺序（必须保持）

### 4.1 默认模式（不接管）

```cpp
ImGui_ImplDX9_NewFrame();
ImGui_ImplWin32_NewFrame();
ImGui::NewFrame();

RenderRetroSkillScene(state, assets, g_pd3dDevice, mainScale);

ImGui::Render();
ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
```

### 4.2 接管模式（宿主拦截 tab/+ /init）

```cpp
RetroSkillActionDecision OnTab(const RetroSkillActionContext& context, void* userData)
{
    // 返回 RetroSkill_UseDefault: 使用模块默认 tab 切换
    // 返回 RetroSkill_SuppressDefault: 宿主自行处理
    return RetroSkill_UseDefault;
}

RetroSkillActionDecision OnPlus(const RetroSkillActionContext& context, void* userData)
{
    // 可通过 context.skillIndex/currentLevel/maxLevel 决定策略
    return RetroSkill_UseDefault;
}

RetroSkillActionDecision OnInit(const RetroSkillActionContext& context, void* userData)
{
    return RetroSkill_UseDefault;
}

RetroSkillBehaviorHooks hooks{};
hooks.userData = myController;
hooks.onTabAction = OnTab;
hooks.onPlusAction = OnPlus;
hooks.onInitAction = OnInit;

ImGui_ImplDX9_NewFrame();
ImGui_ImplWin32_NewFrame();
ImGui::NewFrame();

RenderRetroSkillSceneEx(state, assets, g_pd3dDevice, mainScale, &hooks);

ImGui::Render();
ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
```

回调语义：
- `RetroSkill_UseDefault`：执行模块默认行为（兼容旧逻辑）
- `RetroSkill_SuppressDefault`：跳过默认行为，由宿主完全接管

原因：
- `RenderRetroSkillScene` / `RenderRetroSkillSceneEx` 依赖当前帧鼠标/时间信息（`ImGuiIO`, `ImGui::GetTime()`）
- 若顺序变化，hover 循环、release 判定、拖拽状态可能失真

## 5. 设备重置（DX9）

模块贴图使用 `D3DPOOL_MANAGED` 创建，普通 `Reset` 不需要手动重载资源。
但你仍需维持 ImGui DX9 后端标准流程：

```cpp
ImGui_ImplDX9_InvalidateDeviceObjects();
g_pd3dDevice->Reset(&g_d3dpp);
ImGui_ImplDX9_CreateDeviceObjects();
```

对应示例：`main.cpp:198-205`。

## 6. 退出释放

```cpp
ShutdownRetroSkillApp(assets);
```

会释放 `RetroSkillAssets` 内所有 texture。

## 7. 资源约束

- `assetPath` 必须指向素材目录，且目录下文件名与 `retro_skill_assets.cpp` 一致。
- key 与文件名映射不要随意改。
- 历史拼写 `SkillEx.inital.pressed.png` 被有意保留（不是笔误修正遗漏）。

## 8. 可调整项（安全）

这些可改，不影响交互语义：
- `mainScale`（整体 DPI/缩放）
- `ResetRetroSkillData` 的默认技能文本与颜色

这些不建议改（会改变手感/语义）：
- release 触发规则
- hover 动画节奏与首帧随机机制
- tab/滚动条/列表 hit box 对齐关系
