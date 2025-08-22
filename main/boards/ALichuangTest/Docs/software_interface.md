# ALichuangTest 软件接口文档

## 概述

本文档描述了 ALichuangTest 板的软件接口，包括交互事件（输入）和输出事件两大部分。

- **交互事件（输入）**: 设备运动事件、触摸事件和事件管理功能，通过 interaction 文件夹中的模块实现
- **输出事件**: 直流马达控制、振动马达控制和屏幕动画播放功能，通过 skills 文件夹中的模块实现

---

# 第一部分：交互事件（输入事件）

## 1. 设备运动事件 (MotionEngine)

### 1.1 类定义
- **文件位置**: `interaction/motion_engine.h`, `interaction/motion_engine.cc`
- **类名**: `MotionEngine`
- **功能**: 专门处理IMU相关的运动检测，识别各种设备运动模式

### 1.2 构造函数

```cpp
MotionEngine();
```

**参数说明**: 默认构造函数，无参数

### 1.3 主要接口函数

#### 1.3.1 系统控制接口

```cpp
void Initialize(Qmi8658* imu);
```
- **功能**: 初始化运动引擎，设置IMU传感器
- **输入**: `imu` - QMI8658 IMU传感器实例指针
- **输出**: 无

```cpp
void Enable(bool enable);
bool IsEnabled() const;
```
- **功能**: 启用/禁用运动检测，查询启用状态
- **输入**: `enable` - true为启用，false为禁用
- **输出**: `IsEnabled()` 返回当前启用状态

#### 1.3.2 事件注册接口

```cpp
void RegisterCallback(MotionEventCallback callback);
```
- **功能**: 注册运动事件回调函数
- **输入**: `callback` - 运动事件回调函数 `std::function<void(const MotionEvent&)>`
- **输出**: 无

#### 1.3.3 处理和状态查询接口

```cpp
void Process();
```
- **功能**: 处理运动检测（在主循环中调用）
- **输入**: 无
- **输出**: 无

```cpp
bool IsPickedUp() const;
bool IsUpsideDown() const;
const ImuData& GetCurrentImuData() const;
```
- **功能**: 获取当前运动状态和IMU数据
- **输入**: 无
- **输出**: 
  - `IsPickedUp()`: 设备是否被拿起
  - `IsUpsideDown()`: 设备是否倒置
  - `GetCurrentImuData()`: 当前IMU数据

#### 1.3.4 调试接口

```cpp
void SetDebugOutput(bool enable);
```
- **功能**: 启用/禁用调试输出
- **输入**: `enable` - true为启用调试，false为禁用
- **输出**: 无

### 1.4 运动事件类型

支持以下运动事件类型（按优先级排序）：

- `MOTION_FREE_FALL`: 自由落体（加速度<0.3g，持续200ms以上）
- `MOTION_SHAKE_VIOLENTLY`: 剧烈摇晃（加速度变化>3.0g）
- `MOTION_FLIP`: 设备翻转（角速度>400°/s）
- `MOTION_SHAKE`: 普通摇晃（加速度变化>1.5g）
- `MOTION_PICKUP`: 设备被拿起（Z轴加速度变化>0.15g）
- `MOTION_UPSIDE_DOWN`: 设备倒置（Z轴加速度<-0.8g，持续稳定）

### 1.5 事件防抖机制

每种事件都有特定的冷却时间，防止重复触发：
- 自由落体：500ms
- 剧烈摇晃：400ms
- 翻转：300ms
- 普通摇晃：200ms
- 拿起：1000ms
- 倒置：500ms

## 2. 触摸事件 (TouchEngine)

### 2.1 类定义
- **文件位置**: `interaction/touch_engine.h`, `interaction/touch_engine.cc`
- **类名**: `TouchEngine`
- **功能**: 专门处理ESP32-S3触摸传感器输入，支持多种触摸模式检测

### 2.2 构造函数

```cpp
TouchEngine();
```

**参数说明**: 默认构造函数，使用GPIO10（左侧）和GPIO11（右侧）作为触摸输入

### 2.3 主要接口函数

#### 2.3.1 系统控制接口

```cpp
void Initialize();
```
- **功能**: 初始化触摸引擎，配置ESP32-S3触摸传感器
- **输入**: 无
- **输出**: 无

