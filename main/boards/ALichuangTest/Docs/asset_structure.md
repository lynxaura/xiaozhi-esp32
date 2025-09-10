# 小智AI玩具资源结构设计

基于四层响应系统的完整资源组织架构，包含动画、音效、振动、动作的配套管理。

## 目录结构概览

```
/sdcard/assets/
├── manifest.json                   # 全局资源清单
├── responses/                      # 事件响应资源
│   ├── layer1_emergency/           # 第一层：紧急反应
│   │   ├── motion_free_fall/
│   │   ├── motion_shake_violently/
│   │   ├── motion_flip/
│   │   └── motion_upside_down/
│   ├── layer2_reactive/            # 第二层：反应性即时反应
│   │   ├── motion_shake/
│   │   ├── motion_pickup/
│   │   ├── touch_tap/
│   │   ├── touch_long_press/
│   │   ├── touch_cradled/
│   │   └── touch_tickled/
│   ├── layer3_speaking/            # 第三层：说话表情
│   │   ├── talk_calm/
│   │   ├── talk_happy/
│   │   ├── talk_sad/
│   │   ├── talk_angry/
│   │   ├── talk_scared/
│   │   ├── talk_curious/
│   │   ├── talk_shy/
│   │   └── talk_content/
│   ├── layer4_standby/             # 第四层：情感化待机动作
│   │   ├── idle_q1/
│   │   ├── idle_q2/
│   │   ├── idle_q3/
│   │   └── idle_q4/
│   ├── listening/                  # 聆听状态反应
│   │   ├── listening_q1/
│   │   ├── listening_q2/
│   │   ├── listening_q3/
│   │   └── listening_q4/
│   └── system/                     # 系统功能动画
│       ├── system_boot_up/
│       ├── system_shut_down/
│       ├── system_charging/
│       ├── system_low_battery/
│       ├── system_connecting/
│       ├── system_error/
│       ├── photo_taken/
│       ├── detached/
│       └── attached/
└── presets/                        # 预设资源库
    ├── animations/                 # 通用动画模板
    ├── sounds/                     # 通用音效库
    └── vibrations/                 # 振动模式定义
```

## 单个事件资源结构

每个事件文件夹包含完整的响应资源：

### Layer1 紧急反应示例
```
/sdcard/assets/responses/layer1_emergency/motion_free_fall/
├── config.json                    # 事件配置
├── animation/                      # 动画帧文件
│   ├── 001.bin                     # 320x240 RGB565格式
│   ├── 002.bin
│   ├── 003.bin
│   └── ...
├── sound/
│   └── scream.p3                   # 惊恐尖叫音效
└── resources/                      # 额外资源
    └── preview.jpg                 # 预览图（调试用）
```

### Layer2 反应性象限事件示例
```
/sdcard/assets/responses/layer2_reactive/touch_tap/
├── q1/                             # Q1象限：积极高激活（兴奋）
│   ├── config.json
│   ├── animation/
│   │   ├── 001.bin
│   │   ├── 002.bin
│   │   └── ...
│   ├── sound/
│   │   └── giggle.p3
│   └── resources/
├── q2/                             # Q2象限：消极高激活（恐惧/压力）
│   ├── config.json
│   ├── animation/
│   │   ├── 001.bin
│   │   ├── 002.bin
│   │   └── ...
│   ├── sound/
│   │   └── startled.p3
│   └── resources/
├── q3/                             # Q3象限：消极低激活（悲伤/无聊）
│   ├── config.json
│   ├── animation/
│   │   ├── 001.bin
│   │   ├── 002.bin
│   │   └── ...
│   ├── sound/
│   │   └── sigh.p3
│   └── resources/
├── q4/                             # Q4象限：积极低激活（满足/平静）
│   ├── config.json
│   ├── animation/
│   │   ├── 001.bin
│   │   ├── 002.bin
│   │   └── ...
│   ├── sound/
│   │   └── content_hum.p3
│   └── resources/
└── default/                        # 默认响应（象限未定义时）
    ├── config.json
    ├── animation/
    │   ├── 001.bin
    │   ├── 002.bin
    │   └── ...
    ├── sound/
    │   └── neutral_beep.p3
    └── resources/
```

