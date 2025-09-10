# 触摸与运动事件识别概览

本文档汇总 `main/boards/ALichuangTest/interaction/sensors/` 中多点触摸（`multitouch_engine`）与运动（`motion_engine`）当前可识别的事件及其判定逻辑、阈值与默认配置。

## MultitouchEngine（MPR121 多点触摸）

识别事件（`TouchEventType`）：

- `SINGLE_TAP`：单侧单击
- `HOLD`：单侧长按
- `CRADLED`：双侧持续触摸 + IMU 稳定
- `TICKLED`：短时间多次不规则触摸
- 注：`RELEASE` 枚举存在，但当前实现不分发该事件（仅日志记录释放信息）。

核心判定逻辑：

- 消抖：触摸状态变化小于 `debounce_time_ms` 忽略（默认 30ms）。
- 单击 `SINGLE_TAP`：一次按下-释放的总时长 < `tap_max_duration_ms`（默认 500ms），且期间未触发或挂起长按，才分发单击。
- 长按 `HOLD`：
  - 按下持续 ≥ `hold_min_duration_ms`（默认 600ms）时先标记“长按待定”（不立即分发）。
  - 之后等待额外延迟 200ms：
    - 若此期间另一侧也成为“长按待定”，则取消两侧的单侧长按，转入“摇篮/拥抱”检测（见 `CRADLED`）。
    - 若另一侧未触摸，超过（`hold_min_duration_ms` + 200ms）后分发单侧 `HOLD`（分发的 `duration_ms` 为原始时长减去 200ms 延迟）。
    - 若在分发前释放，取消该侧“长按待定”，不分发长按。
- 摇篮 `CRADLED`：左右两侧同时保持触摸，持续时间 ≥ `cradled_min_duration_ms`（默认 2000ms），且 IMU 稳定（通过回调 `IsIMUStable()`，由 `MotionEngine::IsStable()` 评估）；满足后只触发一次。任一侧松开即重置摇篮状态。
- 挠痒 `TICKLED`：在滑动窗口 `tickled_window_ms`（默认 2000ms）内统计“按下”时间点，次数达到 `tickled_min_touches`（默认 4）即分发一次，并清空计数避免重复触发。

默认配置（见 `touch_config.h`）：

- `tap_max_duration_ms = 500`
- `hold_min_duration_ms = 600`
- `cradled_min_duration_ms = 2000`
- `tickled_window_ms = 2000`
- `tickled_min_touches = 4`
- `debounce_time_ms = 30`
- `touch_threshold_ratio = 1.5`

其他实现要点：

- 通过 I2C 读取 MPR121 的触摸状态，并跟踪左右两侧 `TouchState`。
- `ProcessPendingHoldEvents()` 负责将“长按待定”分流为单侧 `HOLD` 或交给摇篮检测处理。
- `ProcessSpecialEvents()` 处理 `CRADLED` 与 `TICKLED`。

### 触摸事件上传映射（摘要）

源事件（TouchEventType） → 统一事件（EventType） → 上传字符串（按位置）：

- `SINGLE_TAP` → `TOUCH_TAP` → `Touch_Left_Tap` / `Touch_Right_Tap` / `Touch_Both_Tap`
- `HOLD` → `TOUCH_LONG_PRESS` → `Touch_Left_LongPress` / `Touch_Right_LongPress` / `Touch_Both_LongPress`
- `CRADLED` → `TOUCH_CRADLED` → `Touch_Both_Cradled`
- `TICKLED` → `TOUCH_TICKLED` → `Touch_Both_Tickled`
- `RELEASE` →（当前不上传）

持续时间规则：`TAP`/`LONG_PRESS`/`CRADLED` 传入检测到的 `duration_ms`；`TICKLED` 在上传层固定为 2000ms 语义窗口。

完整表见 `Docs/event_name_mapping.md`。

## MotionEngine（IMU 运动检测）

识别事件（`MotionEventType`，按检测优先级）：

1. `FREE_FALL`（自由落体）
2. `SHAKE_VIOLENTLY`（剧烈摇晃）
3. `FLIP`（快速翻转）
4. `SHAKE`（普通摇晃）
5. `PICKUP`（拿起）
6. `UPSIDE_DOWN`（倒置，持续状态）

事件冷却时间（微秒）：

- `FREE_FALL`: 500ms（500000µs）
- `SHAKE_VIOLENTLY`: 400ms（400000µs）
- `FLIP`: 300ms（300000µs）
- `SHAKE`: 200ms（200000µs）
- `PICKUP`: 1.5s（1500000µs）
- `UPSIDE_DOWN`: 500ms（500000µs）

核心判定逻辑：

