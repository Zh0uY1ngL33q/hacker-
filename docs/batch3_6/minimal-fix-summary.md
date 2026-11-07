# 本轮实际改动

> 分支 `feat/esp-projection-root-cause`，基线 `411fa4e`。
> 一个提交：`54f9188`，5 个文件，+47 / −1。
> **没有改动任何投影数学。** 本轮改的是"让缺失的数据可被采集"。

---

## 为什么不动数学

三条镜像证据（见 `projection-root-cause.md` §4）说明：矩阵来源与 FOV 单位都无法从源码或镜像自证。此时改公式就是"再让 ESP 换一种方式错位"，任务书明确禁止。

参考实现给出了违反契约时的误差量级，说明这个错误确实会产生用户看到的现象——但它同时说明：**只有拿到运行时数值，才能知道哪一条在生效**。

---

## 改动内容

### 1. 记录矩阵来源

`src/Utils/Structs.hpp` — `FrameTransform` 增加一个诊断字段：

```cpp
    //   0 = ClientInstance::getViewMatrix() accepted as-is
    //   1 = located by scanning ClientInstance / LevelRenderer / RendererPlayer
    //   2 = synthesised from the player's yaw and pitch
    //   3 = none of the above ever validated (the value is untrusted)
    // -1 = not set (the D3D path does not record a source).
    int mSource = -1;
```

`src/Hook/Hooks/RenderHooks/OpenGLGui.cpp` — 赋值处带上 `viewSource`：

```diff
-                        RenderUtils::transform = FrameTransform{
-                            view, *cam, rpc->mPosition, ci->getFov() };
+                        RenderUtils::transform = FrameTransform{
+                            view, *cam, rpc->mPosition, ci->getFov(), viewSource };
```

`mSource` 只被日志读取，不参与任何计算。

### 2. 暴露未经解释的 FOV 原始值

`src/SDK/Minecraft/ClientInstance.hpp/.cpp` 新增：

```cpp
glm::vec2 ClientInstance::getFovRaw()
{
    return { hat::member_at<float>(this, OffsetProvider::ClientInstance_mFovX),
             hat::member_at<float>(this, OffsetProvider::ClientInstance_mFovY) };
}
```

投影仍然只用 `getFov()`；`getFovRaw()` 是诊断接口，让"乘数 / 角度 / 弧度"这个三选一由一次日志回答。

### 3. 一次性 ESP 探针（3.6.1 / 3.6.1a / 3.6.1b / 3.6.1c 修订后）

`src/Features/Modules/Visual/ESP.cpp` — `probeProjectionOnce()`，两块输出，帧号写在标题里：

```text
[ESP probe] ===== preliminary snapshot matrixCaptureFrame=N (matrix only, NOT paired with a test point) =====
[ESP probe] [preliminary] frame=N matrixSource=<0..3>
[ESP probe] [preliminary] origins camera=(..) transformOrigin=(..) mathOrigin=(..) cameraToTransform=(..)
[ESP probe] [preliminary] fov ciFovRaw=(..) ciFovConverted=(..) transformFov=(..) mathFov=(..)
[ESP probe] [preliminary] display displayWidth=.. displayHeight=.. displayAspect=.. fovFactorRatio=.. mathFovFactorRatio=..
[ESP probe] [preliminary] basis v0(..) v0w=.. v1(..) v1w=.. v2(..) v2w=..
[ESP probe] [preliminary] mat16 glmColumnMajor (16 floats)
[ESP probe] [preliminary] affine verdict=<...> (production origin = mathOrigin) rowW=(..) axisDotMathOrigin=(..) residualMath=(..)
[ESP probe] [preliminary] affine control originsAgree=.. axisDotTransformOrigin=(..) residualTransform=(..) verdictIfTransformOrigin=<...>

[ESP probe] ===== paired sample matrixCaptureFrame=N controlCaptureFrame=M sameFrame=? searches=? =====
[ESP probe] [paired] frame=M matrixSource / origins / fov / display / basis / mat16 / affine ...  (同一组字段，重读一遍)
[ESP probe] testPoint source=.. actor=0x.. world=(..) sensitivityScore=.. sensitivity=OK|LOW_SENSITIVITY
                 testViewX=.. testViewY=.. testViewZ=.. transformTestViewZ=.. mathOk=.. transformOk=..
[ESP probe] localPlayer render=(..) known=.. playerToCamera=(..)
[ESP probe] math view: mathOk=.. mathScreen=(..) mathViewX=.. mathViewY=.. mathViewZ=..
[ESP probe] transform view: transformOk=.. transformScreen=(..) transformViewX=.. transformViewY=.. transformViewZ=..
[ESP probe] control comparable=.. originsMatch=.. fovsMatch=.. screenDelta=(..) viewDelta=(..) screenDeltaUsable=..
```

