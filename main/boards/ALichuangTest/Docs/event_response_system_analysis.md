# 事件响应系统现状分析（含最近提交）

## 概览
- 板卡：`ALichuangTest`
- 事件来源：IMU（QMI8658）+ 触摸（MPR121）
- 处理链路：Motion/Multitouch → EventEngine → EventProcessor（策略）→ EmotionEngine（情感VA）→ LocalResponseController（本地响应）→ EventUploader（云上报）
- 运行结构：触摸独立任务；运动每 50ms 定时处理；本地响应即时执行；状态响应随设备状态变化触发

## 最近提交要点
- 完成响应组件配置功能：本地响应系统从 SD 卡加载响应模板，组件化执行振动/动作/动画/音频（支持 can_interrupt）
- 创建完备 SD 卡资源结构并扩展动画帧数：统一 `sdcard/` 资源目录与分层
- 增加显示 GIF 功能：启用 LVGL GIF 播放，`AnimaDisplay` 支持按名称到路径的 GIF 映射
- 交互音频由 P3 改为 OGG：本地声音播放统一从 SD 卡读取 `.ogg`
- 调整本地反应系统初始化顺序：将外设与本地响应的初始化前移，减少冷启动延迟

（详见 `git log` 近 30 次：`git log --oneline -n 30`）

## 架构与流程
- 事件引擎（EventEngine）
  - 负责集成运动/触摸子引擎、统一分发事件、加载配置、批量上传窗口与空闲检测。
  - 初始化与配置加载：main/boards/ALichuangTest/interaction/core/event_engine.cc:57
  - 定时处理：由板卡处创建 50ms 定时器驱动 `Process()`。
- 运动引擎（MotionEngine）
  - 从 IMU 读取数据并检测：自由落体、剧烈摇晃、翻转、摇晃、拿起、倒置。
  - JSON 配置更新接口：main/boards/ALichuangTest/interaction/sensors/motion_engine.cc
- 多点触摸引擎（MultitouchEngine）
  - 独立任务轮询 MPR121，识别单击、长按、摇篮、挠痒等，支持 IMU 稳定性回调。
  - JSON 配置更新接口：main/boards/ALichuangTest/interaction/sensors/multitouch_engine.cc
- 事件处理器（EventProcessor）
  - 策略：IMMEDIATE/DEBOUNCE/THROTTLE/QUEUE/MERGE/COOLDOWN，可按事件类型配置。
  - 实现：main/boards/ALichuangTest/interaction/core/event_processor.{h,cc}
- 情感引擎（EmotionEngine）
  - 维护 VA 情感坐标，按事件增量更新；EventConfigLoader 传入各事件的 VA 影响。
  - 集成：main/boards/ALichuangTest/interaction/core/event_engine.cc:220
- 本地响应控制器（LocalResponseController）
  - 从 SD 卡加载响应模板（动画/音频/振动/动作），按事件与象限组合执行；支持 can_interrupt 与状态响应（idle/listening）。
  - 加载与执行：main/boards/ALichuangTest/interaction/controller/local_response_controller.cc
- 事件上传（EventUploader）
  - 将事件（含情感与设备状态）按窗批量/单发上报，必要时尝试建立连接。
  - 实现：main/boards/ALichuangTest/interaction/upload/event_uploader.cc

## 资源存储位置
- 动画（GIF）
  - 名称→路径映射：`AnimaDisplay::animation_maps_`
  - 文件位于 `sdcard/`，如紧急/交互/状态三层。
  - 参考：main/boards/ALichuangTest/skills/animation.cc:17（哈希映射表）
- 声音（OGG）
  - 名称→路径映射：`Application::audio_file_maps_`
  - 文件位于 `sdcard/` 对应事件目录。
  - 参考：main/application.cc:23（映射表起始）
- 分区
  - 还存在 `assets` SPIFFS 分区（8M），用于通用资源包下载与校验，但事件响应动画/音频当前走 SD 卡。
  - 参考：partitions/v2/16m.csv:6

## 配置文件与加载
- 事件检测与处理配置（EventConfig）
  - 代码期望路径：`/sdcard/event_config.json`
  - 失败回退：编译内置默认 JSON（`main/boards/ALichuangTest/interaction/config/event_config.json`）
  - 加载逻辑：main/boards/ALichuangTest/interaction/core/event_engine.cc:63,72,74,78
  - 作用：为 EventProcessor 写策略、为 Motion/Multitouch 写 detection 参数、为 EmotionEngine 写 VA 影响、为批量上传写窗口与上限等。
- 本地响应模板（ResponseConfig）
  - 实际路径：`/sdcard/config/response_config.json`
  - 加载逻辑：main/boards/ALichuangTest/interaction/controller/local_response_controller.cc:342
  - 作用：将事件（含象限）映射到组件列表：动画名称+循环次数、音频名称+音量、振动模式、动作 ID，以及 can_interrupt 规则。
- 其他
  - `sdcard/config/vasys_config.json`（VA 影响）存在于资源包中，但 VA 实际由 EventConfigLoader 读入。

## 生效时机（初始化顺序）
- 板卡构造 `ALichuangTest`
  - 初始化 SD 卡 → I2C/SPI → 显示/相机/IMU/PCA9685 等
  - InitializeInteractionSystem：创建 `EventEngine`，初始化 `Multitouch` 与 `Motion`，加载 EventConfig，创建 50ms 定时器开始处理，集成 `EmotionEngine`
  - 初始化 `EventUploader`，注册事件回调（单事件与批量）
  - InitializeLocalResponseSystem：创建 `LocalResponseController`，加载 `response_config.json`，注册设备状态变化监听（idle/listening 时做状态响应）
  - StartAnimationPlay（AnimaDisplay 动画线程）：跟随当前动画名循环播放，语音播放结束一段时间（默认 10s）自动回归 neutral
