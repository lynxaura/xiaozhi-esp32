# EventEngine 产品需求文档 (PRD)

## 1. 产品概述

### 1.1 项目背景
EventEngine是ALichuangTest机器人板的核心交互引擎，负责协调处理来自触摸传感器和IMU运动传感器的各种交互事件。它为AI伴侣机器人提供了丰富的物理交互感知能力，使机器人能够理解和响应用户的各种触摸和运动行为。

### 1.2 产品定位
- **角色**：多传感器事件协调器和处理中心
- **目标**：为AI伴侣机器人提供统一的交互事件检测和处理能力
- **价值**：将低级传感器信号转换为高级语义事件，支持情感化人机交互

### 1.3 核心价值
1. **统一事件模型**：将不同传感器的信号统一为Event结构
2. **智能事件处理**：支持防抖、节流、合并等多种处理策略
3. **可配置架构**：通过JSON配置文件灵活调整检测参数和处理策略
4. **实时响应**：50ms事件处理周期，支持实时交互反馈
5. **事件上传**：将交互事件上传到云端LLM进行语义理解

## 2. 功能需求

### 2.1 事件类型定义

#### 2.1.1 触摸事件 (Touch Events)
- **TOUCH_TAP**: 单击事件（支持左侧、右侧、双侧）
- **TOUCH_LONG_PRESS**: 长按事件（>600ms）
- **TOUCH_CRADLED**: 摇篮模式（双侧持续触摸>2秒且IMU静止）
- **TOUCH_TICKLED**: 挠痒模式（2秒内多次无规律触摸>4次）
- **TOUCH_DOUBLE_TAP**: 双击事件（预留）
- **TOUCH_HOLD/RELEASE**: 持续按住/释放事件（预留）

#### 2.1.2 运动事件 (Motion Events)
- **MOTION_FREE_FALL**: 自由落体（总加速度<0.3g持续200ms）
- **MOTION_SHAKE_VIOLENTLY**: 剧烈摇晃（加速度变化>3.0g）
- **MOTION_FLIP**: 设备翻转（角速度>400°/s）
- **MOTION_SHAKE**: 普通摇晃（加速度变化>1.5g）
- **MOTION_PICKUP**: 设备拿起（Z轴变化>0.15g后稳定）
- **MOTION_UPSIDE_DOWN**: 设备倒置（Z轴<-0.8g持续10帧）

#### 2.1.3 系统事件 (System Events)
- **AUDIO_WAKE_WORD/SPEAKING/LISTENING**: 音频事件（预留）
- **SYSTEM_BOOT/SHUTDOWN/ERROR**: 系统事件（预留）

### 2.2 事件数据结构

#### 2.2.1 Event结构体
```cpp
struct Event {
    EventType type;                 // 事件类型
    int64_t timestamp_us;          // 时间戳（微秒）
    union {
        ImuData imu_data;          // 运动事件IMU数据
        TouchEventData touch_data;  // 触摸事件数据
        int audio_level;           // 音频级别
        int error_code;            // 错误码
    } data;
};
```

#### 2.2.2 TouchEventData结构体
```cpp
struct TouchEventData {
    TouchPosition position;    // 触摸位置：LEFT/RIGHT/BOTH/ANY
    uint32_t duration_ms;     // 持续时间（毫秒）
    uint32_t tap_count;       // 点击次数（用于合并事件）
};
```

### 2.3 事件处理策略

#### 2.3.1 支持的处理策略
1. **IMMEDIATE**: 立即处理每个事件
2. **DEBOUNCE**: 防抖处理，只处理最后一个事件
3. **THROTTLE**: 节流处理，固定时间内只处理第一个事件
4. **QUEUE**: 队列处理，按顺序处理并设置最小间隔
5. **MERGE**: 合并处理，相同类型事件合并为一个
6. **COOLDOWN**: 冷却处理，处理后需要冷却时间

#### 2.3.2 默认策略配置
- **触摸事件**: MERGE策略，1.5秒合并窗口，500ms冷却
- **运动事件**: THROTTLE策略，2秒节流间隔
- **紧急事件**: IMMEDIATE策略，立即处理
- **拿起事件**: DEBOUNCE策略，500ms防抖

### 2.4 配置系统

#### 2.4.1 配置文件支持
- **文件位置**: `/spiffs/event_config.json`或嵌入式默认配置
- **热重载**: 支持运行时配置更新
- **参数分类**: 触摸检测参数、运动检测参数、事件处理策略