```cpp
void LoadConfiguration(const char* config_path = nullptr);
```
- **功能**: 加载触摸检测配置
- **输入**: `config_path` - 配置文件路径，nullptr使用默认路径
- **输出**: 无

```cpp
void Enable(bool enable);
bool IsEnabled() const;
```
- **功能**: 启用/禁用触摸检测，查询启用状态
- **输入**: `enable` - true为启用，false为禁用
- **输出**: `IsEnabled()` 返回当前启用状态

#### 2.3.2 事件注册接口

```cpp
void RegisterCallback(TouchEventCallback callback);
```
- **功能**: 注册触摸事件回调函数
- **输入**: `callback` - 触摸事件回调函数 `std::function<void(const TouchEvent&)>`
- **输出**: 无

#### 2.3.3 状态查询接口

```cpp
bool IsLeftTouched() const;
bool IsRightTouched() const;
```
- **功能**: 获取左/右侧触摸状态
- **输入**: 无
- **输出**: true表示当前正在被触摸

```cpp
void Process();
```
- **功能**: 处理触摸检测（由内部任务自动调用）
- **输入**: 无
- **输出**: 无

### 2.4 触摸事件类型

支持以下触摸事件类型：

- `TOUCH_SINGLE_TAP`: 单击（持续时间<500ms）
- `TOUCH_HOLD`: 长按（持续时间≥500ms）
- `TOUCH_RELEASE`: 释放（之前有长按）
- `TOUCH_CRADLED`: 摇篮模式（双侧持续触摸>2秒且IMU静止）
- `TOUCH_TICKLED`: 挠痒模式（2秒内多次无规律触摸>4次）

### 2.5 触摸位置

- `TouchPosition::LEFT`: 左侧触摸（GPIO10）
- `TouchPosition::RIGHT`: 右侧触摸（GPIO11）
- `TouchPosition::BOTH`: 双侧同时触摸
- `TouchPosition::ANY`: 任意侧（用于挠痒事件）

### 2.6 高级功能

#### 2.6.1 自适应阈值
- 自动读取基线值并设置触摸阈值
- 支持去噪功能，减少误触发
- 触摸值需达到基线的150%才认为是有效触摸

#### 2.6.2 防卡死机制
- 检测传感器数值冻结或异常高值
- 自动重置触摸传感器驱动
- 提供详细的故障诊断信息

#### 2.6.3 消抖处理
- 可配置的消抖时间（默认50ms）
- 防止触摸状态快速变化

## 3. 事件处理器 (EventProcessor)

### 3.1 类定义
- **文件位置**: `interaction/event_processor.h`, `interaction/event_processor.cc`
- **类名**: `EventProcessor`
- **功能**: 提供高级事件处理策略，包括防抖、节流、冷却、合并等智能处理机制

### 3.2 构造函数

```cpp
EventProcessor();
```

**参数说明**: 默认构造函数，使用立即处理作为默认策略

### 3.3 主要接口函数

#### 3.3.1 配置接口

```cpp
void ConfigureEventType(EventType type, const EventProcessingConfig& config);
```
- **功能**: 配置特定事件类型的处理策略
- **输入**: 
  - `type` - 事件类型
  - `config` - 处理配置结构
- **输出**: 无

```cpp
void SetDefaultStrategy(const EventProcessingConfig& config);
```
- **功能**: 设置默认处理策略
- **输入**: `config` - 默认处理配置
- **输出**: 无

#### 3.3.2 事件处理接口

```cpp
bool ProcessEvent(const Event& event, Event& processed_event);
```
- **功能**: 处理事件，根据配置策略决定是否立即处理
- **输入**: 
  - `event` - 原始事件
  - `processed_event` - 处理后的事件（引用输出）
- **输出**: `true` 表示应该立即处理，`false` 表示事件被延迟/丢弃/合并

```cpp
bool GetNextQueuedEvent(Event& event);
```
- **功能**: 获取队列中的下一个待处理事件
- **输入**: `event` - 输出事件（引用）
- **输出**: `true` 表示有事件可处理，`false` 表示队列为空

#### 3.3.3 状态管理接口

```cpp
void ClearEventQueue(EventType type);
```
- **功能**: 清空特定类型的事件队列
- **输入**: `type` - 要清空的事件类型
- **输出**: 无

```cpp
bool IsInCooldown(EventType type) const;
```
- **功能**: 检查特定事件类型是否在冷却期
- **输入**: `type` - 事件类型
- **输出**: `true` 表示在冷却期，`false` 表示可以处理

