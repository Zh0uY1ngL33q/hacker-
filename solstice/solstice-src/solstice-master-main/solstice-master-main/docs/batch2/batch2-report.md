# Solstice 1.21.120 Batch 2 — 11 条 Ambiguous Offset 候选分析报告

## 1. 范围与证据边界

- 基线：`517646e7bc48633528b231a54110decef815af02` (`main`)
- 目标：Minecraft `1.21.120.0`, Build `3.9.20.300505`
- 镜像：`Minecraft.Windows_1.21.120.0_20261027_005946.image`
- SHA-256：`11147749F1A4085B15B13CEC4F45952D297ACB275C0966954BDFF4CBD2FF9B70`
- 镜像基址：`0x7FF714E80000`；`SizeOfImage=0xD7AB000`
- 本报告是**静态验证**，不是运行时验证。1.21.44 只作为结构参考，没有复制其偏移、槽号或 RVA。
- 未修改功能模块、Hook 执行策略、渲染控制流或网络发送逻辑。

完整逐候选证据在 [`ambiguous-candidates.json`](ambiguous-candidates.json)：每个命中均含 RVA、VA、Section、前后字节、指令边界、PE x64 `RUNTIME_FUNCTION` 入口/结尾、基本块和反汇编。该文件是“每个候选 RVA 与反汇编”的权威附件，报告正文不重复 495 个完整反汇编窗口。

参考版本的同格式证据在 [`reference-1.21.44-candidates.json`](reference-1.21.44-candidates.json)。最终机器可读结论在 [`batch2-assessment.json`](batch2-assessment.json)。

## 2. 命中计数语义核实

- C++ `OffsetProvider.cpp` 与 `SigManager.cpp` 均调用 `CodeSection::countMatches(sig, 2)`；因此运行时 JSON 的 `matches=2` 是“至少两个”，不是完整总数。
- 两套常规离线扫描器默认最多保留 64 个命中；`matches=64` 同样可能被截断。
- `tools/batch2_analyze.py` 使用同一可执行段范围和重叠匹配语义，但将上限提高到 1,000,000，得到本批真实总数。
- 所有 11 条命中均在目标镜像第一个可执行段；没有跨段拼接。

| 映射 | 运行时可见 | 常规离线可见 | 完整命中 | 指令边界命中 |
|---|---:|---:|---:|---:|
| `GameSession_mEventCallback` | 2+ | 6 | 6 | 6 |
| `Actor_mDestroying` | 2+ | 8 | 8 | 2 |
| `ClientInstance_mStackScene` | 2+ | 8 | 8 | 8 |
| `ClientInstance_setDisableInput` | 2+ | 64+ | 122 | 122 |
| `ClientInstance_grabMouse` | 2+ | 8 | 8 | 8 |
| `ClientInstance_releaseMouse` | 2+ | 64+ | 122 | 122 |
| `Level_getHitResult` | 2+ | 64+ | 182 | 181 |
| `BlockSource_setBlock` | 2 | 2 | 2 | 2 |
| `ClientInstance_getBlockSource` | 2+ | 4 | 4 | 4 |
| `gDataUtil_MiscData_Instance` | 2+ | 31 | 31 | 31 |
| `LevelData_mTick` | 2 | 2 | 2 | 2 |

## 3. 最终判断与修改前后状态

`STATIC VERIFIED` 仍只表示静态语义证据充分；必须经构建机和目标游戏验证后才能称为 `RUNTIME VERIFIED`。

