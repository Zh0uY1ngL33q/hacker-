# FINAL_REPORT — Solstice → Minecraft 1.21.120 迁移

日期：2026-11-01　目标版本：**1.21.120.0**（由样本 PE 头与实际镜像基址确认，非假定）

## 0. 一句话结论

逆向分析部分**按证据完成**了（离线扫描器与线上 74 项未解析逐名一致，找到并修掉 2 个真实缺陷），
**但“迁移完成”宣布不了**：本机没有 C++ 编译器，7 项必须编译/运行才能做的验收全部未执行。

## 1. 关键发现（三条，都可复现）

1. **`Animations` 一直在把 -80° 写进 -14° 的位置。**
   挥剑角度常量在 `0x8F31EF0`（`-1.3962634f` = -80.000°），而代码取的是
   `SigManager::TapSwingAnim` 解出的 `0x8F5A000`（`-0.2443461f` = -14.000°），
   后者处在手持**姿态**表里。而且 `onDisable()` 也用 -80 当“还原值”，所以关掉模块
   之后姿态仍然是错的，直到重启游戏。**已修**。

2. **`NoSwitchAnimation` 的 8 字节补丁多吃了 `movaps xmm1, xmm9`。**
   `xmm1` 在 9 字节后被 `mulss xmm1, xmm9` 读取，补丁等于额外改坏了一项手持旋转。
   **已修为 4 字节**（`audit_patches.py` 判定 `ALIGNED` 且寄存器由补丁自身重定义）。

3. **`DEFINE_INDEX_FIELD(..., Index)` 只保证槽号，不保证调用约定。**
   用 RTTI 链查出 `Level` 槽 72 的实体是 `void f(Level*, shared_ptr*)`（`mov [rdx],rax` ×3
   + `lock inc [rax+8]`），而源码按 `T* f()` 调用它 —— `rdx` 是未初始化寄存器，
   函数会往野地址写。这类声明全树共 30 条，本轮只证实了 72 这一条。

## 2. 旧版对比为什么是空的

工作区只有 1.21.120 的两个样本（磁盘脱壳 + 内存镜像），**没有 1.21.44 的任何二进制**。
`VERSION_DIFF.md` 里旧版一栏因此全部是 UNKNOWN。任何“从 44 迁过来所以是 X”的说法都无法
在本仓库被验证，本轮不为它背书。

顺带确认了一件容易踩的事：磁盘 exe 的 `.rdata` 有 2059/7454 页与镜像不同，差异是指针值
（指向模块之外），且 BASERELOC 只剩 `0x10` 字节。**静态地址只能在镜像基址
`0x7FF714E80000` 下解释**；`.text` 与标量常量两边相同。

## 3. 交付物

```
docs/migration/
  FINAL_REPORT.md                 本文件
  REVERSE_ENGINEERING_REPORT.md   逆向方法、证据链、UNKNOWN 清单
  COMPATIBILITY_MATRIX.md         模块级状态 + 字节补丁矩阵
  CHANGELOG.md                    3 个代码/工具 commit + 1 个文档 commit
  TEST_REPORT.md                  跑了什么、结果、没跑什么
  PE_ANALYSIS.md                  PE 档案（磁盘 vs 镜像）
  SECTION_REPORT.md               节表 + 逐节字节比对
  BINARY_METADATA.json            机器可读元数据
  VERSION_DIFF.md                 差异矩阵（旧版 UNKNOWN）
  MIGRATION_REPORT.md             做了什么/没做什么/下一步
  SOURCE_MAP.md                   源码地图 + 结构性问题区
  DEPENDENCY_MAP.md               外部依赖 + 内部耦合 + 适配层边界
  BUILD_BASELINE.md               构建基线 + 本机环境实测
tools/re/                         5 个可复现脚本
```

源码：`solstice/solstice-src/solstice-master-main/solstice-master-main`（即仓库内被修改的树）。
构建产物 `BUILD/`：**无法提供**，原因见 §4。

## 4. 验收清单（逐项据实）

| 验收项 | 状态 | 依据 |
|---|---|---|
| 源码结构已完整审计 | ✅ | `SOURCE_MAP.md`（623 文件 / 61,307 行，脚本统计） |
| 新版 PE 元数据已分析 | ✅ | `PE_ANALYSIS.md`、`BINARY_METADATA.json` |
| 新旧版本差异已记录 | ⚠️ 部分 | 旧版无样本，`VERSION_DIFF.md` 中旧版栏位全为 UNKNOWN |
| 原始源码完整保留 | ✅ | `_backup/solstice-before-adapt-20261025-191721.zip`（906 文件，可比对）；与 HEAD 归一化比对得 69 改 / 31 增 / 29 删（删除项全是 `out/build/**`）；`git fsck` 无输出 |
| 允许迁移的功能完成架构调整 | ❌ | 主动放弃全量目录重排，理由见 `MIGRATION_REPORT.md §3.1`；仅做 2 处有证据的最小修复 |
| 所有目标模块通过编译 | ❌ | 本机无 MSVC/SDK/Ninja（穷举 0 命中）；干净副本两次配置均 exit 1，见 `logs/configure-*.log` |
| 配置系统正常 | ❌ 未验证 | 需可执行产物 |
| UI 正常 | ❌ 未验证 | 同上 |
| 日志/异常处理正常 | ❌ 未验证 | 同上 |
| 离线测试通过 | ⚠️ 部分 | 15 项静态/环境测试通过；编译与运行时测试未跑（`TEST_REPORT.md §7`） |
| 未解决问题均已披露 | ✅ | `REVERSE_ENGINEERING_REPORT.md §8`、`COMPATIBILITY_MATRIX.md §4/§6` |
| 最终报告与完整源码已交付 | ⚠️ 部分 | 报告齐 + `tools/build/verify-build.ps1`；`BUILD/` 缺失 |