- 自由落体 `FREE_FALL`：总加速度 |a| < `free_fall_threshold_g`（默认 0.3g）并持续 ≥ `free_fall_min_duration_ms`（默认 200ms）。进入/结束均跟踪，满足持续时长才分发。
- 剧烈摇晃 `SHAKE_VIOLENTLY`：Δa > `shake_violently_threshold_g`（默认 3.0g），或（Δa > 2.0g 且陀螺仪模长 > 300°/s）。
- 翻转 `FLIP`：陀螺仪模长 > `flip_threshold_deg_s`（默认 400°/s），且单轴最大角速度 > 阈值的 70%，且加速度变化 Δa > 0.5g，三者同时满足以抑制误报。
- 普通摇晃 `SHAKE`：Δa > `shake_normal_threshold_g`（默认 1.5g）。
- 拿起 `PICKUP`：
  - 冲突/稳定性门槛：若 800ms 内有剧烈运动或连续稳定读数 < 3，则不判定拿起。
  - 反冲/碰撞模式过滤：上一帧 Z 小、当前 Z 接近 1g、Δa 大 → 排除为碰桌面反冲，不判为拿起。
  - “在桌面”过滤：若设备疑似在桌面（Z 近 1g、XY 小），对向上运动阈值更严格。
  - 判定满足以下任一：明显的向上 Z 变化；渐进向上 + 姿态变化 + 不在桌面；幅值变化 + 姿态变化 + 相对稳定，且不为向下、非反冲。
  - 进入“已拿起”状态后，检测“放下”需若干稳定帧且 Z 回到接近 1g，或超时后出现明显向下运动。
- 倒置 `UPSIDE_DOWN`：Z 轴加速度 < `upside_down_threshold_g`（默认 -0.8g），且相对稳定（Δa < 0.5g），连续达到 `upside_down_stable_count`（默认 10 帧）判为倒置；恢复时清零计数。

稳定性评估（供触摸摇篮使用）：

- `IsStable()` 同时要求：Δa < 0.1g、陀螺仪模长 < 30°/s、|a| 接近 1g（±0.3g）。

默认配置（见 `motion_engine.h`）：

- 自由落体：`free_fall_threshold_g = 0.3`，`free_fall_min_duration_ms = 200`
- 摇晃：`shake_normal_threshold_g = 1.5`，`shake_violently_threshold_g = 3.0`
- 翻转：`flip_threshold_deg_s = 400.0`
- 拿起：`pickup_threshold_g = 0.15`，`pickup_stable_threshold_g = 0.05`，`pickup_stable_count = 5`，`pickup_min_duration_ms = 300`
- 倒置：`upside_down_threshold_g = -0.8`，`upside_down_stable_count = 10`
- 调试：`debug_interval_ms = 1000`，`debug_enabled = false`

实现要点：

- 优先级顺序检测并应用各自冷却时间，分发事件后更新时间戳；显著运动（自由落体/剧烈摇晃/翻转）会刷新“最近显著运动时间”，以避免与拿起等事件冲突。
- `IsCurrentlyStable()` 复用 `IsStable()` 的判定，用于 Multitouch 的 `CRADLED` 检查。

### 运动事件上传映射（摘要）

源事件（MotionEventType） → 统一事件（EventType） → 上传字符串：

- `FREE_FALL` → `MOTION_FREE_FALL` → `Motion_FreeFall`
- `SHAKE_VIOLENTLY` → `MOTION_SHAKE_VIOLENTLY` → `Motion_ShakeViolently`
- `FLIP` → `MOTION_FLIP` → `Motion_Flip`
- `SHAKE` → `MOTION_SHAKE` → `Motion_Shake`
- `PICKUP` → `MOTION_PICKUP` → `Motion_Pickup`
- `UPSIDE_DOWN` → `MOTION_UPSIDE_DOWN` → `Motion_UpsideDown`

持续时间规则：运动类事件在上传层视为瞬时（`duration_ms = 0`）。

完整表见 `Docs/event_name_mapping.md`。

## 流程概览

触摸事件流程：

```
MultitouchEngine(检测) -> TouchEventType + TouchPosition + duration
  -> EventEngine::ConvertTouchEventType(...) 映射为 EventType
  -> EventEngine::OnTouchEvent(...) 填充 TouchEventData（必要时强制 BOTH）
  -> EventUploader::GetEventTypeString(...) 生成上传 event_type 字符串
  -> Application::SendEventMessage(JSON)
```

运动事件流程：

```
MotionEngine(检测) -> MotionEventType + ImuData
  -> EventEngine::ConvertMotionEventType(...) 映射为 EventType
  -> EventUploader::GetEventTypeString(...) 生成上传 event_type 字符串
  -> Application::SendEventMessage(JSON)
```
