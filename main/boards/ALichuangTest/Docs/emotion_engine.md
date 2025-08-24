# Emotion Engine PRD (Product Requirements Document)

## 1. 项目概述

### 1.1 目标
构建一个情感状态管理系统，作为机器人"性格"的核心数学模型。该系统不直接产生行为，而是维护和管理机器人的内部情感状态，为其他模块（如动画显示、振动反馈、语音响应）提供情感上下文。

### 1.2 定位
EmotionEngine 是整个交互系统的状态管理核心，与 EventEngine 紧密协作：
- **EventEngine**: 负责检测和处理外部事件
- **EmotionEngine**: 根据事件更新内部情感状态
- **其他模块**: 基于情感状态产生具体行为

## 2. 核心设计

### 2.1 情感模型
采用二维情感空间模型：
- **Happiness (H)**: 快乐度，范围 [-10.0, +10.0]
- **Energy (E)**: 能量值，范围 [-10.0, +10.0]

四个情感象限定义：
1. **HAPPY_ENERGETIC** (H>0, E>0): 快乐且充满活力
2. **HAPPY_CALM** (H>0, E≤0): 快乐但平静
3. **UNHAPPY_ENERGETIC** (H≤0, E>0): 不快乐但有活力
4. **UNHAPPY_CALM** (H≤0, E≤0): 不快乐且低能量

### 2.2 情感映射
将具体情感状态映射到二维空间：
```
happy (开心):      H=+5, E=+3
excited (兴奋):    H=+3, E=+8
sad (悲伤):        H=-5, E=-3
angry (愤怒):      H=-4, E=+6
surprised (惊讶):  H=+1, E=+7
neutral (中性):    H=+2, E=0
sleepy (困倦):     H=+1, E=-5
curious (好奇):    H=+3, E=+4
frightened (害怕): H=-3, E=+5
content (满足):    H=+4, E=-2
```

## 3. 技术架构

### 3.1 类设计

```cpp
class EmotionEngine {
public:
    // 单例模式
    static EmotionEngine& GetInstance();
    
    // 初始化
    void Initialize();
    
    // 事件驱动的情感更新
    void OnEvent(const Event& event);
    
    // 直接设置情感状态（用于云端控制）
    void SetState(float happiness, float energy);
    void SetEmotion(const std::string& emotion_name);
    
    // 查询接口
    EmotionQuadrant GetQuadrant() const;
    std::string GetEmotionName() const;
    std::pair<float, float> GetCoordinates() const;
    
    // 配置接口
    void SetDecayEnabled(bool enabled);
    void SetDecayRate(float rate);
    void SetBaseline(float h, float e);
    
private:
    // 当前情感坐标
    float current_happiness_;
    float current_energy_;
    
    // 基线值（衰减目标）
    float baseline_happiness_ = 2.0f;
    float baseline_energy_ = 0.0f;
    
    // 时间衰减
    esp_timer_handle_t decay_timer_;
    bool decay_enabled_ = true;
    float decay_rate_ = 0.1f;  // 每次衰减的比例
    
    // 事件影响映射表
    struct EventImpact {
        float delta_happiness;
        float delta_energy;
    };
    std::map<EventType, EventImpact> event_impact_map_;
    
    // 情感名称映射
    std::map<std::string, std::pair<float, float>> emotion_coordinates_;
    
    // 内部方法
    void UpdateState(float delta_h, float delta_e);
    void ClampValues();
    void ProcessDecay();
    void LoadConfiguration();
};
```

### 3.2 事件影响配置

基于现有的 EventType，定义每个事件对情感的影响：