- 应用 `Application::Start`
  - 初始化音频服务 → 网络 → 资产校验/下载 → OTA → 协议（WS/MQTT） → 主循环
  - 设备状态变化由 `Application::SetDeviceState()` 统一触发（通知状态响应）

## 已知问题与风险（需要修复/统一）
1) EventConfig 路径不一致
- 代码读取 `/sdcard/event_config.json`（event_engine.cc:72），而仓库资源在 `sdcard/config/event_config.json`。
- 现象：SD 存在配置但加载失败，回退到内置默认，导致阈值/策略/VA 影响与资源包不一致。
- 影响：实机调参无效、事件频率与预期不符、情感状态偏差。
- 建议：统一为 `/sdcard/config/event_config.json` 或调整资源位置为 `/sdcard/` 根目录；同时更新文档与日志提示。

2) 音频映射表错误
- `touch_tickled_q1-4` 指向 `.gif` 而非 `.ogg`；`idle_q1-4` 缺少路径前缀斜杠、`idle_q4` 复用 `idle_q3` 路径。
- 参考：main/application.cc:23-56
- 现象：挠痒事件播放失败或无声，idle 提示音读取失败。
- 建议：改为 `/sdcard/.../xxx.ogg`，并纠正 idle 路径。

3) 动画映射表错误/重复
- 多个条目误指到 q2 或同一路径，如 `motion_pickup_q3/q4` 指到 q2，`motion_shake_q3/q4` 指到 q2；`idle_q4` 指到 q3。
- 参考：main/boards/ALichuangTest/skills/animation.cc:24-33,28-29,49
- 现象：播放与表情不匹配，影响体验与调试。
- 建议：按 `sdcard/` 实际资源修正映射，或从命名规则自动拼路径，减少人工表维护。

4) 文档路径与实现不一致（SPIFFS vs SD）
- `event_engine.md`、`TEST_GUIDE.md` 等提到 `/spiffs/event_config.json`，而代码使用 `/sdcard` 并且 `response_config.json` 位于 `/sdcard/config/`。
- 影响：集成/操作人员易被误导，造成配置文件放错位置。
- 建议：统一文档为 `/sdcard` 路径，并明确 `response_config.json` 位于 `sdcard/config/`，EventConfig 建议 `sdcard/config/`。

5) LocalResponse 字符串池容量限制
- 名称池大小 100、单名最长 32 字符，超限时会被丢弃或截断。
- 参考：main/boards/ALichuangTest/interaction/controller/local_response_controller.h（`MAX_NAME_POOL_SIZE=100`, `MAX_NAME_LENGTH=32`）
- 风险：当事件模板多或名称较长时，后续条目无法加载，出现“无响应模板”现象。
- 建议：扩大池容量或改为 `std::string` 管理；或在加载时做容量告警并统计。

6) IDLE_1MIN 事件与状态响应未统一
- EventEngine 内部会基于设备 `idle` 状态触发 `IDLE_1MIN` 事件，但本地“状态响应”是通过 `DeviceStateEventManager` 的 idle/listening 变化来驱动，与 `IDLE_1MIN` 未直连。
- 参考：main/boards/ALichuangTest/interaction/core/event_engine.cc（空闲检测）
- 建议：若需基于 IDLE_1MIN 做响应，需在 `response_config.json` 补充对应事件，或在 LocalResponseController 中增加专用映射。

7) 事件上传设备 ID 固定值
- `EventUploader::GenerateDeviceId()` 返回固定字符串 `alichuang_test_device`。
- 参考：main/boards/ALichuangTest/interaction/upload/event_uploader.cc:28-32
- 风险：多设备环境上报混淆、追踪困难。
- 建议：使用 MAC/UUID（系统已有 `SystemInfo::GetMacAddress()` 与 `Board::GetUuid()` 可复用）。

8) 初始化与线程安全注意
- 触摸任务（优先级 10）、运动定时器回调、动画任务与 LVGL 端口需保持线程安全；当前 `DisplayLockGuard` 已在 `SetAnima()` 内使用，整体风险可控，但仍需避免在高频回调内做重 IO。

## 建议与下一步
- 统一配置与资源路径：将 EventConfig 路径调整为 `/sdcard/config/event_config.json` 并修正文档；或将资源文件移动到代码期望位置。
- 修正全部映射表：批量核对 `animation_maps_` 与 `audio_file_maps_` 与 `sdcard/` 实际一致。
- 降低人工表维护：按命名规则自动拼路径（`/sdcard/<layer>/<event>/<event>.gif/.ogg`），仅在少数例外条目使用覆盖表。
- 增强本地响应健壮性：扩大字符串池或切换为动态字符串；对加载失败计数/报警。
- 设备 ID 与会话一致：事件上报统一携带设备唯一 ID。

## 关键代码参考（便于交叉核对）
- 音频映射表起始：main/application.cc:23
- 动画映射表起始：main/boards/ALichuangTest/skills/animation.cc:17
- EventConfig 加载路径：main/boards/ALichuangTest/interaction/core/event_engine.cc:72
- ResponseConfig 加载路径：main/boards/ALichuangTest/interaction/controller/local_response_controller.cc:342
- 事件批量上传参数：main/boards/ALichuangTest/interaction/core/event_engine.cc（`LoadUploadConfig`）
- 事件处理策略实现：main/boards/ALichuangTest/interaction/core/event_processor.{h,cc}
- 情感引擎集成与事件驱动：main/boards/ALichuangTest/interaction/core/event_engine.cc:220 及后续
- 设备状态变更派发：main/application.cc（`SetDeviceState()` 内）+ main/device_state_event.{h,cc}
- 分区表：partitions/v2/16m.csv:6

