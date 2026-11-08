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