#### 2.4.2 关键配置参数
```json
{
  "touch_detection_parameters": {
    "tap_max_duration_ms": 500,
    "hold_min_duration_ms": 600,
    "cradled_min_duration_ms": 2000,
    "tickled_window_ms": 2000,
    "tickled_min_touches": 4
  },
  "motion_detection_parameters": {
    "free_fall": {"threshold_g": 0.3, "min_duration_ms": 200},
    "shake": {"normal_threshold_g": 1.5, "violently_threshold_g": 3.0},
    "flip": {"threshold_deg_s": 400.0}
  }
}
```

## 3. 技术架构

### 3.1 系统架构图
```
用户交互 → 传感器层 → 引擎层 → 处理器层 → 应用层
         (GPIO/I2C)  (Engine)  (Processor) (Callback)
```

### 3.2 核心组件

#### 3.2.1 EventEngine (事件引擎)
- **职责**: 事件源协调器，统一事件接口
- **组件管理**: 内部创建和管理MotionEngine、TouchEngine
- **事件分发**: 将原始传感器事件转换为统一Event结构
- **回调管理**: 支持全局回调和特定事件类型回调

#### 3.2.2 EventProcessor (事件处理器)
- **职责**: 事件过滤和策略处理
- **策略实现**: 实现6种事件处理策略
- **状态管理**: 维护每种事件类型的处理状态
- **统计功能**: 提供事件接收/处理/丢弃统计

#### 3.2.3 EventUploader (事件上传器)
- **职责**: 事件格式化和网络上传
- **格式转换**: 将Event转换为服务器可理解的JSON格式
- **批量处理**: 支持事件批量上传，提高网络效率
- **缓存机制**: 网络断开时缓存事件，连接恢复后批量发送

#### 3.2.4 EventConfigLoader (配置加载器)
- **职责**: 配置文件解析和加载
- **文件支持**: 支持SPIFFS文件系统和嵌入式配置
- **动态更新**: 支持运行时配置参数更新
- **默认配置**: 提供完整的默认配置作为备份

### 3.3 子引擎集成

#### 3.3.1 MotionEngine集成
- **传感器**: QMI8658 6轴IMU（I2C地址0x6A）
- **检测算法**: 实现自由落体、摇晃、翻转、拿起、倒置检测
- **回调注册**: 通过lambda回调将MotionEvent转换为Event

#### 3.3.2 TouchEngine集成  
- **硬件接口**: GPIO10(LEFT)、GPIO11(RIGHT)电容触摸
- **手势识别**: 支持点击、长按、摇篮、挠痒等复杂手势
- **位置感知**: 区分左侧、右侧、双侧触摸

### 3.4 事件上传架构

#### 3.4.1 消息格式
```json
{
  "session_id": "uuid",
  "type": "lx/v1/event", 
  "payload": {
    "events": [{
      "event_type": "Touch_Left_Tap",
      "event_text": "主人轻轻拍了我的左侧",
      "start_time": 1755222858360,
      "end_time": 1755222858360
    }]
  }
}
```

#### 3.4.2 网络传输
- **协议**: WebSocket/MQTT直接发送
- **方向**: 单向推送，无需服务器响应
- **连接管理**: 自动检查连接状态，断线重连
- **错误处理**: 发送失败时加入缓存队列

## 4. 性能要求

### 4.1 实时性要求
- **事件处理周期**: 50ms（20Hz）
- **事件检测延迟**: <100ms
- **上传响应时间**: <500ms（网络正常时）
- **内存使用**: <32KB（包括缓存）

### 4.2 可靠性要求
- **事件丢失率**: <1%（正常网络条件下）
- **误检率**: <5%（经过处理策略过滤后）
- **系统稳定性**: 支持7×24小时连续运行
- **内存泄漏**: 零内存泄漏

### 4.3 可扩展性要求
- **事件类型**: 支持扩展到50+种事件类型
- **处理策略**: 支持新增自定义处理策略
- **传感器接入**: 支持新增传感器类型
- **配置参数**: 支持动态新增配置项

## 5. 使用场景

### 5.1 基础交互场景
1. **日常触摸**: 用户轻拍机器人，触发问候或状态查询
2. **情感表达**: 长按表示安慰，摇篮模式表示陪伴
3. **游戏互动**: 挠痒痒模式触发欢乐反应
4. **紧急情况**: 自由落体检测触发安全提醒

### 5.2 高级场景
1. **多模态交互**: 结合语音和触摸的复合交互
2. **个性化学习**: 根据用户触摸习惯调整响应
3. **情境感知**: 结合时间、位置等信息理解交互意图
4. **群体交互**: 多人同时与机器人交互的处理

