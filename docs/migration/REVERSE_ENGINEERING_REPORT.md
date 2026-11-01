# REVERSE_ENGINEERING_REPORT — 逆向依据与结论

## 1. 方法与工具

| 工具 | 作用 | 位置 |
|---|---|---|
| `scan_sigs.py` | 在内存镜像上**重实现**客户端 `SigManager::scanSig` / `OffsetProvider::scanSig` 的语义 | `tools/re/` |
| `rtti_vtable.py` | 类型描述符 → CompleteObjectLocator → vtable → 槽位 | 同上 |
| `pe_report.py` | 两个样本的 PE 元数据（磁盘 / 镜像分开） | 同上 |
| `audit_patches.py` | 字节补丁的两项机械判定：是否切断指令、删掉的寄存器是否仍被读 | 同上 |
| `symbol_usage.py` | 未解析符号 → 源码引用 → 功能映射 | 同上 |
| capstone 5.0.7 | 反汇编 | `E:\PY\python.exe` |

## 2. 证据链 A：离线扫描器与线上行为完全一致

这是所有后续结论的地基。若离线结果与线上不同，后面每一条都不可信。

```
线上（客户端自己的 sigreport.json）        离线（scan_sigs.py + 镜像）
signatures: 115 total / failed 20 / ambiguous 21   ->  115 / 20 / 21
offsets   :  58 total / failed 21 / ambiguous 12   ->   58 / 21 / 12
```

不只是数字相同，**四个集合的 74 个名字逐个相同**：

```
offsets    failed    live= 21 offline= 21 identical=True
offsets    ambiguous live= 12 offline= 12 identical=True
signatures failed    live= 20 offline= 20 identical=True
signatures ambiguous live= 21 offline= 21 identical=True
```

过程中发现并修正了一处自身错误：最初没有剥离 `//` 与 `/* */` 注释，把 5 个被注释掉的
签名和 4 个被注释掉的字段也算进了分母（120/62），去掉注释后与线上完全吻合。
这条写在这里是因为它同时证明了镜像的完整性和扫描语义的正确性。

## 3. 证据链 B：RTTI → 虚表 → 槽位

镜像里 `.rdata` 有 **23,297 个** `.?AV...@@` 类型名，`COL` 与 vtable 指针都在（绝对地址）。
解析链：

```
".?AVLevel@@" @ td 0xB10CED8
  -> COL 0x9E95368 / 0x9E95390 / 0x9E953B8        (3 个，多继承子对象)
  -> vtable 0x921FA60 / 0x9220A78 / 0x9220AD8
  -> 主表 514 个槽
      [  9] 0x05DE1680  mov rax, [rcx + 0x730] ; ret          <- 纯 getter
      [ 72] 0x05DE0A80  mov [rsp+0x18],rbx ; push rbp ; ...
```

### 用这条链推翻了一个现有假设

`DEFINE_INDEX_FIELD(Level_getPlayerMovementSettings, ..., OffsetType::Index)` 解出
`index = 576/8 = 72`。但槽 72 的实体是：

```
mov qword ptr [rdx], rax        ; out[0]  = 0
mov qword ptr [rdx+8], rax      ; out[8]  = 0
mov qword ptr [rdx+0x10], rax   ; out[0x10]= 0
mov rdx, qword ptr [rcx + 0x8e0]
test rdx, rdx ; je ...
mov rax, qword ptr [rdx + 0x10]
lock inc dword ptr [rax + 8]    ; shared_ptr 引用计数
```

即 `void f(Level*, shared_ptr<...>* out)`，返回的是 **shared_ptr**（三级指针 + 引用计数），
而不是 `SyncedPlayerMovementSettings*`。而源码是：

```cpp
return MemUtils::callVirtualFunc<SyncedPlayerMovementSettings*>(vIndex, this);
```