| 映射 | 修改前 | 最终分类 | 修改后离线结果 | 值 | 选择/拒绝理由 |
|---|---|---|---|---:|---|
| `GameSession_mEventCallback` | AMBIGUOUS (6) | **STATIC VERIFIED** | OK (1) | 80 | Adjacent 0x48/0x50/0x58 cleanup sequence; identical structure in 1.21.44. |
| `Actor_mDestroying` | AMBIGUOUS (8) | **STATIC VERIFIED** | OK (1) | 0x166a | Only object-field candidate; Actor-layout destroy gate is structurally homologous to 1.21.44. |
| `ClientInstance_mStackScene` | AMBIGUOUS (8) | **STATIC VERIFIED** | OK (1) | 0x640 | Two independent ClientInstance UI accesses agree on 0x640; selected site is tied to hud_screen. |
| `ClientInstance_setDisableInput` | AMBIGUOUS (122) | **UNVERIFIED** | AMBIGUOUS (64) | — | Pattern is identical to releaseMouse and all 122 candidates coincide; slot 436 is unrelated. |
| `ClientInstance_grabMouse` | AMBIGUOUS (8) | **UNVERIFIED** | AMBIGUOUS (8) | — | Extraction offset lands on the following instruction in all 8 matches. |
| `ClientInstance_releaseMouse` | AMBIGUOUS (122) | **UNVERIFIED** | AMBIGUOUS (64) | — | Pattern is identical to setDisableInput and all 122 candidates coincide; no semantic winner. |
| `Level_getHitResult` | AMBIGUOUS (182) | **UNVERIFIED** | AMBIGUOUS (64) | — | All 182 matches start at direct CALL; extraction reads rel32 bytes, not a vtable slot. |
| `BlockSource_setBlock` | AMBIGUOUS (2) | **STATIC VERIFIED** | OK (1) | 34 | Complete six-argument setBlock call shape; same flow in 1.21.44, with expected slot drift. |
| `ClientInstance_getBlockSource` | AMBIGUOUS (4) | **STATIC VERIFIED** | OK (1) | 318 | Unique full wrapper plus independent ClientInstance vtable getter of member 0x118. |
| `gDataUtil_MiscData_Instance` | AMBIGUOUS (31) | **UNVERIFIED** | AMBIGUOUS (31) | — | All 31 matches are RIP-relative globals; current FieldOffset extraction is semantically invalid. |
| `LevelData_mTick` | AMBIGUOUS (2) | **UNVERIFIED** | AMBIGUOUS (2) | — | One candidate reads opcode bytes; the other is [rbp+0x138] stack-frame state, not LevelData. |

### 3.1 已静态确认的 5 条

#### GameSession_mEventCallback = `0x50`

旧模式的 6 个值为 `0x08 ×4 / 0x20 / 0x50`。RVA `0x57A0570` 是唯一具有相邻成员清理序列的候选：

```asm
0x57A0555  mov rcx,[rbx+48h]
...
0x57A0570  mov rcx,[rbx+50h]   ; target
...
0x57A0586  mov rcx,[rbx+58h]
```

1.21.44 的唯一候选具有相同 `0x48/0x50/0x58` 结构。新签名锚定后续 `+0x58`，目标镜像唯一命中。

#### Actor_mDestroying = `0x166A`

RVA `0x1A68605`：

```asm
mov rbx,rcx
mov rcx,[rcx]
mov rdi,[rbx+18h]
movzx eax,byte ptr [rcx+166Ah] ; target
cmp byte ptr [rbx+10h],0
je ...
test al,al
je ...
```

其控制流与 1.21.44 的 `+0xB8D` 候选同构；其余 6 个大值从指令中间开始，`+0x88` 候选属于栈帧对象。

#### ClientInstance_mStackScene = `0x640`

RVA `0x199292D` 与 `0x199A53F` 两个独立 ClientInstance UI 函数均访问 `+0x640` 并交给同一个 stack-scene 消费函数。被选择的 `0x199292D` 位于 `hud_screen` 状态切换路径；新签名通过该路径唯一化。`0x1788` 属于不同基对象，`0xD8` 为栈帧字段，其余三个为 RIP-relative 全局。

#### BlockSource_setBlock = slot `34`

RVA `0x2A93A7E` (`raw=0x110`) 前的完整参数流为：`rcx=BlockSource, rdx=BlockPos, r8=Block, r9d=flags`，另有两个栈参数。1.21.44 在同形调用中为 slot 32。另一个候选 `0x5DC40F8` 是引用计数清理且提取跨指令。

