# 最终判定 — Batch 3.6.2-E 第十五工作组

## 1. 最终验收状态

主状态：**NEEDS_RUNTIME_EXCEPTION_CONTEXT**（需下一版构建的故障上下文才能对 AV 精确分类）

并叠加两个子状态（均已达成/已标记）：
- ✅ **BUILD_PROVENANCE_CONFIRMED** —— 运行二进制身份已精确确认：它是**货真价实的 73da588 构建**（`BUILD_VERSION` 前缀与 `SOURCE_FINGERPRINT` `e8175b9f0699` 都与 `73da588` 源码树逐字节一致），构建机制可靠；问题是构建机用的是陈旧 checkout，已从 main(669ce2c) 重建即可。
- ⚠️ **RUNTIME_IMAGE_MISMATCH** —— 运行期 EXE 的 SHA 尚未取得，slot-164 对运行期的适用性待比对（静态 unpacked 镜像 SHA `5fb2e893...026deb`）。

> 说明：任务允许从给定清单选一个主状态。选 NEEDS_RUNTIME_EXCEPTION_CONTEXT 作为主状态，是因为 AV 本身的精确根因尚未取得直接证据——我们只确认了「崩溃二进制 = 73da588（缺守卫与诊断）」。下一版（669ce2c + 两个选项）复现后，状态会收敛到 `PLAYUI_GUARD_REJECTED_WITH_REASON` 或 `AV_CONTEXT_CAPTURED_NEEDS_ANALYSIS` 或 `ROOT_CAUSE_CONFIRMED_PATCH_READY`。

## 2. 各工作组结论速览

| 工作组 | 结论 |
|---|---|
| 一/三 构建身份 | 运行二进制 = 73da588（指纹精确匹配），构建机制可靠；构建机陈旧 checkout 导致缺 3.6.2-C/D；已补身份字段 |
| 二/四 两编译选项 | `SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS` 保留人工 DEBUG 提示（Enter 退出、不自动放行、不改 Missing→OK）；`SOLSTICE_DISABLE_AURA_DIAGNOSTIC` 经 `#ifndef` 阻止 Aura 构造/注册/生命周期/事件，隔离成功时 Aura 不出现在模块清单；本批把两选项写入 Build Info |
| 五 运行期镜像 | unpacked(1.21.120.0) SHA `5fb2e893...026deb` 已核对；运行期 SHA 不可达 → RUNTIME_IMAGE_MISMATCH 待补 |
| 六 调用链/ABI | 崩溃 = 调用点 A（Solstice::init 尾音效，initThread）；playUi 可选音效；ABI = RCX/RDX/XMM0/XMM1（非 XMM2/XMM3） |
| 七 slot 164 | unpacked 镜像唯一命中 RVA `0x1CBC3E4`，disp32 `0x520` → 164，四层通过；不盲改数字；运行期待 SHA 比对 |
| 八 守卫审查 | `isValidVirtualCall` 检查对象/vtable/slot/target 可读可执行；不证 target==playUi、不证生命周期；失败静默 return 因 playUi 可选不虚报初始化成功 |
| 六/八 处理器审查 | 故障上下文字段正确（寄存器取自故障 ContextRecord，AV 才解析 AccessType/FaultAddress）；处理器内 spdlog/fmt/ofstream 存在死锁/递归风险，给出零分配 WriteFile 方案（建议项） |
| 九 patchFullStack | BENIGN_SIGNATURE_MISS，与 playUi AV 无关，不合并 |

## 3. 本批修改文件清单（3.6.2-E）

```
solstice/.../CMakeLists.txt
solstice/.../GenerateBuildInfo.cmake
solstice/.../src/Utils/SysUtils/ExceptionHandler.cpp
```

## 4. 完整 Git Diff（3.6.2-E 三文件）

