# slot 164 四层重新验证 — Batch 3.6.2-E 第七工作组

约束：**不得盲改 slot 164**。本文件在 `unpacked`（1.21.120.0）静态镜像上重验四层；对运行期 EXE 的适用域见 `runtime-image-identity.md`（RUNTIME_IMAGE_MISMATCH 待补齐）。

## 第一层：签名与提取（在 unpacked 镜像上实测）

签名（`src/SDK/OffsetProvider.hpp:221`）：
```text
48 8B 49 08   mov rcx, [rcx+8]
48 8B 01      mov rax, [rcx]
48 8B 80 ?? ?? ?? ??   mov rax, [rax+disp32]
FF 15 ?? ?? ?? ??      call [vtable slot]
48 83 C4 38   add rsp, 0x38   (调用方清理栈，确认这是调用 vtable[164] 的 thunk)
```
- `disp_off = 10`（disp32 在签名内偏移 10 字节处）。

**实测扫描结果（unpacked 1.21.120.0）**：
```text
hits: 1   (唯一命中)
fileoff=0x1CBB9E4  rva=0x1CBC3E4
disp32=0x520 (1312)   1312 % 8 == 0   -> slot = 164
```

| 检查 | 结果 |
|---|---|
| 命中数量 | **1**（唯一，无歧义） |
| 命中 RVA | `0x1CBC3E4` |
| 操作数起点 | 偏移 10 |
| 原始 disp32 | `0x520` = 1312 |
| 除以 8 | 164 |
| 对齐 / 范围 | 1312%8==0 通过；164 ∈ (0,4096) 通过 |
| 命中周围反汇编 | `48 8B 80 20050000` = `mov rax,[rax+0x520]`；其后 `FF 15` 间接调用，再 `48 83 C4 38` 调用方 epilogue——确认是 `vtable[164]` 调用 thunk |

## 第二层：对象（传给 playUi 的 this）

- `this` = `ClientInstance::getMinecraftGame()` → `MinecraftGame::getInstance()` → `InstanceLocator::locateGame()` → `locate()` → `isPlausibleGame()`。
- `isPlausibleGame`（InstanceLocator）通过 `firstClientFromGame(game, MinecraftGame_mClientInstances) != nullptr` 确认对象结构合理（堆扫描 + 缓存 + 复验）。
- 明确区分：**结构 plausibility ≠ 真实对象语义**。可读但已脱钩的 `this` 也能通过——这正是 `isValidVirtualCall` 守卫要补的缺口。
- 内存区域 / vptr / RTTI / 生命周期：运行期需在 dump 中确认；静态镜像无法取得运行期对象实例（仅能从运行期 `crash.log`/dump 补）。

## 第三层：vtable[164] 目标

- `callVirtualFunc` 执行 `vtable[index]`（`index=164`），`×8` 由数组下标隐含——164 是**槽位索引**，非字节偏移。
- 在 unpacked 镜像上：`vtable + 0x520` 是可读槽，目标指向游戏代码区（需运行期 `isPatchableCode` 确认可执行）。
- 守卫 `isValidVirtualCall` 已检查：对象可读、vtable 可读、slot 可读、target 可执行（`isPatchableCode(target,1)`）。
- 未覆盖：target 是否**属于游戏模块**、target 语义是否**确实是 playUi**——见 `virtual-call-guard-review.md`。

## 第四层：ABI（见 `playui-callsite-and-abi.md`）

- 声明 `void playUi(const std::string&, float, float)`。
- x64：`RCX=this`、`RDX=std::string&`、`XMM0=volume`、`XMM1=pitch`（**非 XMM2/XMM3**）。
- `__thiscall`：`vtable[index](_this, args...)`，`this` 在 `RCX`。与声明一致。
- **E.1 校正（P0-1）**：E.1 审查主张 `volume=XMM2 / pitch=XMM3`，但经核对 `MinecraftGame::playUi → MemUtils::callVirtualFunc<void, const std::string&, float, float>`（`MinecraftGame.cpp:81`）的真实调用点源码，两个 `float` 作为第 1、第 2 个浮点实参传入，编译后落在 XMM0/XMM1。MS x64 ABI 对整数/指针寄存器（RCX,RDX,R8,R9）与 XMM 浮点寄存器（XMM0–XMM3）按类型**独立计数**，position 法把浮点排到 XMM2/XMM3 是错误结论。slot 164 本身的四层推导不受 ABI 槽位影响；ABI 仅决定实参落点，不影响虚表索引。详见 `playui-callsite-and-abi.md` §4.1。

## 结论

- 在 **unpacked 1.21.120.0** 镜像上，slot 164 四层**全部通过且唯一命中**，推导正确，**不改动该数字**。
- 适用域限制：以上结论基于静态镜像；运行期 `E:\pf\3.9.20.300505\Minecraft.Windows.exe`(1.21.120.0) 的 SHA 尚未取得（RUNTIME_IMAGE_MISMATCH），须比对一致后才可对运行期直接套用；若不一致必须在运行期镜像重验。
- 若下一版构建（带 669ce2c 守卫）复现 AV：`AccessType=execute` 且 `FaultAddress==vtable[164]` 目标落在无效页 → 槽位版本错配（须按实际 exe 重推 disp32）；`AccessType=read/write` 落在 `this`/vtable 附近 → 对象脱钩（查 InstanceLocator 缓存生命周期）。