**结论：11 项中 4 项达成、4 项部分、3 项未达成 → 迁移未完成。**

## 5. 下一步（只有两步是必要的）

1. **在构建机上编一次**（`BUILD_BASELINE.md §2`）。若失败，把 `cmake --build` 的完整输出贴回。
   ——这是解除 7 项阻塞的唯一前置条件。
2. **注入后只看三件事**：
   * 日志是否出现 `[Animations] swing-angle constant 0x... = -1.3963 rad (...)`；
   * 手持物品的默认倾斜是否恢复正常（不再是被 -80° 覆盖的姿态）；
   * 快速切物品时手部旋转是否还抽动（判定 4 字节补丁）。
   这三条能一次性确认本轮两个修复是否成立，不需要看别的模块。

## 6. 仍然 OPEN 的项（不再展开，细节见对应文档）

| 项 | 位置 |
|---|---|
| `DefaultSwing` / `DefaultSwing2` 删掉了仍被读取的 `xmm3`/`xmm0` | `COMPATIBILITY_MATRIX.md §4` |
| `patchInHandSlot` 的 `+2` 落在 disp 上属巧合；`0x1C` 非 8 字节对齐 | `REVERSE_ENGINEERING_REPORT.md §6` |
| `Level` 的 `getHitResult`/`getLevelData`/`getPlayerList` 真实槽号 | `REVERSE_ENGINEERING_REPORT.md §8` |
| 20 个 failed 签名仍未解析（其中 12 个导致功能整体失效） | `FEATURE_COMPATIBILITY.md §1/§2` |
| `.winlice` 节未转储 | `PE_ANALYSIS.md §5` |

---

# 阶段二：工程验证结论（2026-11-01 21:05）

## 7. 本阶段做了什么（只列可复现的）

| 项 | 结果 | 证据位置 |
|---|---|---|
| Git 状态 | HEAD `8bc6c211bef7…`；无已跟踪改动；`git fsck` 无输出；提交树 932 文件 | `TEST_REPORT.md §7` 16-19 |
| 原始源码 | 906 文件备份可解压比对；累计 69 改 / 31 增 / 29 删（删的全是 `out/build/**`） | 同上 20 |
| 干净副本 | `git archive HEAD` 导出 932 文件 / 23 MB 到独立目录 | 同上 21 |
| 工具链（本机） | **0 命中**：无 cl/nmake/ninja/rc、无 Windows Kits、`/mingw64` 无 gcc | 同上 22 + `BUILD_BASELINE.md §2` |
| 工具链（构建机） | 从备份的 CMake 缓存读到真实值：MSVC 19.51.36246.0 / SDK 10.0.26100.0 / Ninja / CMake 4.2.3 / **RelWithDebInfo** | `BUILD_BASELINE.md §3` |
| 干净副本配置 | 两次均 exit 1，停在工具链探测，**未进入源码编译** | `logs/configure-ninja.log`、`logs/configure-default.log` |
| 依赖解析 | spdlog / magic_enum / glm 下载+解压+缓存命中；其余 3 项按“不准备环境”中止 | `logs/deps-resolve-partial.log` |
| include 闭包 | 2152 处工程内 include 全部解析，**0 断链** | `TEST_REPORT.md §7` 26 |
| 改动文件符号 | 4 个外部标识符全部有声明 | 同上 27 |

## 8. 本阶段完成条件的判定

| 条件 | 判定 | 说明 |
|---|---|---|
| 构建环境可复现 | ⚠️ **部分** | 构建机工具链已用备份缓存**确定**（不再靠猜）；复现脚本 `tools/build/verify-build.ps1` 已就绪；但本机无法实际执行 |
| 允许范围内的独立模块通过编译与测试 | ❌ **未达成** | 无编译器；且本工程是单一 SHARED 目标，`add_library(Solstice SHARED ${sourceFiles})` 全量 GLOB，不存在“单模块 target”，见 `COMPATIBILITY_MATRIX.md §6` |
| 所有未解决问题有明确记录 | ✅ **达成** | `REVERSE_ENGINEERING_REPORT.md §8`（逆向未确认项）、`COMPATIBILITY_MATRIX.md §4`（补丁 OPEN 项）、`TEST_REPORT.md §8`（错误四分类） |

**结论：本阶段 3 个完成条件，1 项达成、1 项部分、1 项未达成。**
未达成的唯一原因是本机没有 C++ 工具链，且按用户要求不在本机安装环境。

## 9. 下一步（构建机上执行，一步到位）

```bat
git -C G:\hacker pull
:: 然后在 PowerShell 里执行（脚本路径见 tools/build/verify-build.ps1）
```

脚本产出 4 个文件，回传即可定位：

```
build-verify\env.log        cl / ninja / rc / SDK / git HEAD / 未提交改动
build-verify\configure.log  CMake 完整输出（含依赖下载）
build-verify\build.log      编译器完整输出（-k 0，不在首个错误处停）
build-verify\summary.txt    错误码归并 + 首个出现位置 + 失败文件清单
```

拿到后我会按“环境 / 依赖 / 源码 / 接口”分类，只对**能证明原因**的错误做最小修改，
并明确区分“编译通过”与“运行正常”两件事——后者仍然只能由注入日志判定。
