# 3.6.2-F — Subscriber-Level BEGIN/END Instrumentation (工作组二)

## 目标

当前 `OpenGLGui::render()` 在 GL 第一帧只记录整个 `RenderEvent` 派发的
`HUD_DISPATCH_BEGIN` / `HUD_DISPATCH_END`。一旦某个订阅者卡死或崩溃，日志停在
`HUD_DISPATCH_BEGIN` 之后、没有任何订阅者记录，无法确定**具体卡在哪个回调**。

本轮在 `trigger(holder)` 的回调边界增加了每订阅者 `BEGIN/END` 诊断，输出：

```
[RENDER-DIAG] frame=1 index=0 module=Watermark phase=BEGIN tid=0x34d8
[RENDER-DIAG] frame=1 index=0 module=Watermark phase=END   tid=0x34d8
[RENDER-DIAG] frame=1 index=1 module=Arraylist phase=BEGIN tid=0x34d8
...
```

最后一条是 `BEGIN` 但没有对应 `END` 的订阅者，即为**未完成回调**——
`FIRST_UNFINISHED_SUBSCRIBER`。

## 实现位置与约束

| 文件 | 改动 |
|---|---|
| `CMakeLists.txt` | 新增构建选项 `SOLSTICE_RENDER_DIAG`（默认 OFF），开启时 `add_compile_definitions(SOLSTICE_RENDER_DIAG)`。 |
| `include/nes/include/nes/event_dispatcher.hpp` | 在 `nes::dispatcher<event_t>` 内新增 `diagnostic_trigger()`（仅 `#ifdef SOLSTICE_RENDER_DIAG`）。 |
| `src/Hook/Hooks/RenderHooks/OpenGLGui.cpp` | 新增 flush-on-write 诊断文件记录器；GL HUD 派发点按诊断开关分支。 |

**严格遵守的约束：**

1. 仅诊断构建开关开启时编译/运行；`SOLSTICE_RENDER_DIAG` 关闭时，派发路径与
   原代码**逐字节一致**（直接走原 `trigger()`）。
2. 保留原始执行顺序：诊断路径 `diagnostic_trigger()` 与 `trigger()` 使用**完全相同**
   的 `shared_lock(mMutex)` + 每个 `listener_list` 的 `shared_lock(mWriteLock)` 与
   同样的 `std::map` 优先级遍历 + `vector` 注册顺序。
3. 不跳过任何订阅者；不改变回调参数；不改变模块启用状态；不改变事件注册机制。
4. 优先记录第一帧；设明确上限 `kRenderDiagMaxFrames = 8`，超过后自动回退到原
   `trigger()`（无每帧日志）。
5. 记录模块名、订阅序号 `index`、线程 ID、Frame ID。
6. `BEGIN` 在 `listener.invoke()` 之前记录；`END` 在 `invoke()` 正常返回之后记录。
7. **不捕获 AV 后继续**：诊断只是"在回调前后写一条记录"，回调本身原样执行；
   若回调访问违规崩溃，进程照常终止，现场不掩盖。
8. **日志持久化**：诊断写入独立文件 `solstice_render_diag.log`（默认在 `%TEMP%`，
   回退到进程 CWD），使用低开销 `WriteFile` + 每条记录后 `FlushFileBuffers`。
   - **整条诊断路径完全不经过 `spdlog`**：`OpenGLGui::render()` 在 `glGuiMutex`
     （`recursive_mutex`，第 335 行）内执行派发，spdlog sink 自带锁，从回调内部再进入
     它是本轮明确要避免的"持有未知锁时走可能再取锁的日志路径"。
   - 初始化阶段（`renderDiagOpen()`）只打开一次文件；不在回调中创建/打开文件。
   - 固定 256 B 栈缓冲 `snprintf`，**不在持有锁时进行动态分配**；
     地址表也只在 `renderDiagOpen()` 构建一次。
   - 诊断只在 GL 呈现线程（单一线程）执行，记录器无新全局锁。
   - **每条 flush 的取舍**：`FlushFileBuffers` 是同步磁盘 I/O，有开销。但本轮目标
     正是"卡死时最后一条 `BEGIN` 必须已经在磁盘上"，若缓冲未落盘则失去定位能力。
     该开销被限制在 `8 帧 × 订阅者数 × 2 条` 的窗口内，上限之后回退原 `trigger()`。
     若文件系统本身在故障中不可用，则**不能保证** `BEGIN/END` 一定持久化——
     此时改用 Dump（见 `runtime-capture-guide.md`）。

## 模块名关联方法（工作组一要求）