**为什么两块**：矩阵块不依赖测试点，空场景也能拿到；配对块需要一个**真正在相机前方**的点。上一轮把本地玩家的渲染位置当测试点，它就在相机平面上，被 `OWorldToScreen` 的 `z >= 0` 拒绝——两套参数都被拒，对照根本没做。

**为什么不会跨帧混合**：配对块不复用矩阵块的数字，它把 `matrixSource` / 原点 / fov / mat16 / rowW / affine 全部重读一遍。`sameFrame` 只报告两块是否同帧，不是正确性前提——所以"第 1 帧矩阵 + 第 100 帧测试点"这种拼接在结构上不可能发生。

**为什么测试点取八角而非第一个**：`sensitivityScore = sqrt(viewX²+viewY²)/(-viewZ)`（点与视轴夹角的正切）决定参数差异能否变成像素差。视轴点差 0 px，偏轴点（S≈0.54）差 11.57 px。八个 AABB 顶点里取 S 最大者；分数低于 0.15 或来自本地玩家 fallback 时标 `LOW_SENSITIVITY`，此时 `screenDeltaUsable=false`。

**为什么有两套 view**：`getBoxPoints()` 传给 `OWorldToScreen` 的是 `MathUtils::origin` / `MathUtils::fov`（非限定名解析到类静态量），矩阵却取自 `RenderUtils::transform.mMatrix`。`math view` 是生产参数，`transform view` 是快照。

**为什么 `verdict` 用 `mathOrigin`**：生产路径减的就是它。用 transform 副本算残差，在两者不一致的那一帧会把 `DOUBLE_ORIGIN_SUBTRACTION_CONFIRMED` 误判成 `AFFINE_TRANSLATION_TERM_PRESENT`（`T14a–c` 就是这个反例），所以两个残差都打印，但主判定只认生产原点。

字段与任务书第五工作组要求的对应关系：

| 任务书要求 | 本轮记录 | 说明 |
|---|---|---|
| WorldPosition / CameraOrigin / RelativePosition | `testWorld` / `camera` / `playerToCamera` / `testViewZ` | 测试点改为非本地 actor 的 AABB 角点，不再对坐标做任何修正 |
| ViewMatrix[16] | `mat16` + `basis` | 逐 float 打印，列主序原样 |
| **ProjectionMatrix[16]** | **未记录（UNAVAILABLE）** | 游戏没有暴露投影矩阵；DLL 也不构造它 |
| **CombinedMatrix[16]** | **未记录（UNAVAILABLE）** | 同上 |
| **FOVRaw / FOVUnit** | `ciFovRaw` / `ciFovConverted` / `transformFov` / `mathFov` | 4 个并排；单位不能从单次采样判定，见 `unresolved.md` U-9 |
| AspectRatio | `displayAspect` | 与 `fovFactorRatio` / `mathFovFactorRatio` 分开命名 |
| Viewport / FramebufferSize / ImGuiDisplaySize | `displayWidth` / `displayHeight` | 来自 `MathUtils::displaySize` |
| FramebufferScale | **未记录** | 见 `unresolved.md` U-4 |
| **ClipPosition / ClipW** | **不记录** | 这里只做视图变换，没有投影矩阵，clip.w 不存在；伪造一个等于自欺 |
| CameraForward / Right / Up | `basis` 三行即 right/up/-forward，含第四分量 | 不再重复打印 |
| PartialTicks | **未记录** | SDK 未映射插值系数 |
| ProjectionVerdict | `affine verdict`（仅仿射平移一项）+ 人工判定 | 不在代码里下超出数据的结论 |

刻意**没有**加的部分（以及原因）：