### motion_shake 完整象限结构示例
```
/sdcard/assets/responses/layer2_reactive/motion_shake/
├── q1/                             # Q1：积极高激活（欢快配合）
│   ├── config.json
│   ├── animation/
│   │   ├── 001.bin                 # 欢快摇摆配合的动画帧
│   │   ├── 002.bin
│   │   └── ...
│   ├── sound/
│   │   └── giggle_shake.p3         # 咯咯笑配合音效
│   └── resources/
├── q2/                             # Q2：消极高激活（不耐烦/抗拒）
│   ├── config.json
│   ├── animation/
│   │   ├── 001.bin                 # 抗拒不安的动画帧
│   │   ├── 002.bin
│   │   └── ...
│   ├── sound/
│   │   └── annoyed_grunt.p3        # 不耐烦抱怨声
│   └── resources/
├── q3/                             # Q3：消极低激活（无力/消极）
│   ├── config.json
│   ├── animation/
│   │   ├── 001.bin                 # 无力消极的反应动画
│   │   ├── 002.bin
│   │   └── ...
│   ├── sound/
│   │   └── tired_sigh.p3           # 疲惫叹气声
│   └── resources/
├── q4/                             # Q4：积极低激活（温和配合）
│   ├── config.json
│   ├── animation/
│   │   ├── 001.bin                 # 温和配合的动画帧
│   │   ├── 002.bin
│   │   └── ...
│   ├── sound/
│   │   └── content_purr.p3         # 满足的咕噜声
│   └── resources/
└── default/                        # 默认响应（象限未定义时）
    ├── config.json
    ├── animation/
    │   ├── 001.bin
    │   ├── 002.bin
    │   └── ...
    ├── sound/
    │   └── neutral_response.p3
    └── resources/
```

## 配置文件规范

### 1. 全局资源清单 (manifest.json)

```json
{
  "version": "1.0.0",
  "format_version": "1.0",
  "created": "2024-01-15T10:00:00Z",
  "description": "小智AI玩具资源包",
  "compatibility": {
    "min_firmware_version": "2.1.0",
    "board_types": ["ALichuangTest", "lingxi"]
  },
  "assets": {
    "total_events": 47,
    "total_size_mb": 125.6,
    "languages": ["zh-CN", "en-US"],
    "animation_format": "RGB565_320x240",
    "audio_format": "P3_16KHz_Mono"
  },
  "layers": {
    "layer1_emergency": {
      "priority": 1,
      "interrupts_all": true,
      "events": ["motion_free_fall", "motion_shake_violently", "motion_flip", "motion_upside_down"]
    },
    "layer2_reactive": {
      "priority": 2,
      "quadrant_based": true,
      "events": ["motion_shake", "motion_pickup", "touch_tap", "touch_long_press", "touch_cradled", "touch_tickled"]
    },
    "layer3_speaking": {
      "priority": 3,
      "speaking_only": true,
      "loop_during_tts": true,
      "events": ["talk_calm", "talk_happy", "talk_sad", "talk_angry", "talk_scared", "talk_curious", "talk_shy", "talk_content"]
    },
    "layer4_standby": {
      "priority": 4,
      "idle_only": true,
      "loop_continuous": true,
      "events": ["idle_q1", "idle_q2", "idle_q3", "idle_q4"]
    },
    "listening": {
      "priority": 5,
      "listening_only": true,
      "loop_continuous": true,
      "quadrant_based": true,
      "events": ["listening_q1", "listening_q2", "listening_q3", "listening_q4"]
    },
    "system": {
      "priority": 0,
      "functional_only": true,
      "no_emotion_dependency": true,
      "events": ["system_boot_up", "system_shut_down", "system_charging", "system_low_battery", "system_connecting", "system_error", "photo_taken", "detached", "attached"]
    }
  }
}
```

