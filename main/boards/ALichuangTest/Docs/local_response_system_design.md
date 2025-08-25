# 本地响应系统设计 - 配置驱动架构

## 1. 设计目标

避免硬编码，通过配置文件灵活定义第1/2层响应，支持：
- 事件到响应的映射可配置
- 响应内容组合可配置
- 象限特定的响应变体
- 动态加载和热更新

## 2. 核心架构

### 2.1 响应组件系统

```cpp
// 响应组件基类
class ResponseComponent {
public:
    virtual ~ResponseComponent() = default;
    virtual void Execute(const ExecutionContext& context) = 0;
    virtual bool CanExecute(DeviceState state) = 0;
    virtual uint32_t GetDurationMs() const = 0;
};

// 具体组件实现 - 基于项目已有的代码库
class SoundComponent : public ResponseComponent {
private:
    std::string sound_file_;
    float volume_;
    
public:
    SoundComponent(const std::string& file, float volume) 
        : sound_file_(file), volume_(volume) {}
    
    void Execute(const ExecutionContext& context) override {
        if (CanExecute(context.device_state)) {
            // TODO: 调用项目中的音频播放接口
            // AudioSystem::PlaySound(sound_file_, volume_);
        }
    }
    
    bool CanExecute(DeviceState state) override {
        // Speaking状态下屏蔽音效
        return state != DeviceState::kDeviceStateSpeaking;
    }
    
    uint32_t GetDurationMs() const override { return 300; }
};

class VibrationComponent : public ResponseComponent {
private:
    vibration_id_t pattern_;  // 使用项目中已有的振动ID枚举
    
public:
    VibrationComponent(vibration_id_t pattern) : pattern_(pattern) {}
        
    void Execute(const ExecutionContext& context) override {
        // 使用项目中已有的Vibration类
        if (context.vibration_skill) {
            context.vibration_skill->Play(pattern_);
        }
    }
    
    bool CanExecute(DeviceState state) override {
        // 震动在所有状态下都可执行
        return true;
    }
    
    uint32_t GetDurationMs() const override { 
        // 根据不同振动模式返回持续时间
        switch (pattern_) {
            case VIBRATION_SHORT_BUZZ: return 150;
            case VIBRATION_PURR_SHORT: return 300;
            case VIBRATION_GENTLE_HEARTBEAT: return 800;
            case VIBRATION_GIGGLE_PATTERN: return 1200;
            default: return 500;
        }
    }
};

class MotionComponent : public ResponseComponent {
private:
    motion_id_t motion_;  // 使用项目中已有的运动ID枚举
    
public:
    MotionComponent(motion_id_t motion) : motion_(motion) {}
    
    void Execute(const ExecutionContext& context) override {
        // 使用项目中已有的Motion类（旋转马达，不是舵机）
        if (context.motion_skill) {
            context.motion_skill->Perform(motion_);
        }
    }
    
    bool CanExecute(DeviceState state) override { return true; }
    uint32_t GetDurationMs() const override { 
        // 根据不同运动模式返回持续时间
        switch (motion_) {
            case MOTION_HAPPY_WIGGLE: return 800;
            case MOTION_SHAKE_HEAD: return 1000;
            case MOTION_EXCITED_JIGGLE: return 600;
            case MOTION_TICKLE_TWIST_DANCE: return 1500;
            default: return 500;
        }
    }
};

class AnimationComponent : public ResponseComponent {
private:
    std::string animation_name_;
    uint32_t duration_ms_;
    
public:
    AnimationComponent(const std::string& name, uint32_t duration)
        : animation_name_(name), duration_ms_(duration) {}
        
    void Execute(const ExecutionContext& context) override {
        if (CanExecute(context.device_state) && context.display) {
            // 使用项目中已有的Display接口
            // context.display->PlayAnimation(animation_name_, duration_ms_);
        }
    }
    
    bool CanExecute(DeviceState state) override {
        // Speaking状态下屏蔽动画
        return state != DeviceState::kDeviceStateSpeaking;
    }
    
    uint32_t GetDurationMs() const override { return duration_ms_; }
};
```