#### ClientInstance_getBlockSource = slot `318`

RVA `0x1E95493` (`raw=0x9F0`) 位于唯一完整 wrapper：先从 `[this+0x1A8]` 取得 ClientInstance，再调用 slot 318 并消费返回值。独立 vtable 审计中 slot 318 是返回 `[this+0x118]` 的单行 getter，该字段已由 ClientInstance 初始化布局确认为 BlockSource。两个 slot 35 候选是 getLocalPlayer；slot 69 的参数/返回语义不同。

### 3.2 未确认且继续拒绝的 6 条

- `ClientInstance_setDisableInput` / `ClientInstance_releaseMouse`：模式逐字相同，122 个候选完全重合；121 个提取 opcode 垃圾，唯一看似合理的 slot 436 位于无关 wrapper。不能给两个语义强行共享槽位。
- `ClientInstance_grabMouse`：8 个命中都从 `mov rax,[rcx]` 开始，但提取位置 `+3` 正好落在下一条指令，而不是 vtable disp32。
- `Level_getHitResult`：182 个命中均为直接 `CALL rel32` 序列，`+3` 读取的是 rel32 中间字节，不是虚函数槽。181 个命中虽处于指令边界，也不能使错误的操作数类型变正确。
- `gDataUtil_MiscData_Instance`：31 个命中都是 RIP-relative 全局加载；当前宏按 `FieldOffset` 解释 rel32，语义错误。真实全局初始化链未找到。
- `LevelData_mTick`：RVA `0x5A9151E` 从 `mov rdx,rax` 提取 opcode；RVA `0x63F1CA7` 是 `[rbp+0x138]` 栈帧对象，非 LevelData。1.21.44 的 `+0x468` 不可直接移植。

## 4. 全部候选 RVA 索引

下列列表与 `ambiguous-candidates.json` 中的逐候选反汇编一一对应。

### `GameSession_mEventCallback` (6)

`0x1B894D1`, `0x1C0129C`, `0x1CF0F39`, `0x1CF1075`, `0x1CF11B1`, `0x57A0570`

### `Actor_mDestroying` (8)

`0x1A68605`, `0x3491B70`, `0x34923A7`, `0x3492C5D`, `0x349328F`, `0x349362F`, `0x3494010`, `0x608A540`

### `ClientInstance_mStackScene` (8)

`0x199292D`, `0x199851B`, `0x199A53F`, `0x199BE87`, `0x2A4CBBB`, `0x53E4223`, `0x8494873`, `0x84AAE5E`

### `ClientInstance_setDisableInput` (122)