### 5.3 开发场景
1. **调试模式**: 实时输出传感器数据和事件状态
2. **性能监控**: 提供事件处理统计和性能指标
3. **配置调优**: 通过配置文件调整检测阈值
4. **新功能验证**: 快速添加新的事件类型和处理逻辑

## 6. 接口设计

### 6.1 初始化接口
```cpp
// 基础初始化
EventEngine engine;
engine.Initialize();

// 运动引擎初始化
engine.InitializeMotionEngine(qmi8658_instance, enable_debug);

// 触摸引擎初始化  
engine.InitializeTouchEngine();
```

### 6.2 回调注册接口
```cpp
// 全局事件回调
engine.RegisterCallback([](const Event& event) {
    // 处理所有事件
});

// 特定事件类型回调
engine.RegisterCallback(EventType::TOUCH_TAP, [](const Event& event) {
    // 处理点击事件
});
```

### 6.3 配置管理接口
```cpp
// 配置事件处理策略
EventProcessingConfig config;
config.strategy = EventProcessingStrategy::MERGE;
config.merge_window_ms = 1500;
engine.ConfigureEventProcessing(EventType::TOUCH_TAP, config);

// 更新运动检测参数
engine.UpdateMotionEngineConfig(json_config);
```

### 6.4 状态查询接口
```cpp
// 查询运动状态
bool picked_up = engine.IsPickedUp();
bool upside_down = engine.IsUpsideDown();

// 查询触摸状态  
bool left_touched = engine.IsLeftTouched();
bool right_touched = engine.IsRightTouched();

// 查询事件统计
auto stats = engine.GetEventStats(EventType::TOUCH_TAP);
```

## 7. 开发指南

### 7.1 添加新事件类型
1. 在`EventType`枚举中添加新类型
2. 在Event结构体的union中添加相应数据类型
3. 实现事件检测逻辑
4. 添加事件转换和格式化逻辑
5. 配置相应的处理策略

### 7.2 自定义处理策略
1. 在`EventProcessingStrategy`枚举中添加新策略
2. 在EventProcessor中实现处理逻辑
3. 在配置文件中添加策略参数
4. 更新配置加载器以支持新策略

### 7.3 集成新传感器
1. 创建新的Engine子类（如AudioEngine）
2. 定义传感器特定的事件类型和数据结构
3. 在EventEngine中添加初始化和回调设置
4. 实现事件转换逻辑

### 7.4 调试和测试
```cpp
// 启用调试输出
engine.InitializeMotionEngine(imu, true);

// 手动触发事件进行测试
Event test_event;
test_event.type = EventType::TOUCH_TAP;
engine.TriggerEvent(test_event);

// 查看处理统计
auto stats = engine.GetEventStats(EventType::TOUCH_TAP);
ESP_LOGI(TAG, "Received: %d, Processed: %d, Dropped: %d", 
         stats.received_count, stats.processed_count, stats.dropped_count);
```

## 8. 部署和运维

### 8.1 配置部署
1. 将`event_config.json`部署到设备的`/spiffs/`目录
2. 根据硬件特性调整检测阈值参数
3. 根据应用场景配置处理策略
4. 设置合适的调试输出级别

### 8.2 监控指标
- **事件接收率**: 每秒接收的事件数量
- **事件处理率**: 每秒实际处理的事件数量  
- **事件丢弃率**: 被处理策略丢弃的事件比例
- **网络上传成功率**: 事件成功上传到服务器的比例
- **缓存队列长度**: 待上传事件的缓存数量

### 8.3 故障排除
1. **事件检测失效**: 检查传感器连接和初始化状态
2. **事件重复触发**: 调整防抖/节流参数
3. **网络上传失败**: 检查网络连接和服务器状态
4. **内存泄漏**: 监控事件缓存队列的增长情况
5. **性能问题**: 检查事件处理周期和CPU使用率

## 9. 未来规划

### 9.1 功能扩展
- **机器学习集成**: 基于历史交互数据学习用户偏好
- **多设备协同**: 支持多个机器人之间的事件同步
- **预测性交互**: 基于上下文预测用户意图
- **情感计算**: 从交互模式推断用户情感状态

### 9.2 技术优化
- **事件融合**: 多传感器数据融合提高检测精度
- **边缘计算**: 在设备端进行更多语义分析
- **协议优化**: 更高效的事件上传协议
- **能耗优化**: 降低事件处理的功耗

### 9.3 生态建设
- **开发者工具**: 事件可视化调试工具
- **模拟器支持**: 支持在PC端模拟事件交互
- **插件系统**: 支持第三方开发者扩展功能
- **云端分析**: 提供事件数据的云端分析服务

## 10. 当前实现问题分析