```cpp
// 运动事件的情感影响
event_impact_map_[EventType::MOTION_FREE_FALL] = {-8.0f, +9.0f};    // 恐惧
event_impact_map_[EventType::MOTION_SHAKE_VIOLENTLY] = {-3.0f, +7.0f}; // 晕眩
event_impact_map_[EventType::MOTION_FLIP] = {+2.0f, +4.0f};         // 好玩
event_impact_map_[EventType::MOTION_SHAKE] = {+1.0f, +3.0f};        // 轻微兴奋
event_impact_map_[EventType::MOTION_PICKUP] = {+0.5f, +2.0f};       // 被关注
event_impact_map_[EventType::MOTION_UPSIDE_DOWN] = {-2.0f, +3.0f};  // 不适

// 触摸事件的情感影响
event_impact_map_[EventType::TOUCH_TAP] = {+1.0f, +1.0f};          // 友好互动
event_impact_map_[EventType::TOUCH_DOUBLE_TAP] = {+2.0f, +2.0f};    // 更多关注
event_impact_map_[EventType::TOUCH_LONG_PRESS] = {+3.0f, -1.0f};    // 温暖抚摸
event_impact_map_[EventType::TOUCH_CRADLED] = {+5.0f, -3.0f};      // 被呵护
event_impact_map_[EventType::TOUCH_TICKLED] = {+4.0f, +6.0f};       // 被逗乐

// 音频事件的情感影响（预留）
event_impact_map_[EventType::AUDIO_WAKE_WORD] = {+1.0f, +3.0f};     // 被唤醒
event_impact_map_[EventType::AUDIO_SPEAKING] = {0.0f, +2.0f};       // 表达中
event_impact_map_[EventType::AUDIO_LISTENING] = {0.0f, -1.0f};      // 倾听中
```

### 3.3 集成方式

#### 3.3.1 与 EventEngine 集成

```cpp
// 在 EventEngine::Initialize() 中
void EventEngine::Initialize() {
    // ... 其他初始化
    
    // 初始化情感引擎
    EmotionEngine::GetInstance().Initialize();
    
    // 注册全局事件回调
    RegisterCallback([](const Event& event) {
        // 更新情感状态
        EmotionEngine::GetInstance().OnEvent(event);
    });
}
```

#### 3.3.2 与输出系统集成

```cpp
// 在 ALichuangTest::HandleEvent() 中
void ALichuangTest::HandleEvent(const Event& event) {
    // 获取当前情感状态
    auto emotion = EmotionEngine::GetInstance().GetEmotionName();
    
    // 更新当前显示的情感状态（用于动画选择）
    SetCurrentEmotion(emotion);
    
    // 根据情感播放相应的振动反馈
    if (vibration_skill_) {
        vibration_skill_->PlayForEmotion(emotion);
    }
    
    // 处理具体事件的直接响应（保持现有逻辑）
    switch (event.type) {
        case EventType::MOTION_FREE_FALL:
            vibration_skill_->Play(VIBRATION_SHARP_BUZZ);
            break;
        // ... 其他事件处理
    }
}
```

## 4. 配置文件设计

### 4.1 emotion_config.json

```json
{
  "baseline": {
    "happiness": 2.0,
    "energy": 0.0,
    "comment": "情感基线值，系统会向此值衰减"
  },
  
  "decay": {
    "enabled": true,
    "interval_ms": 10000,
    "rate": 0.1,
    "comment": "每10秒衰减10%向基线靠拢"
  },
  
  "event_impacts": {
    "MOTION_FREE_FALL": {"happiness": -8.0, "energy": 9.0},
    "MOTION_SHAKE_VIOLENTLY": {"happiness": -3.0, "energy": 7.0},
    "MOTION_FLIP": {"happiness": 2.0, "energy": 4.0},
    "MOTION_SHAKE": {"happiness": 1.0, "energy": 3.0},
    "MOTION_PICKUP": {"happiness": 0.5, "energy": 2.0},
    "MOTION_UPSIDE_DOWN": {"happiness": -2.0, "energy": 3.0},
    
    "TOUCH_TAP": {"happiness": 1.0, "energy": 1.0},
    "TOUCH_DOUBLE_TAP": {"happiness": 2.0, "energy": 2.0},
    "TOUCH_LONG_PRESS": {"happiness": 3.0, "energy": -1.0},
    "TOUCH_CRADLED": {"happiness": 5.0, "energy": -3.0},
    "TOUCH_TICKLED": {"happiness": 4.0, "energy": 6.0}
  },
  
  "emotion_coordinates": {
    "happy": {"happiness": 5.0, "energy": 3.0},
    "excited": {"happiness": 3.0, "energy": 8.0},
    "sad": {"happiness": -5.0, "energy": -3.0},
    "angry": {"happiness": -4.0, "energy": 6.0},
    "surprised": {"happiness": 1.0, "energy": 7.0},
    "neutral": {"happiness": 2.0, "energy": 0.0},
    "sleepy": {"happiness": 1.0, "energy": -5.0},
    "curious": {"happiness": 3.0, "energy": 4.0},
    "frightened": {"happiness": -3.0, "energy": 5.0},
    "content": {"happiness": 4.0, "energy": -2.0}
  },
  
  "thresholds": {
    "quadrant_hysteresis": 0.5,
    "emotion_distance": 2.0,
    "comment": "用于防止频繁切换的阈值"
  }
}
```

