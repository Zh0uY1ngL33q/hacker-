# 未解决与停止条件

> 本轮结束后的状态。每条给出"为什么停下来"和"下一步靠什么回答"，不写成待办清单。

---

## U-1 `ClientInstance+0x358` 是不是视图矩阵 —— 未证

**已知**：CI vtable 470 个槽位里没有任何一个访问它；SDK 里其他每一个被依赖的成员（`0xF0`/`0x108`/`0x118`/`0xE0`/`0x6C0`/`0x720`/`0x724`）都有访问器。该偏移是扫描撞上的，不是识别出来的。

**为什么停**：要证明"它是/不是矩阵"，需要看它被谁写、被谁消费。全 `.text` 中 `+0x358` 有 274 处 `lea`、59 处 16 字节读写，但绝大多数属于别的对象（该偏移太常见）；缩小到 ClientInstance 需要运行时对象地址。

**下一步**：`matrixSource` 日志会直接说明当前用的是哪条路径。若长期是 0，且 `affine verdict=AFFINE_TRANSLATION_TERM_PRESENT`（或 `DOUBLE_ORIGIN_SUBTRACTION_CONFIRMED`），则问题被定位到"游戏矩阵不是可用的视图矩阵"。注意 3.6.1a 起 `rowW≠0` 只等同于"有平移项"，不再直接等于"原点被减两次"。

---

## U-2 `0x720/0x724` 的单位 —— 未证

**已知**：vtable 有 get/set 对（455/456、457/458）；除这对之外，镜像里没有渲染代码读它（其余命中是一次 64 字节结构体拷贝）。

**为什么停**：单位只能从数值判断（乘数 ≈ 0.6~1.5；角度 ≈ 60~110；弧度 ≈ 1.0~1.9）。而"乘数"与"弧度"区间重叠，仅凭区间无法区分——必须知道 FOV 设置值是多少，才能判断 1.22 是 70° 的弧度还是 1.22 倍的乘数。

**下一步**：见 U-9。单次采样的 4 个 fov 字段（`ciFovRaw` / `ciFovConverted` / `transformFov` / `mathFov`）只能确认内部一致性，不能定单位。

**判断投影比例时看哪一个**：生产路径吃的是 `mathFov`，所以先看 `mathFovFactorRatio`；`fovFactorRatio` 是 transform 副本，只作对照。`mathFovFactorRatio ≠ displayAspect` 说明"fov 的 aspect 与当前显示不一致"，**不等于**"aspect 已丢失"——后者需要知道 `ciFovRaw` 里到底存了什么，那是 U-9 的问题。

---

## U-3 `LevelRendererPlayer::getCameraPos()` 的坐标空间 —— 未证

**已知**：它返回 `&member_at<glm::vec3>`。AABB 是绝对世界坐标。

**为什么停**：Bedrock 有 render origin（渲染相对原点）机制。若相机位置是 render-relative 而 AABB 是 absolute，相对向量会大出几百万格。这个错误会表现为"所有框都飞出去"，与当前的"错位"不同，因此可能性较低——但没有排除。

**下一步**：日志里已有 `playerRender`、`camera`、`playerToCamera`。若 `playerToCamera` 差出成千上万，就是坐标空间不一致；正常应在眼高量级（≈1.6）。

---

## U-4 Framebuffer 尺寸与 DPI 缩放 —— 未记录

**已知**：`MathUtils::displaySize` 来自 `GetClientRect`，与 ImGui 的 `io.DisplaySize` 同源，两者一致。

**为什么停**：`io.DisplayFramebufferScale` 与游戏自己的 UI scale 没有记录。若客户端在 DPI 缩放下把 3D 渲染到更大的帧缓冲，而 FOV 是按帧缓冲 aspect 算的，就会有比例误差。

**下一步**：在探针里补两个字段（`GetClientRect` 与 `io.DisplayFramebufferScale`）。本轮没加，是因为它会扩大改动面，而 U-1/U-2 更可能是主因。

---

## U-5 `PartialTicks` / 插值 —— 未记录

实体位置来自 `RenderPositionComponent::mPosition`，它本身可能就是插值后的值；但 SDK 没有映射 `PartialTicks`，无法确认相机与实体是否用了同一个插值时刻。若不同步，快速移动时框会滞后。

