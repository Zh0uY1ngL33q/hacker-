# Batch 3.6 最终决策

> 分支 `feat/esp-projection-root-cause`（基于 `411fa4e`）。`main` 未动，未推送。
> 提交链（同一议题，逐轮收紧；**投影数学始终零改动**）：
>
> ```text
> 54f9188  diag(esp): record the projection's provenance, row translation and raw fov once
> a7158d4  diag(esp): make the projection probe capture a complete frame, once
> 11ff965  diag(esp): project the point with the renderer's own parameters too
> 3.6.1c   探针时间一致性与测试点质量修订（本轮，工作区，未提交）
> ```
>
> 镜像 SHA-256 已核对与任务书一致。

---

## 结论

```text
NEEDS_RUNTIME_MATRIX_CAPTURE
```

不是 `ROOT_CAUSE_CONFIRMED_*`。理由：静态证据能把问题压缩到**两个具体数值**上，但无法判定这两个数值是多少；而在不知道它们的情况下改公式，就是任务书禁止的"再让 ESP 换一种方式错位"。

本轮的实际产出是一个**一次性诊断探针**（机制已提交于 `54f9188`/`a7158d4`/`11ff965`，3.6.1c 修订在工作区、未提交），加上一个独立参考实现（**49** 项断言全过：原 16 项 + 3.6.1a 8 项 + 3.6.1b 11 项 + 3.6.1c 14 项）用来判断答案意味着什么。

---

## 十问

### 1. ESP 错位的根因是什么？

**未定论。** 静态证据把范围压到三条，且都不是"公式写错"：

| 候选 | 证据强度 | 能否解释"改一点换个方向偏" |
|---|---|---|
| **A. 矩阵行带平移（w≠0），而投影已自行减去相机原点** | 契约可证；代码在三条路径上不一致（路径 0/3 未归零） | **能** —— 偏移量正比于世界坐标，改原点/改矩阵都会改变偏移方向与大小 |
| **B. FOV 单位错（把角度/弧度当乘数）** | 镜像可证：除 vtable get/set 外无渲染代码读 `0x720` | **能** —— 横向缩放错，靠近中心尚可、越靠边越偏，改 FOV 常数只会改变程度 |
| **C. `0x358` 根本不是视图矩阵** | vtable 470 槽无访问器，而其他被依赖成员都有 | **能** —— 任何补偿都不会稳定 |

A 与 B 可以在同一次运行里同时被读取；C 由 `affine verdict` 与矩阵形态间接判断。3.6.1a 起还要先过一道 **`control`**：`originsMatch` / `fovsMatch` 为假时，说明探针读的参数不是渲染器用的那一套，A/B/C 的判断都得先重算。3.6.1b 起再加一条：`control` 必须建立在**真正在相机前方**的测试点上，且 `screenDelta` 只是灵敏度、不是相等性证明。

### 2. 矩阵实际从哪里取得？

`ClientInstance::getViewMatrix()` → `member_at<glmatrixf>(this, 0x358)`。

但 `OpenGLGui::render` 有三级候选，实际用哪个**运行时才知道**：0 = 游戏矩阵、1 = 扫描定位、2 = yaw/pitch 合成、3 = 全部失败。本轮把这个选择记录进 `FrameTransform::mSource` 并打进日志。

### 3. 当前重建矩阵与游戏真实矩阵有什么区别？

- 路径 1、2 是**纯旋转**（`.w` 显式为 0）；
- 路径 0 是游戏矩阵原样，`isCameraBasis` 只校验 3×3，**不看平移**；
- 路径 3 什么都不校验。

三者只有路径 0 可能与"真实视图矩阵"不同——如果游戏矩阵带平移，路径 0 就多算了一次平移。

### 4. 是否存在矩阵转置或乘法顺序错误？

**没有发现。**

- `OWorldToScreen` 用 `dot((*this)[i], vec4(pos,1))`，`operator[]` 取的是列；`viewMatrixFromRotation` 用 `m[i] = vec4(axis,0)` 写入的也是列——两者一致。
- 参考实现按同一约定推导（`rows[0]=right, rows[1]=up, rows[2]=-forward`），在 yaw 0 与 yaw 90 下左右上下都对（T3/T4/T9）。
- 合成基是右手系且与 `m[2] = -forward` 自洽（T1/T9）。

