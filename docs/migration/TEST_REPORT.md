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