## 5. 实现步骤

### 5.1 第一阶段：基础实现
1. 创建 `emotion_engine.h` 和 `emotion_engine.cc`
2. 实现基本的状态维护和更新逻辑
3. 实现事件影响映射
4. 集成到 EventEngine

### 5.2 第二阶段：时间衰减
1. 实现定时器驱动的衰减机制
2. 添加可配置的衰减参数
3. 实现平滑过渡算法

### 5.3 第三阶段：高级特性
1. 实现情感惯性（防止突变）
2. 添加情感组合逻辑
3. 实现上下文相关的情感响应
4. 添加学习机制（根据用户偏好调整）

## 6. 测试计划

### 6.1 单元测试
- 测试情感坐标更新逻辑
- 测试边界值处理
- 测试衰减机制
- 测试事件影响映射

### 6.2 集成测试
- 测试与 EventEngine 的集成
- 测试与显示系统的集成
- 测试与振动系统的集成
- 测试配置文件加载

### 6.3 场景测试
- 连续事件的情感变化
- 长时间无交互的衰减
- 极端事件的响应
- 云端控制覆盖

## 7. 性能考虑

### 7.1 内存使用
- 使用静态分配避免动态内存
- 映射表使用 const static 存储
- 避免字符串操作

### 7.2 CPU使用
- 衰减定时器使用低频率（10-30秒）
- 事件处理使用简单的查表和加法
- 避免复杂的数学运算

### 7.3 实时性
- 情感更新在事件回调中同步完成
- 衰减处理在低优先级任务中执行
- 查询接口立即返回当前值

## 8. 扩展性设计

### 8.1 可扩展点
- 事件影响映射可通过配置文件修改
- 情感坐标系可扩展到三维
- 衰减算法可替换
- 可添加情感历史记录

### 8.2 接口预留
- 预留机器学习接口
- 预留云端同步接口
- 预留情感分析接口
- 预留用户偏好接口

## 9. 依赖关系

### 9.1 依赖的模块
- EventEngine: 事件源
- 配置加载器: 读取 emotion_config.json
- ESP定时器: 时间衰减

### 9.2 被依赖的模块
- ALichuangTest主板类: 在HandleEvent中查询情感状态，并根据情感触发相应的输出
- 显示系统: 根据情感状态选择对应的动画帧组
- 振动输出: 根据情感状态选择对应的振动模式播放
- 未来扩展: 语音合成情感参数、LED灯效等

## 10. 关键决策记录

### 10.1 为什么选择二维模型
- 简单直观，易于理解和调试
- 计算效率高，适合嵌入式系统
- 足够表达基本情感状态
- 易于映射到具体行为

### 10.2 为什么使用事件驱动
- 与现有 EventEngine 架构一致
- 响应及时，无需轮询
- 解耦合，易于测试和维护
- 支持异步处理

### 10.3 为什么需要时间衰减
- 模拟真实的情感变化
- 避免情感状态永久极端化
- 提供自然的交互体验
- 减少用户干预需求