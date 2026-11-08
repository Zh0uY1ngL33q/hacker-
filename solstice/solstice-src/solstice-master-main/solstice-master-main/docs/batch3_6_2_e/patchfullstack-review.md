# patchFullStack 独立检查 — Batch 3.6.2-E 第九工作组

## 1. 现象

历史日志：`Failed to patch patchFullStack at 0x0 [Invalid address]`

## 2. 定义与调用点

- `patchFullStack` = `DEFINE_PATCH_FUNC(...)`（`src/Features/Modules/Visual/Interface.cpp`），内部 `addr = SigManager::ResourcePackManager_composeFullStackBp`。
- 所有调用点位于 `Interface.cpp`，**全部被 `#ifdef __DEBUG__` 包裹**（仅 DEBUG 构建执行）。

## 3. 0 地址是否有安全早返回

`DEFINE_PATCH_FUNC`（`src/Utils/MemUtils.hpp`）逻辑：
```cpp
if (addr == 0) {
    spdlog::error("Failed to patch {} at 0x{:X} [Invalid address]", ...);
    return;   // 安全早返回，绝不解引用
}
```
`0x0 [Invalid address]` 表示签名 `ResourcePackManager_composeFullStackBp` **未解析成功**（`addr==0`），宏检测到后只打一条错误日志并**安全返回**，不触碰任何内存。

## 4. 失败后是否有其他非法操作

无。签名未命中 → 不补丁 → 不影响启动、渲染、`playUi`。它是 `ResourcePackManager` 全栈补丁（DEBUG-only），与 `playUi` 虚调用（init 路径）是**不同代码路径、不同失败模式、不同线程/阶段**。

## 5. 标记

**BENIGN_SIGNATURE_MISS** —— 安全的 no-op 失败，与 playUi AV **无因果关系**，不并入。

## 6. 是否需要修复

- 不合并到 playUi AV 根因。
- 保持现状（安全 no-op）即可，无需为本次崩溃改动。
- 可选清理：把 `ResourcePackManager_composeFullStackBp` 的命中情况记一笔，供后续 DEBUG 构建完善补丁用，但不影响本次交付。
