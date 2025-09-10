# 小智AI玩具资源结构设计 (最终版)

基于功能分类的四层响应系统资源组织架构。

## 目录结构

```
/sdcard/
├── response_config.json           # 统一配置文件 (所有事件参数)
├── manifest.json                  # 资源清单和版本信息
│
├── emergency/                     # 紧急响应层
│   ├── motion_free_fall/
│   │   ├── animation/
│   │   │   ├── 001.bin
│   │   │   ├── 002.bin
│   │   │   └── ...
│   │   └── sound.p3
│   ├── motion_shake_violently/
│   │   ├── animation/
│   │   └── sound.p3
│   ├── motion_flip/
│   │   ├── animation/
│   │   └── sound.p3
│   └── motion_upside_down/
│       ├── animation/
│       └── sound.p3
│
├── interaction/                   # 常规交互层
│   ├── motion_shake_q1/
│   │   ├── animation/
│   │   └── sound.p3
│   ├── motion_shake_q2/
│   │   ├── animation/
│   │   └── sound.p3
│   ├── motion_shake_q3/
│   │   ├── animation/
│   │   └── sound.p3
│   ├── motion_shake_q4/
│   │   ├── animation/
│   │   └── sound.p3
│   ├── motion_pickup_q1/
│   │   ├── animation/
│   │   └── sound.p3
│   ├── motion_pickup_q2/
│   ├── motion_pickup_q3/
│   ├── motion_pickup_q4/
│   ├── touch_tap_q1/
│   ├── touch_tap_q2/
│   ├── touch_tap_q3/
│   ├── touch_tap_q4/
│   ├── touch_long_press_q1/
│   ├── touch_long_press_q2/
│   ├── touch_long_press_q3/
│   ├── touch_long_press_q4/
│   ├── touch_cradled_q1/
│   ├── touch_cradled_q2/
│   ├── touch_cradled_q3/
│   ├── touch_cradled_q4/
│   ├── touch_tickled_q1/
│   ├── touch_tickled_q2/
│   ├── touch_tickled_q3/
│   └── touch_tickled_q4/
│
├── state_expression/              # 状态表情层
│   ├── speaking/                 # Speaking状态
│   │   ├── talk_calm/
│   │   │   └── animation/      # 说话时仅播放动画
│   │   ├── talk_happy/
│   │   │   └── animation/
│   │   ├── talk_sad/
│   │   │   └── animation/
│   │   ├── talk_angry/
│   │   │   └── animation/
│   │   ├── talk_scared/
│   │   │   └── animation/
│   │   ├── talk_curious/
│   │   │   └── animation/
│   │   ├── talk_shy/
│   │   │   └── animation/
│   │   └── talk_content/
│   │       └── animation/
│   │
│   ├── idle/                     # Idle状态
│   │   ├── idle_q1/
│   │   │   ├── animation/
│   │   │   └── sound.p3        # 可选的待机音效
│   │   ├── idle_q2/
│   │   │   ├── animation/
│   │   │   └── sound.p3
│   │   ├── idle_q3/
│   │   │   ├── animation/
│   │   │   └── sound.p3
│   │   └── idle_q4/
│   │       ├── animation/
│   │       └── sound.p3
│   │
│   └── listening/                # Listening状态
│       ├── listening_q1/
│       │   └── animation/
│       ├── listening_q2/
│       │   └── animation/
│       ├── listening_q3/
│       │   └── animation/
│       └── listening_q4/
│           └── animation/
│
└── system/                       # 系统状态层
    ├── boot_up/
    │   ├── animation/
    │   └── sound.p3
    ├── shut_down/
    │   ├── animation/
    │   └── sound.p3
    ├── charging/
    │   ├── animation/
    │   └── sound.p3
    ├── low_battery/
    │   ├── animation/
    │   └── sound.p3
    ├── connecting/
    │   ├── animation/
    │   └── sound.p3
    ├── error/
    │   ├── animation/
    │   └── sound.p3
    ├── photo_taken/
    │   ├── animation/
    │   └── sound.p3
    ├── body_detached/
    │   ├── animation/
    │   └── sound.p3
    └── body_attached/
        ├── animation/
        └── sound.p3
```

## 配置文件

### 事件检测参数 (event_config.json)