**为什么停**：需要新映射，而本轮明确不新增映射修复。

---

## U-6 诊断运行的批准条件（未实施）

默认构建（`SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS=OFF`）在 `Solstice.cpp:322` 退出，`gFeatureManager` 在 366、`HookManager::init` 在 370 —— **ESP 探针一次都不会执行**。

要看那两行日志，必须有一次能跑到 RenderEvent 的运行。两种做法：

| 做法 | 内容 | 前提 |
|---|---|---|
| A 局部放行 | 编译时 `-DSOLSTICE_ALLOW_UNVERIFIED_MAPPINGS=ON`，但只启用 ESP，其余模块全关 | 需要说明 ESP 实际用到哪些映射、哪些已有证据；需要接受"整机放行"这个事实 |
| B 隔离诊断 | 在 `Solstice.cpp` 的拒绝分支后加一条只读诊断路径：初始化 FeatureManager 前先读 `[CI+0x358]`、`[CI+0x720/0x724]` 并打印，不装载 Hook | 需要改 `Solstice.cpp`，而任务书禁止改全局拒绝机制 |

ESP 的映射依赖只有 `ClientInstance_getLocalPlayer`（OK，91 个消费者），**不需要任何 non-OK 映射**。所以方案 A 在技术上可行，但它会同时放出 41 条未验证映射，崩溃将无法归因。

**本轮的判断**：两个方案都需要人工决策，且都超出"最小诊断"的边界。已把探针写好，等批准。

---

## U-7 参考实现的覆盖边界

`tools/projection_reference.py` 覆盖 **49** 项：原 16 项（identity / 平移 / 左右 / 上下 / 相机后方 / aspect / w 泄漏 / 角度当乘数 / yaw 90 / 相机原点）+ 3.6.1a 的 8 项（`T11a–d` 生产参数 vs transform 快照；`T12a–d` 仿射平移分类）+ 3.6.1b 的 11 项（`T13a–c` `screenDelta` 语义与视轴反例；`T14a–c` 生产原点 vs 滞后原点的残差；`T15a–e` 测试点有效性与选择器）+ 3.6.1c 的 14 项（`T16a–c` 跨帧/同帧配对不得误拼、paired 块自足；`T17a–c` 八角完整性；`T18a–b` 选择器偏好高灵敏度；`T19a–c` LOW_SENSITIVITY 两个来源；`T20a–c` 搜索间隔与预算计数）。它**不覆盖**：

- 游戏的矩阵布局（transpose、3×4 + padding 行）
- 投影矩阵（DLL 不构造，游戏不暴露）
- 插值与部分 tick

它验证的是"契约成立时公式正确、契约被违反时误差多大"，不能替代运行时数据。

`T11`/`T12` 验证的是**判据本身**：参数相同时像素差恰好为 0（对照非空），origin 差 0.25 格差 19.28 px、fov 漂 5% 差 1.93 px（对照敏感），`w = -dot(axis, origin)` 被判为 `DOUBLE_ORIGIN_SUBTRACTION_CONFIRMED` 且实际位移 333.7 px，来源不明的 `w` 只被判为 `AFFINE_TRANSLATION_TERM_PRESENT`。

`T13` 是**反例组**：视轴上的点在 fov 差 30% 时像素差仍为 0，而同一对参数在偏轴点差 11.57 px——所以 `screenDelta=0` 不能当作"两套参数相同"的证据。`T14` 证明原点选错会得出相反结论。`T15` 证明上一轮那个测试点（本地玩家渲染位置，落在相机平面上）会被 `z >= 0` 直接拒绝。

`T16` 是**时间一致性**：把矩阵块和测试点块的数据分别带上帧号，跨帧拼接（`matrixCaptureFrame=1` + `controlCaptureFrame=100`）被判为不可配对，`sameFrame=false` 时不允许把 preliminary 的数字用于 paired 判读；同帧（两块都在帧 N）则允许。paired 块被断言为"自足"——它重读 `matrixSource` / origin / fov / mat16 / rowW / affine，不引用 preliminary 的变量。`T17` 证明四角扫描会漏掉"只有另外四角可用"的箱子（八角才完整）。`T18` 证明选偏轴候选（S 最大）而非第一个合格点。`T19` 区分 LOW_SENSITIVITY 的两个来源（分数 < 0.15，或来自本地玩家 fallback）。`T20` 证明搜索节流：首搜在第 1 帧、之后间隔 ≥ 20 帧、预算按**搜索次数**计（900 次而非 900 帧）、预算用尽后降到 600 帧节奏。