#### 3.3.4 统计接口

```cpp
EventStats GetStats(EventType type) const;
```
- **功能**: 获取特定事件类型的统计信息
- **输入**: `type` - 事件类型
- **输出**: 事件统计结构

### 3.4 处理策略类型

EventProcessor支持6种智能处理策略：

#### 3.4.1 IMMEDIATE（立即处理）
- **特点**: 每个事件都立即处理
- **用途**: 紧急事件或需要快速响应的事件
- **配置**: 无额外参数

#### 3.4.2 DEBOUNCE（防抖）
- **特点**: 只处理最后一个事件，忽略连续快速触发
- **用途**: 按键防抖、防止重复点击
- **配置**: `interval_ms` - 防抖时间

#### 3.4.3 THROTTLE（节流）
- **特点**: 固定时间内只处理第一个事件
- **用途**: 限制高频事件的处理频率
- **配置**: `interval_ms` - 节流间隔时间

#### 3.4.4 QUEUE（队列处理）
- **特点**: 按顺序处理事件，设置最小间隔
- **用途**: 需要保证事件顺序的场景
- **配置**: 
  - `interval_ms` - 事件间最小间隔
  - `max_queue_size` - 最大队列长度

#### 3.4.5 MERGE（合并处理）
- **特点**: 相同类型事件在时间窗口内合并
- **用途**: 多次点击转换为特殊动作
- **配置**: 
  - `merge_window_ms` - 合并时间窗口
  - `interval_ms` - 处理后冷却时间

#### 3.4.6 COOLDOWN（冷却处理）
- **特点**: 处理事件后需要冷却时间
- **用途**: 防止事件过于频繁
- **配置**: `interval_ms` - 冷却时间

### 3.5 事件统计结构

```cpp
struct EventStats {
    uint32_t received_count;    // 接收的事件数
    uint32_t processed_count;   // 处理的事件数
    uint32_t dropped_count;     // 丢弃的事件数
    uint32_t merged_count;      // 合并的事件数
    int64_t last_process_time;  // 最后处理时间
};
```

### 3.6 预定义配置

EventProcessor提供了多个预定义的处理策略配置：

#### 3.6.1 TouchTapConfig()
- **策略**: COOLDOWN
- **冷却时间**: 300ms
- **用途**: 触摸事件防抖

#### 3.6.2 MultiTapConfig()
- **策略**: MERGE
- **合并窗口**: 2秒
- **冷却时间**: 500ms
- **用途**: 多次触摸合并处理

#### 3.6.3 MotionEventConfig()
- **策略**: THROTTLE
- **节流间隔**: 1秒
- **用途**: 运动事件频率限制

#### 3.6.4 EmergencyEventConfig()
- **策略**: IMMEDIATE
- **中断允许**: true
- **用途**: 紧急事件立即处理

#### 3.6.5 QueuedEventConfig()
- **策略**: QUEUE
- **事件间隔**: 800ms
- **队列大小**: 5
- **用途**: 顺序处理的动作序列

### 3.7 高级特性

#### 3.7.1 智能事件合并
- 支持触摸事件计数合并
- 可扩展支持其他事件类型的合并逻辑
- 自动跟踪合并统计信息

#### 3.7.2 内存管理
- 自动管理待处理事件的内存分配
- 安全的事件队列清理机制
- 防止内存泄漏的析构函数

#### 3.7.3 实时监控
- 详细的事件处理日志
- 实时统计信息更新
- 处理策略执行状态跟踪

## 4. 事件管理 (EventEngine)

### 4.1 类定义
- **文件位置**: `interaction/event_engine.h`, `interaction/event_engine.cc`
- **类名**: `EventEngine`
- **功能**: 作为各种事件源的协调器，统一管理运动和触摸事件

### 4.2 构造函数

```cpp
EventEngine();
```

**参数说明**: 默认构造函数，自动创建事件处理器

### 4.3 主要接口函数

#### 4.3.1 系统控制接口

```cpp
void Initialize();
```
- **功能**: 初始化事件引擎，加载配置
- **输入**: 无
- **输出**: 无

```cpp
void InitializeMotionEngine(Qmi8658* imu, bool enable_debug = false);
```
- **功能**: 初始化并注册运动引擎
- **输入**: 
  - `imu` - QMI8658 IMU传感器实例指针
  - `enable_debug` - 是否启用调试输出
