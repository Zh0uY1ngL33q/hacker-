# TEST_REPORT — 实际执行过的测试

环境：Windows 10/11 x64，`E:\PY\python.exe` 3.13.5（pefile 2024.8.26、capstone 5.0.7），
CMake 4.4.4；**无 MSVC / 无 Ninja / 无 ld**（见 `BUILD_BASELINE.md §3`）。

## 1. 测试清单与结果

| # | 测试 | 命令 | 结果 |
|---|---|---|---|
| 1 | 编译环境检查 | `vswhere -all`、`where cl/ninja/ld` | **FAIL（阻塞）** 全部为空，本机无 C++ 工具链 |
| 2 | 第三方依赖检查 | 读 `cmake/Dependencies.cmake` | **SKIP** 需要编译器才能 configure；本机未下载任何依赖 |
| 3 | 离线签名扫描 vs 线上 | `scan_sigs.py ... --json Dump/baseline_offline.json` | **PASS** 74 个未解析名字与 `sigreport.json` 完全一致 |
| 4 | PE 元数据（磁盘） | `pe_report.py unpacked/... --json Dump/pe_disk.json` | **PASS** PE32+/AMD64/15 节/89 导入 |
| 5 | PE 元数据（镜像） | `pe_report.py ...image --image` | **PASS** PE32+/14 节/基址 `0x7FF714E80000` |
| 6 | 两样本逐节字节比对 | 内联脚本（见 `PE_ANALYSIS.md §1`） | **PASS（信息性）** `.text` 相同；`.rdata` 2059/7454 页不同 |
| 7 | RTTI→vtable 解析 | `rtti_vtable.py --class Level --exact` | **PASS** COL×3 → 主表 514 槽，槽体可反汇编 |
| 8 | 补丁边界/寄存器活性审计 | `audit_patches.py` | **PASS（发现 3 项）** 见 `COMPATIBILITY_MATRIX.md §4` |
| 9 | 符号→功能映射 | `symbol_usage.py --only-bad` | **PASS** 74 条未解析 → 54 条被源码引用 |
| 10 | 配置读写测试 | — | **NOT RUN** 需要可执行产物 |
| 11 | UI 初始化测试 | — | **NOT RUN** 同上 |
| 12 | 日志系统测试 | — | **NOT RUN** 同上 |
| 13 | 错误恢复测试 | — | **NOT RUN** 同上 |
| 14 | 资源释放测试 | — | **NOT RUN** 同上 |
| 15 | 本地离线回归测试 | — | **NOT RUN** 同上 |

**通过 7 项、阻塞 1 项、跳过 7 项。没有任何一项被标记为“通过”却未真正执行。**

## 2. 测试 3 的原始输出（关键证据）

```
=== SIGNATURES: total=115 failed=20 ambiguous=21
=== OFFSETS: total=58 failed=21 ambiguous=12

offsets    failed    live= 21 offline= 21 identical=True
offsets    ambiguous live= 12 offline= 12 identical=True
signatures failed    live= 20 offline= 20 identical=True
signatures ambiguous live= 21 offline= 21 identical=True
```

## 3. 测试 7 的原始输出

```
===== Level
  .?AVLevel@@  td=0xb10ced8 cols=['0x9e95368','0x9e95390','0x9e953b8']
               vtables=['0x921fa60','0x9220a78','0x9220ad8']
  vtable rva=0x921fa60 slots=514
    [  9] 0x05de1680  mov rax, qword ptr [rcx + 0x730] ; ret
```

## 4. 历史日志中可用的运行时证据（非本轮产生，仅引用）

来自 `solstice.log` / `crash.log`（历史注入记录），用于说明“为什么只走 GL”：

* 整份日志里没有 `[GUI probe] Present reached`、`[GUI probe] RenderEvent dispatch reached`
  → D3D11/DX12 的 Present 钩子从未被调用；
* 存在 `[GUI probe] GL swap entry` 类记录与 `OpenGL Modern ClickGUI` 相关流程
  → 实际呈现路径是 `wglSwapBuffers` / `gdi32!SwapBuffers`；
* `Patched patchInHandSlot at 0x...86E7` → 与 §6 的静态结论一致（`+2` 落在 disp 字节）。

这些只作为**旁证**。本轮所有新结论都不依赖它们。

## 5. 复现命令

```bash
E:/PY/python.exe tools/re/scan_sigs.py Dump/Minecraft.Windows_1.21.120.0_20261027_005946.image \
  --sig-header  <root>/src/SDK/SigManager.hpp \
  --field-header <root>/src/SDK/OffsetProvider.hpp \
  --json Dump/baseline_offline.json --show failed

E:/PY/python.exe tools/re/audit_patches.py \
  Dump/Minecraft.Windows_1.21.120.0_20261027_005946.image --baseline Dump/baseline_offline.json
```