**总体评价**: 当前 `EventEngine` 及其子系统（`EventProcessor`, `EventUploader`, `MotionEngine`, `TouchEngine`）的架构设计良好，功能基本完整。但通过深入分析代码，发现存在一些在**内存安全、架构耦合、错误处理、可维护性**方面的风险和待改进点。本章节将对这些问题进行详细阐述。

### 10.1 内存管理问题 (Memory Management) （已处理）

#### 10.1.1 [高风险] EventProcessor 内存与类型安全问题
**问题描述**: `EventProcessor` 在实现 `DEBOUNCE`, `MERGE`, `QUEUE` 等策略时，使用了 `void*` 指针来存储待处理的 `Event` 对象，并依赖手动 `new` 和 `delete` 进行内存管理。这是整个系统中最严重的安全隐患。

**具体表现**:
```cpp
// event_processor.h:83, 101
void* pending_event;            // 使用void*，丢失类型信息，极不安全
std::queue<void*> event_queue_; // 队列中存储裸指针，需要手动管理生命周期
```
**风险分析**:
1.  **类型不安全**: `void*` 的使用完全绕开了C++的类型系统，后续的 `static_cast` 或 `(Event*)` 强制转换无法在编译期得到验证，如果指针类型错误，将导致未定义行为。
2.  **内存泄漏**: 在复杂的逻辑分支中（如 `ProcessMerge`），非常容易忘记或错误地 `delete` 事件对象，导致内存泄漏。
3.  **代码脆弱**: `EventProcessor` 的析构函数虽然尝试清理内存，但无法保证在所有异常路径下都能被正确调用。

**建议改进**: **立即采用智能指针**，这是解决此类问题的现代C++标准做法。
```cpp
// 改进建议: event_processor.h
#include <memory> // 引入智能指针头文件

// ...
struct EventState {
    // ...
    std::unique_ptr<Event> pending_event; // 使用unique_ptr管理对象生命周期
    // ...
};

std::queue<std::unique_ptr<Event>> event_queue_; // 队列同样使用智能指针
```
通过此修改，可以完全消除手动内存管理的负担和风险，实现RAII（资源获取即初始化），让C++编译器自动处理内存回收。

### 10.2 架构设计问题 (Architecture)

#### 10.2.1 [高风险] EventUploader 职责混合与紧耦合
**问题描述**: `EventUploader` 类同时承担了**事件格式化**、**JSON序列化**、**离线缓存**和**网络发送**四项差异巨大的职责，严重违反了单一职责原则。同时，它直接依赖 `Application::GetInstance()` 单例，导致与应用主逻辑紧密耦合。

**具体表现**:
- **格式化**: `ConvertEvent()`, `GetEventTypeString()`, `GenerateEventText()`
- **序列化**: `BuildEventPayload()`
- **缓存**: `AddToCache()`, `ProcessCachedEvents()`, `ClearExpiredEvents()`
- **网络发送**: `SendSingleEvent()` 内部直接调用 `Application::GetInstance().SendEventMessage(payload);`

**风险分析**:
- **难以测试**: 由于紧耦合，无法对 `EventUploader` 进行独立的单元测试，必须启动整个 `Application` 环境。
- **难以维护**: 四种职责的逻辑混杂在一个类中，任何一处的修改都可能影响其他功能，增加了维护成本和引入错误的风险。

**建议改进**: 将 `EventUploader` 拆分为多个独立、可测试的组件，并通过依赖注入解耦。
- **`EventFormatter`**: 负责将 `Event` 对象转换为可序列化的结构体或`cJSON`对象。
- **`EventCache`**: 负责事件的缓存策略（如FIFO队列、过期清理）。
- **`IEventSender` (接口)**: 定义事件发送的抽象接口 `virtual bool Send(const std::string& payload) = 0;`。
- **`NetworkEventSender` (实现)**: 实现 `IEventSender` 接口，其内部调用 `Application::GetInstance()`，将耦合点隔离在此处。
- **`EventUploader` (重构后)**: 作为协调器，接收 `Event`，调用 `EventFormatter` 和 `EventCache`，并通过注入的 `IEventSender` 接口发送数据。

#### 10.2.2 缓存机制与网络错误处理不匹配 （已处理）
**问题描述**: 当前的缓存机制 (`event_cache_`) 主要用于**网络离线时的缓冲**，当连接恢复时 (`OnConnectionOpened`) 批量发送。但是，对于网络在线时 `SendSingleEvent` 发送失败的场景，**缺乏即时的、带重试逻辑的错误处理**。

**风险分析**: 如果设备在线但网络暂时抖动，`SendSingleEvent` 的调用会失败，而该事件会**被直接丢弃**，不会进入缓存或重试，导致事件丢失。

