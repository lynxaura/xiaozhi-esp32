# 小智AI玩具资源结构设计 (最终版)

基于功能分类的四层响应系统资源组织架构。

## 目录结构

```
/sdcard/
├── config/                        # 配置集中目录
│   ├── event_config.json          # 事件检测和处理配置文件
│   ├── response_config.json       # 响应配置文件
│   ├── vasys_config.json          # VA 系统参数
│   └── vasys.txt
├── manifest.json                  # 资源清单和版本信息
│
├── emergency/                     # 紧急响应层
│   ├── motion_free_fall/
│   │   ├── animation/
│   │   │   ├── 001.bin
│   │   │   ├── 002.bin
│   │   │   └── ...
│   │   └── motion_free_fall.p3
│   ├── motion_shake_violently/
│   │   ├── animation/
│   │   └── motion_shake_violently.p3
│   ├── motion_flip/
│   │   ├── animation/
│   │   └── motion_flip.p3
│   └── motion_upside_down/
│       ├── animation/
│       └── motion_upside_down.p3
│
├── interaction/                   # 常规交互层
│   ├── motion_shake_q1/
│   │   ├── animation/
│   │   └── motion_shake_q1.p3
│   ├── motion_shake_q2/
│   │   ├── animation/
│   │   └── motion_shake_q2.p3
│   ├── motion_shake_q3/
│   │   ├── animation/
│   │   └── motion_shake_q3.p3
│   ├── motion_shake_q4/
│   │   ├── animation/
│   │   └── motion_shake_q4.p3
│   ├── motion_pickup_q1/
│   │   ├── animation/
│   │   └── motion_pickup_q1.p3
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
│   │   │   └── idle_q1.p3        # 可选的待机音效
│   │   ├── idle_q2/
│   │   │   ├── animation/
│   │   │   └── idle_q2.p3
│   │   ├── idle_q3/
│   │   │   ├── animation/
│   │   │   └── idle_q3.p3
│   │   └── idle_q4/
│   │       ├── animation/
│   │       └── idle_q4.p3
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
    │   └── boot_up.p3
    ├── shut_down/
    │   ├── animation/
    │   └── shut_down.p3
    ├── charging/
    │   ├── animation/
    │   └── charging.p3
    ├── low_battery/
    │   ├── animation/
    │   └── low_battery.p3
    ├── connecting/
    │   ├── animation/
    │   └── connecting.p3
    ├── error/
    │   ├── animation/
    │   └── error.p3
    ├── photo_taken/
    │   ├── animation/
    │   └── photo_taken.p3
    ├── body_detached/
    │   ├── animation/
    │   └── body_detached.p3
    └── body_attached/
        ├── animation/
        └── body_attached.p3
```

## 配置文件

### 资源清单 (manifest.json)

```json
{
  "version": "1.0.0",
  "format_version": "1.0",
  "created": "2024-01-15T10:00:00Z",
  "description": "小智AI玩具资源包",
  "compatibility": {
    "min_firmware_version": "2.1.0",
    "board_types": ["ALichuangTest"]
  },
  "animation_specs": {
    "format": "RGB565",
    "resolution": "320x240",
    "fps": 24,
    "encoding": "raw_binary"
  },
  "audio_specs": {
    "format": "P3",
    "sample_rate": 16000,
    "channels": 1,
    "encoding": "compressed"
  },
  "assets": {
    "total_events": 47,
    "total_size_mb": 125.6,
    "animation_total_frames": 234,
    "audio_total_files": 25
  },
  "layers": {
    "emergency": {
      "priority": 1,
      "interrupts_all": true,
      "events": ["motion_free_fall", "motion_shake_violently", "motion_flip", "motion_upside_down"]
    },
    "interaction": {
      "priority": 2,
      "quadrant_based": true,
      "events": ["motion_shake", "motion_pickup", "touch_tap", "touch_long_press", "touch_cradled", "touch_tickled"]
    },
    "state_expression": {
      "priority": 3,
      "context_dependent": true,
      "events": ["speaking", "idle", "listening"]
    },
    "system": {
      "priority": 0,
      "functional_only": true,
      "events": ["boot_up", "shut_down", "charging", "low_battery", "connecting", "error", "photo_taken", "body_detached", "body_attached"]
    }
  }
}
```

### 响应配置 (response_config.json)