## 6. 要做而未做的测试（以及怎么补）

| 测试 | 前置条件 | 判定标准 |
|---|---|---|
| 编译通过 | 装 VS2022 + Ninja + MSYS2 ld | `cmake --build` 退出码 0，产出 `Solstice.dll` |
| 挥剑角度修复生效 | 注入后开 Animations，`Custom Swing Angle` 关闭 | 手持物品默认倾斜恢复（不再是 -80° 的姿态）；日志出现 `swing-angle constant 0x... = -1.3963 rad` |
| NoSwitchAnimation 修复生效 | 快速切物品 | 切枪时不再出现手部旋转抽动；手持物品不再随机抖动 |
| `DefaultSwing` OPEN 项 | 选 `Animation = Test` | 若手部姿态抖动/卡死 → 采用 §5 的位移替换方案 |
| 配置读写 | 产物存在 | 改设置 → 重启 → 设置保留 |

---

# 阶段二：工程验证（2026-11-01 20:40–21:05）

本阶段只做**能真正跑起来**的检查；凡是需要编译器的，一律记为阻塞，不用静态结果顶替。

## 7. 新增测试与结果

| # | 测试 | 命令/方法 | 结果 | 分类 |
|---|---|---|---|---|
| 16 | Git HEAD 与历史 | `git rev-parse HEAD`、`git log --oneline -6` | **PASS** HEAD=`8bc6c211bef7…`，5 个本轮提交在列 | — |
| 17 | 工作区干净度 | `git status --porcelain -uno` | **PASS** 无已跟踪改动 | — |
| 18 | 仓库完整性 | `git fsck --no-progress` | **PASS** 无输出（无悬挂/损坏对象） | — |
| 19 | 提交树规模 | `git ls-tree -r HEAD --name-only \| wc -l` | **PASS** 932 文件（`solstice/` 下 908） | — |
| 20 | 原始源码保留 | 解压 `_backup/solstice-before-adapt-20261025-191721.zip` 与 HEAD 归一化比对 | **PASS** 原始 906 文件可比对；累计 **69 改 / 31 增 / 29 删**（删除项全是 `out/build/**` 构建残留） | — |
| 21 | 干净副本导出 | `git archive HEAD \| tar -x -C _buildtest/clean` | **PASS** 独立目录 932 文件 / 23 MB，注释/工作区均未参与 | — |
| 22 | 工具链穷举 | `vswhere -all`；`find` 三个盘 `cl.exe/nmake.exe/ninja.exe/rc.exe` | **FAIL（环境）** 0 命中；无 Windows Kits；`/mingw64` 无 gcc | 环境问题 |
| 23 | 干净副本配置 A（Ninja） | `cmake -S <clean> -B <b> -G Ninja` | **FAIL（环境）** `unable to find a build program corresponding to "Ninja"` + 编译器未设置，exit 1 | 环境问题 |
| 24 | 干净副本配置 B（默认） | `cmake -S <clean> -B <b>` | **FAIL（环境）** `'nmake' '-?' failed` + 编译器未设置，exit 1 | 环境问题 |
| 25 | 依赖解析（无编译器可跑） | `cmake -P cmake/Dependencies.cmake` | **PASS（部分）** spdlog 1.9.2 / magic_enum 0.9.5 / glm 1.0.1 下载+解压+二次命中缓存；nlohmann_json 起下载未完成；DirectXTK、cryptopp 未开始（按“不准备环境”要求中止） | 依赖问题（未完成，非失败） |
| 26 | include 闭包（全树） | `tools/re/lint_includes.py` | **PASS** 2152 处工程内 include 全部可解析；113 个系统/SDK 头忽略；**0 断链** | — |
| 27 | 改动文件的外部标识符 | grep 声明位置 | **PASS** `mSwingAngleOriginal`→`Animations.hpp:97`；`ItemInHandRenderer_TapSwingAngleRadians`→`SigManager.hpp:172`；`isReadableMemory`/`setProtection` 均有声明 | — |
| 28 | 编译（整体 / 单模块） | — | **NOT RUN** 无编译器 | 环境问题 |
| 29 | 配置读写 / UI / 日志 / 异常恢复 / 资源释放 / 离线回归 | — | **NOT RUN** 需可执行产物 | 环境问题 |

阶段二：**通过 8 项（16-21、25-27）、环境阻塞 2 项（22-24 属同类）、未运行 2 项组**。

### 7.1 构建机首轮（用户执行，2026-11-01 21:13）

| # | 测试 | 结果 | 分类 |
|---|---|---|---|
| 30 | 构建机跑 `verify-build.ps1` v1 | **FAIL（环境）** `cl/rc/ninja` 均不在 PATH，configure `exit 1` | 环境问题 |
| 31 | 同一 `env.log` 的正面信息 | VS **18 Community 已安装**（`C:\Program Files\Microsoft Visual Studio\18\Community`）、cmake 4.4.4 在 PATH、HEAD 命中 `a93ff55` | — |

