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

- 声明 `void playUi(const std::string&, float, float)`；函数指针 `Fn = Ret(__thiscall*)(void*, const std::string&, float, float)`；调用实参 `(_this, soundName, volume, pitch)`。
- **层 ①（按当前 C++ 函数类型推导的 ABI，E.1.2.1 用户裁定）**：`RCX=this`、`RDX=std::string&`、`XMM2=volume`（参数序位第 3）、`XMM3=pitch`（参数序位第 4）。
- `__thiscall`：在 x64 被忽略，使用默认 Windows x64 ABI；`vtable[index](_this, args...)`，`this` 在 `RCX`。隐式 `this` 是第一个参数，XMM 编号对应参数在完整参数列表中的序位。
- **三层区分（E.1.2.1）**：层 ① 类型推导 ABI = `volume=XMM2 / pitch=XMM3`（确定性）；层 ②（游戏调用方在 `call` 前把 volume/pitch 装进哪个 XMM）与层 ③（playUi 目标函数从哪个 XMM 读取）在混淆二进制中未能定位 thunk `0x1CBC3E4` 的调用者、也未能追踪 `call [rip+0x6eb6398]` 间接目标，标记 **BINARY_ABI_CORROBORATION_PENDING**——层 ②③ 的待验证状态不改变层 ① 的 ABI 推导结论。
- E.1/E.1.1/E.1.2 曾以"浮点按类型独立编号"推导 `volume=XMM0 / pitch=XMM1`，该结论被 E.1.2.1 用户裁定修订为 XMM2/XMM3（按参数整体序位编号）。见 `playui-callsite-and-abi.md` §4、§4.1、§4.2 与 `corrections-E1.2.1.md`。
- slot 164 本身的四层推导不受 ABI 槽位影响；ABI 仅决定实参落点，不影响虚表索引（164 不变）。

## 结论

- 在 **unpacked 1.21.120.0** 镜像上，slot 164 四层**全部通过且唯一命中**，推导正确，**不改动该数字**。
- 适用域限制：以上结论基于静态镜像；运行期 `E:\pf\3.9.20.300505\Minecraft.Windows.exe`(1.21.120.0) 的 SHA 尚未取得（RUNTIME_IMAGE_MISMATCH），须比对一致后才可对运行期直接套用；若不一致必须在运行期镜像重验。
- 若下一版构建（带 669ce2c 守卫）复现 AV：`AccessType=execute` 且 `FaultAddress==vtable[164]` 目标落在无效页 → 槽位版本错配（须按实际 exe 重推 disp32）；`AccessType=read/write` 落在 `this`/vtable 附近 → 对象脱钩（查 InstanceLocator 缓存生命周期）。