### 2.2 响应模板系统

```cpp
// 执行上下文 - 包含所有可能需要的接口
struct ExecutionContext {
    DeviceState device_state;
    Event event;
    EmotionalQuadrant current_quadrant;
    Vec2 current_va;
    
    // 项目中已有的技能接口
    Motion* motion_skill;
    Vibration* vibration_skill;
    Display* display;
};

// 响应模板定义
struct ResponseTemplate {
    std::string name;
    EventType trigger_event;
    int priority;  // 1=紧急, 2=象限相关
    int max_loops; // 循环次数：1=第1/2层，>1=第4层
    std::vector<std::unique_ptr<ResponseComponent>> components;
    
    // 象限特定变体（可选）
    std::map<EmotionalQuadrant, std::vector<std::unique_ptr<ResponseComponent>>> quadrant_variants;
};

// 响应执行器
class LocalResponseExecutor {
private:
    std::vector<std::unique_ptr<ResponseTemplate>> templates_;
    EmotionEngine* emotion_engine_;
    Motion* motion_skill_;
    Vibration* vibration_skill_;
    Display* display_;
    
public:
    LocalResponseExecutor(Motion* motion, Vibration* vibration, Display* display)
        : motion_skill_(motion), vibration_skill_(vibration), display_(display) {}
        
    void LoadResponseConfig(const std::string& config_path);
    void ProcessEvent(const Event& event, DeviceState device_state);
    
private:
    ResponseTemplate* FindTemplate(EventType event_type);
    std::vector<ResponseComponent*> GetComponents(
        ResponseTemplate* template_, 
        EmotionalQuadrant quadrant
    );
};
```

### 2.3 配置文件结构