### 2. 紧急反应配置 (layer1_emergency/motion_free_fall/config.json)

```json
{
  "event": {
    "name": "motion_free_fall",
    "type": "emergency",
    "layer": 1,
    "priority": 100,
    "description": "自由落体检测 - 立即保护性反应"
  },
  "trigger": {
    "event_type": "MOTION_FREE_FALL",
    "conditions": {
      "acceleration_threshold": 0.3,
      "duration_ms": 200
    }
  },
  "state_behavior": {
    "interrupts": {
      "idle": true,
      "listening": true,
      "speaking": true
    },
    "restoration": {
      "auto_return": false,
      "manual_trigger_required": true
    }
  },
  "response": {
    "animation": {
      "enabled": true,
      "path": "animation/",
      "play_count": 1,
      "loop": false,
      "priority": "immediate",
      "frame_rate": 24,
      "total_duration_ms": 1500
    },
    "sound": {
      "enabled": true,
      "path": "sound/scream.p3",
      "volume": 95,
      "interrupt_current": true,
      "priority": "emergency"
    },
    "vibration": {
      "enabled": true,
      "pattern": "VIBRATION_ERRATIC_STRONG",
      "duration_ms": 2000,
      "intensity": 90
    },
    "motion": {
      "enabled": true,
      "action": "MOTION_STRUGGLE_TWIST",
      "duration_ms": 1800
    }
  },
  "upload": {
    "report_to_server": true,
    "priority": "high",
    "include_sensor_data": true
  }
}
```

### 3. 反应性象限事件配置 (layer2_reactive/touch_tap/q1/config.json)

```json
{
  "event": {
    "name": "touch_tap_q1",
    "type": "reactive_instant",
    "layer": 2,
    "priority": 50,
    "description": "触摸点击 - Q1象限（积极高激活/兴奋）反应"
  },
  "trigger": {
    "event_type": "TOUCH_TAP",
    "emotion_quadrant": "Q1",
    "conditions": {
      "valence_range": [0.1, 1.0],
      "arousal_range": [0.1, 1.0],
      "tap_duration_max": 500
    }
  },
  "state_behavior": {
    "allowed_states": ["idle", "listening"],
    "during_speaking": {
      "animation": true,
      "sound": false,
      "vibration": true,
      "motion": true
    }
  },
  "response": {
    "animation": {
      "enabled": true,
      "path": "animation/",
      "play_count": 2,
      "loop": false,
      "frame_rate": 20,
      "total_duration_ms": 800,
      "transition": {
        "fade_in_ms": 100,
        "fade_out_ms": 100
      }
    },
    "sound": {
      "enabled": true,
      "path": "sound/giggle.p3",
      "volume": 80,
      "randomize": {
        "enabled": true,
        "variations": ["giggle.p3", "laugh_short.p3", "happy_chirp.p3"]
      }
    },
    "vibration": {
      "enabled": true,
      "pattern": "VIBRATION_GIGGLE_PATTERN",
      "intensity": 70
    },
    "motion": {
      "enabled": true,
      "action": "MOTION_HAPPY_WIGGLE",
      "randomize_intensity": true
    }
  },
  "emotion_impact": {
    "valence_delta": 0.05,
    "arousal_delta": 0.03,
    "duration_ms": 3000
  }
}
```

### 4. 说话表情配置 (layer3_speaking/talk_happy/config.json)