真正的问题不是转置，是**对齐方式**：用一个已经把点变成相机相对的公式，去点乘一个可能自带平移的矩阵。

### 5. Camera Origin 是否正确？

**减了一次，位置正确；但坐标空间未证。**

- 代码只在 `OWorldToScreen` 里减一次，没有重复减（`getBoxPoints` 把 `MathUtils::origin` 传进去，`transform.mOrigin` 与它是同一个值）。
- 未证的是 `LevelRendererPlayer::getCameraPos()` 是否与 AABB 处于同一空间（绝对世界坐标 vs render origin）。
- 注意：如果矩阵本身带平移，"减 origin" + "矩阵里的平移"合起来**等效于减了两次**——这正是候选 A。

### 6. FOV 的单位和方向是否正确？

**方向对，单位未证。**

- 方向：`fov.x` 用于水平、`fov.y` 用于垂直，与 `sx = W/2 + W/2·x/(-z)·fov.x` 一致。若 `0x720` 是垂直 FOV 而 `0x724` 是其它，`getFov()` 会原样返回两个值（只要都落在 (0.1,10)），**不做任何 aspect 校正**。
- 单位：`toFactor()` 把 (0.1,10) 当乘数、(30,130) 当角度。**弧度落在这个乘数区间里**（70°=1.2217 rad），无法仅凭区间区分。
- 更强的证据：镜像里除 vtable get/set 外**没有渲染代码读 `0x720`**，所以它很可能不是渲染器用的那个 FOV。

### 7. Viewport 与 ImGui 坐标是否一致？

**一致。** 两者同源：`GetClientRect` → `io.DisplaySize` → `MathUtils::displaySize`，绘制走同一个 ImGui 帧的 `GetBackgroundDrawList`。这一条可以排除。

唯一没记录的是 `io.DisplayFramebufferScale`（DPI 缩放），列在 `unresolved.md` U-4。

### 8. 最小修复修改了什么？

**没有修改投影数学。** 只加了诊断：

1. `FrameTransform::mSource` —— 记录矩阵来自哪条路径；
2. `ClientInstance::getFovRaw()` —— 暴露未经解释的两个 float；
3. ESP 探针：3.6.1 修生命周期与字段语义，3.6.1a 加**生产参数对照**（`math view` / `transform view` / `control`）与**仿射分类**（`affine verdict`）。

同时准备好但**未应用**的一处改法（`view[i].w = 0`）写在 `minimal-fix-summary.md` 末尾：在 `rowW=(0,0,0)` 时它是恒等变换，不会破坏任何当前正确的情形。触发条件在 3.6.1a 之后从"`rowW≠0`"收紧为"`affine verdict=DOUBLE_ORIGIN_SUBTRACTION_CONFIRMED`"——非零 `w` 本身只说明存在平移项，不说明它是谁的平移。

### 9. 是否能同时改善 Tracers、Nametags 等模块？

**能，如果最终改在共享层。**

它们与 ESP 共用同一条链：`RenderUtils::transform.mMatrix` + `MathUtils::origin` + `MathUtils::fov` + `MathUtils::displaySize`，且都经 `OWorldToScreen`。任何在 `OpenGLGui::render`（矩阵安装点）或 `ClientInstance::getFov()` 上的修正会自动覆盖：Tracers、Nametags、ChinaHat、ItemESP、JumpCircles、BoneEsp、Aura 的球体投影。

**这也是本轮坚持不在 `ESP.cpp` 里加补偿的原因**：那样只能修一个模块。

### 10. 构建机下一步需要执行什么？

```powershell
git checkout feat/esp-projection-root-cause
cmake -S . -B out/build/x64-Release -DSOLSTICE_ALLOW_UNVERIFIED_MAPPINGS=OFF
cmake --build out/build/x64-Release --config Release --parallel
```

**这只验证编译。** 默认拒绝构建在 `Solstice.cpp:322` 退出，探针不会执行，不能用它证明投影。

要拿到数据需要一次受控运行（需人工批准，方案见 `unresolved.md` U-6）。运到能画 ESP 的帧之后，回传这两块（其余同批输出一起带上）：

