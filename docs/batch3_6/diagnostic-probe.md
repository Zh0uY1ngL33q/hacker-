# Batch 3.6.1 → 3.6.1c — ESP 投影探针：同帧、有效、可读

对象：`src/Features/Modules/Visual/ESP.cpp` 的 `probeProjectionOnce()`。
提交链（同一议题逐轮收紧，**投影数学始终零改动**）：

```
54f9188  diag(esp): record the projection's provenance, row translation and raw fov once
a7158d4  diag(esp): make the projection probe capture a complete frame, once
11ff965  diag(esp): project the point with the renderer's own parameters too
3.6.1b/c 探针有效性 + 同帧配对（工作区，未提交）
```

不触碰：`OWorldToScreen`、`FrameTransform`、`getFov`、`getCameraPos`、矩阵构造、ESP 正常绘制算法、`OffsetProvider`、`InstanceLocator`、其他模块、CMake、默认映射拒绝机制。**未应用 `.w = 0` 补丁。**

---

## 0. 生产路径本来就是混合的（前提，不是本轮的改动）

`MathUtils::getBoxPoints()`（`MathUtils.cpp:149`）：

```cpp
auto& corrected = RenderUtils::transform.mMatrix;
...
if (!corrected.OWorldToScreen(origin, wp, result, fov, displaySize)) return {};
```

`origin` / `fov` / `displaySize` 是非限定名，在 `MathUtils` 成员函数里解析到**类静态量**；矩阵却取自 `RenderUtils::transform.mMatrix`。所以：

| 输入 | 实际绘制用的 | 探针早期读的 |
|---|---|---|
| matrix | `transform.mMatrix` | 同 |
| origin | `MathUtils::origin` | `transform.mOrigin` ✗ |
| fov | `MathUtils::fov` | `transform.mFov` ✗ |
| display | `MathUtils::displaySize` | 同 |

两套参数各算一遍，是唯一能发现它们分歧的方法。

---

## 1. 同帧配对：这是本轮的主目标

之前 `matrixCaptured` 与 `controlCaptured` 可以落在不同帧。最坏情况是"矩阵取第 1 帧、测试点取第 100 帧"——两份数据拼起来的投影，渲染器从来没做过。

现在有一条单调递增的 RenderEvent 帧计数，输出拆成两块：

```text
[ESP probe] ===== preliminary snapshot matrixCaptureFrame=N (matrix only, NOT paired with a test point) =====
[ESP probe] ===== paired sample matrixCaptureFrame=N controlCaptureFrame=M sameFrame=<?> searches=<?> =====
```

- **preliminary**：闸门一过就采，只有矩阵事实，明确标注"未与测试点配对"。允许单独看，**不允许**和 paired 块拼起来解释。
- **paired**：找到测试点的那一帧重新读一遍 `matrixSource` / `origins` / `fov` / `display` / `basis` / `mat16` / `affine`，再用**同一帧**的矩阵、原点、FOV 与这个点算两套投影。

这条保证是**结构性**的：paired 块不复用 preliminary 的任何数字，所以即使 `sameFrame=false` 也不存在"旧矩阵 + 新控制点"的混合。`sameFrame` 只是报告两块是否恰好同帧，不是正确性前提。

```
preliminary ──frame N──▶ matrix facts (labelled, unpaired)
paired      ──frame M──▶ matrix facts (re-read) + test point + both projections
```

---

## 2. 测试点：八个角点 + 灵敏度

### 2.1 八个角，不是四个

`cornersOf()` 现在枚举 `(min|max) × (min|max) × (min|max)` 全部 8 个顶点。之前只查 4 个：`(max,max,max)`、`(min,max,min)`、`(max,min,min)`、`(min,min,max)`——另外 4 个从来没看过，一个只有那 4 个角可用的箱子会被误报"无候选"（`T17b` 就是这个反例：八角扫描找到 2 个，四角扫描找到 0 个）。

### 2.2 选偏轴的，不是选第一个

```text
S = sqrt(viewX² + viewY²) / (-viewZ)
```

这是点与视轴夹角的**正切**，与距离无关。逐角计算，在扫描预算内取 `S` 最大者，而不是第一个 `viewZ` 合格的。