---

## U-8 过程中被推翻的两个自己的判断

记录在此，避免下一轮重复：

1. **"CI+0x358 零取址"** —— 扫描器索引差 1 字节导致的假阴性。修正后是 274 处 `lea`。**该结论作废。**
2. **"0x720 有 4 处在渲染区被读取"** —— 反汇编后发现那 4 处是一次 64 字节结构体拷贝，不是 FOV 计算。**该判断作废**，改为"除 vtable 对之外无渲染代码读取"。

两次都是同一个教训：**扫描器必须先拿已知样本自证**，否则"零命中"和"有命中"都可能是工具 bug。

3.6.1a 追加一条同类：**"探针读的就是生产参数"** —— 不成立。`getBoxPoints()` 读的是 `MathUtils` 静态量，探针读的是 transform 副本。**该假设作废**，改为两套各算一遍并输出差值（U-10）。

3.6.1b 追加两条：

4. **"`screenDelta=0` 说明两套参数相同"** —— 不成立。视轴上的点在 fov 差 30% 时像素差仍为 0（`T13a`）。**该判据作废**，相等性改由 `originsMatch` / `fovsMatch` 判定，`screenDelta` 只作灵敏度量化。
5. **"仿射残差可以用 transformOrigin 算"** —— 不成立。生产路径减的是 `mathOrigin`，用错原点会把 `DOUBLE_ORIGIN_SUBTRACTION_CONFIRMED` 判成 `AFFINE_TRANSLATION_TERM_PRESENT`（`T14a–c`）。**该写法作废**，主判定改用 `mathOrigin`，transform 版本降级为对照。

3.6.1c 追加一条：

6. **"矩阵块和测试点块天然来自同一帧"** —— 不成立。两者原先各自取数，跨帧拼接会在日志里静默出现（矩阵第 1 帧、测试点第 100 帧），拼出的投影渲染器从没做过。3.6.1c 改为单调帧计数 + paired 块结构性重读全部字段，从机制上杜绝；但 `sameFrame` 是否在特定运行里曾经为 `true`、preliminary 与 paired 是否双双出现，仍只能由真实运行确认。**该假设作废**，改为"同帧是结构性保证、正确性仍需运行时确认"。

---

## U-9 FOV 单位需要**两次独立运行** —— 未取得

**为什么停**：乘数（≈0.6–1.5）、角度（≈60–110）、弧度（≈1.0–1.9）三个区间在边缘上重叠，单次采样的数值无法判定。例如 1.22 既可能是 70° 的弧度，也可能是 1.22 倍的乘数。

**需要的数据**：两个**不同 FOV 设置**的独立运行（或同一次运行里改 FOV 前后各一次采集），比较 `ciFovRaw` 与 `ciFovConverted` 的变化方式：

| 观察 | 结论 |
|---|---|
| `ciFovRaw` 随设置非线性变化、`ciFovConverted` 近似不变 | `0x720` 已存乘数，`getFov()` 的转换是多余的 |
| `ciFovRaw` 线性变化且数值在 30–130 | 存的是角度 |
| `ciFovRaw` 线性变化且数值在 0.5–2.5 | 存的是弧度 |
| 两者都不动 | `0x720` 不是当前 FOV 的来源（与"无渲染代码读它"一致） |

**一次采样只能确认内部一致性**（`CI_COMPONENT=v2` / `FIELD_PRESENT` 之类的判断不在此列），不能定单位。本轮不根据单次采样下结论。

---

## U-10 生产参数与 transform 快照是否真的一致 —— 现在可测，尚未测

**已知**：`getBoxPoints()` 用 `MathUtils::origin` / `MathUtils::fov`，矩阵用 `RenderUtils::transform.mMatrix`。`OpenGLGui::render()` 在同一块里同时赋值两边，所以**按代码推断**它们相等。