**结论**：`DEFINE_INDEX_FIELD(..., Index)` 只能保证“槽号算对了”，**不能保证调用约定对**。
它把 `mov rax,[rax+disp]` 一律解释成“虚表槽加载”，因此对
“返回 `T*` 的虚函数”成立，对“往 `rdx` 写出参的虚函数”就是错的——`rdx` 此时是未初始化的
寄存器，函数会往一个野地址写三个 qword。

这条不是猜测：可以从反汇编直接读出 `mov [rdx], rax` 三次写和一个 `lock inc [rax+8]`。
同类风险适用于所有 `OffsetType::Index` 声明（共 30 条），但**逐条核实需要运行时或
更深的调用点分析，本轮只证实了 72 这一条**。

## 4. 证据链 C：常量归属（已修）

`Animations` 把签名地址当 `float*` 写。两个候选地址的实际内容：

| 地址 | 值 | 含义 |
|---|---|---|
| `0x8F5A000`（`SigManager::TapSwingAnim` 解出的地址） | `-0.2443461` = **-14.000°** | 手持姿态表：邻近 `-1.50 / -2.00 / -10.54 / -29.79 / -32.09 / -41.25 / -87.73 / -108.00` |
| `0x8F31EF0`（`ItemInHandRenderer_TapSwingAngleRadians` 命中的 `mulss xmm12,[rip+d]` 的操作数） | `-1.3962634` = **-80.000°** | 整角度常量表：邻近 `-45 / -50 / -150` |

模块写的是 `glm::radians(-80.f)`，即它想改的是 **-80° 那个**。三条互相独立的佐证：

1. 数值精确等于 -80.0000°（float 位模式 `c2 b8 b2 bf`，与 `radians(-80.f)` 同值）；
2. 它所在的表全是整角度（-45/-50/-80/-150），而 -14° 所在的表是非整数姿态角；
3. 唯一命中的那条签名名字就叫 `..._TapSwingAngleRadians`，且它确实以 `mulss` 消费该常量。

顺带证明的副作用：旧代码在 `onDisable()` 里也写 `radians(-80)`，等于把 -14° 的条目
永久改成 -80°，**关掉模块也不会恢复**。已改为缓存原值后还原。

## 5. 证据链 D：字节补丁的边界与寄存器活性（已修 1 处，2 处 OPEN）

`NoSwitchAnimation` 旧形态在 `0x58EB4EC` 写 8 字节：

```
0x58EB4EC  f3 0f 59 c7        mulss xmm0, xmm7      <- 想改的就是这个
0x58EB4F0  41 0f 28 c9        movaps xmm1, xmm9     <- 被顺带覆盖
0x58EB4F4  48 8b ce           mov rcx, rsi
0x58EB4F7  f3 41 0f 59 d0     mulss xmm2, xmm8
0x58EB4FC  0f 57 f6           xorps xmm6, xmm6
0x58EB4FF  f3 41 0f 59 c9     mulss xmm1, xmm9      <- xmm1 在这里被读
```

`xmm1` 的定义被删除、读者仍在同一基本块内 → 补丁改变了与“切枪动画”无关的手持旋转项。
改成 4 字节 `0F 57 C0 90` 后 `audit_patches.py` 判定为 `ALIGNED` 且 `xmm0` 由补丁自身重定义。

`audit_patches.py` 的完整输出（本轮）：

```
NoSwitchAnimation  site=0x58eb4ec size=4  -> ALIGNED   xmm0 由补丁重定义
DefaultSwing       site=0x58fb1b0 size=8  -> ALIGNED   !! xmm3 在 0x58fb1c3 被读
DefaultSwing2      site=0x58fb1bb size=8  -> ALIGNED   !! xmm0 在 0x58fb1c3 被读
FluxSwing          签名未解析 -> 补丁是空操作
summary: 0 misaligned, 2 with a live register removed, 1 unresolved
```

后两处**没有改**，原因见 `MIGRATION_REPORT.md §3`（改完无法在本机编译验证）。建议的最小
替代方案（同长度、不引入未定义寄存器）：

