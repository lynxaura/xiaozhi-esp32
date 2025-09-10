# 事件命名与上传映射对照表

本文档对齐底层传感器事件（Multitouch/Motion）到统一事件（EventEngine::EventType）与上传字符串（EventUploader）的映射关系，并补充注意事项与命名建议。

## 总览

- 触摸引擎（`multitouch_engine`）产出：`TouchEventType`（`SINGLE_TAP`、`HOLD`、`CRADLED`、`TICKLED` 等）。
- 运动引擎（`motion_engine`）产出：`MotionEventType`（`FREE_FALL`、`SHAKE`、`FLIP`、`PICKUP`、`UPSIDE_DOWN` 等）。
- 事件在 `event_engine` 中映射为统一的 `EventType`，然后由 `event_uploader` 生成上传字符串（`event_type`）。

## 触摸事件映射

源事件（TouchEventType） → 统一事件（EventType） → 上传字符串（按位置）

- `SINGLE_TAP` → `TOUCH_TAP` →
  - 左：`Touch_Left_Tap`
  - 右：`Touch_Right_Tap`
  - 双侧：`Touch_Both_Tap`
- `HOLD` → `TOUCH_LONG_PRESS` →
  - 左：`Touch_Left_LongPress`
  - 右：`Touch_Right_LongPress`
  - 双侧：`Touch_Both_LongPress`
- `CRADLED` → `TOUCH_CRADLED` → `Touch_Both_Cradled`
  - 注：在 `event_engine::OnTouchEvent` 中强制位置为 `BOTH`，与上传命名一致。
- `TICKLED` → `TOUCH_TICKLED` → `Touch_Both_Tickled`
  - 注：在 `event_engine::OnTouchEvent` 中强制位置为 `BOTH`（底层 `multitouch_engine` 使用 `ANY` 计数）。
- `RELEASE` →（当前不上传）
  - `event_engine::ConvertTouchEventType` 将其映射为 `MOTION_NONE`，因此不会进入上传层；`multitouch_engine` 仅打印释放日志。

持续时间（触摸）：
- `TOUCH_TAP`/`TOUCH_LONG_PRESS`/`TOUCH_CRADLED` 使用事件内的 `duration_ms`。
- `TOUCH_TICKLED` 在上传层固定使用 2000ms 语义窗口（“2 秒内多次触摸”），与检测窗口一致。

## 运动事件映射

源事件（MotionEventType） → 统一事件（EventType） → 上传字符串

- `FREE_FALL` → `MOTION_FREE_FALL` → `Motion_FreeFall`
- `SHAKE_VIOLENTLY` → `MOTION_SHAKE_VIOLENTLY` → `Motion_ShakeViolently`
- `FLIP` → `MOTION_FLIP` → `Motion_Flip`
- `SHAKE` → `MOTION_SHAKE` → `Motion_Shake`
- `PICKUP` → `MOTION_PICKUP` → `Motion_Pickup`
- `UPSIDE_DOWN` → `MOTION_UPSIDE_DOWN` → `Motion_UpsideDown`

持续时间（运动）：
- 运动类均视为瞬时事件（上传层 `duration_ms = 0`）。

## 注意事项

- `RELEASE` 未上传：如需上报释放，可在 `event_engine::ConvertTouchEventType` 中开启映射，并在 `event_uploader::GetEventTypeString` 里增加相应命名（例如 `Touch_Left_Release`/`Right/Both`），同时定义对应的中文描述文本。
- `TICKLED` 的 `duration_ms` 采用固定窗口值（2000ms），用于语义表达聚合事件时长，并不代表单次触摸时长。

## 命名与维护建议

- 统一命名规范：建议采用统一的分隔风格（如 `Touch.Left.Tap` 或当前的 `Touch_Left_Tap`），并在全局文档中固定标准，减少歧义。
- 映射集中维护：将“统一事件 → 上传字符串”的逻辑从 `event_uploader` 抽取到独立的 `event_names.h/.cc`，供 `event_engine`/`event_uploader` 及测试复用，避免遗漏与漂移。
- 完整性测试：添加映射覆盖测试，保证所有 `TouchEventType`/`MotionEventType` 均能映射到 `EventType`，且每个 `EventType` 都能生成非空且受限长度的 `event_type`/中文描述。

## 参考文件

- 触摸检测：`interaction/sensors/multitouch_engine.cc`，`interaction/sensors/multitouch_engine.h`
- 运动检测：`interaction/sensors/motion_engine.cc`，`interaction/sensors/motion_engine.h`
- 统一事件与路由：`interaction/core/event_engine.cc`，`interaction/core/event_engine.h`
- 事件上传：`interaction/upload/event_uploader.cc`