```text
[ESP probe] ===== preliminary snapshot matrixCaptureFrame=N (matrix only, NOT paired with a test point) =====
[ESP probe] ===== paired sample matrixCaptureFrame=N controlCaptureFrame=M sameFrame=? searches=? =====
[ESP probe] testPoint source=? actor=0x? world=(?,?,?) sensitivityScore=? sensitivity=? testViewX=? testViewY=? testViewZ=? transformTestViewZ=? mathOk=? transformOk=?
[ESP probe] fov ciFovRaw=(?,?) ciFovConverted=(?,?) transformFov=(?,?) mathFov=(?,?)
[ESP probe] display displayWidth=? displayHeight=? displayAspect=? fovFactorRatio=? mathFovFactorRatio=?
[ESP probe] control comparable=? originsMatch=? fovsMatch=? screenDelta=(?,?) viewDelta=(?,?,?) screenDeltaUsable=?
[ESP probe] affine verdict=? (production origin = mathOrigin) rowW=(?,?,?) axisDotMathOrigin=(?,?,?) residualMath=(?,?,?)
[ESP probe] affine control originsAgree=? axisDotTransformOrigin=(?,?,?) residualTransform=(?,?,?) verdictIfTransformOrigin=?
[ESP probe] basis v0(?,?,?) v0w=? v1(?,?,?) v1w=? v2(?,?,?) v2w=?
```

**判定规则（先写好，避免事后解释）：**

| 观测 | 结论 | 动作 |
|---|---|---|
| 只有 `preliminary snapshot`，没有 `paired sample` | 没找到有效测试点 | 看 `no usable test point` 的计数；**不要**把 preliminary 的数据当投影结论 |
| 两块的 `frame` 不同 | 正常，两块各自完整 | 不要跨块拼接；以 paired 为准 |
| `comparable=false` | 至少一套参数拒绝了该点 | 先看 `testViewZ` / `transformTestViewZ`，其余不解读 |
| `originsMatch=false` 或 `fovsMatch=false` | 两套参数不同 | **先修对照**，A/B/C 结论作废重来 |
| `sensitivity=LOW_SENSITIVITY` 或 `screenDeltaUsable=false` | 这个点不敏感 | `screenDelta` 不能当灵敏度证据；换有实体在视野内的场景重采 |
| `screenDelta` 很小甚至是 0 | **不代表参数相同**，只说明这个测试点不敏感（T13 反例：视轴上 fov 差 30% 仍是 0 px） | 以 `originsMatch` / `fovsMatch` 为准 |
| `originsAgree=false` | 该帧两套 origin 不同 | 以主判定（`mathOrigin`）为准，`verdictIfTransformOrigin` 只作对照 |
| `matrixSource=0` 且 `verdict=DOUBLE_ORIGIN_SUBTRACTION_CONFIRMED` | 候选 A 成立（矩阵减的正是调用方又减一次的那个原点） | 应用 `.w` 归零补丁 |
| `matrixSource=0` 且 `verdict=AFFINE_TRANSLATION_TERM_PRESENT` | 有平移项，来源未知 | 不归零；用 `axisDotMathOrigin` 与 `camera` 对齐，先定来源 |
| `matrixSource=0` 且 `verdict=NO_TRANSLATION_TERM` | A 排除，转 B | 看 `ciFovRaw` 与 `mathFovFactorRatio` |
| `v2w=1` 且 `v2=(0,0,0)` | 第三行不是视图 Z 轴 | 布局/偏移问题，改 FOV 无效 |
| `mathFovFactorRatio ≠ displayAspect` | fov 的 aspect 与当前显示不一致 | 可疑，但**不能**据此断言 aspect 已丢，也不能定单位 |
| `matrixSource=2` | **合成矩阵被采用，不代表矩阵正确** | 继续验证 yaw/pitch 约定、相机朝向、坐标系手性（U-11），不要因为"合成路径在跑"就排除矩阵问题 |
| `matrixSource=3` | 一个候选都没过校验 | 先修校验/兜底链 |

---

## 本轮没有做到的事

- 没有给出"改这一行就好"的补丁。**这是刻意的**：三条候选都能解释现象，随便选一条改，下一轮还得重来。
- 没有找到游戏真实的投影矩阵，也没有找到 view matrix 的唯一生产者。`0x358` 仍是扫描撞上的偏移。

---

## 交付

