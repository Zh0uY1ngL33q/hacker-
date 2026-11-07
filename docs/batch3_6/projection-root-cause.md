# ESP 投影根因 — 调用链、历史与证据

> 基线 `411fa4e` → 工作分支 `feat/esp-projection-root-cause`。
> 目标镜像 `unpacked/unpacked_Minecraft.Windows.exe`，SHA-256 `5fb2e893…026deb`（已核对一致）。
> 本机无 MSVC，未编译、未运行。所有结论标注证据等级。

---

## 1. 先给结论

**没有取得足以断定"哪一步算错"的静态证据。** 取得的是三件事：

1. 把投影链的每个输入都追到了具体代码与具体内存位置，并证明其中**两个输入的来源无法从源码或镜像自证**。
2. 用一个独立参考实现证明了**当前代码的契约要求**，以及当某个输入违反契约时会产生多大的像素误差（可量化，不是"感觉偏"）。
3. 把缺失的两个数值做成了**一次性诊断**（已提交），使下一次构建运行就能定性。

因此本轮的最终状态是 `NEEDS_RUNTIME_MATRIX_CAPTURE`，不是"改了一个常数让它看起来对"。

---

## 2. 真实调用链（逐节点）

| # | 节点 | 文件 : 行 | 输入 → 输出 | 单位 / 空间 | 取值时机 | 可为空 |
|---|---|---|---|---|---|---|
| 1 | `ESP::onRenderEvent` | `ESP.cpp:26` | `RenderEvent&` → 绘制 | — | 每帧，GL swap 内 | 否 |
| 2 | 实体枚举 | `ActorUtils::getActorList` | `(bool,bool)` → `vector<Actor*>` | — | 每 tick 缓存 | 是（返回空） |
| 3 | AABB | `Actor::getAABB(false)` `Actor.cpp:173` | `Actor*` → `AABB` | **世界坐标，方块** | 每帧 | 是（组件缺失） |
| 4 | 相机位置 | `LevelRendererPlayer::getCameraPos()` `LevelRenderer.cpp:10` | `this` → `glm::vec3*` | **世界坐标，方块** | 每帧 | 是 |
| 5 | 视图矩阵 | `ClientInstance::getViewMatrix()` `ClientInstance.cpp:45` | `this` → `glmatrixf`（64 B） | **未证** | 每帧 | 否（值可能无效） |
| 6 | FOV | `ClientInstance::getFov()` `ClientInstance.cpp:50` | 两个 float → `vec2` | **未证（乘数？角度？弧度？）** | 每帧 | 回退 70° |
| 7 | 投影 | `glmatrixf::OWorldToScreen` `Structs.hpp:47` | `(origin,pos,fov,display)` → `vec2` | 像素 | 每帧 | 返回 false |
| 8 | 绘制 | `ImGui::GetBackgroundDrawList()` | `ImVec2[]` | **ImGui 逻辑像素** | 每帧 | — |
| 9 | 呈现 | `ImGui_ImplOpenGL3_RenderDrawData` | — | 帧缓冲 | 每帧 | — |

两处单位可疑：

- **节点 3 与 4 都是"方块"**，同一空间，这一步没有可疑之处（已验证 `getAABB` 用 `renderPositionComponent->mPosition` 重建，不是组件原始 min/max）。
- **节点 8 用 ImGui 逻辑像素，节点 7 的分母是 `MathUtils::displaySize`**，两者同源（`OpenGLGui.cpp:400` 赋值为 `io.DisplaySize`，来自 `GetClientRect`），所以 **viewport 与 ImGui 坐标是一致的**——这一条排除了。

---

## 3. 历史硬修表

| 提交 | 改了什么 | 目的 | 有数学依据吗 |
|---|---|---|---|
| `f50d0f0` / `244e5de` | RenderEvent 从 D3D present 搬到 GL swap 路径 | 让 40 个订阅者重新被调用 | 有（D3D present 在 GL 下被短路） |
| `6cfe64c` / `ef5f1b9` | 在 GL 路径重建 `RenderUtils::transform` 与 `MathUtils::fov/origin` | 这两个全局量原本只由 D3D 写 | 有（赋值来源与 D3D 相同） |
| `3d423d2` / `a845248` | 矩阵缺 Z 轴时用 yaw/pitch 合成；修正左右手与镜像 | 让投影至少在数学上成立 | **有**，但见下 |
| `6a95fe0` / `00f8166` | 改为扫描对象内存找"像相机基"的 4×4，偏移指到 `0x358` | 找不到可靠来源时的兜底 | **没有**：按结构相似性挑选，非语义识别 |
| `f8c3541` | FOV 从 `0x728/0x73C` 改到 `0x720/0x724`，并加双解释 `toFactor()` | 旧偏移读到 `-FLT_MAX`，每个点都被拒 | **部分**：偏移由 vtable 三元组佐证，单位靠"两种都接受" |

