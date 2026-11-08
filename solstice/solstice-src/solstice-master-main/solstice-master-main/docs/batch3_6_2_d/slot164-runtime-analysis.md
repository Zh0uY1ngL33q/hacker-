# slot 164 四层验证 — Batch 3.6.2-D 第四工作组

任务硬性约束：**不得直接把 slot 164 改成其他数值**。本文件用四层静态验证说明
「164 这个推导本身是对的」，以及「为什么盲改数字不可取」。

## 1. 第一层：签名层

`src/SDK/OffsetProvider.hpp:221`：
```cpp
DEFINE_INDEX_FIELD(MinecraftGame_playUi,
    "48 8B 49 08 48 8B 01 48 8B 80 ? ? ? ? FF 15 ? ? ? ? 48 83 C4 38",
    10, OffsetType::Index);
```
- `48 8B 49 08` = `mov rcx, [rcx+8]`（取对象二级指针）
- `48 8B 01` = `mov rax, [rcx]`
- `48 8B 80 [disp32]` = `mov rax, [rax+disp32]`（读 vtable 基址）
- `FF 15 [disp32]` = 间接调用
- disp32 在签名内偏移 **10** 处读取。签名唯一命中，无歧义。

## 2. 第二层：对象/数值层（宏展开）

`DEFINE_INDEX_FIELD` 对 `OffsetType::Index` 的处理（OffsetProvider.hpp:54-86）：
- 取 `index_offset=10` 处的 disp32 原始值 `rawValue`；
- 要求 `(rawValue % sizeof(uintptr_t)) == 0`，否则拒绝；
- `value = rawValue / 8`。

要得到 `value = 164`，则 `rawValue = 164 * 8 = 1312`，且 `1312 % 8 == 0` ✓。
**数学正确，不存在「槽位/字节偏移」混淆**：164 是 vtable 索引，不是字节偏移。

## 3. 第三层：虚表层

`MemUtils::callVirtualFunc<void, const std::string&, float, float>(index, this, ...)`：
```cpp
auto vtable = *reinterpret_cast<uintptr_t**>(_this);
return reinterpret_cast<Fn>(vtable[index])(_this, args...);
```
`index` 直接作为 **vtable 槽位**下标使用（×8 由数组下标隐含），与「164 是索引」语义一致。
`Fn = Ret(__thiscall*)(void*, Args...)`，与游戏侧 `playUi(const std::string&, float, float)` 形参顺序匹配。

## 4. 第四层：调用 ABI 层

- `__thiscall` 在 x64 被忽略，使用默认 Windows x64 ABI：隐式 `this` 是第一个参数（指针→RCX）；`const std::string&` 引用=指针走 `RDX`；浮点参数走 `XMM0..XMM3`，XMM 编号对应参数在完整参数列表中的序位（E.1.2.1 用户裁定）。
- **层 ①（按当前 C++ 函数类型推导的 ABI，E.1.2.1）**：`this=RCX`、`const std::string& soundName=RDX`、`float volume=XMM2`（参数序位第 3）、`float pitch=XMM3`（参数序位第 4），返回 `void`。
- **三层区分（E.1.2.1）**：层 ① 类型推导 ABI = `volume=XMM2 / pitch=XMM3`（确定性）；层 ②（游戏调用方装载 XMM）/ 层 ③（playUi 目标读取 XMM）在混淆二进制中未能定位，标 **BINARY_ABI_CORROBORATION_PENDING**——层 ②③ 缺失不改变层 ① 结论。
- 与任务 §二 最初请求的 `RCX / RDX / XMM2 / XMM3` 一致。E.1.1 曾订正原 D 文档误写的 `volume=R8/pitch=R9`；E.1.1/E.1.2 一度维持 XMM0/XMM1，该结论被 E.1.2.1 修订为 XMM2/XMM3（按参数整体序位编号，详见 `../batch3_6_2_e/playui-callsite-and-abi.md` §4 与 `corrections-E1.2.1.md`）。

## 5. 关键告诫：四层都是「静态」验证

四层验证证明的是 **「164 这个映射的推导过程是正确且自洽的」**，
**并不证明** slot 164 在「运行中的游戏版本」上仍指向正确的 `playUi` 函数。

若运行中的 `Minecraft.Windows.exe` 版本与当初扫描签名时的版本不同，
`vtable[164]` 可能指向另一个虚函数。这种情形下：
- `isPlausibleGame` 仍会通过（对象结构合理）；
- 四层静态检查仍全绿（映射推导没错）；
- 但虚调用会跳进错误/垃圾代码 → AV。

## 6. 为什么「盲改 164 成别的数字」是错的

- 164 的推导是对的；真正的变量是**运行期版本与签名扫描期版本的 vtable 布局差异**。
- 单改一个数字既无法保证命中正确函数，又可能把原本偶发的问题变成必崩，
  且违背任务「不随机改动映射」的约束。
- 正确做法：用 3.6.2-D 新增的 `isValidVirtualCall` 闸门 + 崩溃捕获的故障上下文
  （见 `exception-context-analysis.md`），让下一次构建精确判定
  「槽位版本错配」还是「对象脱钩」，再决定是否需要针对实际 exe 重新推导 disp32。

## 7. 结论

slot 164 的**四层静态验证全部通过**，数字本身无需改动。
保留 164，把判定权交给运行期 `isValidVirtualCall` 与故障上下文捕获。