可用的振动模式：
- 轻触反馈类：`VIBRATION_SHORT_BUZZ`、`VIBRATION_PURR_SHORT`
- 持续交互类：`VIBRATION_PURR_PATTERN`、`VIBRATION_GENTLE_HEARTBEAT`、`VIBRATION_HEARTBEAT_STRONG`
- 情绪表达类：`VIBRATION_STRUGGLE_PATTERN`、`VIBRATION_TREMBLE_PATTERN`、`VIBRATION_GIGGLE_PATTERN`
- 警示反应类：`VIBRATION_SHARP_BUZZ`、`VIBRATION_ERRATIC_STRONG`

可用的旋转动作：
- 情感表达类：`MOTION_HAPPY_WIGGLE`、`MOTION_SHAKE_HEAD`、`MOTION_EXCITED_JIGGLE`
- 社交互动类：`MOTION_NUZZLE_FORWARD`、`MOTION_CURIOUS_PEEK_LEFT`、`MOTION_CURIOUS_PEEK_RIGHT`、`MOTION_RELAX_COMPLETELY`
- 防御反应类：`MOTION_DODGE_SUBTLE`、`MOTION_DODGE_SLOWLY`、`MOTION_DODGE_OPPOSITE_LEFT`、`MOTION_DODGE_OPPOSITE_RIGHT`、`MOTION_TENSE_UP`、`MOTION_BODY_SHIVER`
- 复杂表演类：`MOTION_TICKLE_TWIST_DANCE`、`MOTION_ANNOYED_TWIST_TO_HAPPY`、`MOTION_STRUGGLE_TWIST`、`MOTION_UNWILLING_TURN_BACK`

中断机制说明：
- **can_interrupt**: 定义事件可以中断的状态列表
  - `["all"]`: 可以中断所有状态（紧急事件专用）
  - `["idle", "listening"]`: 可以中断空闲和监听状态（交互事件）
  - 不设置: 不能中断任何状态（状态表达事件）