```json
{
  "event": {
    "name": "talk_happy",
    "type": "speaking_expression",
    "layer": 3,
    "priority": 30,
    "description": "开心说话表情"
  },
  "trigger": {
    "device_state": "speaking",
    "emotion_tag": "happy",
    "mcp_command": "set_emotion"
  },
  "state_behavior": {
    "exclusive_to_speaking": true,
    "interrupt_on_layer2_events": false,
    "interrupt_on_layer1_events": true
  },
  "response": {
    "animation": {
      "enabled": true,
      "path": "animation/",
      "play_count": -1,
      "loop": true,
      "frame_rate": 15,
      "sync_with_audio": true,
      "lip_sync": {
        "enabled": false,
        "mouth_frames": []
      }
    },
    "sound": {
      "enabled": false,
      "note": "音效在speaking状态下被TTS占用"
    },
    "vibration": {
      "enabled": false,
      "note": "避免干扰语音播放"
    },
    "motion": {
      "enabled": true,
      "action": "MOTION_SUBTLE_SWAY",
      "sync_with_speech": true
    }
  },
  "lifecycle": {
    "start_delay_ms": 200,
    "stop_when_tts_ends": true,
    "fade_out_duration_ms": 500
  }
}
```

### 5. 待机状态配置 (layer4_standby/idle_q1/config.json)

```json
{
  "event": {
    "name": "idle_q1",
    "type": "standby_expression",
    "layer": 4,
    "priority": 10,
    "description": "Q1象限待机动作 - 兴奋状态的自发表现"
  },
  "trigger": {
    "device_state": "idle",
    "emotion_quadrant": "Q1",
    "idle_timeout_ms": 5000,
    "conditions": {
      "valence_range": [0.1, 1.0],
      "arousal_range": [0.1, 1.0]
    }
  },
  "state_behavior": {
    "continuous_loop": true,
    "interrupt_on_any_event": true,
    "resume_after_interrupt": true,
    "auto_intensity_scaling": true
  },
  "response": {
    "animation": {
      "enabled": true,
      "path": "animation/",
      "play_count": -1,
      "loop": true,
      "frame_rate": 12,
      "random_variations": [
        "animation/variant_1/",
        "animation/variant_2/",
        "animation/variant_3/"
      ],
      "variation_interval_ms": 15000
    },
    "sound": {
      "enabled": true,
      "path": "sound/",
      "volume": 60,
      "play_probability": 0.3,
      "interval_range_ms": [8000, 20000],
      "variations": [
        "content_hum.p3",
        "happy_chirp.p3",
        "playful_trill.p3"
      ]
    },
    "vibration": {
      "enabled": true,
      "pattern": "VIBRATION_GENTLE_HEARTBEAT",
      "intensity": 40,
      "interval_ms": 12000
    },
    "motion": {
      "enabled": true,
      "actions": [
        {
          "action": "MOTION_CURIOUS_PEEK_LEFT",
          "probability": 0.4,
          "interval_range_ms": [10000, 25000]
        },
        {
          "action": "MOTION_HAPPY_WIGGLE",
          "probability": 0.3,
          "interval_range_ms": [15000, 30000]
        }
      ]
    }
  },
  "adaptation": {
    "reduce_frequency_over_time": true,
    "min_interval_multiplier": 0.5,
    "max_interval_multiplier": 2.0,
    "adaptation_period_ms": 300000
  }
}
```


## 播放控制逻辑

### 动画播放次数控制

```cpp
enum class PlayMode {
    ONCE = 1,           // 播放一次
    TWICE = 2,          // 播放两次
    TRIPLE = 3,         // 播放三次
    INFINITE = -1,      // 无限循环
    UNTIL_INTERRUPT = 0 // 播放直到被打断
};

class AnimationController {
public:
    void PlayAnimation(const std::string& path, PlayMode mode) {
        AnimationConfig config = LoadAnimationConfig(path);
        
        switch (mode) {
            case PlayMode::ONCE:
                PlaySequence(config, 1, false);
                break;
            case PlayMode::INFINITE:
                PlaySequence(config, 1, true);  // loop = true
                break;
            case PlayMode::UNTIL_INTERRUPT:
                PlayUntilEvent(config);
                break;
            default:
                PlaySequence(config, static_cast<int>(mode), false);
                break;
        }
    }
};
```