```json
{
  "touch_detection_parameters": {
    "tap_max_duration_ms": 500,
    "hold_min_duration_ms": 600,
    "cradled_min_duration_ms": 2000,
    "tickled_window_ms": 2000,
    "tickled_min_touches": 4,
    "debounce_time_ms": 30,
    "touch_threshold_ratio": 1.5
  },
  
  "motion_detection_parameters": {
    "free_fall": {
      "threshold_g": 0.3,
      "min_duration_ms": 200
    },
    "shake": {
      "normal_threshold_g": 1.5,
      "violently_threshold_g": 3.0
    },
    "flip": {
      "threshold_deg_s": 400.0
    },
    "pickup": {
      "threshold_g": 0.15,
      "stable_threshold_g": 0.05,
      "stable_count": 5,
      "min_duration_ms": 300
    },
    "upside_down": {
      "threshold_g": -0.8,
      "stable_count": 10
    }
  },
  
    "touch_events": {
      "TOUCH_TAP": {
        "strategy": "MERGE",
        "merge_window_ms": 1500,
        "interval_ms": 500
      },
      "TOUCH_LONG_PRESS": {
        "strategy": "COOLDOWN",
        "interval_ms": 1000
      }
    },
    
    "motion_events": {
      "MOTION_SHAKE": {
        "strategy": "THROTTLE",
        "interval_ms": 2000
      },
      "MOTION_PICKUP": {
        "strategy": "DEBOUNCE",
        "interval_ms": 500
      },
      "MOTION_FREE_FALL": {
        "strategy": "IMMEDIATE",
        "allow_interrupt": true
      },
      "MOTION_SHAKE_VIOLENTLY": {
        "strategy": "IMMEDIATE",
        "allow_interrupt": true
      }
    },
    
    "default_strategy": {
      "strategy": "IMMEDIATE",
      "interval_ms": 0
    }
  },
  
  "event_upload_config": {
    "batch_upload_enabled": true,
    "batch_window_ms": 500,
    "max_batch_size": 10
  }
}
```

### 响应配置 (response_config.json)

```json
{
  "version": "1.0",
  
  "events": {
    "emergency/motion_free_fall": {
      "layer": 1,
      "priority": 100,
      "animation": { "frames": 6, "fps": 24, "loop": false, "count": 1 },
      "sound": { "volume": 95, "interrupt": true },
      "vibration": { "pattern": 0, "intensity": 90, "duration_ms": 2000 },
      "motion": { "action": 0, "duration_ms": 1800 }
    },
    "emergency/motion_shake_violently": {
      "layer": 1,
      "priority": 95,
      "animation": { "frames": 8, "fps": 30, "loop": false, "count": 1 },
      "sound": { "volume": 90, "interrupt": true },
      "vibration": { "pattern": 1, "intensity": 85, "duration_ms": 2500 }
    },
    "interaction/motion_shake_q1": {
      "layer": 2,
      "priority": 50,
      "animation": { "frames": 4, "fps": 15, "loop": true, "count": 2 },
      "sound": { "volume": 70, "interrupt": false },
      "vibration": { "pattern": 2, "intensity": 60, "duration_ms": 1000 }
    },
    "interaction/touch_tap_q1": {
      "layer": 2,
      "priority": 45,
      "animation": { "frames": 3, "fps": 20, "loop": false, "count": 1 },
      "sound": { "volume": 65, "interrupt": false },
      "vibration": { "pattern": 3, "intensity": 50, "duration_ms": 300 }
    },
    "state_expression/speaking/talk_happy": {
      "layer": 3,
      "priority": 30,
      "animation": { "frames": 5, "fps": 20, "loop": true, "count": -1 },
      "sound": { "enabled": false },
      "vibration": { "enabled": false }
    },
    "state_expression/idle/idle_q1": {
      "layer": 4,
      "priority": 20,
      "animation": { "frames": 3, "fps": 5, "loop": true, "count": -1 },
      "sound": { "enabled": false },
      "vibration": { "enabled": false }
    },
    "system/boot_up": {
      "layer": 0,
      "priority": 90,
      "animation": { "frames": 8, "fps": 30, "loop": false, "count": 1 },
      "sound": { "volume": 80, "interrupt": false },
      "vibration": { "pattern": 4, "intensity": 40, "duration_ms": 500 }
    }
  },
  
  "vibration_patterns": [
    { "name": "ERRATIC_STRONG", "keyframes": [[4000,100], [0,50], [3000,200]] },
    { "name": "DIZZY_WOBBLE", "keyframes": [[2000,100], [1000,100], [2000,100]] },
    { "name": "HAPPY_PULSE", "keyframes": [[2000,200], [0,100], [2000,200]] },
    { "name": "QUICK_TAP", "keyframes": [[2000,100]] },
    { "name": "BOOT_SEQUENCE", "keyframes": [[1000,100], [0,100], [2000,200]] }
  ],
  
  "motion_actions": [
    { "name": "STRUGGLE_TWIST", "servo_channel": 1, "sequence": [[45,100], [0,100], [-45,100]] }
  ]
}
```