**为什么停**：这是推断，不是观测。静态检查无法证明运行时两者始终相等（D3D 路径只写 transform，不写静态量；两个静态量的最后一次写入者不一定都是 GL 路径）。

**下一步**：`control` 一行的 `originsMatch` / `fovsMatch`（这两个才是相等性证据）。若 `originsMatch=false`，则此前所有基于 transform 快照的投影结论都要重算——这是加这段对照的唯一目的。

`screenDelta` 只在两者都 match 的前提下用来"量化这个测试点有多敏感"，**不能**反过来当相等性证据（3.6.1b 修正，见 U-8 第 4 条）。

---

## U-11 `matrixSource=2` 只说明合成矩阵被采用，不说明它正确 —— 未证

**为什么写这条**：3.6.1a 的判读表里有一行写着"`matrixSource=2` → 合成路径在跑，矩阵没问题"，这是**错的**。source 2 只表示 `viewMatrixFromRotation(yaw, pitch)` 被采纳了（因为游戏矩阵没通过 `isCameraBasis`），它本身没有被任何东西验证过。

**仍需单独验证的三件事**：

1. yaw/pitch 的符号与零点约定（Minecraft：yaw 0 朝 +Z、顺时针增大；pitch 正值向下）；
2. 相机朝向是否真的是玩家朝向（第三人称、自由视角、相机实体都会让两者分叉）；
3. 坐标系手性以及 `OWorldToScreen` 把 `m[0..2]` 当行的约定，与该矩阵被构造为"列"的方式是否互相抵消。

**下一步**：source 2 时，用 `basis` 三行与 `mat16` 手工重建一次投影，和 `mathScreen` 对照；或按 `viewMatrixFromRotation` 的定义离线重算后比。不能因为"合成路径在跑"就排除矩阵问题。

---

## U-12 投影对照依赖场景里存在另一个实体 —— 未能自足

**已知**：段 2 要求一个**两套参数都看得见（`viewZ < -0.1`）**的点。取非本地 actor 的 AABB 角点；没有非本地 actor 时退到本地玩家自己的 AABB 角点，并标 `local_actor_aabb_corner_low_sensitivity`。

**为什么停**：本地玩家角点虽然在相机下方、能投影，但它几乎落在视轴上，对参数差异不敏感（`T13a` 的反例就是这个几何）。所以空场景下 `control` 要么低灵敏度，要么直接失败。

**下一步**：空场景下不要用 `local_actor_aabb_corner_low_sensitivity` 的 `screenDelta` 下任何结论；等到有实体在视野内再采一次。失败时日志会给出 `no usable test point: N visible actors, M examined...`，据 N/M 判断是"场景空"还是"AABB 组件缺失"。

---

## U-13 同帧配对已结构性保证，但正确性仍需真实运行确认 —— 部分解决

**已知**：3.6.1c 之前，矩阵块（`matrixCaptured`）与测试点块（`controlCaptured`）可以在不同帧取数，`sameFrame` 从未被记录，跨帧拼接在日志里静默发生（见 U-8 第 6 条）。

**本轮做了什么**：引入单调 `sFrame` 计数器；输出标题写 `matrixCaptureFrame` / `controlCaptureFrame` / `sameFrame`；paired 块重读 `matrixSource` / 原点 / fov / mat16 / rowW / affine，不复用 preliminary 的任何局部变量。因此"第 1 帧矩阵 + 第 100 帧测试点"在结构上不可能出现在同一条 paired 判读里。`T16a–c` 离线验证了这一保证。

**为什么仍停在"部分解决"**：配对是否真的在同一帧发生、preliminary 与 paired 是否双双出现，只能由真实运行确认——参考测试只能证明"机制正确"，不能证明"运行时确实同帧"。运行日志里若 `sameFrame=false` 长期出现，说明场景里长期没有可用测试点（段 2 滞后于段 1），此时只允许用 preliminary 的矩阵数据，禁止与任何测试点结论拼接。

---

## 停止条件

- 未修改任何投影数学。
- 未修改 `OffsetProvider` / `InstanceLocator` / `BlockSource` / Aura / 其他视觉模块。
- 未修改 `SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS`。
- 未推送（等人工审查）。
- `main` 未动。