```text
docs/batch3_6/
    projection-root-cause.md          调用链、历史硬修表、三条镜像证据、一处自我更正
    projection-contract.json          逐阶段契约、候选矩阵路径、四个待答问题、诊断字段表
    projection-test-results.json      独立参考实现 49 项断言（ALL_PASS）
    minimal-fix-summary.md            实际改动（诊断）+ 预置未应用的数学补丁
    unresolved.md                     12 条未解决项，含批准条件与六个被推翻的自证
    diagnostic-probe.md               探针生命周期、字段表、采集后的判读规则
    tools/projection_reference.py     独立参考实现（不引用生产代码）
    tools/scan_0x358.py               偏移扫描（第一版有索引 bug，已修正）
    offset-0x358-writers.json         扫描原始输出
```

---

## Batch 3.6.1 附记（本轮追加）

判定不变：**`NEEDS_RUNTIME_MATRIX_CAPTURE`**。本轮只修了采集工具的可靠性，没有取得任何新证据。

修的三件事，都会直接影响下一次采集能不能用：

1. `sProbed` 在检查数据之前就置位，导致唯一一次采样可能落在对象未就绪的帧上且永不重试 → 改为"数据齐了才置 `captured`"，900 次预算内重试，之后静默低频重试。
2. 测试点上的 `self.y += 1.0f` 把一个待测的量改成了别的量 → 删除，改为记录四个位置与两个差值。
3. `clipX/clipY/clipZ + w(camera depth)` 与 `aspectUsed` 两个命名都在描述没有发生的事（没有投影矩阵、没有 clip.w；fov 因子比不是屏幕宽高比）→ 改为 `viewX/viewY/viewZ`，并把 `displayAspect` 与 `fovFactorRatio` 分开。

细节、字段表与**采集后的判读规则**见 `diagnostic-probe.md`。参考实现的断言在 3.6.1 时仍是 16 项全过（3.6.1a 扩到 24 项）。

---

## Batch 3.6.1a 附记（本轮追加）

判定仍不变：**`NEEDS_RUNTIME_MATRIX_CAPTURE`**。没有新证据，但修掉了一个**会让所有未来结论系统性偏移**的问题。

**新发现（读代码得到，不是推断）**：`MathUtils::getBoxPoints()` 第 149 行是

```cpp
if (!corrected.OWorldToScreen(origin, wp, result, fov, displaySize)) return {};
```

`origin` / `fov` / `displaySize` 是非限定名，在 `MathUtils` 成员函数里解析到**类静态量**；而 `corrected` 是 `RenderUtils::transform.mMatrix`。所以生产路径是**混合的**：矩阵来自 transform，三个投影参数来自 MathUtils。探针此前只读 transform 的 `mOrigin` / `mFov`，读的不是渲染器实际用的那两个。

**改了什么**（仍然只是诊断）：

1. 加 `math view` / `transform view` 两套投影（同帧、同矩阵、同点）+ `control` 一行给出 `screenDelta` / `viewDelta` / `originsMatch` / `fovsMatch`。这是本轮唯一能证伪旧结论的手段。
2. 仿射判定收紧：非零第四分量现在只报 `AFFINE_TRANSLATION_TERM_PRESENT`；只有在 `residual = w + dot(axis, transformOrigin)` 三行全 ~0 时才报 `DOUBLE_ORIGIN_SUBTRACTION_CONFIRMED`。非零 `w` 不再自动等于"相机偏移重复减去"。
3. fov 四个字段分开（`ciFovRaw` / `ciFovConverted` / `transformFov` / `mathFov`），并要求 **两次不同 FOV 设置的独立运行**才能定单位（`unresolved.md` U-9）。

参考实现从 16 项扩到 **24 项**，原 16 项断言一字未改；新增 8 项验证的是**判据本身**（对照非空、对照敏感、分类正确）。

---

## Batch 3.6.1b 附记（本轮追加）

判定仍为 **`NEEDS_RUNTIME_MATRIX_CAPTURE`**。没有新证据；本轮修的是"上一轮那套对照本身不成立"的三处。

**1. 仿射残差用错了原点。**
3.6.1a 用 `transformOrigin` 算 `residual`，但生产路径 `getBoxPoints()` 减的是 `MathUtils::origin`。矩阵若烘的是后者而被前者判定，结论会反过来。现在 `verdict` 一律用 `mathOrigin`，`transformOrigin` 的残差与"若用它会得到什么结论"作为独立对照一并打印（`verdictIfTransformOrigin`）。参考实现 `T14a–c` 给出这个场景：同一矩阵，生产原点判 `DOUBLE_ORIGIN_SUBTRACTION_CONFIRMED`，滞后的 transform 原点判 `AFFINE_TRANSLATION_TERM_PRESENT`。