### 状态锁机制

```cpp
class StateLockManager {
public:
    struct LockRules {
        bool blocks_animation = false;
        bool blocks_sound = false;
        bool blocks_vibration = false;
        bool blocks_motion = false;
    };
    
    LockRules GetLockRules(DeviceState state, int event_layer) {
        LockRules rules;
        
        switch (state) {
            case kDeviceStateSpeaking:
                if (event_layer == 1) {
                    // Layer1紧急事件：打断一切
                    rules = {false, false, false, false};
                } else {
                    // 其他层级：屏蔽音效，保留体感
                    rules = {false, true, false, false};
                }
                break;
            case kDeviceStateIdle:
            case kDeviceStateListening:
                // 完整播放
                rules = {false, false, false, false};
                break;
        }
        
        return rules;
    }
};
```

## 资源加载策略

### 分级加载优先级

```cpp
class AssetLoadManager {
private:
    enum class LoadPriority {
        CRITICAL = 1,    // Layer1紧急事件
        HIGH = 2,        // 当前情感象限Layer2事件
        MEDIUM = 3,      // Layer3说话表情
        LOW = 4,         // Layer4待机动作
        BACKGROUND = 5   // 其他象限和系统动画
    };
    
public:
    void InitializeAssets() {
        // 1. 立即加载关键资源
        LoadAssetsWithPriority(LoadPriority::CRITICAL);
        
        // 2. 预加载当前象限资源
        EmotionQuadrant current = GetCurrentQuadrant();
        LoadQuadrantAssets(current, LoadPriority::HIGH);
        
        // 3. 后台异步加载其他资源
        StartBackgroundLoading();
    }
};
```

## 使用示例

### 事件触发流程

基于当前系统架构，事件处理流程如下：

#### 1. 事件检测和预处理
```cpp
// EventEngine 作为事件协调器，管理所有事件源
class EventEngine {
    // 传感器引擎检测到事件后回调
    void OnTouchEvent(const TouchEvent& touch_event) {
        Event event;
        event.type = ConvertTouchEventType(touch_event.type, touch_event.position);
        event.timestamp_us = esp_timer_get_time();
        event.data.touch_data = touch_event.data;
        
        // 通过EventProcessor进行事件处理策略过滤
        Event processed_event;
        if (event_processor_->ProcessEvent(event, processed_event)) {
            DispatchEvent(processed_event);
        }
    }
    
    void DispatchEvent(const Event& event) {
        // 添加到批量上传队列
        AddToPendingBatch(event);
        
        // 分发给注册的回调函数（包括LocalResponseController）
        if (global_callback_) {
            global_callback_(event);
        }
    }
};
```

#### 2. 本地响应控制器处理
```cpp
void LocalResponseController::ProcessEvent(const Event& event) {
    ESP_LOGI(TAG, "Processing event type: %d", static_cast<int>(event.type));
    
    // 1. 创建执行上下文（包含当前设备状态和情感状态）
    ExecutionContext context = CreateContext(event);
    
    // 2. 查找对应的响应模板
    ResponseTemplate* tmpl = FindTemplate(event.type);
    if (!tmpl) {
        ESP_LOGW(TAG, "No template found for event type: %d", static_cast<int>(event.type));
        return;
    }
    
    // 3. 根据情感象限获取响应组件
    ResponseComponent* components[10];
    size_t component_count = 0;
    tmpl->GetComponents(context.current_quadrant, components, &component_count);
    
    // 4. 执行所有响应组件
    ExecuteComponents(components, component_count, context);
}

ExecutionContext LocalResponseController::CreateContext(const Event& event) const {
    ExecutionContext context;
    
    // 设置事件信息
    context.event = event;
    context.device_state = Application::GetInstance().GetDeviceState();
    
    // 获取当前情感状态
    auto& emotion_engine = EmotionEngine::GetInstance();
    context.current_quadrant = emotion_engine.GetQuadrant();
    context.current_valence = emotion_engine.GetValence();
    context.current_arousal = emotion_engine.GetArousal();
    
    // 设置硬件接口
    context.motion_skill = motion_skill_;
    context.vibration_skill = vibration_skill_;
    context.display = display_;
    
    return context;
}
```