```json
{
  "version": "1.0",

  "events": {
    "emergency/motion_free_fall": {
      "layer": 1,
      "can_interrupt": ["all"],
      "animation": { "frames": 6, "count": 2 },
      "sound": { "volume": 95 },
      "vibration": { "pattern": "VIBRATION_ERRATIC_STRONG" },
      "motion": { "action": "MOTION_STRUGGLE_TWIST" }
    },
    "emergency/motion_shake_violently": {
      "layer": 1,
      "can_interrupt": ["all"],
      "animation": { "frames": 6, "count": 2 },
      "sound": { "volume": 90 },
      "vibration": { "pattern": "VIBRATION_STRUGGLE_PATTERN" }
    },
    "interaction/motion_shake_q1": {
      "layer": 2,
      "can_interrupt": ["idle", "listening"],
      "animation": { "frames": 4, "count": 2 },
      "sound": { "volume": 70 },
      "vibration": { "pattern": "VIBRATION_PURR_SHORT" }
    },
    "interaction/touch_tap_q1": {
      "layer": 2,
      "can_interrupt": ["idle", "listening"],
      "animation": { "frames": 6, "count": 2 },
      "sound": { "volume": 65 },
      "vibration": { "pattern": "VIBRATION_SHORT_BUZZ" }
    },
    "state_expression/speaking/talk_happy": {
      "layer": 3,
      "animation": { "frames": 5, "count": -1 },
      "sound": { "enabled": false },
      "vibration": { "enabled": false }
    },
    "state_expression/idle/idle_q1": {
      "layer": 4,
      "animation": { "frames": 5, "count": -1 },
      "sound": { "enabled": false },
      "vibration": { "enabled": false }
    },
    "state_expression/listening/listening_q1": {
      "layer": 5,
      "animation": { "frames": 4, "count": -1 },
      "sound": { "enabled": false },
      "vibration": { "enabled": false }
    },
    "system/boot_up": {
      "layer": 0,
      "can_interrupt": ["all"],
      "animation": { "frames": 6, "count": 1 },
      "sound": { "volume": 80 },
      "vibration": { "pattern": "VIBRATION_SHORT_BUZZ" }
    }
  }
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
        snprintf(path, sizeof(path), "/sdcard/%s/%s/%s.p3", 
                category.c_str(), event_name.c_str(), event_name.c_str());
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

// 响应配置
struct ResponseConfig {
    uint8_t layer;
    std::vector<std::string> can_interrupt;  // 可以中断的状态列表

    struct {
        uint8_t frames;  // 动画帧数
        int8_t count;    // -1 表示无限循环, 正数表示播放次数
    } animation;

    struct {
        bool enabled = true;
        uint8_t volume;
    } sound;

    struct {
        bool enabled = true;
        std::string pattern_name;
    } vibration;

    struct {
        bool enabled = false;
        std::string action_name;
    } motion;
};

class ConfigManager {
private:
    std::map<std::string, ResponseConfig> response_configs_;
    
public:
    void LoadConfigs() {
        LoadResponseConfig();
    }
    
private:
    void LoadResponseConfig() {
        FILE* f = fopen("/sdcard/config/response_config.json", "r");
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
    const ResponseConfig* GetResponseConfig(const std::string& event_key) const {
        auto it = response_configs_.find(event_key);
        return (it != response_configs_.end()) ? &it->second : nullptr;
    }

    // 检查事件是否可以中断当前状态
    bool CanInterrupt(const std::string& event_key, const std::string& current_state) const {
        auto it = response_configs_.find(event_key);
        if (it == response_configs_.end()) {
            return false;
        }

        const auto& can_interrupt = it->second.can_interrupt;

        // 检查是否可以中断所有状态
        if (std::find(can_interrupt.begin(), can_interrupt.end(), "all") != can_interrupt.end()) {
            return true;
        }

        // 检查是否可以中断特定状态
        return std::find(can_interrupt.begin(), can_interrupt.end(), current_state) != can_interrupt.end();
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
    std::string current_state_ = "idle";  // 当前设备状态

public:
    void SetCurrentState(const std::string& state) {
        current_state_ = state;
    }

    void ExecuteEvent(EventType type, int quadrant = -1) {
        // 构建事件键
        std::string event_key = ConfigManager::BuildEventKey(type, quadrant);

        // 检查是否可以中断当前状态
        if (!config_mgr_->CanInterrupt(event_key, current_state_)) {
            ESP_LOGI(TAG, "Event %s cannot interrupt current state %s",
                    event_key.c_str(), current_state_.c_str());
            return;
        }

        // 获取响应配置
        const auto* config = config_mgr_->GetResponseConfig(event_key);
        if (!config) {
            ESP_LOGW(TAG, "Response config not found: %s", event_key.c_str());
            return;
        }

        // 解析分类和事件名
        std::string category, event_name;
        ResourcePathBuilder::ParseEventKey(event_key, category, event_name);

        ESP_LOGI(TAG, "Executing: %s (layer=%d, interrupting state=%s)",
                event_key.c_str(), config->layer, current_state_.c_str());

        // 执行响应组件
        ExecuteResponse(category, event_name, config);
    }
    
private:
    void ExecuteResponse(const std::string& category,
                        const std::string& event_name,
                        const ResponseConfig* config) {
        // 播放动画
        PlayAnimation(category, event_name, config);
        
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
        // 根据配置的帧数播放动画
        for (int frame = 1; frame <= config->animation.frames; frame++) {
            auto path = ResourcePathBuilder::GetAnimationPath(category, event_name, frame);
            uint8_t* frame_buffer = heap_caps_malloc(153600, MALLOC_CAP_SPIRAM);
            if (!loader_->LoadFrame(path, frame_buffer)) {
                ESP_LOGW(TAG, "Failed to load frame %d for %s/%s", frame, category.c_str(), event_name.c_str());
                heap_caps_free(frame_buffer);
                break;
            }
            display_->DrawImageOnCanvas(0, 0, 320, 240, frame_buffer);
            heap_caps_free(frame_buffer);
            vTaskDelay(1000 / 24 / portTICK_PERIOD_MS);  // 固定24fps
        }
    }
    
    void PlaySound(const std::string& category,
                  const std::string& event_name,
                  const ResponseConfig* config) {
        auto path = ResourcePathBuilder::GetSoundPath(category, event_name);
        audio_->PlayFile(path, config->sound.volume);
    }
    
    void PlayVibration(const ResponseConfig* config) {
        vibration_->PlayPatternByName(config->vibration.pattern_name);
    }
    
    void PlayMotion(const ResponseConfig* config) {
        // 执行伺服动作
        motion_->ExecuteActionByName(config->motion.action_name);
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
config_mgr.LoadConfigs();  // 加载响应配置文件

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