`0x1BD40E7`, `0x1BD42F7`, `0x1BD4507`, `0x1BD4717`, `0x1CF33D7`, `0x1CF35E7`, `0x1CF37F7`, `0x1CF3A07`, `0x1CF3C17`, `0x1CF3E27`, `0x1CF4037`, `0x1F02017`
`0x1F02227`, `0x1F02437`, `0x1F02647`, `0x1F02857`, `0x1F02A67`, `0x1F02C77`, `0x1F02E87`, `0x1F03097`, `0x1F032A7`, `0x1F034B7`, `0x1F036C7`, `0x1F038D7`
`0x1F03AE7`, `0x1F03CF7`, `0x1F03F07`, `0x1F2DBE7`, `0x1F2DDF7`, `0x1F2E007`, `0x1F2E217`, `0x1F2E427`, `0x1F2E637`, `0x1F2E847`, `0x1F2EA57`, `0x1F2EC67`
`0x1F2EE77`, `0x1F2F087`, `0x1F5CDB7`, `0x1F5CFC7`, `0x1F5D1D7`, `0x1F5D3E7`, `0x1F5D5F7`, `0x1F5D807`, `0x1F5DA17`, `0x1F5DC27`, `0x1F5DE37`, `0x1F5E047`
`0x1F5E257`, `0x1F5E467`, `0x1F5E677`, `0x1F5E887`, `0x1F5EA97`, `0x1F5ECA7`, `0x1F5EEB7`, `0x1F5F0C7`, `0x1FBC8A7`, `0x1FBCAB7`, `0x1FBCCC7`, `0x1FBCED7`
`0x1FBD0E7`, `0x1FBD2F7`, `0x1FBD507`, `0x1FBD717`, `0x1FBD927`, `0x1FBDB37`, `0x1FBDD47`, `0x1FBDF57`, `0x1FBE167`, `0x1FBE377`, `0x1FBE587`, `0x1FDF697`
`0x1FDF8A7`, `0x1FDFAB7`, `0x1FDFCC7`, `0x1FDFED7`, `0x1FE00E7`, `0x1FE02F7`, `0x1FE0507`, `0x1FE0717`, `0x1FE0927`, `0x1FE0B37`, `0x1FE0D47`, `0x1FE0F57`
`0x1FE1167`, `0x1FE1377`, `0x1FE1587`, `0x1FE1797`, `0x1FE19A7`, `0x1FE1BB7`, `0x1FE1DC7`, `0x1FE1FD7`, `0x1FE21E7`, `0x1FE23F7`, `0x201A807`, `0x201AA17`
`0x201AC27`, `0x201AE37`, `0x201B047`, `0x201B257`, `0x201B467`, `0x201B677`, `0x201B887`, `0x201BA97`, `0x201BCA7`, `0x201BEB7`, `0x201C0C7`, `0x201C2D7`
`0x201C4E7`, `0x201C6F7`, `0x201C907`, `0x201CB17`, `0x201CD27`, `0x201CF37`, `0x201D147`, `0x201D357`, `0x2036EE7`, `0x20370F7`, `0x2037307`, `0x2037517`
`0x2037727`, `0x394B0E0`

### `ClientInstance_grabMouse` (8)

`0xBFC377`, `0x1992638`, `0x19989AD`, `0x1E5F604`, `0x1E72BAC`, `0x1E8A851`, `0x2935A2F`, `0x3A049A7`

### `ClientInstance_releaseMouse` (122)

`0x1BD40E7`, `0x1BD42F7`, `0x1BD4507`, `0x1BD4717`, `0x1CF33D7`, `0x1CF35E7`, `0x1CF37F7`, `0x1CF3A07`, `0x1CF3C17`, `0x1CF3E27`, `0x1CF4037`, `0x1F02017`
`0x1F02227`, `0x1F02437`, `0x1F02647`, `0x1F02857`, `0x1F02A67`, `0x1F02C77`, `0x1F02E87`, `0x1F03097`, `0x1F032A7`, `0x1F034B7`, `0x1F036C7`, `0x1F038D7`
`0x1F03AE7`, `0x1F03CF7`, `0x1F03F07`, `0x1F2DBE7`, `0x1F2DDF7`, `0x1F2E007`, `0x1F2E217`, `0x1F2E427`, `0x1F2E637`, `0x1F2E847`, `0x1F2EA57`, `0x1F2EC67`
`0x1F2EE77`, `0x1F2F087`, `0x1F5CDB7`, `0x1F5CFC7`, `0x1F5D1D7`, `0x1F5D3E7`, `0x1F5D5F7`, `0x1F5D807`, `0x1F5DA17`, `0x1F5DC27`, `0x1F5DE37`, `0x1F5E047`
`0x1F5E257`, `0x1F5E467`, `0x1F5E677`, `0x1F5E887`, `0x1F5EA97`, `0x1F5ECA7`, `0x1F5EEB7`, `0x1F5F0C7`, `0x1FBC8A7`, `0x1FBCAB7`, `0x1FBCCC7`, `0x1FBCED7`
`0x1FBD0E7`, `0x1FBD2F7`, `0x1FBD507`, `0x1FBD717`, `0x1FBD927`, `0x1FBDB37`, `0x1FBDD47`, `0x1FBDF57`, `0x1FBE167`, `0x1FBE377`, `0x1FBE587`, `0x1FDF697`
`0x1FDF8A7`, `0x1FDFAB7`, `0x1FDFCC7`, `0x1FDFED7`, `0x1FE00E7`, `0x1FE02F7`, `0x1FE0507`, `0x1FE0717`, `0x1FE0927`, `0x1FE0B37`, `0x1FE0D47`, `0x1FE0F57`
`0x1FE1167`, `0x1FE1377`, `0x1FE1587`, `0x1FE1797`, `0x1FE19A7`, `0x1FE1BB7`, `0x1FE1DC7`, `0x1FE1FD7`, `0x1FE21E7`, `0x1FE23F7`, `0x201A807`, `0x201AA17`
`0x201AC27`, `0x201AE37`, `0x201B047`, `0x201B257`, `0x201B467`, `0x201B677`, `0x201B887`, `0x201BA97`, `0x201BCA7`, `0x201BEB7`, `0x201C0C7`, `0x201C2D7`
`0x201C4E7`, `0x201C6F7`, `0x201C907`, `0x201CB17`, `0x201CD27`, `0x201CF37`, `0x201D147`, `0x201D357`, `0x2036EE7`, `0x20370F7`, `0x2037307`, `0x2037517`
`0x2037727`, `0x394B0E0`