#### 3. 响应组件执行
```cpp
void ResponseComponent::Execute(const ExecutionContext& context) const {
    switch (type) {
        case ComponentType::VIBRATION:
            if (context.vibration_skill) {
                context.vibration_skill->Play(data.vibration_pattern);
                ESP_LOGI(TAG, "Executed vibration pattern: %d", static_cast<int>(data.vibration_pattern));
            }
            break;
        case ComponentType::MOTION:
            if (context.motion_skill) {
                context.motion_skill->Perform(data.motion_id);
                ESP_LOGI(TAG, "Executed motion: %d", static_cast<int>(data.motion_id));
            }
            break;
        case ComponentType::EMOTION:
            if (context.display) {
                context.display->SetEmotion(data.emotion.emotion_name);
                ESP_LOGI(TAG, "Set emotion: %s for %u ms", data.emotion.emotion_name, data.emotion.duration_ms);
            }
            break;
    }
}
```

#### 4. SD卡配置驱动的扩展方案
```cpp
// 扩展现有系统以支持SD卡配置
class EnhancedLocalResponseController : public LocalResponseController {
private:
    std::map<std::string, EventConfig> sd_configs_;
    
public:
    void ProcessEvent(const Event& event) override {
        // 1. 尝试从SD卡加载配置
        std::string config_key = GetEventConfigKey(event);
        if (sd_configs_.find(config_key) != sd_configs_.end()) {
            ExecuteFromSDConfig(event, sd_configs_[config_key]);
            return;
        }
        
        // 2. 降级到固件内置响应
        LocalResponseController::ProcessEvent(event);
    }
    
private:
    std::string GetEventConfigKey(const Event& event) {
        EmotionQuadrant quadrant = EmotionEngine::GetInstance().GetQuadrant();
        return fmt::format("{}_q{}", 
                          EventTypeToString(event.type),
                          static_cast<int>(quadrant) + 1);
    }
    
    void ExecuteFromSDConfig(const Event& event, const EventConfig& config) {
        ExecutionContext context = CreateContext(event);
        
        // 检查状态锁
        if (context.device_state == kDeviceStateSpeaking) {
            if (!config.animation.enabled) config.animation.enabled = false;
            if (!config.sound.enabled) config.sound.enabled = false;
        }
        
        // 执行SD卡配置的响应
        if (config.animation.enabled) {
            PlayAnimationFromSD(config.animation.path, config.animation);
        }
        if (config.sound.enabled) {
            PlaySoundFromSD(config.sound.path, config.sound);
        }
        if (config.vibration.enabled) {
            vibration_skill_->Play(config.vibration.pattern);
        }
        if (config.motion.enabled) {
            motion_skill_->Perform(config.motion.action);
        }
    }
};
```

#### 5. 完整的事件流水线
```
传感器检测 → EventEngine → EventProcessor → LocalResponseController
     ↓              ↓              ↓                    ↓
   硬件事件      事件转换       策略过滤           响应执行
                                 ↓                    ↓
                            批量上传队列         硬件控制接口
                                 ↓                    ↓
                            EventUploader      振动/动作/显示
```

这个流程既保持了现有架构的稳定性，又为SD卡配置驱动的扩展预留了接口。

这个设计提供了完整的事件驱动响应架构，支持动画播放控制、状态锁机制、分级加载等高级功能，完美契合你的四层响应系统需求。