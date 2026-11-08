# 3.6.2-F — patchFullStack 精确追踪（工作组四）

## 现象

最新日志：

```
[GL-HANG] stage=1 phase=HUD_DISPATCH_BEGIN tid=0x34d8 frame=1 ctx=0x10001
[error] Failed to patch patchFullStack at 0x0 [Invalid address]
```

## 定义与宏展开

`patchFullStack` 在 `src/Features/Modules/Visual/Interface.cpp:33` 定义：

```cpp
#ifdef __DEBUG__
std::vector<unsigned char> gFsBytes2 = { 0x0f, 0x85 };
DEFINE_PATCH_FUNC(patchFullStack, SigManager::ResourcePackManager_composeFullStackBp, gFsBytes2);
#endif
```

`DEFINE_PATCH_FUNC` 宏（`src/Utils/MemUtils.hpp:184-210`）关键分支：

```cpp
#define DEFINE_PATCH_FUNC(name, addr, bytes) \
    void name(bool patch) { \
        static std::vector<unsigned char> ogBytes = { bytes }; \
        static bool wasPatched = false; \
        static bool reportedInvalid = false; \
        if (addr == 0) { \
            if (!reportedInvalid) { \
                reportedInvalid = true; \
                spdlog::error("Failed to patch {} at 0x{:X} [Invalid address]", #name, addr); \
            } \
            return; \                 // <- 安全早返回
        } \
        ...
```

## 调用者

`patchFullStack` 仅在 `Interface` 中调用，且全部位于 `__DEBUG__` 块内：

- `Interface.cpp:101` `onDisable()` → `patchFullStack(false)`
- `Interface.cpp:213` `onRenderEvent()` → `patchFullStack(true)`
- `Interface.cpp:216` `onRenderEvent()` → `patchFullStack(false)`

即：**`patchFullStack` 只可能从 `Interface::onRenderEvent` 内部被调用**（以及禁用时）。
结合日志，这条 `[error]` 必然来自 `Interface::onRenderEvent` 执行期间。

## 地址为零时的分支

`SigManager::ResourcePackManager_composeFullStackBp` 解析为 `0`（该签名属于 73 条
不可用映射之一）。宏在 `addr == 0` 时：

1. **仅打印一次**错误（`reportedInvalid` 守卫）；
2. **直接 `return`**，**不执行任何内存读写**。

→ **`BENIGN_SIGNATURE_MISS`**：零地址是安全早返回，不构成无效内存操作，不会因此崩溃。

## 结论

- `patchFullStack at 0x0` 是**良性签名缺失**，不是 RenderEvent 卡死/闪退的根因。
- 更重要的是：该错误出现在 `Interface::onRenderEvent` 内部，说明 **Interface 的
  回调已经执行**（错误打印后 `return`）。因此故障更可能位于 dispatch 顺序中
  **Interface 之后**的某个订阅者，而不是 Interface 本身（静态顺序见
  `render-subscriber-order.json`，Interface `dispatchOrder = 15/39`；精确列表见
  下文"与静态派发顺序的对应"）。此判断为**静态假设**，非已证实的运行时结论。
- **不得**将 `patchFullStack` 错误与 RenderEvent 卡死合并成同一根因。二者独立：
  - 前者 = 一条无关可选补丁的签名未命中（良性、可选功能降级）。
  - 后者 = 派发过程中某订阅者卡死/崩溃，需由 `subscriber-instrumentation.md`
    的 `BEGIN/END` 诊断定位。

## 错误日志出现在哪个线程（第 7 项）

`[error] Failed to patch patchFullStack ...` 这一行**本身没有 tid 字段**，因此没有直接
的运行时线程证据。可做的只有静态推断：

- `patchFullStack` 唯一的帧内调用点是 `Interface::onRenderEvent`（`Interface.cpp:213/216`）。
- `Interface::onRenderEvent` 由 `nes::dispatcher::trigger()` 在
  `OpenGLGui::render()` 内**同步**调用（`event_dispatcher.hpp:196-203`，无线程切换、
  无任务队列），而该 `render()` 由 GL `wglSwapBuffers` 路径进入。
- 因此按源码推断，该 `[error]` 与 `HUD_DISPATCH_BEGIN` 同线程，即 `tid=0x34d8`。

> **证据等级：静态推断，非运行时验证。** 要证实需在 `DEFINE_PATCH_FUNC` 的 error 行
> 加 tid（本轮不改宏），或用 Dump 取该线程的调用栈。本轮不把推断写成"已确认"。

## 与静态派发顺序的对应（第 8 项）

`render-subscriber-order.json` 中 `Interface` 的静态 `dispatchOrder = 15 / 39`。
其后依次为 `Arraylist, LevelInfo, DestroyProgress, ESP, ItemESP, Nametags,
FullBright, Keystrokes, SessionInfo, Tracers, ChinaHat, Zoom, ...`，
再往后是 `LAST / VERY_LAST` 层级（`ClickGui, MotionBlur, TargetHUD,
Notifications, UpdateForm` 等）。

由于该 `[error]` 由 `Interface::onRenderEvent` 打印且打印后安全 `return`，
**若该行确实来自本次第一帧**，则 `Interface` 已进入并大概率已返回，故障点应在
`dispatchOrder > 15` 的订阅者中。但这一结论依赖两个尚未证实的假设：
(a) 这条日志确实属于本次 `frame=1` 的 GL 派发；(b) 静态顺序等于运行期顺序
（配置可能改变启用集，见 `render-subscriber-inventory.json` 的
`configOverrideMechanism`）。因此**仅作为待验证假设**，真正定位以
`SOLSTICE_RENDER_DIAG` 的 `BEGIN/END` 为准。

## 是否需最小防御修复

不需要。`DEFINE_PATCH_FUNC` 对 `addr == 0` 已经安全早返回，符合"零地址安全早返回 →
标记 `BENIGN_SIGNATURE_MISS`"的要求。本轮不修改 `patchFullStack`、不修改 `Interface`、
不修改 `MemUtils.hpp`。
