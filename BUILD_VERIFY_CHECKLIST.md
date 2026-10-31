# Solstice 1.21.120 — Build Command Sheet & Verification Checklist

> **唯一事实来源：`SOLSTICE_1.21.120_报告.md`。** 本文件只是它每轮的执行附件：
> 要跑什么命令、回传什么、怎么自查。凡与报告冲突，一律以报告为准。
>
> 当前轮次：`f613bdf`（窗口解析 + 日志刷屏修复）。

---

## 1. 构建机命令（`C:\work\hacker-git`，用户 `morelink`）

```bat
REM --- 1. 拉取最新提交 ---
cd /d C:\work\hacker-git
git pull origin main

REM --- 2. 编译 x64-Release ---
cmake --build solstice\solstice-src\solstice-master-main\solstice-master-main\out\build\x64-Release --config Release
```

本轮只改四个文件，应为增量编译：

```
src\Utils\ProcUtils.cpp
src\Utils\ProcUtils.hpp
src\Hook\Hooks\RenderHooks\OpenGLGui.cpp
src\Hook\Hooks\RenderHooks\D3DHook.cpp
```

要求 `0 errors`。失败就贴**第一段**报错并停止，不要注入。

---

## 2. 注入与操作（用户执行）

1. 启动 `Minecraft.Windows.exe`（1.21.120），注入新编的 `Solstice.dll`。
2. 等到 `Solstice initialized!`。
3. **按一次 TAB。**
4. 若 ClickGUI 打开：启用 **TargetHUD、Animations、HudEditor、ViewModel**。
5. 进世界活动约 30 秒（走动、挥刀、右键格挡一次、手持物品），按 **END** 卸载，然后拷出日志。

> TAB 仍无反应就停在第 3 步，直接把日志发回来——这正是本轮要修或要缩小的范围。

---

## 3. 日志自查（本轮只看这五项）

回传 `solstice.log` 后按顺序核对：

| # | 期望内容 | 含义 | 不符合时 |
|---|----------|------|----------|
| 1 | `[GUI probe] GL swap entry: context=0x... dcWindow=0x... clickGui=true` | GL(wgl) 呈现路径确定在跑，且拿到了 DC 所属窗口 | `dcWindow=0x0` → 画到内存 DC 上了，属另一类问题，把日志发回 |
| 2 | `Minecraft window handle: 0x<非0>, process ... (class='...', title='...')` | 窗口解析成功，并首次打印真实类名/标题 | 只有一条 `unresolved ... (retrying quietly)` → 枚举没命中，其后紧跟一次性 `[window]` 候选清单，把那段发回 |
| 3 | `OpenGL Modern ClickGUI initialized on the game window; TAB opens, ESC closes` | ImGui 上下文 + WndProc 子类化成功 | 缺失则看有无 `OpenGL ClickGUI backend initialization failed` / `cannot subclass the game window` |
| 4 | `[GUI probe] GL HUD dispatch active (HudEditor/TargetHUD)` | HUD 模块已在 GL 路径派发 | 缺失说明覆盖层没画出来，回到第 3 项 |
| 5 | `grep -c "Minecraft window handle"` ≈ **0–3** | 刷屏消失（GL 路径已不再逐帧查询窗口，0 行也正常） | 几十上百行 → 改动没生效（装错 DLL 或没重编） |

同时确认 `Failed to patch ... at 0x0` 不再重复刷、`crash.log` 无新增。

---

## 4. 四模块验证口径

> 前提：ClickGUI 打不开时，四模块都无从判定——既无法开关，也没有 RenderEvent 把 HUD 送到屏上。
> 所以第 3 节的 1–3 项是本节的前置条件。

| 模块 | 报告口径 | 怎么验 | 异常 |
|------|----------|--------|------|
| **ViewModel** | 用户已确认生效 | 把 X/Y/Z 设为非零，手持物品，物品应平移 | 不动 → 先查 `doBobHurt invoked ... plausible=true` 是否出现 |
| **TargetHUD** | 用户确认早前测试正常 | 瞄准生物/玩家，准星附近应出现目标信息面板 | 无面板 / 获取目标即崩 |
| **HudEditor** | 用户确认早前测试正常 | 打开编辑器拖动元素：视角不得旋转，软件鼠标要能动 | 拖动时视角跟着转（回归） |
| **Animations** | 启用崩溃已修；NoSwitch / DefaultSwing / TapSwingAngle 已确认生效 | 逐个开关子项，挥刀 + 右键格挡 | 挥刀无变化；启用即崩；`isHoldingSwordSafe` 未 resolved（格挡姿势不可见） |

---

## 5. 模块状态（按报告第三节）

| 模块 | 状态 | 依据 |
|------|------|------|
| ViewModel | **已确认生效** | `GameRenderer_bobHurt` detour 接上 BobHurtEvent，`plausible=true`，xyz 平移可用（`750a4b3`） |
| TargetHUD | 用户确认早前正常 | 用户反馈 |
| HudEditor | 用户确认早前正常 | 用户反馈 |
| Animations | 崩溃已修，子效果已确认，blocking 未验证 | `2ed5b0b`；NoSwitch `0x58EB4EC`、DefaultSwing `renderItem` bytepatch、TapSwingAngle 均 patch 成功 |
| 窗口解析 / 日志刷屏 | 本轮修复，待验证 | `f613bdf` |

---

## 6. 报告第四节列出的阻碍（本轮未动）

- 挥拳速度失效：`Mob_getCurrentSwingDuration` 解析为 `0x0`（1.21.120 已内联）。
- FluxSwing 失效：签名解析为 `0x0`。
- 挥拳调用方追踪 detour 未产出数据：日志里没有 `[SwingTrace]` 行。
- Animations blocking 可见性未验证：取决于运行时 `isHoldingSwordSafe` 是否 `resolved`。

本次日志额外确认：`Failed to find 43 signatures/offsets!`（59 个 offset 中 21 个、115 个 signature 中 22 个未解析），其中 `FluxSwing`、`Mob_getCurrentSwingDuration`、`ActorAnimationControllerPlayer_applyToPose` 等在 `sigreport.json` 中连近似命中都没有。

---

## 7. 每轮汇报格式

**跑了什么命令 → 回了什么 → 自查口径。**

本轮自查：

- `git log --oneline -1` = `f613bdf`。
- 工作区与 `origin/main` 同步，无落后。
- 构建机 `git pull` 拿到 `f613bdf`，无冲突。
- `solstice.log` 满足第 3 节第 1–5 项。