## 代码实现

### 资源路径构建器
```cpp
class ResourcePathBuilder {
public:
    // 构建动画路径
    static std::string GetAnimationPath(const std::string& category, 
                                       const std::string& event_name, 
                                       int frame) {
        char path[256];
        snprintf(path, sizeof(path), "/sdcard/%s/%s/animation/%03d.bin", 
                category.c_str(), event_name.c_str(), frame);
        return std::string(path);
    }
    
    // 构建音效路径
    static std::string GetSoundPath(const std::string& category,
                                   const std::string& event_name) {
        char path[256];
        snprintf(path, sizeof(path), "/sdcard/%s/%s/sound.p3", 
                category.c_str(), event_name.c_str());
        return std::string(path);
    }
    
    // 从完整事件键解析分类和名称
    static void ParseEventKey(const std::string& event_key,
                            std::string& category,
                            std::string& event_name) {
        size_t pos = event_key.find('/');
        if (pos != std::string::npos) {
            category = event_key.substr(0, pos);
            event_name = event_key.substr(pos + 1);
            
            // 处理子分类 (如 state_expression/speaking/talk_happy)
            size_t sub_pos = event_name.find('/');
            if (sub_pos != std::string::npos) {
                category += "/" + event_name.substr(0, sub_pos);
                event_name = event_name.substr(sub_pos + 1);
            }
        }
    }
};
```

### 配置管理器
```cpp
// 检测参数配置
struct DetectionConfig {
    struct {
        uint16_t tap_max_duration_ms;
        uint16_t hold_min_duration_ms;
        uint16_t cradled_min_duration_ms;
        uint16_t tickled_window_ms;
        uint8_t tickled_min_touches;
        uint8_t debounce_time_ms;
        float touch_threshold_ratio;
    } touch;
    
    struct {
        float free_fall_threshold_g;
        uint16_t free_fall_duration_ms;
        float shake_normal_g;
        float shake_violent_g;
        float flip_threshold_deg_s;
        float pickup_threshold_g;
        float pickup_stable_g;
        uint8_t pickup_stable_count;
        float upside_down_threshold_g;
        uint8_t upside_down_stable_count;
    } motion;
};

// 响应配置
struct ResponseConfig {
    uint8_t layer;
    uint8_t priority;
    
    struct {
        uint8_t frames;
        uint8_t fps;
        bool loop;
        int8_t count;  // -1 表示无限循环
    } animation;
    
    struct {
        bool enabled = true;
        uint8_t volume;
        bool interrupt;
    } sound;
    
    struct {
        bool enabled = true;
        uint8_t pattern_id;
        uint8_t intensity;
        uint16_t duration_ms;
    } vibration;
    
    struct {
        bool enabled = false;
        uint8_t action_id;
        uint16_t duration_ms;
    } motion;
};

class ConfigManager {
private:
    DetectionConfig detection_config_;
    std::map<std::string, ResponseConfig> response_configs_;
    std::vector<VibrationPattern> vibration_patterns_;
    std::vector<MotionAction> motion_actions_;
    
public:
    void LoadConfigs() {
        LoadDetectionConfig();
        LoadResponseConfig();
    }
    
private:
    void LoadDetectionConfig() {
        FILE* f = fopen("/sdcard/event_config.json", "r");
        if (!f) {
            ESP_LOGE(TAG, "Failed to open event_config.json");
            return;
        }
        
        // 解析检测参数
        // cJSON* json = cJSON_Parse(buffer);
        // ...
        
        fclose(f);
        ESP_LOGI(TAG, "Detection config loaded");
    }
    
    void LoadResponseConfig() {
        FILE* f = fopen("/sdcard/response_config.json", "r");
        if (!f) {
            ESP_LOGE(TAG, "Failed to open response_config.json");
            return;
        }
        
        // 解析响应配置
        // ...
        
        fclose(f);
        ESP_LOGI(TAG, "Loaded %d response configs", response_configs_.size());
    }

public:
    const DetectionConfig& GetDetectionConfig() const {
        return detection_config_;
    }
    
    const ResponseConfig* GetResponseConfig(const std::string& event_key) const {
        auto it = response_configs_.find(event_key);
        return (it != response_configs_.end()) ? &it->second : nullptr;
    }
    
    // 根据事件类型和象限构建事件键
    static std::string BuildEventKey(EventType type, int quadrant = -1) {
        std::string key;
        
        // 判断事件分类
        if (IsEmergencyEvent(type)) {
            key = "emergency/";
        } else if (IsInteractionEvent(type)) {
            key = "interaction/";
        } else if (IsSystemEvent(type)) {
            key = "system/";
        } else {
            key = "state_expression/";
        }
        
        // 添加事件名称
        key += GetEventBaseName(type);
        
        // 如果是象限相关事件，添加象限后缀
        if (IsQuadrantBasedEvent(type) && quadrant > 0 && quadrant <= 4) {
            key += "_q" + std::to_string(quadrant);
        }
        
        return key;
    }
};
```