`nes` 事件系统使用类型擦除：订阅者存为 `event_listener{ void* mInstance,
std::function<void(event_t&)> mMethod, entt::id_type mMethodHash }`
（`event_dispatcher.hpp:89-103`，三个成员均为 public）。
`diagnostic_trigger()` 在每次 `invoke` 前把 `listener.mInstance` 交给调用点的
`nameFn`，但**不把 `void*` 强转回 `Module*` 再解引用**：无法保证所有监听者都是
`Module` 派生对象，误读未知指针会制造新的 AV，违背"定位而非掩盖异常"。

改为在 `renderDiagOpen()`（首次派发前、一次性）从
`gFeatureManager->mModuleManager->getModules()` 构建 `void* -> mName` 地址表：

```cpp
for (const auto& mod : gFeatureManager->mModuleManager->getModules()) {
    if (!mod) continue;
    void* mostDerived = dynamic_cast<void*>(mod.get());   // 最派生对象地址
    gRenderDiagNames.emplace(mostDerived, mod->mName);
    gRenderDiagNames.emplace(static_cast<void*>(mod.get()), mod->mName);
}
```

`listen<&T::onRenderEvent>(this)` 存入 `mInstance` 的正是 `this`（最派生对象）的
`void*`；`dynamic_cast<void*>(Module*)` 精确复现该地址，**不依赖任何基类偏移假设**
（尽管本项目 `ModuleBase<T> : public Module` 为单继承、偏移为 0）。查表未命中返回
`"(non-module)"`，绝不 deref 未知指针。

调用点：

```cpp
disp.diagnostic_trigger(
    holder,
    [](void* inst) -> const char* { return renderDiagName(inst); },
    [frame](unsigned index, const char* name, int phase) {
        renderDiagRecord(frame, index, name, phase);
    });
```

`nameFn` 只做一次哈希查找（无格式化、无堆分配、无锁）；`diagnostic_trigger` 本身
只多了一个 `index` 计数与两处 `logFn` 调用，**不改变注册、派发顺序、锁语义或
回调参数**。`Module*` 的转换只发生在建表时（可信对象），不在回调热路径内。

## 构建与运行

```bat
rem 重新配置（开启诊断），然后按现有流程构建
cmake -S . -B build -DSOLSTICE_RENDER_DIAG=ON
cmake --build build --config Release
```

运行游戏至第一帧卡死/崩溃后，读取：

```
%TEMP%\solstice_render_diag.log
```

## 如何判定

- 日志中**最后一个 `phase=BEGIN` 之后没有对应 `phase=END`** → 该 `module` 即
  `FIRST_UNFINISHED_SUBSCRIBER`，进入该模块定点分析（工作组三/六）。
- 若所有订阅者都有配对的 `BEGIN/END` 但仍停在 `HUD_DISPATCH_BEGIN` →
  说明问题在 `trigger` 自身/锁/日志路径（见 `runtime-capture-guide.md` 的
  `DISPATCHER_INTERNAL_FAILURE` 分支）。
- 若进程闪退且无 `END` → 取异常上下文/Dump（工作组七），`BEGIN` 已落盘可定位。

## 自检

- 诊断路径与生产路径仅在 `#ifdef SOLSTICE_RENDER_DIAG` 下分叉；宏关闭时编译结果为
  原 `trigger()`，零开销。
- `diagnostic_trigger()` 的锁与遍历顺序与 `trigger()` 一致，已逐行核对。
- 上限 8 帧后回退原 `trigger()`，避免每帧无限日志。
- **编译正确性复核（源码级）**：
  - `diagnostic_trigger()` 声明为**非 const**。`listener_list::begin()/end()`
    （`event_dispatcher.hpp:141-142`）只有非 const 版本，若加 `const` 则遍历
    `mListeners` 无法编译；此处与 `trigger()` 保持一致。
  - `mDispatcher->get<RenderEvent>()` 返回 `nes::dispatcher<RenderEvent>&`
    （`event_dispatcher.hpp:237-241`，static 实例），可调用非 const 成员。
  - `OpenGLGui.cpp` 显式补齐 `<cstdio>`（`snprintf`）、`<unordered_map>`（地址表）、
    `<Features/Modules/Module.hpp>`（`mod->mName`、RTTI），不依赖间接包含顺序。
  - `dynamic_cast<void*>` 要求 `Module` 为多态类型（有纯虚 `getTypeID()`，满足），
    且项目已启用 RTTI（`ModuleBase::TypeID()` 使用 `typeid`）。
  - `glGuiFrameCounter` 为 `unsigned`（`OpenGLGui.cpp:146`），仅 GL 线程读写，
    取作 `frame` 无需原子操作。

> **编译状态**：本机为仓库/分析机，无 MSVC/`cl`、无 `ninja`、无 `g++`，
> 且目标 EXE 与 `CMakeCache.txt` 均不在本机，**本轮未产生实际编译产物**。
> 全量编译与注入由构建机执行（命令见本文件"构建与运行"及
> `minimal-fix-summary.md`）。本文件所有结论均为**静态源码证据**，不是运行结果。