### `Level_getHitResult` (182)

`0x2A986F`, `0x2B9B58`, `0x8466AF`, `0x850259`, `0x19AD242`, `0x19B0F64`, `0x19F0556`, `0x1A48566`, `0x1A48573`, `0x1A48580`, `0x1E21D0B`, `0x24F5A1F`
`0x24F5A2C`, `0x2D35C1C`, `0x2D35C78`, `0x2D46E31`, `0x3716980`, `0x66867FE`, `0x7EB8679`, `0x7FC7EC5`, `0x7FC7ED2`, `0x7FC7EDF`, `0x7FC7EEC`, `0x7FC7EF9`
`0x7FC7F06`, `0x7FC7F13`, `0x7FC7F20`, `0x7FC7F2D`, `0x7FC7F3A`, `0x7FC7F47`, `0x7FC7F54`, `0x7FC7F61`, `0x7FC7F6E`, `0x7FC7F7B`, `0x7FC7F88`, `0x7FC7F95`
`0x7FC7FA2`, `0x7FC7FAF`, `0x7FC7FBC`, `0x7FC7FC9`, `0x7FC7FD6`, `0x7FC7FE3`, `0x7FC8015`, `0x7FC805F`, `0x7FC806C`, `0x7FC8079`, `0x7FC8086`, `0x8077475`
`0x8077482`, `0x807748F`, `0x807749C`, `0x80774A9`, `0x80774B6`, `0x80774C3`, `0x80774D0`, `0x80774DD`, `0x8077564`, `0x80775AE`, `0x807765A`, `0x8077667`
`0x8077674`, `0x8077681`, `0x807768E`, `0x807769B`, `0x80776E5`, `0x80776F2`, `0x80776FF`, `0x8077749`, `0x8077756`, `0x8077763`, `0x8077770`, `0x80777BA`
`0x80777C7`, `0x80777D4`, `0x80777E1`, `0x80777EE`, `0x8077880`, `0x807788D`, `0x807789A`, `0x80778FC`, `0x8077909`, `0x8077916`, `0x8077990`, `0x807799D`
`0x80779AA`, `0x80779B7`, `0x80779C4`, `0x80779D1`, `0x80779DE`, `0x80779EB`, `0x80779F8`, `0x8077A05`, `0x8077A12`, `0x8077A81`, `0x8077A8E`, `0x8077A9B`
`0x8077AA8`, `0x8077AB5`, `0x8077AC2`, `0x8077ACF`, `0x8077ADC`, `0x8077AE9`, `0x8077AF6`, `0x8077B65`, `0x8077B72`, `0x8077B7F`, `0x8077B8C`, `0x8077B99`
`0x8077BA6`, `0x8077BB3`, `0x8077BC0`, `0x8077BCD`, `0x8077BDA`, `0x8077BE7`, `0x8077C56`, `0x8077C63`, `0x8077C70`, `0x8077C7D`, `0x8077C8A`, `0x8077C97`
`0x8077CA4`, `0x8077CB1`, `0x8077CBE`, `0x8077CCB`, `0x8077CD8`, `0x8077CE5`, `0x8077D2F`, `0x8077D3C`, `0x8077D49`, `0x8077D56`, `0x8077D63`, `0x8077D70`
`0x8077D7D`, `0x8077D8A`, `0x8077DD4`, `0x8077DE1`, `0x8077DEE`, `0x8077DFB`, `0x8077E08`, `0x8077E15`, `0x8077E22`, `0x8077E2F`, `0x8077E79`, `0x8077E86`
`0x8077E93`, `0x8077EA0`, `0x8077EAD`, `0x8077EF7`, `0x8077F04`, `0x8077F11`, `0x8077F1E`, `0x8077F2B`, `0x8077F75`, `0x8077F82`, `0x8077F8F`, `0x8077F9C`
`0x8077FA9`, `0x8077FF3`, `0x8078000`, `0x807800D`, `0x807801A`, `0x807807A`, `0x80780C4`, `0x807810E`, `0x807811B`, `0x8078128`, `0x8078135`, `0x8078142`
`0x807814F`, `0x807815C`, `0x8078169`, `0x8078176`, `0x8078183`, `0x8078190`, `0x807819D`, `0x80781AA`, `0x80781B7`, `0x80781C4`, `0x815D9D1`, `0x82D39C4`
`0x82D39D1`, `0x866E6B4`