- 六组"相机正前/偏左/偏右/偏上/偏下/后方"测试点：这些点的世界坐标由相机位置与朝向推算，而朝向本身可能取自同一个可疑矩阵，会形成**自证**。改由参考实现离线验证（`tools/projection_reference.py` T1–T20，共 49 项）。
- 每帧全量扫描：探针**不**每帧重扫 actor 列表。搜索被节流为每 `kSearchIntervalFrames=20` 帧最多一次，900 次**搜索**预算（≈5 分钟），之后每 600 帧静默重试。preliminary 矩阵快照在 gate 通过瞬间就记，不依赖搜索；配对块只在搜到有效测试点时才写。没有线程、没有 Hook、没有改事件派发。日志总量：preliminary 约 16 行 + 配对约 12 行，集中在两次，日常静默。
- 用 `screenDelta` 判断参数是否相同：**不成立**（`T13a`：视轴上 fov 差 30%，像素差 0）。相等性只由 `originsMatch` / `fovsMatch` 判断。

---

## 回归影响

| 项 | 结论 |
|---|---|
| 投影数学 | 零改动 |
| 渲染路径 / Hook 安装 | 零改动（`OpenGLGui` 只多传一个整型字段） |
| 其他视觉模块 | 零改动（`FrameTransform` 新增字段带默认值，聚合初始化仍合法） |
| D3D 路径 | 零改动（它整体赋值 `FrameTransforms->front()`，新字段保持 -1） |
| 默认识别拒绝机制 | 零改动 |
| 每帧开销 | 零（探针只跑一次） |
| 编译风险 | 低：`FrameTransform` 仍是聚合类型（C++14 起带默认成员初始化器不影响聚合性）；`getFovRaw` 使用与 `getFov` 相同的 `hat::member_at` |

已知的一个编译风险点：若某处用 `FrameTransform{...}` 显式初始化全部 4 个字段并依赖 `sizeof`，新增字段会改变布局。用 `grep` 检查过，全树只有两处赋值（`OpenGLGui.cpp:481`、`D3DHook.cpp:438`），都不依赖 `sizeof`。

---

## 构建机命令

```powershell
git checkout feat/esp-projection-root-cause
cmake -S . -B out/build/x64-Release -DSOLSTICE_ALLOW_UNVERIFIED_MAPPINGS=OFF
cmake --build out/build/x64-Release --config Release --parallel
```

**这一步只验证编译。** 默认拒绝构建在 `Solstice.cpp:322` 就退出，ESP 探针一次都不会执行——用它证明投影问题等于没证。

要看那两行日志，需要一次**受控的诊断运行**（方案见 `unresolved.md` U-6，需人工批准）。

---

## 如果下一步确认了 w 泄漏，要改的是这一处

预置但**未应用**（因为 w 是否非零尚未证实）：

```diff
--- a/src/Hook/Hooks/RenderHooks/OpenGLGui.cpp
+++ b/src/Hook/Hooks/RenderHooks/OpenGLGui.cpp
@@
-                        RenderUtils::transform = FrameTransform{
-                            view, *cam, rpc->mPosition, ci->getFov(), viewSource };
+                        // OWorldToScreen subtracts the camera origin itself, so
+                        // the rows it dots must be translation free. The located
+                        // and synthesised paths already guarantee that; the game
+                        // matrix does not.
+                        for (int i = 0; i < 3; ++i) view[i].w = 0.f;
+                        view[3] = glm::vec4(0.f, 0.f, 0.f, 1.f);
+                        RenderUtils::transform = FrameTransform{
+                            view, *cam, rpc->mPosition, ci->getFov(), viewSource };
```

这段在 `rowW=(0,0,0)` 时是恒等变换，不会破坏任何当前正确的情形；在 `rowW≠0` 时消除一个随世界坐标增长的常量偏移。

**触发条件在 3.6.1b 之后定为**：不看"`rowW` 是否非零"，只看 `affine verdict`（它已按生产原点 `mathOrigin` 计算）：

- `DOUBLE_ORIGIN_SUBTRACTION_CONFIRMED` → 数据已证明矩阵减掉的正是调用方又减一次的那个原点，这段可以应用；
- `AFFINE_TRANSLATION_TERM_PRESENT` → 只证明"有平移项"，来源未知，**不应用**，先用 `rowW` / `axisDotMathOrigin` 与 `camera` 对齐；
- `NO_TRANSLATION_TERM` → 不适用。

顺序上还要先看两处：

1. 有 `control captured` 且 `comparable=true`（否则连可比都没有）；
2. `originsMatch=true` 且 `fovsMatch=true`（否则探针描述的仍是另一套参数，`verdict` 一并不可信）。

`originsAgree=false` 时不作废，但要以主判定为准：`verdictIfTransformOrigin` 只作对照——同一矩阵在两个原点下会得出相反结论（`T14a–c`）。