```json
{
  "response_templates": [
    {
      "name": "free_fall_emergency",
      "event_type": "MOTION_FREE_FALL",
      "priority": 1,
      "max_loops": 1,
      "components": [
        {
          "type": "sound",
          "params": {
            "file": "scream.wav",
            "volume": 1.0
          }
        },
        {
          "type": "vibration", 
          "params": {
            "pattern": "VIBRATION_ERRATIC_STRONG"
          }
        },
        {
          "type": "animation",
          "params": {
            "name": "eyes_wide_open",
            "duration_ms": 800
          }
        },
        {
          "type": "motion",
          "params": {
            "action": "MOTION_STRUGGLE_TWIST"
          }
        }
      ]
    },
    {
      "name": "touch_tap_quadrant",
      "event_type": "TOUCH_TAP", 
      "priority": 2,
      "max_loops": 1,
      "base_components": [
        {
          "type": "vibration",
          "params": {
            "pattern": "VIBRATION_SHORT_BUZZ"
          }
        }
      ],
      "quadrant_variants": {
        "Q1_EXCITED_HAPPY": [
          {
            "type": "sound",
            "params": {
              "file": "happy_beep.wav",
              "volume": 0.8
            }
          },
          {
            "type": "animation", 
            "params": {
              "name": "eyes_sparkle",
              "duration_ms": 400
            }
          },
          {
            "type": "motion",
            "params": {
              "action": "MOTION_HAPPY_WIGGLE"
            }
          }
        ],
        "Q2_STRESSED_ANGRY": [
          {
            "type": "sound",
            "params": {
              "file": "alert_beep.wav", 
              "volume": 0.6
            }
          },
          {
            "type": "animation",
            "params": {
              "name": "eyes_narrow",
              "duration_ms": 300
            }
          },
          {
            "type": "motion",
            "params": {
              "action": "MOTION_DODGE_SUBTLE"
            }
          }
        ],
        "Q3_SAD_BORED": [
          {
            "type": "sound",
            "params": {
              "file": "sigh.wav",
              "volume": 0.4
            }
          },
          {
            "type": "animation",
            "params": {
              "name": "eyes_droop",
              "duration_ms": 600
            }
          },
          {
            "type": "motion", 
            "params": {
              "action": "MOTION_SLOW_TURN_LEFT"
            }
          }
        ],
        "Q4_CONTENT_CALM": [
          {
            "type": "sound",
            "params": {
              "file": "content_hum.wav",
              "volume": 0.5
            }
          },
          {
            "type": "animation",
            "params": {
              "name": "eyes_slow_blink", 
              "duration_ms": 500
            }
          },
          {
            "type": "motion",
            "params": {
              "action": "MOTION_NUZZLE_FORWARD"
            }
          }
        ]
      }
    },
    {
      "name": "idle_quadrant_loop",
      "event_type": "IDLE_TIMEOUT",
      "priority": 4,
      "max_loops": 3,
      "quadrant_variants": {
        "Q1_EXCITED_HAPPY": [
          {
            "type": "motion",
            "params": {
              "action": "MOTION_EXCITED_JIGGLE"
            }
          },
          {
            "type": "vibration",
            "params": {
              "pattern": "VIBRATION_GIGGLE_PATTERN"
            }
          }
        ],
        "Q2_STRESSED_ANGRY": [
          {
            "type": "motion",
            "params": {
              "action": "MOTION_BODY_SHIVER"
            }
          },
          {
            "type": "vibration",
            "params": {
              "pattern": "VIBRATION_TREMBLE_PATTERN"
            }
          }
        ],
        "Q3_SAD_BORED": [
          {
            "type": "motion",
            "params": {
              "action": "MOTION_RELAX_COMPLETELY"
            }
          },
          {
            "type": "vibration",
            "params": {
              "pattern": "VIBRATION_GENTLE_HEARTBEAT"
            }
          }
        ],
        "Q4_CONTENT_CALM": [
          {
            "type": "motion",
            "params": {
              "action": "MOTION_RELAX_TO_CENTER"
            }
          },
          {
            "type": "vibration",
            "params": {
              "pattern": "VIBRATION_PURR_PATTERN"
            }
          }
        ]
      }
    }
  ],
  "component_library": {
    "vibration_patterns": [
      "VIBRATION_SHORT_BUZZ",
      "VIBRATION_PURR_SHORT", 
      "VIBRATION_PURR_PATTERN",
      "VIBRATION_GENTLE_HEARTBEAT",
      "VIBRATION_STRUGGLE_PATTERN",
      "VIBRATION_SHARP_BUZZ",
      "VIBRATION_TREMBLE_PATTERN",
      "VIBRATION_GIGGLE_PATTERN",
      "VIBRATION_HEARTBEAT_STRONG",
      "VIBRATION_ERRATIC_STRONG"
    ],
    "motion_patterns": [
      "MOTION_HAPPY_WIGGLE",
      "MOTION_SHAKE_HEAD",
      "MOTION_DODGE_SUBTLE",
      "MOTION_NUZZLE_FORWARD",
      "MOTION_TENSE_UP",
      "MOTION_DODGE_SLOWLY",
      "MOTION_QUICK_TURN_LEFT",
      "MOTION_QUICK_TURN_RIGHT",
      "MOTION_CURIOUS_PEEK_LEFT",
      "MOTION_CURIOUS_PEEK_RIGHT",
      "MOTION_SLOW_TURN_LEFT",
      "MOTION_SLOW_TURN_RIGHT",
      "MOTION_DODGE_OPPOSITE_LEFT",
      "MOTION_DODGE_OPPOSITE_RIGHT",
      "MOTION_BODY_SHIVER",
      "MOTION_EXCITED_JIGGLE",
      "MOTION_RELAX_COMPLETELY",
      "MOTION_TICKLE_TWIST_DANCE",
      "MOTION_ANNOYED_TWIST_TO_HAPPY",
      "MOTION_STRUGGLE_TWIST",
      "MOTION_UNWILLING_TURN_BACK",
      "MOTION_RELAX_TO_CENTER"
    ]
  }
}
```

## 3. 实现细节

### 3.1 配置加载器