### `BlockSource_setBlock` (2)

`0x2A93A7E`, `0x5DC40F8`

### `ClientInstance_getBlockSource` (4)

`0x1E95493`, `0x21BDFD0`, `0x21CCCB0`, `0x645F87C`

### `gDataUtil_MiscData_Instance` (31)

`0x24F288`, `0x13F055A`, `0x19B2383`, `0x256A95D`, `0x31EB5DD`, `0x37287AC`, `0x372892C`, `0x3728AAC`, `0x3728C2C`, `0x3728DAC`, `0x3728F2C`, `0x372F2BD`
`0x3845E58`, `0x3845FB8`, `0x3846118`, `0x3846278`, `0x38463D8`, `0x3846538`, `0x3846698`, `0x3889248`, `0x38893A8`, `0x38899C8`, `0x38E0438`, `0x38E0598`
`0x38E0858`, `0x38E09B8`, `0x38E0B18`, `0x38E8858`, `0x38E99A8`, `0x38E9B08`, `0x3A04709`

### `LevelData_mTick` (2)

`0x5A9151E`, `0x63F1CA7`

## 5. 消费者与共享依赖

| 映射 | 消费者/功能 | 依赖类别 | 当前状态 | 直接解锁能力 |
|---|---|---|---|---|
| `GameSession_mEventCallback` | PacketReceiveHook (network receive callback chain) | 网络接收关键链 | STATIC VERIFIED | PacketReceiveHook 回调链可进入运行时验证 |
| `Actor_mDestroying` | Regen; OreMiner; Nuker; FastMine; Extinguisher; Derp; AutoTool; AutoKick | Player/挖掘共享状态 | STATIC VERIFIED | Regen/OreMiner/Nuker/FastMine 等可验证销毁状态 |
| `ClientInstance_mStackScene` | No current source consumer | UI 对象布局 | STATIC VERIFIED | 当前无源码消费者；不直接解锁模块 |
| `ClientInstance_setDisableInput` | No current external caller | 输入系统 | UNVERIFIED | 无外部调用；继续拒绝 |
| `ClientInstance_grabMouse` | Offset mapping unused; ClickGui/HudEditor use SigManager function instead | 输入系统 | UNVERIFIED | Offset 映射未被实现使用；实际仍依赖 SigManager |
| `ClientInstance_releaseMouse` | Offset mapping unused; ClickGui/HudEditor/UpdateForm use SigManager function instead | 输入系统 | UNVERIFIED | Offset 映射未被实现使用；实际仍依赖 SigManager |
| `Level_getHitResult` | SkinStealer; DestroyProgress; TriggerBot; AutoClicker; MidclickAction; FastMine; ClickTp; AutoTool; BlockUtils | 世界/准星目标共享 | UNVERIFIED | 继续拒绝；相关模块仍不可依赖该接口 |
| `BlockSource_setBlock` | BlockUtils::setBlock; shared local-world block mutation | 世界方块写入 | STATIC VERIFIED | BlockUtils 本地方块写入可进入运行时验证 |
| `ClientInstance_getBlockSource` | BlockUtils; Regen; OreMiner; Nuker; BlockESP; AutoPath; InfiniteAura; Jesus and other world/block consumers | 世界系统核心共享 | STATIC VERIFIED | 解锁大量方块查询、路径、矿工、BlockESP 的共同入口 |
| `gDataUtil_MiscData_Instance` | No current source consumer | 未使用全局 | UNVERIFIED | 当前无源码消费者 |
| `LevelData_mTick` | PacketSendHook::sendPacket (LevelData tick write) | 网络发送 tick 同步 | UNVERIFIED | 继续拒绝；PacketSendHook 的 setTick 仍保持无操作 |