- **输出**: 无

```cpp
void InitializeTouchEngine();
```
- **功能**: 初始化并注册触摸引擎
- **输入**: 无
- **输出**: 无

#### 4.3.2 事件注册接口

```cpp
void RegisterCallback(EventCallback callback);
void RegisterCallback(EventType type, EventCallback callback);
```
- **功能**: 注册全局事件回调或特定事件类型回调
- **输入**: 
  - `callback` - 事件回调函数 `std::function<void(const Event&)>`
  - `type` - 特定事件类型（可选）
- **输出**: 无

#### 4.3.3 事件处理接口

```cpp
void Process();
```
- **功能**: 处理所有事件源（在主循环中调用）
- **输入**: 无
- **输出**: 无

```cpp
void TriggerEvent(const Event& event);
void TriggerEvent(EventType type);
```
- **功能**: 手动触发事件
- **输入**: 
  - `event` - 完整事件结构
  - `type` - 事件类型（简化版本）
- **输出**: 无

#### 4.3.4 状态查询接口

```cpp
bool IsPickedUp() const;
bool IsUpsideDown() const;
bool IsLeftTouched() const;
bool IsRightTouched() const;
```
- **功能**: 获取设备状态（通过子引擎）
- **输入**: 无
- **输出**: 对应的状态布尔值

#### 4.3.5 配置接口

```cpp
void ConfigureEventProcessing(EventType type, const EventProcessingConfig& config);
void SetDefaultProcessingStrategy(const EventProcessingConfig& config);
```
- **功能**: 配置事件处理策略（防抖、节流、冷却等）
- **输入**: 
  - `type` - 事件类型
  - `config` - 处理配置
- **输出**: 无

```cpp
EventProcessor::EventStats GetEventStats(EventType type) const;
```
- **功能**: 获取事件统计信息
- **输入**: `type` - 事件类型
- **输出**: 事件统计结构（触发次数、丢弃次数等）

### 4.4 统一事件类型

EventEngine定义了统一的事件类型枚举，包括：

**运动事件**:
- `MOTION_FREE_FALL`, `MOTION_SHAKE_VIOLENTLY`, `MOTION_FLIP`
- `MOTION_SHAKE`, `MOTION_PICKUP`, `MOTION_UPSIDE_DOWN`

**触摸事件**:
- `TOUCH_TAP`, `TOUCH_DOUBLE_TAP`, `TOUCH_LONG_PRESS`
- `TOUCH_SWIPE_UP`, `TOUCH_SWIPE_DOWN`, `TOUCH_SWIPE_LEFT`, `TOUCH_SWIPE_RIGHT`

**系统事件（预留）**:
- `AUDIO_WAKE_WORD`, `AUDIO_SPEAKING`, `AUDIO_LISTENING`
- `SYSTEM_BOOT`, `SYSTEM_SHUTDOWN`, `SYSTEM_ERROR`

### 4.5 事件处理特性

#### 4.5.1 智能事件处理
- 防抖：避免短时间内重复触发
- 节流：限制事件频率
- 冷却：事件间最小间隔时间
- 队列：支持事件排队处理

#### 4.5.2 配置管理
- 支持从JSON文件加载配置
- 嵌入式默认配置作为备选
- 运行时动态调整处理策略

---

# 第二部分：输出事件

## 1. 直流马达控制 (Motion)

### 1.1 类定义
- **文件位置**: `skills/motion.h`, `skills/motion.cc`
- **类名**: `Motion`
- **功能**: 通过 PCA9685 控制 DRV883X 驱动板来驱动直流马达，支持预设动作序列和精确角度控制

### 1.2 构造函数

```cpp
Motion(Pca9685* pca9685, uint8_t channel_a = 1, uint8_t channel_b = 2);
```

**参数说明**:
- `pca9685`: PCA9685控制器实例指针
- `channel_a`: DRV883X的IN1引脚连接的PCA9685通道 (默认1)
- `channel_b`: DRV883X的IN2引脚连接的PCA9685通道 (默认2)

### 1.3 主要接口函数

#### 1.3.1 系统控制接口

```cpp
esp_err_t Initialize();
```
- **功能**: 初始化身体动作技能模块，创建命令队列
- **输入**: 无
- **输出**: `ESP_OK` 如果成功，其他错误码如果失败

