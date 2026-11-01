# CHANGELOG

## 2026-11-01

### 1. `30cc8ab` tools(re): offline PE/RTTI/signature harness

新增文件（不影响构建，不在 `CMakeLists` 里）：

| 文件 | 内容 |
|---|---|
| `tools/re/scan_sigs.py` | 重实现 `SigManager::scanSig` / `OffsetProvider::scanSig`：只扫可执行节、首个命中、`RefSig` 取 rel32、`Index` 取 `disp/8`。剥离注释后再解析声明 |
| `tools/re/rtti_vtable.py` | `. ?AV...@@` → TypeDescriptor → COL（校验 signature==1）→ vtable（-8 槽为 COL 的 VA）→ 槽位 |
| `tools/re/pe_report.py` | PE 元数据，磁盘布局与 RVA 布局分开 |
| `tools/re/audit_patches.py` | 补丁边界 + 寄存器活性判定 |
| `tools/re/symbol_usage.py` | 未解析符号 → 源码引用 → 功能 |

原因：需要在不注入游戏的前提下复现客户端的解析结果。**验证**：离线结果与线上 `sigreport.json`
的四个集合逐名相同（20/21/21/12）。

### 2. `eb4a064` build: source fingerprint + timestamp in build_info.h

| 文件 | 改动 | 原因 |
|---|---|---|
| `CMakeLists.txt` | `add_custom_target(generate_build_info ALL ...)` | 之前只有显式请求才生成，旧构建目录会留下过期 commit 号 |
| `GenerateBuildInfo.cmake` | 增加 `SOLSTICE_BUILD_TIMESTAMP`、`SOLSTICE_SOURCE_FINGERPRINT(_SHORT)`（对 `src/**` 逐个 SHA256 再汇总） | 日志/崩溃报告要能对应到“实际编译的那份源码” |
| `ExceptionHandler.cpp` | `#ifndef` 兜底 + crash log 输出 source/built 两行 | 旧 `build_info.h` 仍可编译 |

> 这三个文件在本轮开始前就已经是工作区未提交状态，原样提交以保证用户构建机拉到的树与本地一致。

### 3. `608707a` fix(Animations): swing angle wrote -80 deg into a -14 deg pose constant

| 文件 | 改动 |
|---|---|
| `src/Features/Modules/Visual/Animations.cpp` | ① `mSwingAngle` 改为 `resolveSwingAngleConstant()` 解出，并缓存原值；② `gNoSwitchAnimation` 由 8 字节改为 4 字节；③ `onDisable()` 还原缓存值 |
| `src/Features/Modules/Visual/Animations.hpp` | 新增 `float mSwingAngleOriginal` |

原因（全部来自镜像反汇编，不是日志推断）：

* `SigManager::TapSwingAnim` 解出 `0x8F5A000`，值为 `-0.2443461 rad = -14.000°`，处在手持
  **姿态**常量表中间（-1.50/-2.00/-10.54/-14.00/-29.79/-32.09/-41.25/-87.73/-108.00）。
  模块每 tick 往里写 `radians(-80)`，`onDisable()` 又写一次 -80 当“还原值”，于是关掉模块
  之后 -14° 条目仍然是 -80°。
* 真正等于 `-80.000°` 的常量在 `0x8F31EF0`，由
  `0x2D4CAE0: F3 44 0F 59 25 <disp32>` (`mulss xmm12,[rip+disp32]`) 引用；邻值
  `-45 / -50 / -150°` 全是整角度。
* `0x58EB4EC` 处旧的 8 字节补丁覆盖了 `mulss xmm0,xmm7` **和** `movaps xmm1,xmm9`，
  而 `xmm1` 在 `0x58EB4FF` 被 `mulss xmm1,xmm9` 读取 → 顺带改坏了手持旋转项。

风险控制：新函数在使用前校验
① 站点 5 字节 opcode 必须是 `F3 44 0F 59 25`；
② 目标常量必须可读且 `0.5 ≤ |x| ≤ 6.5` rad；
任一不满足就报错并放弃写入，而不是盲目覆盖。

**未验证**：本机无编译器，未编译、未注入。

### 4. 文档

`docs/migration/` 下 11 份（见 `FINAL_REPORT.md §5`），其中每个数字都能由
`tools/re/*.py` 复现。

---

### 5. 构建脚本修正（构建机首轮反馈后）

**触发**：构建机回传 `env.log` —— `cl/rc/ninja` 全 `NOT FOUND`、configure `exit 1`，
但同一份日志证明 VS 18 Community 已装、cmake 4.4.4 在 PATH。根因是普通控制台没有 `vcvars`。

| 文件 | 改动 | 说明 |
|---|---|---|
| `tools/build/verify-build.ps1` | 增加 MSVC 环境自举：`vswhere -requires ...VC.Tools.x86.x64` → `call vcvars64.bat` → 导入 `set` 输出；新增 `vcvars.log`；`env.log` 增 `vcvars64` 行 | 仅此一段是本次新增，其余保留 `6bcd7be` 的实现 |
| 同上 | 新增 `Invoke-Logged`：把原生命令的 stderr 从 `ErrorRecord` 还原为纯文本再落盘 | 之前日志里混入 `CategoryInfo / FullyQualifiedErrorId`，会干扰错误分类 |

**一次需要记录的失误**：本轮开始时我直接重写了 `verify-build.ps1`，覆盖了 `6bcd7be`
里已有的执行策略包装、源码树自动定位、`run.log`、依赖缓存复用等改进。
发现后用 `git checkout 6bcd7be -- tools/build/verify-build.ps1` 恢复，再只补 vcvars 一段。
`verify-build.cmd` 全程未改。教训：改别人的文件前先 `git log -1 -- <path>` 看有没有新提交。

---

## 历史（本轮之前，供对照）

| 提交 | 说明 |
|---|---|
| `8f0679b` | 修 Scaffold 在 `ItemUtils::getAllPlaceables` 的崩（读坏 block name） |
| `3fbfa34` | Scaffold 放置改用 transaction 包 |
| `5da2987` | `BlockUtils::isMiningPosition` 对 `getHitResult` 判空 |
| `c33e895` | 解析 ItemUse/ItemRelease vtable，Scaffold 移植到 transaction |
| `750a4b3` | `GameRenderer_bobHurt` detour 重新接上（旧 `SigManager::BobHurt` 误命中字符串构造器） |