## 6. 扫描与测试结果

- `tools/verify_sigs.py`：修复后 5 条分别为唯一 `OK`：`0x50 / 0x166A / 0x640 / slot 34 / slot 318`；其余 6 条继续 `AMBIGUOUS`。
- `tools/re/scan_sigs.py`：上述 11 条的状态、值和截断后计数与 `verify_sigs.py` 完全一致。
- 完整计数由 `batch2_analyze.py` 给出：`6, 8, 8, 122, 8, 122, 182, 2, 4, 31, 2`。
- `python -m unittest ... test_verify_sigs.py`：37/37 通过。
- 全部 tools 测试：46/47 通过；唯一失败是既有 `test_opengl_gui.py::test_existing_modern_gui_not_a_debug_panel` 对旧文本 `gui->onRenderEvent(event)` 的断言，与本批 OffsetProvider 修改无关，且本轮按范围限制没有修改 GUI。
- `git diff --check`：通过。
- 本机没有 MSVC；未声称编译成功。

输出：
- [`post-fix-verify.json`](post-fix-verify.json)
- [`post-fix-scan.json`](post-fix-scan.json)
- [`ambiguous-candidates.json`](ambiguous-candidates.json)
- [`reference-1.21.44-candidates.json`](reference-1.21.44-candidates.json)

## 7. 尚未解决与停止条件

1. 6 条 UNVERIFIED 保持安全拒绝，没有选第一个命中、没有硬编码旧版数值。
2. `grabMouse/releaseMouse` 的 OffsetProvider 声明不是当前实现的实际调用源；实际 GUI 鼠标函数仍由 `SigManager` 管理，本批未越界修改。
3. `Level_getHitResult` 需要从 Level vtable/真实消费者反向恢复，不可从现有 CALL 串提取。
4. `gDataUtil_MiscData_Instance` 需要先确认全局对象初始化与指针链，再改用正确的全局地址解析类型。
5. `LevelData_mTick` 还依赖未恢复的 `Level_getLevelData`，本批没有把 1.21.44 的 `0x468` 当作目标值。
6. 构建机 Release 和真实运行时 JSON 尚待执行；因此 5 条均仍是 STATIC VERIFIED，不是 RUNTIME VERIFIED。

## 8. 构建机与运行时验证命令

```bat
cd /d C:\work\hacker-git
git pull --ff-only
git rev-parse --short HEAD
cd /d C:\work\hacker-git\solstice\solstice-src\solstice-master-main\solstice-master-main
cmake --build out/build/x64-Release
```

运行时保持默认拒绝：不要设置 `SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS`，或明确：

```bat
set SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS=OFF
```

进入游戏后回传新的 `solstice.log` 与 `sigreport.json`。验收期望：5 条新签名 `matches=1` 且值分别为 `80, 5738, 1600, 34, 318`；其余 6 条继续拒绝。