```cpp
esp_err_t StartTask();
```
- **功能**: 启动后台FreeRTOS任务处理动作命令
- **输入**: 无
- **输出**: `ESP_OK` 如果成功，其他错误码如果失败

```cpp
void StopTask();
```
- **功能**: 停止后台任务
- **输入**: 无
- **输出**: 无

#### 1.3.2 声明式动作接口

```cpp
void Perform(motion_id_t id);
```
- **功能**: 执行预设的复杂动作序列（非阻塞）
- **输入**: `id` - 动作ID枚举值
- **输出**: 无
- **预设动作ID包括**:
  - `MOTION_HAPPY_WIGGLE`: 开心摇摆
  - `MOTION_SHAKE_HEAD`: 摇头
  - `MOTION_DODGE_SUBTLE`: 微妙躲闪
  - `MOTION_NUZZLE_FORWARD`: 向前蹭
  - `MOTION_TENSE_UP`: 紧张绷紧
  - `MOTION_DODGE_SLOWLY`: 缓慢躲开
  - `MOTION_QUICK_TURN_LEFT/RIGHT`: 快速转向
  - `MOTION_CURIOUS_PEEK_LEFT/RIGHT`: 好奇窥探
  - `MOTION_SLOW_TURN_LEFT/RIGHT`: 慢速转向
  - 等22种预设动作

#### 1.3.3 命令式控制接口

```cpp
void SetAngle(float angle, motion_speed_t speed);
```
- **功能**: 控制身体转到精确的静态角度（非阻塞）
- **输入**: 
  - `angle`: 目标角度 (-90.0 到 90.0 度)
  - `speed`: 转动速度枚举 (`MOTION_SPEED_SLOW`, `MOTION_SPEED_MEDIUM`, `MOTION_SPEED_FAST`)
- **输出**: 无

#### 1.3.4 状态查询接口

```cpp
bool IsBusy();
```
- **功能**: 查询身体当前是否正在执行动作
- **输入**: 无
- **输出**: `true` 如果正在执行动作，`false` 如果空闲

```cpp
void Stop();
```
- **功能**: 立即停止当前所有身体动作
- **输入**: 无
- **输出**: 无

## 2. 振动马达控制 (Vibration)

### 2.1 类定义
- **文件位置**: `skills/vibration.h`, `skills/vibration.cc`
- **类名**: `Vibration`
- **功能**: 管理所有触觉反馈，支持情绪驱动的振动模式

### 2.2 构造函数

```cpp
Vibration(Pca9685* pca9685, uint8_t channel = 0);
```

**参数说明**:
- `pca9685`: PCA9685驱动器实例指针
- `channel`: PWM通道号（0-15），默认0（LED0）

### 2.3 主要接口函数

#### 2.3.1 系统控制接口

```cpp
esp_err_t Initialize();
```
- **功能**: 初始化振动系统（只初始化GPIO）
- **输入**: 无
- **输出**: `ESP_OK` 如果成功，其他错误码如果失败

```cpp
esp_err_t StartTask();
```
- **功能**: 启动振动任务（创建队列和任务）
- **输入**: 无
- **输出**: `ESP_OK` 如果成功，其他错误码如果失败

#### 2.3.2 振动控制接口

```cpp
void Play(vibration_id_t id);
```
- **功能**: 播放指定的振动模式
- **输入**: `id` - 振动模式ID
- **输出**: 无
- **预设振动模式ID包括**:
  - `VIBRATION_SHORT_BUZZ`: 短促确认反馈
  - `VIBRATION_PURR_SHORT`: 短促咕噜声
  - `VIBRATION_PURR_PATTERN`: 持续咕噜声
  - `VIBRATION_GENTLE_HEARTBEAT`: 温暖心跳
  - `VIBRATION_STRUGGLE_PATTERN`: 挣扎振动
  - `VIBRATION_SHARP_BUZZ`: 尖锐振动
  - `VIBRATION_TREMBLE_PATTERN`: 颤抖模式
  - `VIBRATION_GIGGLE_PATTERN`: 笑声振动
  - `VIBRATION_HEARTBEAT_STRONG`: 强心跳
  - `VIBRATION_ERRATIC_STRONG`: 混乱强振动

```cpp
void Stop();
```
- **功能**: 停止所有振动
- **输入**: 无
- **输出**: 无

#### 2.3.3 情绪驱动接口