判定：v1 脚本假设“在 VS 开发者命令行里运行”，实际是从普通 PowerShell 启动的，
所以 `vcvars64.bat` 从未执行。**这不是源码问题**，也不是环境缺陷——机器上有 VS，
只是没有加载环境。已出 v2：脚本自己 `call vcvars64.bat`，并从 VS 目录取 Ninja。
v2 已用 PowerShell 解析器校验（1049 token、0 错误），并用真实 VS 路径验证三处推导：

```
<VS>\VC\Auxiliary\Build\vcvars64.bat
<VS>\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe
<VS>\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
```

> 说明：v2 的 `.bat` 调用链（`& $batPath` + `$LASTEXITCODE`）在本机沙箱里无法执行
> （沙箱禁止由 PowerShell 拉起 `cmd.exe`），因此**只做了语法与路径推导验证**，
> 执行结果仍然要等构建机回传。这一条按 UNKNOWN 记，不算通过。

## 8. 错误分类（本阶段全部错误）

| 分类 | 条目 | 证据 | 处置 |
|---|---|---|---|
| **环境问题** | 无 MSVC / 无 Windows SDK(rc.exe) / 无 Ninja / 无 MSYS2 ld | 测试 22-24，日志 `logs/configure-*.log` | 本机不安装；改由构建机执行 `tools/build/verify-build.ps1` |
| **依赖问题** | nlohmann_json / DirectXTK / cryptopp 未完成解析 | `logs/deps-resolve-partial.log` | 未验证，标记 UNKNOWN；构建机首次配置会自行下载 |
| **源码问题** | 本阶段**未发现**。include 闭包 0 断链、改动文件标识符均有声明 | 测试 26-27 | — |
| **接口问题** | 本阶段**无法判定**（需要编译到链接期才知道） | — | 标记 UNKNOWN |

> 说明：`lint_includes.py` 只证明“引用能解析到文件”，**不证明语法/类型正确**。
> 它不能替代编译，本报告不会把它算作“编译通过”。

## 9. 与阶段一的口径差异（避免误读）

* 阶段一 §1 的“通过 7 项”全部是**静态**测试（离线扫描、PE、RTTI、补丁审计）。
* 阶段二新增的 8 项里，**没有一项是编译或运行**。
* 因此“静态分析通过 ≠ 运行时兼容”这条约束仍然有效，且在本阶段被主动遵守：
  `COMPATIBILITY_MATRIX.md` 里没有任何模块因为“include 能解析”而被升级状态。

## 10. 回传清单（构建机执行后）

```
build-verify/env.log         工具链实际版本（cl / ninja / rc / SDK / git HEAD / 未提交改动）
build-verify/configure.log   CMake 完整输出（含依赖下载）
build-verify/build.log      编译器完整输出（-k 0，不在首个错误处停）
build-verify/summary.txt    错误码归并 + 每个码的首个出现 + 失败文件清单
```

拿到这 4 个文件后，可按“环境 / 依赖 / 源码 / 接口”四类直接归档，并对**能证明原因**的错误做最小修改。

## 11. 脚本在用户构建机上“启动即报错”的根因（已定位并修复）

用户把 `verify-build.ps1` 放在构建机的 `solstice` 同级目录，双击后报错但窗口一闪而过。

复现与结论（本机 PowerShell 实测）：

```
& : 无法加载文件 ...\verify-build.ps1，因为在此系统上禁止运行脚本。
    + FullyQualifiedErrorId : UnauthorizedAccess
```

两个独立原因叠加：

| # | 原因 | 修复 |
|---|---|---|
| 1 | Windows 默认执行策略 `Restricted`，**`.ps1` 根本不允许运行** | 新增 `verify-build.cmd`，以 `-ExecutionPolicy Bypass` 只对本次进程放开，**不改系统设置** |
| 2 | 旧脚本按“自己躺在 `tools\build\`”推算仓库根目录，放在 `solstice` 同级时算出错误路径后 `exit 2`；窗口关闭导致错误信息丢失 | 改为从脚本位置向上 6 层找 `CMakeLists.txt`（校验 `project(Solstice)`），再浅搜 5 层，最后才要求 `-Source`；任何早期失败写 `verify-build-error.log` 并暂停 |

顺带修掉一个同类缺陷：`.git` 根目录原先按固定 3 层推算，现改为向上最多 8 层搜索。
模拟用户目录布局实测输出：

```
source tree     : G:\hacker\_pstest\solstice\solstice-src\solstice-master-main\solstice-master-main
=== git ===
repo        : G:\hacker
a93ff555fb6441577e103bba938f67df1c52e290
```

`verify-build.ps1` 语法经 PowerShell Parser 校验：**1570 token，0 错误**。