```
DefaultSwing  把 movss xmm3,[rcx+0x8D0] 的位移改成 0x87C
              f3 0f 10 99 d0 08 00 00  ->  f3 0f 10 99 7c 08 00 00
DefaultSwing2 不要再 NOP；与上一条合起来使 subss xmm0,xmm3 恒为 0（x - x）
```

## 6. 证据链 E：签名命中在指令中间

`ItemInHandRenderer_renderItem_bytepatch2` 命中 `0x1CC86E5`，但真正的指令起点是 `0x1CC86E4`：

```
0x1CC86E4  48 8b 4e 60   mov rcx, [rsi+0x60]
              ^ 0x1CC86E5 从这里开始匹配 "8B 4E ? ..."
0x1CC86E7      ^ 低字节 0x60 —— 代码把 +2 当作“新偏移 0x1C”写进去
```

有效结果确实是 `mov rcx,[rsi+0x1C]`，但：

* `+2` 之所以落在位移上，是因为匹配点比指令起点晚 1 字节——**巧合**，不是设计；
* `0x1C` 不是 8 字节对齐，而这条指令把该字段当**指针**解引用（`mov rax,[rcx]`），
  因此 `[rsi+0x1C]` 是否真是指针字段 **UNKNOWN**。

## 7. 本轮新增的确定性结论（可直接引用）

1. `.text` 在磁盘样本与内存镜像之间**逐字节相同**（0x8B6E8A9 字节），代码分析可互认。
2. `.rdata` 的 2059/7454 页不同，差异全是指针值；**vtable/COL/RTTI 只能读镜像**。
3. 客户端的 `DEFINE_INDEX_FIELD(Index)` 有**调用约定风险**，已用槽 72 证实一例。
4. `-80°` 的挥剑角度常量在 `0x8F31EF0`，`-14°` 的手持姿态常量在 `0x8F5A000`。
5. `NoSwitchAnimation` 的 8 字节形态会破坏 `xmm1` 的定义。

## 8. 尚未确认（明确 UNKNOWN）

| 项目 | 为什么不能确认 |
|---|---|
| `Level` 的 `getHitResult` / `getLevelData` / `getPlayerList` 真实槽号 | 需要语义级识别（返回指针 vs 写出参），本轮只建立了 getter 枚举能力，未逐槽分析 |
| `Mob::getCurrentSwingDuration` 是否仍以独立函数存在 | 旧签名 0 命中；`0x58FCE90` 这个“已验证 RVA”来自历史记录，**本轮未复核**，不作为事实 |
| `FluxSwing` 的补丁点 | 0 命中，且 `ItemInHandRenderer_FluxSwingCallsite` 有 3 处多义 |
| `patchInHandSlot` 的 `0x1C` 是否真是指针字段 | 只能运行时验证 |
| `ScreenView_setupAndRender` / `ItemRenderer_render` 的新入口 | 0 命中，未做语义搜索 |
| `.winlice` 节内容 | 镜像未转储该节（rawSize=0） |
| 1.21.44 的任何二进制事实 | 无样本 |

## 9. 复现

```bash
S=solstice/solstice-src/solstice-master-main/solstice-master-main/src/SDK
E:/PY/python.exe tools/re/scan_sigs.py Dump/Minecraft.Windows_1.21.120.0_20261027_005946.image \
    --sig-header $S/SigManager.hpp --field-header $S/OffsetProvider.hpp \
    --json Dump/baseline_offline.json --show failed
E:/PY/python.exe tools/re/audit_patches.py Dump/Minecraft.Windows_1.21.120.0_20261027_005946.image \
    --baseline Dump/baseline_offline.json
E:/PY/python.exe tools/re/rtti_vtable.py Dump/Minecraft.Windows_1.21.120.0_20261027_005946.image \
    --class Level --exact --slots 0-10
E:/PY/python.exe tools/re/symbol_usage.py --baseline Dump/baseline_offline.json --only-bad
```