```cpp
void PlayForEmotion(const std::string& emotion);
```
- **功能**: 根据情绪播放对应的振动
- **输入**: `emotion` - 情绪字符串（如 "happy", "angry", "sad" 等）
- **输出**: 无

```cpp
void SetEmotionBasedEnabled(bool enabled);
```
- **功能**: 启用/禁用基于情绪的自动振动
- **输入**: `enabled` - true为启用，false为禁用
- **输出**: 无

#### 2.3.4 状态查询接口

```cpp
bool IsInitialized() const;
```
- **功能**: 检查是否已初始化
- **输入**: 无
- **输出**: `true` 如果已初始化

```cpp
vibration_id_t GetCurrentPattern() const;
```
- **功能**: 获取当前播放的振动模式
- **输入**: 无
- **输出**: 当前振动模式ID

```cpp
const std::string& GetCurrentEmotion() const;
```
- **功能**: 获取当前情绪
- **输入**: 无
- **输出**: 当前情绪字符串

#### 2.3.5 测试功能接口

```cpp
void EnableButtonTest(vibration_id_t pattern_id = VIBRATION_SHORT_BUZZ, bool cycle_test = false);
```
- **功能**: 启用按键测试功能
- **输入**: 
  - `pattern_id`: 按下按键时执行的振动模式ID
  - `cycle_test`: 是否启用循环测试模式，默认false
- **输出**: 无

```cpp
void DisableButtonTest();
```
- **功能**: 禁用按键测试功能
- **输入**: 无
- **输出**: 无

## 3. 屏幕动画播放 (Animation)

### 3.1 类定义
- **文件位置**: `skills/animation.h`, `skills/animation.cc`
- **类名**: `AnimaDisplay`
- **继承自**: `LcdDisplay`
- **功能**: 专门用于动画播放的显示类，使用画布(Canvas)在UI顶层显示图片

### 3.2 构造函数

```cpp
AnimaDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
             int width, int height, int offset_x, int offset_y,
             bool mirror_x, bool mirror_y, bool swap_xy,
             DisplayFonts fonts);
```

**参数说明**:
- `panel_io`: LCD面板IO句柄
- `panel`: LCD面板句柄  
- `width`, `height`: 显示区域尺寸
- `offset_x`, `offset_y`: 显示偏移
- `mirror_x`, `mirror_y`: 镜像设置
- `swap_xy`: 坐标轴交换
- `fonts`: 字体配置

### 3.3 主要接口函数

#### 3.3.1 情绪相关接口

```cpp
virtual void OnEmotionChanged(std::function<void(const std::string&)> callback);
```
- **功能**: 设置情感变化回调函数
- **输入**: `callback` - 情感变化时调用的回调函数
- **输出**: 无

```cpp
virtual void SetEmotion(const char* emotion) override;
```
- **功能**: 设置当前情绪并触发回调
- **输入**: `emotion` - 情绪字符串
- **输出**: 无

```cpp
virtual void SetTheme(const std::string& theme_name) override;
```
- **功能**: 设置主题（仅保存名称，不影响UI元素）
- **输入**: `theme_name` - 主题名称
- **输出**: 无

#### 3.3.2 画布控制接口

```cpp
virtual void CreateCanvas();
```
- **功能**: 创建用于显示图片的画布
- **输入**: 无
- **输出**: 无

```cpp
virtual void DestroyCanvas();
```
- **功能**: 销毁画布并释放内存
- **输入**: 无
- **输出**: 无

```cpp
virtual void DrawImageOnCanvas(int x, int y, int width, int height, const uint8_t* img_data);
```
- **功能**: 在画布上指定位置绘制图像
- **输入**: 
  - `x`, `y`: 绘制位置坐标
  - `width`, `height`: 图像尺寸
  - `img_data`: 图像数据指针（RGB565格式）
- **输出**: 无

```cpp
virtual bool HasCanvas() const;
```
- **功能**: 检查是否已创建画布
- **输入**: 无
- **输出**: `true` 如果画布已创建

#### 3.3.3 重载的UI接口（空实现）

由于使用画布替代传统UI，以下接口被重载为空实现：

