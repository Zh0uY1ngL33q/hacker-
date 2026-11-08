# patchFullStack @ 0x0 [Invalid address] 独立缺陷分析 — Batch 3.6.2-D 第六工作组

## 1. 现象

日志里出现 `Failed to patch ... at 0x0 [Invalid address]`（来自 `patchFullStack`）。
容易被误认为是 playUi AV 的一部分或同一根因。结论：**这是一条独立、良性的缺陷，与 playUi AV 无关。**

## 2. 代码路径

- `src/Features/Modules/Visual/Interface.cpp:33`：
  `patchFullStack` = `DEFINE_PATCH_FUNC(...)`，内部 `addr = SigManager::ResourcePackManager_composeFullStackBp`。
- 调用点：`Interface.cpp:101 / 213 / 216`，**全部被 `#ifdef __DEBUG__` 包裹**（仅 DEBUG 构建执行）。
- `DEFINE_PATCH_FUNC` 宏（`src/Utils/MemUtils.hpp:184-210`）逻辑：
  ```cpp
  if (addr == 0) {
      spdlog::error("Failed to patch {} at 0x{:X} [Invalid address]", ...);
      return;   // 安全早返回，绝不解引用
  }
  ```

## 3. 含义

`0x0 [Invalid address]` 表示签名 `ResourcePackManager_composeFullStackBp` **未解析成功**
（`addr == 0`）。宏检测到 `addr == 0` 后只打一条错误日志并**安全返回**，不触碰任何内存。
这是一个**无害的 no-op 失败**：签名没中 → 不补丁 → 不影响启动、不影响渲染、不影响 playUi。

## 4. 与 playUi AV 的关系

- **不同代码路径**：patchFullStack 走 ResourcePackManager 全栈补丁（且 DEBUG-only）；
  playUi AV 走 `Solstice::init → MinecraftGame::playUi` 虚调用。
- **不同失败模式**：patchFullStack 是「签名未命中 + 安全早返回」；playUi AV 是「虚调用内部访问违规」。
- **不同线程/阶段**：patchFullStack 在 DEBUG 模块初始化期；playUi AV 在 initThread。

## 5. 处理建议

- **不合并**到 playUi AV 的根因分析里。
- **最小处理**：保持现状（安全 no-op）即可，无需为本次崩溃做任何改动。
- 可选清理：把 `ResourcePackManager_composeFullStackBp` 的签名命中情况记一笔，
  供后续 DEBUG 构建完善补丁用，但不影响本次交付。

## 6. 结论

`patchFullStack` 的 `0x0 [Invalid address]` 是一条独立、安全、DEBUG-only 的签名未命中日志，
与 playUi 运行期访问违规无因果关系，跳过即可。