```cpp
class ReactionConfigLoader {
public:
    static std::vector<std::unique_ptr<ReactionTemplate>> LoadTemplates(
        const std::string& config_path
    ) {
        std::vector<std::unique_ptr<ReactionTemplate>> templates;
        
        // 读取JSON配置
        std::string json_content = ReadFile(config_path);
        cJSON* root = cJSON_Parse(json_content.c_str());
        
        cJSON* templates_array = cJSON_GetObjectItem(root, "reaction_templates");
        cJSON* template_json;
        cJSON_ArrayForEach(template_json, templates_array) {
            auto template_obj = ParseTemplate(template_json);
            templates.push_back(std::move(template_obj));
        }
        
        cJSON_Delete(root);
        return templates;
    }
    
private:
    static std::unique_ptr<ReactionTemplate> ParseTemplate(cJSON* json) {
        auto template_obj = std::make_unique<ReactionTemplate>();
        
        template_obj->name = cJSON_GetObjectItem(json, "name")->valuestring;
        template_obj->trigger_event = ParseEventType(
            cJSON_GetObjectItem(json, "event_type")->valuestring
        );
        template_obj->priority = cJSON_GetObjectItem(json, "priority")->valueint;
        
        // 解析基础组件
        cJSON* components = cJSON_GetObjectItem(json, "components");
        if (components) {
            template_obj->components = ParseComponents(components);
        }
        
        // 解析象限变体
        cJSON* variants = cJSON_GetObjectItem(json, "quadrant_variants");
        if (variants) {
            template_obj->quadrant_variants = ParseQuadrantVariants(variants);
        }
        
        return template_obj;
    }
    
    static std::vector<std::unique_ptr<ReactionComponent>> ParseComponents(cJSON* components) {
        std::vector<std::unique_ptr<ReactionComponent>> result;
        
        cJSON* component;
        cJSON_ArrayForEach(component, components) {
            std::string type = cJSON_GetObjectItem(component, "type")->valuestring;
            cJSON* params = cJSON_GetObjectItem(component, "params");
            
            if (type == "sound") {
                std::string file = cJSON_GetObjectItem(params, "file")->valuestring;
                float volume = cJSON_GetObjectItem(params, "volume")->valuedouble;
                result.push_back(std::make_unique<SoundComponent>(file, volume));
            }
            else if (type == "vibration") {
                std::string pattern_str = cJSON_GetObjectItem(params, "pattern")->valuestring;
                uint32_t duration = cJSON_GetObjectItem(params, "duration_ms")->valueint;
                VibrationPattern pattern = ParseVibrationPattern(pattern_str);
                result.push_back(std::make_unique<VibrationComponent>(pattern, duration));
            }
            // ... 其他组件类型解析
        }
        
        return result;
    }
};
```

### 3.2 反应执行逻辑

```cpp
void LocalReactionExecutor::ProcessEvent(const Event& event, DeviceState device_state) {
    // 查找匹配的模板
    ReactionTemplate* template_ = FindTemplate(event.type);
    if (!template_) return;
    
    ExecutionContext context{
        .device_state = device_state,
        .event = event,
        .current_quadrant = emotion_engine_->GetQuadrant(),
        .current_va = emotion_engine_->GetVA()
    };
    
    // 获取要执行的组件列表
    std::vector<ReactionComponent*> components = GetComponents(
        template_, 
        context.current_quadrant
    );
    
    // 并行执行所有组件
    for (auto* component : components) {
        if (component->CanExecute(device_state)) {
            component->Execute(context);
        }
    }
    
    ESP_LOGI("LocalReaction", "Executed reaction '%s' for event %d in quadrant %d",
             template_->name.c_str(), static_cast<int>(event.type), 
             static_cast<int>(context.current_quadrant));
}

std::vector<ReactionComponent*> LocalReactionExecutor::GetComponents(
    ReactionTemplate* template_, 
    EmotionalQuadrant quadrant
) {
    std::vector<ReactionComponent*> result;
    
    // 添加基础组件
    for (auto& component : template_->components) {
        result.push_back(component.get());
    }
    
    // 添加象限特定组件
    auto it = template_->quadrant_variants.find(quadrant);
    if (it != template_->quadrant_variants.end()) {
        for (auto& component : it->second) {
            result.push_back(component.get());
        }
    }
    
    return result;
}
```

