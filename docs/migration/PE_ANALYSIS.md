# PE_ANALYSIS — Minecraft.Windows.exe 1.21.120.0

> 所有数字来自 `tools/re/pe_report.py` 的输出（`Dump/pe_disk.json`、`Dump/pe_image.json`），
> 汇总在 `BINARY_METADATA.json`。没有一项来自记忆或推测。

## 1. 两个样本，必须分开对待

| 样本 | 路径 | SHA-256 | 布局 |
|---|---|---|---|
| 磁盘脱壳文件 | `unpacked/unpacked_Minecraft.Windows.exe` | `5fb2e893f724b1d644561161b8a5c2180a333e8d7f181b44b856a05f89026deb` | 文件偏移 = `PointerToRawData` |
| 运行时内存镜像 | `Dump/Minecraft.Windows_1.21.120.0_20261027_005946.image` | `11147749f1a4085b15b13cec4f45952d297acb275c0966954bdff4cbd2ff9b70` | 文件偏移 = RVA |

**这不是同一份东西。** 逐节比对（`tools/re/pe_report.py` + 逐节哈希）：

```
idx        rva       size   disk==image
  0     0x1000  0x8b6e8a9    identical     <- .text，全部签名都在这里
  1  0x8b70000  0x1d1e06e      DIFFERS     <- .rdata
  2  0xa88f000  0x11adf20      DIFFERS     <- 可写数据（运行时状态）
  3  0xba3d000   0x4b1cf8    identical
  4  0xbeef000     0xeae0    identical
  5  0xbefe000    0x42638    identical
  6  0xbf41000   0x149b74    identical
  7  0xc08b000     0x1000      DIFFERS     <- .edata
  8  0xc08c000     0x1a00      DIFFERS     <- .idata
  9  0xc08e000     0x4000      DIFFERS     <- .tls
 10  0xc092000    0x42800    identical
 11  0xc0d5000   0xea6000      n/a        <- .winlice：磁盘有 15 MB，镜像里没抓到
 12  0xcf7b000   0x82f000    identical     <- .boot
 13  0xd7aa000     0x1000      DIFFERS     <- .reloc
```

### .rdata 为什么不一样

7454 个页里有 **2059 页不同**，差异全是 8 字节绝对指针（模块 VA），例如：

```
0x8b70000  disk: 20 bc db 72 fa 7f 00 00   -> 0x00007FFA72DBBC20
           img : 88 6d 26 21 f7 7f 00 00   -> 0x00007FF721266D88
```

磁盘文件里的指针值指向模块之外（`0x7ff714e80000 + 0xd7b7000 = 0x7ff7f3b7000` 之上），
说明它们是**另一次进程运行/另一个基址上的残留**，不可直接使用。

标量常量两边一致，例如本案用到的两个 float：

```
0x8f5a000  disk == img  -> dd 35 7a be = -0.2443461 rad = -14.000 deg
0x8f31ef0  disk == img  -> c2 b8 b2 bf = -1.3962634 rad = -80.000 deg
```

**结论**：`.text` 与标量常量可以跨样本互认；**指针、vtable、COL、IAT 只能读内存镜像**。

## 2. 头部信息

| 字段 | 磁盘脱壳文件 | 内存镜像 |
|---|---|---|
| Machine | `0x8664` (AMD64) | 同 |
| Optional magic | `0x20B` (PE32+) | 同 |
| ImageBase | `0x7FF66B9F0000` | `0x7FF714E80000` |
| EntryPoint | `0x236940` (在 `.text`) | `0xCF7B058` (在 `.boot`) |
| SizeOfImage | `0xD7B7000` | `0xD7AB000` |
| Sections | 15 | 14 |
| TimeDateStamp | `0x6ACA042B` | 同 |
| Subsystem | `0x2` (GUI) | 同 |
| DllCharacteristics | `0xC120` (DYNAMIC_BASE \| NX \| GUARD_CF \| HIGH_ENTROPY_VA \| TS_AWARE) | 同 |
| BASERELOC 目录 | `0xD7AA000`, size `0x10` → 4 条重定位 | `0xD7AA000`, size `0x10` → 0 条 |
| DEBUG 目录 | 空 | 空 |
| TLS 目录 | `0xC0911D0`, size `0x28` | 同 |