```diff
diff --git a/.../CMakeLists.txt b/.../CMakeLists.txt
@@ -193,8 +193,14 @@
 # 3.6.2-E: also forward the build configuration and the two diagnostic CMake
 # options so a crash log can show exactly how the binary was built.
 add_custom_target(generate_build_info ALL
-        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/GenerateBuildInfo.cmake
+        COMMAND ${CMAKE_COMMAND}
+            -DSOLSTICE_CMAKE_BUILD_TYPE=$<CONFIG>
+            -DSOLSTICE_OPT_ALLOW_UNVERIFIED=${SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS}
+            -DSOLSTICE_OPT_DISABLE_AURA=${SOLSTICE_DISABLE_AURA_DIAGNOSTIC}
+            -P ${CMAKE_SOURCE_DIR}/GenerateBuildInfo.cmake
         COMMENT "Generating build info [${CMAKE_BINARY_DIR}/build_info.h]")

diff --git a/.../GenerateBuildInfo.cmake b/.../GenerateBuildInfo.cmake
@@ -156,6 +156,32 @@
 file(APPEND "${TEMP_BUILD_INFO_PATH}" "#define SOLSTICE_INTENDED_VERSION ...
+
+# --- Build identity completeness (3.6.2-E) ---
+if (DEFINED SOLSTICE_CMAKE_BUILD_TYPE AND NOT SOLSTICE_CMAKE_BUILD_TYPE STREQUAL "")
+    file(APPEND ... "#define SOLSTICE_BUILD_CONFIG \"${SOLSTICE_CMAKE_BUILD_TYPE}\"\n")
+else()
+    file(APPEND ... "#define SOLSTICE_BUILD_CONFIG \"unknown\"\n")
+endif()
+if (SOLSTICE_OPT_ALLOW_UNVERIFIED)
+    file(APPEND ... "#define SOLSTICE_BUILD_ALLOW_UNVERIFIED_MAPPINGS 1\n")
+endif()
+if (SOLSTICE_OPT_DISABLE_AURA)
+    file(APPEND ... "#define SOLSTICE_BUILD_DISABLE_AURA_DIAGNOSTIC 1\n")
+endif()
+execute_process(COMMAND ${GIT_EXECUTABLE} status --porcelain ...)
+if (NOT _solstice_git_status STREQUAL "")
+    file(APPEND ... "#define SOLSTICE_SOURCE_DIRTY 1\n")
+else()
+    file(APPEND ... "#define SOLSTICE_SOURCE_DIRTY 0\n")
+endif()

diff --git a/.../src/Utils/SysUtils/ExceptionHandler.cpp b/.../src/Utils/SysUtils/ExceptionHandler.cpp
@@ -149,6 +149,20 @@
         excFile << ... "Minecraft version: " ...
+#ifndef SOLSTICE_BUILD_CONFIG
+#define SOLSTICE_BUILD_CONFIG "unknown"
+#endif
+        excFile << ... "Build config: " << SOLSTICE_BUILD_CONFIG ...
+#ifdef SOLSTICE_BUILD_ALLOW_UNVERIFIED_MAPPINGS
+        excFile << ... "Build opt: ALLOW_UNVERIFIED_MAPPINGS=ON\n";
+#endif
+#ifdef SOLSTICE_BUILD_DISABLE_AURA_DIAGNOSTIC
+        excFile << ... "Build opt: DISABLE_AURA_DIAGNOSTIC=ON\n";
+#endif
+#ifndef SOLSTICE_SOURCE_DIRTY
+#define SOLSTICE_SOURCE_DIRTY 0
+#endif
+        excFile << ... "Source dirty: " << (SOLSTICE_SOURCE_DIRTY ? "yes" : "no") ...
         excFile << ... "----------------------------------------\n";
```

## 5. 构建机验收配置（第十一节命令）

```bat
cmake -S <src> -B <build> ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DSOLSTICE_ALLOW_UNVERIFIED_MAPPINGS=ON ^
  -DSOLSTICE_DISABLE_AURA_DIAGNOSTIC=ON
cmake --build <build> --config Release
```
要点：
1. 保留人工 DEBUG 提示（Enter 退出，不自动放行，不改 Missing/Ambiguous→OK）。
2. 真正隔离 Aura（构建后模块清单不应再出现 Aura，而非仅 "Disabled"）。
3. 保留现有 SDK/ESP 成果，不回退。
4. 确保 Build Info 对应当前源码（从 main=669ce2c 配置；`generate_build_info` 每次构建重算）。
5. 构建后记录 DLL SHA-256。
6. 记录运行期 `Minecraft.Windows.exe` SHA-256（补 RUNTIME_IMAGE_MISMATCH）。

## 6. 下一次运行必须取得的证据

- `solstice.log` / `crash.log` / `sigreport.json`。
- 若再 AV：须出现 `ExceptionCode/ExceptionAddress/AccessType/FaultAddress/RIP/RSP/RBP/RCX/RDX/R8/R9/RAX`。
- 若守卫拒绝 playUi：须出现明确拒绝原因（建议项落地后）。
- 若守卫放行仍 AV：用 `ExceptionAddress`/`FaultAddress` 定位具体指令。
- 优先取得完整 Dump（但 Dump 不作为唯一证据）。

## 7. 核心原则重申

本轮不继续猜 slot 164、不改动 OpenGL。先让构建身份可信（已确认 73da588 + 补身份字段），再让异常上下文完整（669ce2c 已带），最后才决定修改什么。保留当前所有适配成果，不全项目回退，不以防御性 return 冒充根因修复。
