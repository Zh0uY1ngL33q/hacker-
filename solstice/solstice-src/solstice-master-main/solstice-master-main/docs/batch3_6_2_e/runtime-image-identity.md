# 运行时游戏镜像身份 — Batch 3.6.2-E 第三工作组

## 1. 运行期镜像（来自崩溃日志）

```text
host process: E:\pf\3.9.20.300505\Minecraft.Windows.exe
Minecraft version: 1.21.120.0
```

该路径**在本沙箱内不可达**（沙箱只有 `G:/hacker`），因此无法在此计算其 SHA-256 / 大小 / PE ImageBase / SizeOfImage。

> 标记为 **RUNTIME_IMAGE_MISMATCH / NEEDS_RUNTIME**：运行期 EXE 的身份必须由构建机补齐，不得用「版本号都是 1.21.120.0」就假定与静态镜像同一构建（任务明确警告过）。

## 2. 本沙箱内可用的静态镜像

| 镜像 | SHA-256 | 大小 | PE | ImageBase | SizeOfImage | 版本 |
|---|---|---|---|---|---|---|
| `unpacked/unpacked_Minecraft.Windows.exe` | `5fb2e893f724b1d644561161b8a5c2180a333e8d7f181b44b856a05f89026deb` | 223,629,824 | PE32+ | `0x7FF66B9F0000` | `0xD7B7000` (226,193,408) | 1.21.120.0 |
| `1.21.44/Minecraft.Windows.exe` | `b9229ac97c32622ae65fed8e3b5a33f4c8b9658c62a6fbb1673abb2a57d3882c` | 114,485,760 | PE32+ | `0x140000000` | `0x6E57000` | 1.21.44 |

- `unpacked` 镜像的 SHA **与任务引用的 `5fb2e893...026deb` 完全一致**，是本次静态分析的 1.21.120.0 基准镜像。
- `1.21.44` 镜像版本不同、ImageBase 不同（`0x140000000` vs `0x7FF66B9F0000`），是**另一个构建**，不可比，仅作对照。
- 任务提到的内存镜像 `Minecraft.Windows_1.21.120.0_20261027_005946.image`（SHA `11147749...`）**不在本沙箱**（按名搜索无命中），如需要由构建机提供。

## 3. slot 164 结论对运行时镜像的适用域

- 对 `unpacked`（1.21.120.0）镜像：slot 164 已在本批 `slot164-validation.md` 中**重新验证为唯一命中**（RVA `0x1CBC3E4`，disp32 `0x520`，`/8=164`，8 字节对齐）。
- 但这是**静态分析镜像**，不是运行期 EXE。运行期 `1.21.120.0` 与 `unpacked` 1.21.120.0 是否字节一致，**必须等运行期 SHA 比对**才能确认。
- 若运行期 SHA == `5fb2e893...026deb`：slot 164 对运行期同样成立。
- 若不同（RUNTIME_IMAGE_MISMATCH）：**必须在实际目标镜像上重新验证 slot 164**，不得沿用不匹配镜像的静态结论。

## 4. 构建机下一步（补齐 RUNTIME_IMAGE_MISMATCH）

```bat
certutil -hashfile "E:\pf\3.9.20.300505\Minecraft.Windows.exe" SHA256
```
并将结果与 `5fb2e893f724b1d644561161b8a5c2180a333e8d7f181b44b856a05f89026deb` 比对；同时记录大小、ImageBase、SizeOfImage。只有 SHA 一致时，本批 slot-164 静态结论才能直接套用到运行期崩溃。

## 5. E.1 校正（P1-3）：`5fb2e893…026deb` 仅是可静态分析的 unpacked 镜像 SHA

> **关键澄清**：`5fb2e893f724b1d644561161b8a5c2180a333e8d7f181b44b856a05f89026deb` 是 **`unpacked/unpacked_Minecraft.Windows.exe` 这一个被 dump/解包后的静态文件的 SHA-256**，它**不是**运行期 `E:\pf\3.9.20.300505\Minecraft.Windows.exe` 的身份。**两者不能画等号。**

- 本沙箱里的 `unpacked` 镜像**没有标准 `.rdata` 段**（段名为空的自定义段 + `.winlice`/`.boot`/`.SCY`），说明它不是一次干净的构建产物，而是被解包/转储、段结构被改写过的镜像。因此它**既不代表磁盘上的运行 EXE，也不代表运行期解密后的内存映像**。
- 运行期身份必须由构建机从**真实运行的进程**取得三项，缺一不可：
  1. **磁盘 EXE SHA-256**（`certutil -hashfile` 或运行时 `GetFileInformationByHandle`+哈希）—— 与 `5fb2e893…` 比对；
  2. **PE 元数据**：ImageBase、SizeOfImage、段表（尤其确认是否存在打包/加密段，如本沙箱见到的 `.winlice`/`.boot`/`.SCY`）；
  3. **解密后的内存映像 SHA**（若游戏在加载时解密代码段）。磁盘 SHA 一致 ≠ 内存映像一致；若代码段运行期才解密，`unpacked` 静态镜像的 slot-164 落点**对内存映像可能根本不成立**。

- **严禁仅凭版本号 `1.21.120.0` 复用 slot 164**：版本号相同只说明游戏大版本一致，不代表该具体构建的 vtable 布局/代码偏移一致。slot 164 的 RVA `0x1CBC3E4`/disp32 `0x520` 是**针对 `unpacked` 这个具体镜像**推导的；只有在运行期镜像（磁盘 SHA 或解密后内存 SHA）被证明与之一致时，才能直接套用。否则**必须在目标镜像上重新扫描验证**，不得沿用。

- E.1 另确认：本次为在 `unpacked` 镜像上用 RTTI → vtable → slot 164 反汇编游戏 `playUi` 实况失败（无 `.rdata`、RTTI 不可靠），因此 P0-3 的"真实调用点反汇编"改以**本仓库 `callVirtualFunc` 源码**作为真实调用点证据（见 `playui-callsite-and-abi.md` §4），游戏侧 `playUi` 的实况反汇编留待运行期 dump 补齐。