`GUARD_CF` 置位解释了 `SigManager`/`OffsetProvider` 里那条关键指令形状：
虚调用不是 `FF D0`，而是

```
mov rax, [rcx]            ; vtable
mov rax, [rax + slot*8]   ; slot
call qword ptr [rip+X]    ; __guard_dispatch_icall_fptr
```

`DEFINE_INDEX_FIELD` 里 `48 8B 80 <disp32> FF 15` 的 `disp32/8` 就是槽号，机制成立。

## 3. 这是被处理过的文件，不是游戏原始 EXE

磁盘样本带 3 个非标准节：`.SCY`（`0xE0000060`，可执行可写）、`.winlice`、`.boot`。
`.SCY` 是 Scylla 的默认节名；且 IMPORT 目录被搬到了 `0xD7AD8F0` —— 这个 RVA 落在
`.SCY` 内部，而原 `.idata`（`0xC08C000`）被保留为空壳。同时 BASERELOC 只剩 `0x10` 字节。

因此：

* **不能**用这个文件做 OS 加载器行为推断（重定位表已被破坏，它不可能在任意基址正常加载）；
* 只把它当作 `.text` + 标量常量的只读副本；
* 任何“运行时地址”一律以内存镜像 `moduleBase = 0x7FF714E80000` 为准。

## 4. 导入表（89 个描述符，含重复项）

与图形/兼容性相关的部分：

| DLL | 函数数 | 意义 |
|---|---|---|
| `opengl32.dll` | 4 | GL 呈现路径存在，与客户端只走 `wglSwapBuffers` 的实测一致 |
| `gdi32.dll` | 18 | `SwapBuffers` 也在其中 |
| `d3d11/d3d12/dxgi/d2d1/dwrite` | 链接期静态 | 见 CMakeLists `target_link_libraries` |
| `cohtml.WindowsDesktop.dll` | 51 | 内嵌 HTML UI（主菜单/商店） |
| `fmod64.dll` / `PhysX*` | 55 / 59 | 音频 / 物理 |
| `msvcp140` `vcruntime140` `vcruntime140_1` `ucrtbase` | 245/25/1/… | MSVC 动态 CRT |

`.edata` 有 28 个导出（`DllGetActivationFactory` 一族 + WinRT 激活），与 UWP 打包形态一致。

## 5. 无法确认的内容（明确标记 UNKNOWN）

| 项目 | 状态 | 原因 |
|---|---|---|
| 精确 MSVC 工具集版本（19.4x?） | UNKNOWN | 无 Rich header、无 PDB、无 `/VERBOSE` 信息；只能确认是 MSVC + 动态 CRT |
| 编译优化开关（/O2 /Ob3 /GL） | UNKNOWN | 只能从代码形态观察到大量内联（`Mob::getCurrentSwingDuration` 已消失即一例），不足以定版本 |
| `.winlice` 内容 | UNKNOWN | 内存镜像该节 `rawSize = 0`（未转储）；磁盘副本熵 7.05，不是明文代码 |
| 是否有运行时解密后再执行的代码 | UNKNOWN | 单次 dump 只能证明“dump 时刻”的字节；`.winlice` 缺失本身就是一个反例 |
| PDB / 调试信息 | 无 | DEBUG 目录为空，`debug: []` |

## 6. 复现命令

```bash
E:/PY/python.exe tools/re/pe_report.py unpacked/unpacked_Minecraft.Windows.exe --json Dump/pe_disk.json
E:/PY/python.exe tools/re/pe_report.py Dump/Minecraft.Windows_1.21.120.0_20261027_005946.image --image --json Dump/pe_image.json
```