因为它决定了"参数差异能不能变成像素差异"：视轴上的点 `S=0`，fov 差 30% 像素差仍是 0（`T13a`）；偏轴点 `S=0.54` 时同样差异是 11.57 px（`T13c`）。

记录字段：`sensitivityScore`、`testViewX`、`testViewY`、`testViewZ`、`transformTestViewZ`。

### 2.3 低灵敏度必须标出来

```text
sensitivity = OK | LOW_SENSITIVITY
```

两个独立判据：

1. `sensitivityScore < kMinSensitivity (0.15)` —— 点太靠近视轴；
2. `source == local_actor_aabb_corner_low_sensitivity` —— 本地玩家 fallback。

第 2 条按**来源**判定，不看分数：fallback 点离相机只有约 0.3 格，`S` 反而会被放大到 5 以上（`T19a` 的实测值），但那对远处的场景毫无代表性——近点不是更好的探针，而是会被裁掉的探针。

`LOW_SENSITIVITY` 时 `screenDeltaUsable=false`，`screenDelta` **不得**被当作灵敏度证据。

---

## 3. 搜索节流（本轮新增）

之前每个 RenderEvent 都可能调一次 `ActorUtils::getActorList` 并扫最多 256 个 Actor。现在：

| 常量 | 值 | 含义 |
|---|---:|---|
| `kSearchIntervalFrames` | 20 | 至少间隔 20 帧才搜一次（任务书要求 15–30） |
| `kProbeSearchBudget` | 900 | 预算按**搜索次数**计，不按帧 |
| `kProbeSearchLogEvery` | 100 | 每 100 次搜索才可能打一行进度 |
| `kProbeSlowRetryFrames` | 600 | 预算用尽后每 600 帧静默重试一次 |
| `kMaxActorsScanned` | 256 | 单次搜索的 Actor 上限 |

900 次 × 20 帧 ≈ 18000 帧 ≈ 5 分钟（60 fps）后才进入慢速重试。没有线程、没有 Hook、没有改事件派发。

日志上界可算：闸门未就绪最多 3 行；搜索失败在 1 / 100 / 200 … 次各一行；成功时 preliminary 8 行 + paired 14 行，之后彻底无声。

---

## 4. 对照语义

```text
[ESP probe] control comparable=? originsMatch=? fovsMatch=? screenDelta=(..) viewDelta=(..) screenDeltaUsable=?
```

判定顺序：`comparable` → `originsMatch` / `fovsMatch` → 最后才是 `screenDelta`（且要与 `screenDeltaUsable` 一起看）。

**`screenDelta` 不是相等性证明**：视轴上 fov 差 30% 时它恰好是 0（`T13a`），而同一对参数在偏轴点差 11.57 px（`T13c`）。相等性只由 `originsMatch` / `fovsMatch` 判断。

`comparable=false` 时两个 delta 是 `(-1,-1)` 哨兵之差，无意义。

---

## 5. FOV

```text
[ESP probe] fov ciFovRaw=(..) ciFovConverted=(..) transformFov=(..) mathFov=(..)
[ESP probe] display displayWidth=.. displayHeight=.. displayAspect=.. fovFactorRatio=.. mathFovFactorRatio=..
```

生产路径吃的是 `mathFov`，所以判断投影比例优先看 `mathFovFactorRatio`；`fovFactorRatio` 是 transform 副本，只作对照。`displayAspect ≠ mathFovFactorRatio` 只说明"不一致"，**不等于**"aspect 已丢失"。

**单次采样不能定单位**，见 `unresolved.md` U-9（需要两次不同 FOV 设置的独立运行）。

---

## 6. 仿射判定

```text
[ESP probe] [paired] affine verdict=<..> (production origin = mathOrigin) rowW=(..) axisDotMathOrigin=(..) residualMath=(..)
[ESP probe] [paired] affine control originsAgree=? axisDotTransformOrigin=(..) residualTransform=(..) verdictIfTransformOrigin=<..>
```

| 条件 | verdict |
|---|---|
| `max|w_i| <= 1e-4` | `NO_TRANSLATION_TERM` |
| 有 w 非零，且 `residualMath_i = w_i + dot(axis_i, mathOrigin)` 三行全 `<= 1e-4` | `DOUBLE_ORIGIN_SUBTRACTION_CONFIRMED` |
| 有 w 非零，残差不为零 | `AFFINE_TRANSLATION_TERM_PRESENT` |

