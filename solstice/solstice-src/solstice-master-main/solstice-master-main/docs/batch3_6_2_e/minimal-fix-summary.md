# 最小修复总结 — Batch 3.6.2-E 第八/十工作组

原则：**只做有直接证据支撑的最小改动**；不删 playUi 调用、不盲改映射、不回退已有成果。

## 1. 本批实际修改（3.6.2-E，3 个文件，+47/−1）

### 1.1 `CMakeLists.txt`
`generate_build_info` 自定义目标现在向 `GenerateBuildInfo.cmake` 转发构建配置与两个诊断选项：
```cmake
add_custom_target(generate_build_info ALL
        COMMAND ${CMAKE_COMMAND}
            -DSOLSTICE_CMAKE_BUILD_TYPE=$<CONFIG>
            -DSOLSTICE_OPT_ALLOW_UNVERIFIED=${SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS}
            -DSOLSTICE_OPT_DISABLE_AURA=${SOLSTICE_DISABLE_AURA_DIAGNOSTIC}
            -P ${CMAKE_SOURCE_DIR}/GenerateBuildInfo.cmake
        COMMENT "Generating build info [${CMAKE_BINARY_DIR}/build_info.h]")
```

### 1.2 `GenerateBuildInfo.cmake`
追加 4 个身份字段（不改动 `SOLSTICE_SOURCE_FINGERPRINT`；旧 `build_info.h` 无这些宏时自动跳过）：
```cmake
SOLSTICE_BUILD_CONFIG            // Debug/Release/...
SOLSTICE_BUILD_ALLOW_UNVERIFIED_MAPPINGS   // 仅当选项 ON 时定义
SOLSTICE_BUILD_DISABLE_AURA_DIAGNOSTIC    // 仅当选项 ON 时定义
SOLSTICE_SOURCE_DIRTY            // git status --porcelain 非空 => 1
```

### 1.3 `src/Utils/SysUtils/ExceptionHandler.cpp`
崩溃日志打印上述新字段（`#ifndef/#ifdef` 保护，旧 `build_info.h` 仍可编译）：
```cpp
Build config: ...
Build opt: ALLOW_UNVERIFIED_MAPPINGS=ON   (若定义)
Build opt: DISABLE_AURA_DIAGNOSTIC=ON     (若定义)
Source dirty: yes|no
```

### 实际 Git Diff（节选）
```
 CMakeLists.txt               |  8 ++++++-
 GenerateBuildInfo.cmake      | 26 ++++++++++++++++++++++
 src/Utils/SysUtils/ExceptionHandler.cpp | 14 ++++++++++++
 3 files changed, 47 insertions(+), 1 deletion(-)
```
完整 diff 见 `final-decision.md` 附录与本次对话回传。

## 2. 明确未做的事（遵守约束）

- ❌ 未把 slot 164 改成其他数值（盲改无效且更危险，`slot164-validation.md` 已证明 164 推导正确）。
- ❌ 未删除 `playUi` 调用（不靠掩盖症状绕过）。
- ❌ 未动 `OpenGLGui` / `OWorldToScreen` / `FrameTransform` / `FOV` / ESP 矩阵。
- ❌ 未改 `InstanceLocator` 整体架构、未改全局映射可信度判据。
- ❌ 未批量禁用模块、未回退 Batch 1-3 / 3.5 / 3.6 / 3.6.1a/b/c / 3.6.2-A / 3.6.2-C / 3.6.2-D 任何成果。

## 3. 669ce2c 的 carry-over 修复（已在 main，本批不重复）

- `ExceptionHandler.cpp`：故障上下文捕获（ExceptionAddress/AccessType/FaultAddress/寄存器）。
- `MinecraftGame.cpp`：`playUi` 虚调用前 `isValidVirtualCall` 守卫。

## 4. 建议项（未写入源，需构建机编译测试）

1. **异常处理器零分配故障记录**：在 `TopLevelExceptionHandler` 顶部、spdlog/fmt 路径之前，用 `wsprintfA`+`WriteFile` 直接追加故障原始记录，规避死锁/递归风险（见 `exception-handler-review.md` 第 3 节）。
2. **playUi 守卫拒绝日志**：仅在 `isValidVirtualCall` 实际拒绝时输出原因 warn（便于下次运行直接判定 `PLAYUI_GUARD_REJECTED_WITH_REASON`）。

> 以上两项因沙箱无 MSVC 无法编译验证，且任务要求「不要重写整个 ExceptionHandler」/「最小修复」，故仅作为经过审查的建议交付，不强行写入源。

## 5. 编译结果 / 测试结果

- **编译结果**：本沙箱无 MSVC，**无法编译**；改动为标准 CMake/C++，低风险。构建机须编译验证。
- **测试结果**：pending——下一版构建 + 运行复现后取得 `solstice.log` / `crash.log` / `sigreport.json`。

## 6. 构建配置（下一版，见 `final-decision.md` 第十一节命令）

目标：`-DSOLSTICE_ALLOW_UNVERIFIED_MAPPINGS=ON -DSOLSTICE_DISABLE_AURA_DIAGNOSTIC=ON`，保留人工 DEBUG 提示，真正隔离 Aura，保留现有 SDK/ESP 成果，确保 Build Info 对应当前源码。

## 7. 未解决问题

- **RUNTIME_IMAGE_MISMATCH**：运行期 `E:\pf\3.9.20.300505\Minecraft.Windows.exe` 的 SHA 未取得，slot-164 对运行期的适用性待比对。
- **BUILD_PROVENANCE**：运行二进制确认为 73da588（陈旧），需从 main(669ce2c) 重建。
- **AV 精确分类**：73da588 崩溃缺新字段；需带守卫+选项的 669ce2c 构建复现后，凭 `FaultAddress`/`AccessType` 收敛到「槽位版本错配」或「对象脱钩」。