**2. 测试点根本不在相机前方。**
上一轮用本地玩家的 `RenderPositionComponent::mPosition`，它就落在相机平面上，`OWorldToScreen` 的 `z >= 0` 直接拒绝——两套参数都被拒，`screenDelta` 只是两个 `(-1,-1)` 哨兵的差。现在取**非本地 actor 的 AABB 角点**，并要求两套参数的 `viewZ` 都 `< -0.1`；找不到就明确报原因，而不是把 `control` 当作已采。采集也拆成 `matrixCaptured` / `projectionControlCaptured` 两段，段 2 失败不会连带把段 1 关掉。

**3. `screenDelta` 被当成了相等性证明。**
不成立。把点放在视轴上，fov 差 30% 像素差仍然是 0（`T13a`）；同一对参数换成偏轴点差 11.57 px（`T13c`）。相等性只能由 `originsMatch` / `fovsMatch` 判断，`screenDelta` 仅用于量化这个测试点有多敏感。

**决策规则同步修正的两条**：`matrixSource=2` 只表示合成矩阵被采用，**不代表矩阵正确**，yaw/pitch 约定、相机朝向、坐标系手性仍需单独验证（U-11）；FOV 判断优先看 `mathFovFactorRatio`（生产路径吃的是 `mathFov`），且**单次采样不得判定单位或断言 aspect 已丢失**。

参考实现 24 → **35 项**，原 24 项断言一字未改；新增 11 项：T13（3，`screenDelta` 语义与反例）、T14（3，生产原点残差）、T15（5，无效测试点拒绝与选择器）。

---

## Batch 3.6.1c 附记（本轮追加）

判定仍为 **`NEEDS_RUNTIME_MATRIX_CAPTURE`**。没有新证据；本轮解决的是"采到的两份数据可能不属于同一帧"这一类会让结论整体作废的问题。

**1. 同帧配对。**
之前 `matrixCaptured` 与 `controlCaptured` 可以落在不同帧——最坏是"矩阵第 1 帧、测试点第 100 帧"，拼出来的投影渲染器从没做过。现在有单调帧计数，输出拆成 `preliminary snapshot matrixCaptureFrame=N`（只有矩阵，明确未配对）与 `paired sample matrixCaptureFrame=N controlCaptureFrame=M sameFrame=?`。关键是**结构性**保证：paired 块重新读一遍 `matrixSource` / 原点 / fov / mat16 / rowW / affine，所以它不复用 preliminary 的任何数字，`sameFrame=false` 也不存在混合。

**2. 八个角点。**
之前只查 4 个顶点（`(max,max,max)`、`(min,max,min)`、`(max,min,min)`、`(min,min,max)`），另外 4 个从未看过。`T17b` 给出反例：一个只有那 4 个角可用的箱子，四角扫描报"无候选"，八角扫描找到 2 个。

**3. 选偏轴的候选，而不是第一个合格的。**
`sensitivityScore = sqrt(viewX²+viewY²)/(-viewZ)`，即点与视轴夹角的正切。它直接决定"参数差异能不能变成像素差异"：`T13a` 视轴点 fov 差 30% 仍是 0 px，`T13c` 偏轴点 `S=0.54` 时是 11.57 px。`LOW_SENSITIVITY` 有两个来源——分数低于 0.15，或来自本地玩家 fallback；后者按来源判定，因为它在相机前方仅约 0.3 格，`S` 会被放大到 5 以上，而这对远处场景毫无代表性。

**4. 搜索节流。**
每 20 帧最多搜一次，预算按搜索次数计（900 次 ≈ 5 分钟），之后每 600 帧静默重试。没有线程、没有 Hook、没有改事件派发。

参考实现 35 → **49 项**，原 35 项断言一字未改；新增 14 项：T16（3，跨帧配对必须被拒 / 同帧可配 / paired 块自足）、T17（3，八角完整性）、T18（2，选择器偏好高灵敏度）、T19（3，LOW_SENSITIVITY 的两个来源）、T20（3，搜索间隔与预算计数）。