推导：矩阵若已减 `T`，则 `w_i = -dot(axis_i, T)`，残差为 0。**非零 w 本身只说明存在平移项**。`verdict` 一律用 `mathOrigin`（生产原点）；transform 版本只作对照——同一矩阵在两原点下会得出相反结论（`T14a–c`）。

---

## 7. 空值保护

闸门四项（`clientInstance` / `cameraPos` / `displaySize` / `transformNotAssigned`）一次性全列出，最多打 3 行就停（防止闸门永不打开时变成日志水龙头）。`ClientInstance` 为空时不碰后面任何一环。

---

## 8. 验证

| 项 | 结果 |
|---|---|
| 投影参考测试 | **49/49 PASS**：原 35 项一字未改；新增 T16（3 同帧配对）、T17（3 八角点）、T18（2 选择器）、T19（3 低灵敏度）、T20（3 搜索节流） |
| 失败退出码 | `sys.exit(0 if allPass else 1)`，实测失败时 `exit=1` |
| 日志格式串 vs 参数 | 19 条调用逐条比对，**0 mismatch** |
| 括号平衡 | braces/parens/brackets 全 +0 |
| 改动范围 | 仅 `ESP.cpp`，且只在探针区 |
| 编译 | **未做**（本机无 MSVC） |

---

## 9. 采集后怎么读（先定规则，避免事后解释）

| 观察 | 结论 |
|---|---|
| 只有 `preliminary snapshot`，没有 `paired sample` | 没找到有效测试点。**不要**把 preliminary 当作投影结论 |
| `preliminary` 与 `paired` 的 `frame` 不同 | 正常，两块各自完整；不要跨块拼接数字 |
| `sameFrame=true` | 两块同帧，可以直接对照（但 paired 仍是权威） |
| `comparable=false` | 至少一套参数拒绝了该点，先看 `testViewZ` / `transformTestViewZ` |
| `originsMatch=false` / `fovsMatch=false` | 两套参数不同；此前基于 transform 快照的结论作废 |
| `sensitivity=LOW_SENSITIVITY` | `screenDelta` 不能当灵敏度证据；等一个有实体在视野内的场景再采 |
| `sensitivityScore` 很小 | 选点没找到更偏轴的候选，换场景重采 |
| `screenDelta` 很小 | **不代表参数相同**，只说明这个点不敏感（T13 反例） |
| `verdict=NO_TRANSLATION_TERM` 且 `v2=(0,0,0)` | 第三行不是视图 Z 轴 → 布局/偏移问题，改 FOV 无效 |
| `verdict=DOUBLE_ORIGIN_SUBTRACTION_CONFIRMED` | 数据证明原点被减两次 → 预置的 `.w` 归零补丁适用（本轮仍未应用） |
| `verdict=AFFINE_TRANSLATION_TERM_PRESENT` | 有平移项但来源未知 → 用 `rowW` / `axisDotMathOrigin` 与 `camera` 对齐 |
| `originsAgree=false` | 该帧两套 origin 不同；以主判定为准，`verdictIfTransformOrigin` 只作对照 |
| `matrixSource=2` | **合成矩阵被采用，不代表正确**，yaw/pitch、相机朝向、手性仍需验（U-11） |
| `mathFovFactorRatio ≠ displayAspect` | 不一致；但不得据此断言 aspect 已丢，也不得定单位（U-9） |

---

## 10. 已知限制

- 探针只在 `ESP` 启用时运行，ESP 默认关闭 → 仍需要一次受控诊断构建（`unresolved.md` U-6，**未经批准不实施**）。
- 搜索间隔 20 帧：进世界后最长约 0.33 s 才产生第一次搜索；这是刻意的成本交换。
- `S` 是夹角正切，不含"点在屏幕内的可读性"：一个在相机后方被你排除、一个在视野边缘但极扁的箱子，`S` 可能很大。它衡量的是敏感度，不是可见度。
- `mathScreen` / `transformScreen` 是 `OWorldToScreen` 的原始输出，不是最终 ImGui 像素。
- 一次性采样只覆盖一帧；旋转/移动的连续性只能靠画面判断。