关键观察：`3d423d2` 与 `a845248` 精修的是 `viewMatrixFromRotation()`——**这个函数只在"游戏矩阵没通过校验"时才会执行**。而校验函数 `isCameraBasis` 只检查 3×3 的正交归一，几乎任何旋转矩阵都能通过。也就是说：

> 那两次对合成矩阵的精修，在游戏矩阵存在时是**死代码**；真正参与投影的是未经任何归一化处理的游戏矩阵。

---

## 4. 三条镜像级证据

### 4.1 ClientInstance vtable 里没有 `0x358` 的访问器

扫描 CI vtable 全部 470 个槽位，反汇编每个槽位的前几条指令，检索是否提到某个偏移。方法先用已知成员自证：

| 偏移 | 命中的槽位 | 已知含义 |
|---|---|---|
| `0xF0` | 1, 8, 27, 44, **197**, 198 | `getLevelRenderer` = 197 ✓ |
| `0x108` | **308** | `getPacketSender` ✓ |
| `0x118` | **318** | `getBlockSource` ✓ |
| `0xE0` | 17, 27, 64, 177–179 | `mMinecraftSim` |
| `0x6C0` | 232, 233 | `GuiData` |
| `0x720` | **455, 456** | FOV get / set ✓ |
| `0x724` | **457, 458** | FOV get / set ✓ |
| **`0x358`** | **无** | — |
| **`0x368`** | **无** | — |

方法在七个已知成员上全部命中，在 `0x358`/`0x368` 上全空。这不证明它们不是矩阵（矩阵可以不经接口暴露），但说明**这两个偏移不是从游戏接口识别出来的，而是扫描撞上的**。

### 4.2 除了 vtable 的 get/set 对，没有渲染代码读 `0x720`

`movss` 精确字节扫描 `0x720` 得到 7 处：`0x1A2A330`（getter）、`0x1A614E0`（setter），其余 5 处在 `0x352B3A7 / 0x352C8B3 / 0x352DE07 / 0x352F347 / 0x5EB82F6`。

反汇编 `0x352B340` 一带：

```text
0x352B347  movss xmm1, [rax+4]      ; 0x352B34C  movss [rbp+0x704], xmm1
0x352B395  movss xmm1, [rax+0x1c]   ; 0x352B39A  movss [rbp+0x71c], xmm1
0x352B3A2  movss xmm0, [rax+0x20]   ; 0x352B3A7  movss [rbp+0x720], xmm0
0x352B3AF  movss xmm1, [rax+0x24]   ; 0x352B3B4  movss [rbp+0x724], xmm1
```

`[rbp+0x700]` 起连续 16 个 `movss` —— 这是**一次 64 字节结构体整体拷贝**，`0x720` 只是恰好落在被复制区间里。所以：**镜像中没有任何渲染代码读取 `ClientInstance+0x720`**。

推论：`0x720/0x724` 是 ClientInstance 暴露的一个属性（有 get/set），但**不是渲染器每帧用来构造投影的那个值**。SDK 用它做投影，等于用一个"副本"去对齐"另一个值"——两者在默认设置下可能相等，在改 FOV、疾跑、望远镜时会分叉。

### 4.3 `LevelRendererPlayer::getFovX/getFovY` 是死代码

`OffsetProvider` 里有这两条映射，`LevelRenderer.cpp:15/20` 也实现了，但**全树没有任何调用者**。也就是说 SDK 曾经有过第二个 FOV 来源，但投影没用它。

---

## 5. 一处更正（我自己的错误）

第一版扫描器在做 `movss`/`lea` 匹配时索引差了 1 个字节，导致 `+0x358` 返回"0 处取址 / 0 处 16 字节读写"。我据此差点写下"`0x358` 不是矩阵"的结论。用已知的 `0x720` 站点自证后发现问题——修正索引后 `0x358` 实际有 274 处 `lea`、59 处 16 字节读写。

**"零命中"是扫描器 bug，不是证据。** 已作废该判断，本报告不再引用。

---

## 6. 目前能确定性说出的与不能说的

**能确定：**

- `OWorldToScreen` 的契约要求 `rows[0..2].w == 0`（它自己减了相机原点）。
- 三条矩阵路径里，路径 1（扫描）与路径 2（合成）显式把 w 置 0；路径 0（游戏矩阵）没有。
- `isCameraBasis` 只校验 3×3，不校验 w，所以路径 0 会无条件胜出。
- 当 `getActorRotationComponent()` 返回 null 时，`viewSource` 停在 3，**整段兜底逻辑都不执行**，未验证的矩阵直接进入投影。
- 4.1 / 4.2 / 4.3 三条镜像证据。

**不能确定：**

- `ClientInstance+0x358` 处到底是不是视图矩阵（没有访问器，也没找到唯一的生产者）。
- `0x720/0x724` 存的是乘数、角度还是弧度。
- 因此无法断定"错位"的主因是 w 泄漏、FOV 单位，还是矩阵本身就不是视图矩阵。

这三条正是下一轮必须用一次运行日志回答的问题。
