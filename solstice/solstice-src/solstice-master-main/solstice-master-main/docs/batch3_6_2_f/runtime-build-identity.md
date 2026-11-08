# 3.6.2-F — Runtime Build Identity（基线确认）

## 固定版本（不可混淆）

本轮唯一目标版本：**Minecraft 1.21.120.0**。

- 之前出现的 `1.21.90.1` 日志属于另一次运行，**不作为本轮分析基线**。
- 不得因为版本号相似就混用不同镜像的 RVA / offset / vtable / 签名。

## 当前运行基线（来自用户提供日志）

| 项 | 值 |
|---|---|
| Build | `794fc65` |
| Branch | `main` |
| Minecraft | `1.21.120.0` |
| 游戏 EXE 路径 | `E:\pf\3.9.20.300505\Minecraft.Windows.exe` |
| 渲染路径 | GL (`wgl` SwapBuffers → `OpenGLGui::render`)，`D3DHook::present` 从未被调用 |

> 上述 EXE 路径/SHA/PE 字段不在本机，需在本轮构建/运行机器上采集填充。

## 本机实测（代码/分析机，2026-11-08）

为避免把"没查"写成"已确认"，本轮在本机实际探测了 §二 要求的全部身份字段：

| 字段 | 本机实测结果 | 证据等级 |
|---|---|---|
| 游戏 EXE 路径 | `E:\pf\3.9.20.300505\Minecraft.Windows.exe` → 映射为 `/e/pf/3.9.20.300505/Minecraft.Windows.exe`，**不存在** | 实测：运行机与代码机分离 |
| EXE SHA-256 | **无法计算**（文件不在本机） | PENDING |
| PE `ImageBase` / `SizeOfImage` | **无法计算**（文件不在本机） | PENDING |
| 当前 DLL SHA-256 | **无本机构建产物** | PENDING |
| Build Info | 仅有生成脚本 `GenerateBuildInfo.cmake`，**无生成的 `build_info.h`** | 实测 |
| CMakeCache | `find` 全仓无 `CMakeCache.txt` | 实测 |
| 编译器 | 无 `cl`/`ninja`/`g++`；仅有 Python wheel 提供的 `cmake` | 实测 |

因此本轮**不能**给出任何 EXE/DLL 哈希或 PE 头数值；也不拿 1.21.90.1 或其他
镜像的 RVA / offset / vtable / 签名代填。**所有镜像身份字段保持 PENDING**，
不以推测值填充，避免制造"版本已确认"的假象。

## 需在构建/运行机器上采集的字段（不得留空）

| 字段 | 采集方式 |
|---|---|
| EXE 绝对路径 | 运行进程镜像路径 |
| EXE SHA-256 | `GetFileInformationByHandle` / PowerShell `Get-FileHash` |
| PE `ImageBase` | `dumpbin /headers` 或 `ReadProcessMemory` 读 `IMAGE_NT_HEADERS.OptionalHeader.ImageBase` |
| PE `SizeOfImage` | 同上 `OptionalHeader.SizeOfImage` |
| 当前 DLL SHA-256 | 注入 DLL 文件的 `Get-FileHash` |
| Build Info | `build_info.h`（`SOLSTICE_VERSION` / `SOLSTICE_BUILD_VERSION` / cmake build type） |
| CMakeCache | `CMakeCache.txt` 中的 `SOLSTICE_*` 选项（含 `SOLSTICE_RENDER_DIAG`、`SOLSTICE_DISABLE_AURA_DIAGNOSTIC`、`SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS`） |

## 当前映射可信度（保留，不修复）

```
Offsets:   OK=26/57  MISSING=24  AMBIGUOUS=6  IMPLAUSIBLE=1
Signatures:OK=73/115 MISSING=20  AMBIGUOUS=21 OUT_OF_RANGE=1
Combined unusable = 73
```

状态仍为 **NOT TRUSTWORTHY**。本轮**不得修改** `OffsetProvider` 的映射数值；
保留原有 DEBUG 人工放行；不把人工放行视为映射已正确。

## 本轮新构建开关

新增 `SOLSTICE_RENDER_DIAG`（默认 OFF）。本轮交付的诊断代码仅在开启该开关时
编译进二进制。开构建前请确认该开关状态并记录到 CMakeCache 中，以便把"诊断构建"
与"发布构建"区分开（见 `subscriber-instrumentation.md`）。

## 运行期镜像身份

仍为 `RUNTIME_IMAGE_IDENTITY_UNVERIFIED`：尚未用构建机运行 EXE 的 SHA 与本地逆向
镜像比对。若构建机可提供运行 EXE 的 SHA，应补齐该比对（对应历史 O4 项）。