**建议改进**:
1.  为 `SendSingleEvent` 增加一个简单的、带重试次数限制的同步重试逻辑。
2.  或者，将 `SendSingleEvent` 的逻辑统一到缓存中，即使是单个事件也先入队，由一个统一的发送任务负责出队、发送和失败重试（如指数退避策略）。

### 10.3 配置与错误处理 (Configuration & Error Handling)

#### 10.3.1 配置系统不够灵活
**问题描述**:
1.  **优先级不明确**: `event_engine.cc` 中，配置文件加载失败后直接回退到嵌入的默认配置，而不是一个清晰的“分层覆盖”逻辑（例如：文件配置覆盖嵌入配置，嵌入配置覆盖代码默认值）。
2.  **热重载不完整**: `MotionEngine` 支持从JSON动态更新配置，但 `TouchEngine` 和 `EventProcessor` 的策略配置缺乏统一的热重载接口，导致配置管理不一致。

**建议改进**:
- 设计一个统一的配置管理器，支持分层加载和运行时热重载。
- 为所有主要引擎（`TouchEngine`, `EventProcessor`）提供 `UpdateConfig(const cJSON&)` 接口。

#### 10.3.2 错误恢复机制不一致 （已处理）
**问题描述**: `TouchEngine` 中包含了非常好的硬件卡死恢复逻辑 (`ResetTouchSensor`)，当检测到触摸读数长时间不变时会触发。然而，在其他错误路径下（如 `touch_pad_read_raw_data` 返回错误），却没有调用此恢复机制，只是打印日志。

**具体表现**:
- `TouchEngine::Process()` 中有 `frozen_count` 检测，可以成功调用 `ResetTouchSensor`。 ✓
- `TouchEngine::Process()` 中处理 `touch_pad_read_raw_data` 失败的逻辑块，仅有日志输出，没有调用恢复函数。 ✗

**建议改进**: 在所有检测到触摸硬件可能异常的地方，统一调用 `ResetTouchSensor` 恢复机制，增强系统的鲁棒性。

### 10.4 性能与可维护性 (Performance & Maintainability)

#### 10.4.1 事件序列化效率
**问题描述**: 在高频事件场景下，`SendSingleEvent` 对每个事件都进行一次完整的JSON序列化，存在性能开销。

**建议改进**: 引入批量处理机制。收集一小段时间内（如100ms）或一定数量（如5个）的事件，然后进行一次JSON序列化和发送，减少I/O和计算开销。

#### 10.4.2 配置文件版本管理 （小概率事件，不处理）
**问题描述**: `event_config.json` 缺乏版本号。如果未来固件升级，修改了配置项的名称或结构，旧的配置文件可能会导致解析失败或行为异常。

**建议改进**: 在JSON文件中增加一个 `config_version` 字段，并在加载时进行校验，以支持向后兼容和配置迁移。
```json
{
  "config_version": "1.1",
  // ... other params
}
```

#### 10.4.3 调试信息有待增强
**问题描述**: 当前主要依赖 `ESP_LOG` 进行调试，缺乏结构化的性能指标，难以诊断复杂问题（如事件处理延迟、内存占用）。

**建议改进**: 增加一个独立的 `Metrics` 模块或接口，用于查询关键性能指标，如：
```cpp
struct EventEngineMetrics {
    uint32_t events_per_second;
    uint32_t avg_processing_delay_us;
    size_t event_queue_size;
    size_t cache_size;
};
```

### 10.5 改进优先级建议

#### 高优先级（核心风险，必须解决）
1.  **`EventProcessor` 内存安全**: **立即**将 `void*` 替换为 `std::unique_ptr<Event>`，消除内存泄漏和类型安全风险。
2.  **`EventUploader` 职责分离与解耦**: 拆分 `EventUploader`，引入依赖注入，这是提升系统可测试性和可维护性的关键。

#### 中优先级（影响功能健壮性和开发效率）
1.  **网络错误处理**: 为即时发送的事件增加重试机制，防止数据丢失。
2.  **`TouchEngine` 错误恢复**: 统一调用 `ResetTouchSensor`，提升硬件鲁棒性。
3.  **配置系统统一**: 实现统一的热重载接口和更灵活的配置加载策略。

#### 低优先级（长期优化）
1.  **性能监控**: 增加详细的性能指标和调试接口。
2.  **事件批量序列化**: 优化高频场景下的性能。
3.  **配置文件版本管理**: 提高系统的长期可维护性。

通过解决以上问题，`EventEngine` 将变得更加**安全、稳定、高效和易于维护**，为产品提供更可靠的交互体验。