### 3.3 热更新支持

```cpp
class ReactionSystemManager {
private:
    LocalReactionExecutor executor_;
    std::string config_path_;
    time_t last_config_mtime_;
    
public:
    void Initialize(const std::string& config_path) {
        config_path_ = config_path;
        ReloadConfig();
        
        // 启动配置文件监控
        StartConfigMonitor();
    }
    
    void CheckConfigUpdate() {
        struct stat file_stat;
        if (stat(config_path_.c_str(), &file_stat) == 0) {
            if (file_stat.st_mtime > last_config_mtime_) {
                ESP_LOGI("ReactionSystem", "Config file updated, reloading...");
                ReloadConfig();
            }
        }
    }
    
private:
    void ReloadConfig() {
        try {
            executor_.LoadReactionConfig(config_path_);
            
            struct stat file_stat;
            stat(config_path_.c_str(), &file_stat);
            last_config_mtime_ = file_stat.st_mtime;
            
            ESP_LOGI("ReactionSystem", "Config reloaded successfully");
        } catch (const std::exception& e) {
            ESP_LOGE("ReactionSystem", "Failed to reload config: %s", e.what());
        }
    }
};
```

## 4. 使用示例

### 4.1 在ALichuangTest中集成

```cpp
// 在ALichuangTest.cc中
class ALichuangTest : public WifiBoard {
private:
    std::unique_ptr<ReactionSystemManager> reaction_manager_;
    
public:
    ALichuangTest() {
        // 初始化反应系统
        reaction_manager_ = std::make_unique<ReactionSystemManager>();
        reaction_manager_->Initialize("/spiffs/reaction_config.json");
        
        // 注册事件回调
        if (event_engine_) {
            event_engine_->RegisterCallback([this](const Event& event) {
                DeviceState state = Application::GetInstance().GetDeviceState();
                reaction_manager_->GetExecutor().ProcessEvent(event, state);
            });
        }
    }
    
    void Update() override {
        WifiBoard::Update();
        
        // 检查配置更新（每5秒检查一次）
        static uint32_t last_check = 0;
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - last_check > 5000) {
            reaction_manager_->CheckConfigUpdate();
            last_check = now;
        }
    }
};
```

### 4.2 调试和测试

```cpp
// 调试工具
class ReactionDebugger {
public:
    static void TestReaction(EventType event_type, EmotionalQuadrant quadrant) {
        Event test_event;
        test_event.type = event_type;
        test_event.timestamp_us = esp_timer_get_time();
        
        // 模拟情感状态
        EmotionEngine dummy_engine;
        dummy_engine.SetVA(GetQuadrantVA(quadrant));
        
        LocalReactionExecutor executor;
        executor.LoadReactionConfig("/spiffs/reaction_config.json");
        executor.ProcessEvent(test_event, DeviceState::kDeviceStateIdle);
    }
    
    static void ListAllReactions() {
        // 输出所有已加载的反应模板
        ESP_LOGI("Debug", "=== Loaded Reaction Templates ===");
        // ... 实现
    }
};
```

## 5. 优势总结

### 5.1 灵活性
- **零代码修改**：修改反应只需编辑JSON配置
- **动态组合**：支持组件的灵活组合
- **象限适配**：同一事件在不同象限有不同反应

### 5.2 可维护性
- **模块化设计**：每个组件职责单一
- **配置集中**：所有反应逻辑集中在配置文件
- **热更新**：无需重启即可更新反应

### 5.3 可扩展性
- **组件扩展**：轻松添加新的反应组件类型
- **模板扩展**：支持复杂的反应模板
- **调试友好**：提供调试和测试工具

### 5.4 性能
- **预编译**：配置在启动时解析，运行时高效
- **并行执行**：多个组件可并行执行
- **状态感知**：根据设备状态智能筛选组件

这样的架构完全避免了硬编码，同时保持了高性能和灵活性！