### 响应执行器
```cpp
class ResponseExecutor {
private:
    ConfigManager* config_mgr_;
    ResourceLoader* loader_;
    AnimaDisplay* display_;
    AudioPlayer* audio_;
    VibrationSkill* vibration_;
    
public:
    void ExecuteEvent(EventType type, int quadrant = -1) {
        // 构建事件键
        std::string event_key = ConfigManager::BuildEventKey(type, quadrant);
        
        // 获取响应配置
        const auto* config = config_mgr_->GetResponseConfig(event_key);
        if (!config) {
            ESP_LOGW(TAG, "Response config not found: %s", event_key.c_str());
            return;
        }
        
        // 解析分类和事件名
        std::string category, event_name;
        ResourcePathBuilder::ParseEventKey(event_key, category, event_name);
        
        ESP_LOGI(TAG, "Executing: %s (layer=%d, priority=%d)", 
                event_key.c_str(), config->layer, config->priority);
        
        // 执行响应组件
        ExecuteResponse(category, event_name, config);
    }
    
private:
    void ExecuteResponse(const std::string& category,
                        const std::string& event_name,
                        const ResponseConfig* config) {
        // 播放动画
        if (config->animation.frames > 0) {
            PlayAnimation(category, event_name, config);
        }
        
        // 播放音效
        if (config->sound.enabled) {
            PlaySound(category, event_name, config);
        }
        
        // 触发振动
        if (config->vibration.enabled) {
            PlayVibration(config);
        }
        
        // 执行动作
        if (config->motion.enabled) {
            PlayMotion(config);
        }
    }
    
    void PlayAnimation(const std::string& category,
                      const std::string& event_name,
                      const ResponseConfig* config) {
        for (int i = 1; i <= config->animation.frames; i++) {
            auto path = ResourcePathBuilder::GetAnimationPath(category, event_name, i);
            uint8_t* frame_buffer = heap_caps_malloc(153600, MALLOC_CAP_SPIRAM);
            if (loader_->LoadFrame(path, frame_buffer)) {
                display_->DrawImageOnCanvas(0, 0, 320, 240, frame_buffer);
            }
            heap_caps_free(frame_buffer);
            vTaskDelay(1000 / config->animation.fps / portTICK_PERIOD_MS);
        }
    }
    
    void PlaySound(const std::string& category,
                  const std::string& event_name,
                  const ResponseConfig* config) {
        auto path = ResourcePathBuilder::GetSoundPath(category, event_name);
        audio_->PlayFile(path, config->sound.volume);
    }
    
    void PlayVibration(const ResponseConfig* config) {
        vibration_->PlayPatternById(config->vibration.pattern_id,
                                   config->vibration.intensity);
    }
    
    void PlayMotion(const ResponseConfig* config) {
        // 执行伺服动作
        motion_->ExecuteActionById(config->motion.action_id);
    }
};
```

## 优势

1. **清晰的层级结构**: 四个功能分类一目了然
2. **逻辑分组**: 相似功能的事件在同一目录
3. **易于扩展**: 每个分类可独立添加新事件
4. **配置集中**: 单一配置文件便于管理
5. **路径规范**: 统一的路径命名规则

## 使用示例

```cpp
// 初始化
ConfigManager config_mgr;
ResourceLoader loader;
ResponseExecutor executor(&config_mgr, &loader);

// 启动时加载配置
config_mgr.LoadConfigs();  // 加载两个配置文件

// 紧急事件
executor.ExecuteEvent(MOTION_FREE_FALL);
// 路径: /sdcard/emergency/motion_free_fall/

// 常规交互（带象限）
executor.ExecuteEvent(MOTION_SHAKE, 1);
// 路径: /sdcard/interaction/motion_shake_q1/

// 系统状态
executor.ExecuteEvent(SYSTEM_BOOT_UP);
// 路径: /sdcard/system/boot_up/

// 状态表情（通过专门的接口）
executor.ExecuteStateExpression("speaking", "talk_happy");
executor.ExecuteStateExpression("idle", 2);  // idle_q2
```