```cpp
virtual void SetStatus(const char* status) override {}
virtual void ShowNotification(const char* notification, int duration_ms = 3000) override {}
virtual void SetChatMessage(const char* role, const char* content) override {}
virtual void SetIcon(const char* icon) override {}
virtual void SetPreviewImage(const lv_img_dsc_t* image) override {}
virtual void UpdateStatusBar(bool update_all = false) override {}
virtual void SetPowerSaveMode(bool on) override {}
```

## 4. 板级集成函数 (ALichuangTest类)

### 4.1 情绪动画系统

```cpp
void StartImageSlideshow();
```
- **功能**: 启动基于情绪的图片循环显示任务
- **输入**: 无
- **输出**: 无

```cpp
std::pair<const uint8_t**, int> GetEmotionImageArray(const std::string& emotion);
```
- **功能**: 根据情绪获取对应的图片数组
- **输入**: `emotion` - 情绪字符串
- **输出**: 图片数组指针和数组大小的配对

```cpp
std::string GetCurrentEmotion();
void SetCurrentEmotion(const std::string& emotion);
```
- **功能**: 获取/设置当前情绪状态（线程安全）
- **输入**: `emotion` - 情绪字符串（设置时）
- **输出**: 当前情绪字符串（获取时）

```cpp
int GetEmotionPlayInterval(const std::string& emotion);
```
- **功能**: 根据情绪获取动画播放间隔
- **输入**: `emotion` - 情绪字符串
- **输出**: 播放间隔时间（毫秒）

## 5. 数据结构定义

### 5.1 动作相关枚举

```cpp
typedef enum {
    MOTION_SPEED_SLOW,    // 慢速
    MOTION_SPEED_MEDIUM,  // 中速
    MOTION_SPEED_FAST,    // 快速
} motion_speed_t;
```

### 5.2 振动关键帧结构

```cpp
typedef struct {
    uint16_t strength;    // 振动强度 (0-4095，12位PWM)
    uint16_t duration_ms; // 持续时间 (毫秒)
} vibration_keyframe_t;
```

## 6. 使用示例

### 6.1 初始化和使用Motion

```cpp
// 初始化
Motion* motion = new Motion(pca9685_instance, 1, 2);
motion->Initialize();
motion->StartTask();

// 执行预设动作
motion->Perform(MOTION_HAPPY_WIGGLE);

// 精确角度控制
motion->SetAngle(45.0f, MOTION_SPEED_MEDIUM);

// 检查状态
if (!motion->IsBusy()) {
    // 可以执行新动作
}
```

### 6.2 初始化和使用Vibration

```cpp
// 初始化
Vibration* vibration = new Vibration(pca9685_instance, 0);
vibration->Initialize();
vibration->StartTask();

// 播放振动
vibration->Play(VIBRATION_PURR_PATTERN);

// 根据情绪播放
vibration->PlayForEmotion("happy");

// 启用按键测试
vibration->EnableButtonTest(VIBRATION_SHORT_BUZZ, false);
```

### 6.3 使用AnimaDisplay

```cpp
// 创建显示实例
AnimaDisplay* display = new AnimaDisplay(panel_io, panel, width, height, 
                                        offset_x, offset_y, mirror_x, mirror_y, 
                                        swap_xy, fonts);

// 设置情绪回调
display->OnEmotionChanged([](const std::string& emotion) {
    ESP_LOGI(TAG, "Emotion changed to: %s", emotion.c_str());
});

// 创建画布并绘制图像
display->CreateCanvas();
display->DrawImageOnCanvas(0, 0, width, height, image_data);
```

## 7. 注意事项

1. **线程安全**: Motion和Vibration类使用FreeRTOS任务和队列实现线程安全的命令处理
2. **内存管理**: AnimaDisplay的画布使用PSRAM存储图像缓冲区，注意内存分配失败的处理
3. **PWM通道**: 确保PCA9685的通道分配不冲突（Motion使用通道1-2，Vibration使用通道0）
4. **错误处理**: 所有Initialize函数都返回esp_err_t，需要检查返回值
5. **资源释放**: 使用析构函数自动清理资源，也可手动调用StopTask()等函数

## 8. 性能参数

- **Motion控制精度**: ±0.5度角度容差
- **Vibration PWM分辨率**: 12位 (0-4095)
- **动画刷新率**: 根据情绪调整，快速情绪40-50ms间隔，慢速情绪80-120ms间隔
- **任务优先级**: Motion任务优先级5，Vibration任务优先级3，图像显示任务优先级3
- **队列大小**: Motion命令队列10个，Vibration命令队列8个