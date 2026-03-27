# Retro Skill Panel (Win32 + DirectX9 + ImGui)

这是一个已模块化的复古技能面板示例，当前目录可直接编译运行。

## 1. 模块结构

- `main.cpp`：Win32/DX9 初始化、消息循环、ImGui 帧驱动（壳层）
- `retro_skill_app.h/.cpp`：对外生命周期入口（初始化/渲染/释放）
- `retro_skill_panel.h/.cpp`：面板 UI 与交互逻辑（tab、滚动、加点、拖拽）
- `retro_skill_assets.h/.cpp`：贴图加载/查询/释放
- `retro_skill_state.h/.cpp`：运行时状态与默认技能数据

## 2. 一键构建

在已加载 VS 编译环境（`vcvars32.bat` 或 `vcvarsall.bat`）后执行：

```bat
build_win32.bat
```

产物：`Debug/example_win32_directx9.exe`

## 3. 最小调用方式（核心 API）

对外 API 在 `retro_skill_app.h`：

```cpp
void InitializeRetroSkillApp(RetroSkillRuntimeState& state, RetroSkillAssets& assets, LPDIRECT3DDEVICE9 device, const char* assetPath);
void RenderRetroSkillScene(RetroSkillRuntimeState& state, RetroSkillAssets& assets, LPDIRECT3DDEVICE9 device, float mainScale);
void RenderRetroSkillSceneEx(RetroSkillRuntimeState& state, RetroSkillAssets& assets, LPDIRECT3DDEVICE9 device, float mainScale, const RetroSkillBehaviorHooks* hooks);
void ShutdownRetroSkillApp(RetroSkillAssets& assets);
```

默认模式（不接管按钮）调用顺序：

1. 创建 `RetroSkillRuntimeState` / `RetroSkillAssets`
2. DX9 设备可用后调用 `InitializeRetroSkillApp(...)`
3. 每帧：
   - `ImGui_ImplDX9_NewFrame()`
   - `ImGui_ImplWin32_NewFrame()`
   - `ImGui::NewFrame()`
   - `RenderRetroSkillScene(...)`
   - `ImGui::Render()` + `ImGui_ImplDX9_RenderDrawData(...)`
4. 退出前调用 `ShutdownRetroSkillApp(...)`

## 4. 宿主接管按钮（tab / + / init）

如需由其他程序接管按钮行为，使用 `RenderRetroSkillSceneEx(..., hooks)`：

```cpp
RetroSkillBehaviorHooks hooks{};
hooks.userData = ...;
hooks.onTabAction = MyTabHandler;
hooks.onPlusAction = MyPlusHandler;
hooks.onInitAction = MyInitHandler;

RenderRetroSkillSceneEx(state, assets, g_pd3dDevice, mainScale, &hooks);
```

回调返回值：
- `RetroSkill_UseDefault`：继续执行模块默认行为
- `RetroSkill_SuppressDefault`：由宿主接管，模块跳过默认行为

示例实现可见：`main.cpp`（`OnHostTabAction` / `OnHostPlusAction` / `OnHostInitAction`）。

## 5. 资源路径

示例当前用的是外部资源目录：

```cpp
const char* assetPath = "G:\\code\\UI\\SkillEx\\";
```

见 `main.cpp:102`。请改成你的资源实际目录；`retro_skill_assets.cpp` 会按固定文件名拼接加载。

## 6. 常见注意事项（必看）

- 不要改动资源 key（例如 `"Passive.1"`, `"scroll.pressed"`），否则贴图会丢失。
- `initial.pressed` 对应文件名是 `SkillEx.inital.pressed.png`（历史拼写，保留以保证兼容）。见 `retro_skill_assets.cpp:79`。
- 标题栏当前明确为“不可拖动窗口”，交互只用于按下态表现。核心逻辑在 `retro_skill_panel.cpp:53-65`。
- 标题栏按住后移出标题区域会锁定为“已松开”，未抬鼠标移回也不会恢复按下，必须重新按。见 `retro_skill_panel.cpp:58-65`。
- `+` 与 `init` 都是 release 触发（不是 press 触发），避免误触。见 `retro_skill_panel.cpp:304-305`, `retro_skill_panel.cpp:439-443`。
- 鼠标悬停动画首帧随机起始（normal.1 / normal.2），并按 0.5 秒节奏循环。见 `retro_skill_panel.cpp:465-469`, `retro_skill_app.cpp:56-64`。

## 7. 详细文档

- 集成与调用细节：`docs/integration-guide.md`
- UI 交互设计意图：`docs/interaction-intent.md`
