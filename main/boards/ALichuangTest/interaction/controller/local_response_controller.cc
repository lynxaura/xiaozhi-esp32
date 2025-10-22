#include "local_response_controller.h"
#include <esp_log.h>
#include <cstring>
#include "../../sddata_pro.h"

static const char* TAG = "LocalResponse";

// ==================== ResponseComponent 实现 ====================

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
        case ComponentType::ANIMATION:
            if (context.display) {
                ESP_LOGI(TAG, "[DEBUG] About to set animation: ptr=%p, name='%s', loop=%d",
                         (void*)data.animation.animation_name,
                         data.animation.animation_name,
                         data.animation.loop_count);
                context.display->SetAnima(data.animation.animation_name, data.animation.loop_count);
                ESP_LOGI(TAG, "Set animation: %s with loop_count=%d",
                         data.animation.animation_name, data.animation.loop_count);
            }
            break;
        case ComponentType::AUDIO:
            ESP_LOGI(TAG, "[DEBUG] About to play audio: ptr=%p, name='%s', volume=%d",
                     (void*)data.audio.audio_name,
                     data.audio.audio_name,
                     data.audio.volume);
            Application::GetInstance().PlaySoundOGGFile(data.audio.audio_name, data.audio.volume);
            ESP_LOGI(TAG, "Playing audio: %s with volume=%d",
                     data.audio.audio_name, data.audio.volume);
            break;
    }
}

bool ResponseComponent::CanExecute(DeviceState state) const {
    // 简化的状态检查
    return state != kDeviceStateFatalError && state != kDeviceStateUpgrading;
}

const char* ResponseComponent::GetTypeName() const {
    switch (type) {
        case ComponentType::VIBRATION: return "Vibration";
        case ComponentType::MOTION: return "Motion";
        case ComponentType::ANIMATION: return "Animation";
        case ComponentType::AUDIO: return "Audio";
        default: return "Unknown";
    }
}

// ResponseTemplate 实现
ResponseTemplate::ResponseTemplate(const char* name, EventType event, int priority) 
    : name(name), trigger_event(event), priority(priority) {
}

void ResponseTemplate::AddBaseComponent(const ResponseComponent& component) {
    if (base_component_count < MAX_BASE_COMPONENTS) {
        base_components[base_component_count++] = component;
    } else {
        ESP_LOGW(TAG, "Base components full for template: %s", name ? name : "(null)");
    }
}

void ResponseTemplate::AddQuadrantComponent(EmotionQuadrant quadrant, const ResponseComponent& component) {
    int idx = static_cast<int>(quadrant);
    if (idx >= 0 && idx < 4 && quadrant_variants[idx].count < MAX_QUADRANT_COMPONENTS) {
        quadrant_variants[idx].components[quadrant_variants[idx].count++] = component;
    } else {
        ESP_LOGW(TAG, "Quadrant components full for template: %s, quadrant=%d", name ? name : "(null)", idx);
    }
}

bool ResponseTemplate::CanInterrupt(DeviceState current_state) const {
    // 如果标记为可以打断所有状态，直接返回true
    if (can_interrupt_all) {
        return true;
    }

    // 如果没有设置任何can_interrupt限制，默认允许执行（向后兼容）
    if (can_interrupt_count == 0) {
        return true;
    }

    // 检查当前状态是否在允许列表中
    for (size_t i = 0; i < can_interrupt_count; ++i) {
        if (can_interrupt_states[i] == current_state) {
            return true;
        }
    }

    return false;
}

void ResponseTemplate::GetComponents(EmotionQuadrant quadrant,
                                     ResponseComponent** out, size_t* out_count) const {
    size_t k = 0;
    // 先拷贝基础组件
    for (size_t i = 0; i < base_component_count; ++i) {
        out[k++] = const_cast<ResponseComponent*>(&base_components[i]);
    }
    // 再拷贝象限组件
    const int qidx = static_cast<int>(quadrant);
    if (qidx >= 0 && qidx < 4) {
        const auto& qv = quadrant_variants[qidx];
        for (size_t j = 0; j < qv.count; ++j) {
            out[k++] = const_cast<ResponseComponent*>(&qv.components[j]);
        }
    }
    *out_count = k;
}

// ==================== LocalResponseController 实现 ====================

LocalResponseController::LocalResponseController(
    Motion* motion_skill,
    Vibration* vibration_skill,
    Display* display)
    : motion_skill_(motion_skill)
    , vibration_skill_(vibration_skill)
    , display_(display)
    , name_pool_index_(0)
    , initialized_(false) {
}

LocalResponseController::~LocalResponseController() {
}

bool LocalResponseController::Initialize() {
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }
    
    ESP_LOGI(TAG, "Initializing Local Response Controller...");
    
    // 加载默认配置
    if (!LoadDefaultConfig()) {
        ESP_LOGE(TAG, "Failed to load default configuration");
        return false;
    }
    
    initialized_ = true;
    ESP_LOGI(TAG, "✅ Local Response Controller initialized successfully");
    ESP_LOGI(TAG, "Loaded %u response templates", template_count_);
    
    return true;
}

void LocalResponseController::ProcessEvent(const Event& event) {
    if (!initialized_) {
        ESP_LOGW(TAG, "Controller not initialized, ignoring event");
        return;
    }

    ResponseTemplate* tmpl = FindTemplate(event.type);
    if (!tmpl) {
        ESP_LOGD(TAG, "No response template found for event type: %d", static_cast<int>(event.type));
        return;
    }

    ExecutionContext context = CreateContext(event);

    // 检查是否可以在当前设备状态下执行此响应
    if (!tmpl->CanInterrupt(context.device_state)) {
        ESP_LOGI(TAG, "⏸️  Event %d blocked: template '%s' cannot interrupt current state %d",
                 static_cast<int>(event.type),
                 tmpl->name ? tmpl->name : "(null)",
                 static_cast<int>(context.device_state));
        return;
    }

    // 紧急事件处理：如果可以打断所有状态，且当前设备正在speaking，先中止TTS输出
    bool was_speaking = false;
    if (tmpl->can_interrupt_all && context.device_state == kDeviceStateSpeaking) {
        ESP_LOGI(TAG, "🚨 Emergency event detected while speaking, aborting TTS output first");
        was_speaking = true;
        Application::GetInstance().AbortSpeaking(kAbortReasonNone);
        // 给系统一些时间处理中止请求
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // 收集要执行的组件（基础5 + 象限3 = 最多8个）
    constexpr size_t kMaxComponents = 8;
    ResponseComponent* components[kMaxComponents];
    size_t comp_cnt = 0;
    tmpl->GetComponents(context.current_quadrant, components, &comp_cnt);

    if (comp_cnt == 0) {
        ESP_LOGD(TAG, "No components to execute for template: %s", tmpl->name ? tmpl->name : "(null)");
        return;
    }

    ESP_LOGI(TAG, "🎯 Processing event %d with template '%s' in quadrant %d",
             static_cast<int>(event.type),
             tmpl->name ? tmpl->name : "(null)",
             static_cast<int>(context.current_quadrant));

    ExecuteComponents(components, comp_cnt, context);

    // 紧急事件响应完成后，如果之前在speaking状态，模拟唤醒词激活大模型
    // 让大模型基于已发送的紧急事件信息生成新的响应
    if (tmpl->can_interrupt_all && was_speaking) {
        ESP_LOGI(TAG, "🔄 Emergency response completed, simulating wake word to trigger LLM response");

        // 延迟一段时间，确保紧急响应的音频播放完成
        vTaskDelay(pdMS_TO_TICKS(500));

        // 使用 WakeWordInvoke 模拟唤醒词激活，让大模型响应紧急事件
        // 这会发送 SendWakeWordDetected + SetListeningMode，触发完整的激活流程
        Application::GetInstance().Schedule([event]() {
            // 构造一个表示紧急事件的虚拟唤醒词
            std::string emergency_trigger = "emergency_event";
            Application::GetInstance().WakeWordInvoke(emergency_trigger);
        });
    }
}

void LocalResponseController::ProcessStateChange(DeviceState new_state) {
    if (!initialized_) {
        ESP_LOGW(TAG, "Controller not initialized, ignoring state change");
        return;
    }

    // 只处理 idle 和 listening 状态
    if (new_state != kDeviceStateIdle && new_state != kDeviceStateListening) {
        return;
    }

    StateResponseTemplate* state_tmpl = FindStateTemplate(new_state);
    if (!state_tmpl) {
        ESP_LOGD(TAG, "No state response template found for state: %d", static_cast<int>(new_state));
        return;
    }

    // 获取当前情感象限
    auto& emotion_engine = EmotionEngine::GetInstance();
    EmotionQuadrant current_quadrant = emotion_engine.GetQuadrant();
    int quadrant_idx = static_cast<int>(current_quadrant);

    if (quadrant_idx < 0 || quadrant_idx >= 4 || !state_tmpl->has_response[quadrant_idx]) {
        ESP_LOGD(TAG, "No response configured for state %d in quadrant %d",
                 static_cast<int>(new_state), quadrant_idx);
        return;
    }

    // 创建执行上下文
    Event dummy_event;
    dummy_event.type = EventType::MOTION_NONE;
    dummy_event.timestamp_us = esp_timer_get_time();
    ExecutionContext context = CreateContext(dummy_event);

    // 执行响应组件
    ResponseComponent* component = &state_tmpl->quadrant_responses[quadrant_idx];
    ResponseComponent* components[1] = { component };

    ESP_LOGI(TAG, "🎭 Processing state change to %d in quadrant %d",
             static_cast<int>(new_state), quadrant_idx);

    ExecuteComponents(components, 1, context);
}

bool LocalResponseController::LoadDefaultConfig() {
    ESP_LOGI(TAG, "Loading response configuration from SD card...");

    try {
        CreateDefaultTemplatesFromSD();
        return template_count_ > 0;  // 至少需要加载一个模板才算成功
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception while loading templates: %s", e.what());
        return false;
    }
}

ResponseTemplate* LocalResponseController::FindTemplate(EventType event_type) {
    for (size_t i = 0; i < template_count_; i++) {
        if (templates_[i].trigger_event == event_type) {
            return &templates_[i];
        }
    }
    return nullptr;
}

ExecutionContext LocalResponseController::CreateContext(const Event& event) const {
    ExecutionContext context;
    
    // 设置事件信息
    context.event = event;
    context.device_state = Application::GetInstance().GetDeviceState();
    
    // 获取情感状态
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

void LocalResponseController::ExecuteComponents(ResponseComponent** components,
                                                size_t count,
                                                const ExecutionContext& context) {
    ESP_LOGI(TAG, "Executing %u response components...", count);

    for (size_t i = 0; i < count; ++i) {
        ResponseComponent* comp = components[i];
        if (comp && comp->CanExecute(context.device_state)) {
            comp->Execute(context);
            ESP_LOGI(TAG, "  - %s component executed", comp->GetTypeName());
        } else {
            ESP_LOGI(TAG, "  - component skipped (state not allowed)");
        }
    }
    ESP_LOGI(TAG, "Response execution completed");
}


// 旧的默认模板函数已删除 - 现在完全使用SD卡配置

void LocalResponseController::CreateDefaultTemplatesFromSD() {
    SDdata_Pro* sdcard = GetSDHandle();
    if (sdcard == nullptr) {
        ESP_LOGE(TAG, "❌ SD Card not found - response system requires SD card configuration");
        return;
    }

    const char *filePath = "/sdcard/config/response_config.json";
    FILE *f = fopen(filePath, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "❌ Failed to open config file: %s", filePath);
        return;
    }

    // 获取文件大小置位到文件起始处
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    ESP_LOGI(TAG, "response_config.json file size: %ld bytes", file_size);
    fseek(f, 0, SEEK_SET);

    char* json_data = (char*)malloc((file_size + 1) * sizeof(char));
    if (!json_data) {
        ESP_LOGE(TAG, "❌ Failed to allocate memory for config file");
        fclose(f);
        return;
    }

    fread(json_data, 1, file_size, f);
    json_data[file_size] = '\0';
    fclose(f);

    cJSON* root = cJSON_Parse(json_data);
    if (!root) {
        ESP_LOGE(TAG, "❌ response_config.json parse error");
        free(json_data);
        return;
    }

    cJSON* events = cJSON_GetObjectItem(root, "events");
    if (!events) {
        ESP_LOGE(TAG, "❌ json 'events' field not found");
        cJSON_Delete(root);
        free(json_data);
        return;
    }

    template_count_ = 0;

    // 遍历所有事件配置
    cJSON* event_item = NULL;
    cJSON_ArrayForEach(event_item, events) {
        const char* event_name = event_item->string;  // 获取事件名称
        if (!event_name) continue;

        LoadEventTemplate(event_name, event_item);
    }

    cJSON_Delete(root);
    free(json_data);

    if (template_count_ == 0) {
        ESP_LOGW(TAG, "⚠️ No valid templates loaded from config file");
    } else {
        ESP_LOGI(TAG, "✅ Successfully loaded %u response templates from response_config.json", template_count_);
    }
}

// ==================== 新的配置加载实现 ====================

// 解析事件名称为EventType (例如: "interaction/motion_shake_q1" -> motion_shake_q1)
EventType LocalResponseController::ParseEventName(const char* event_name) {
    // 提取最后一段作为事件标识
    const char* last_slash = strrchr(event_name, '/');
    const char* event_id = last_slash ? (last_slash + 1) : event_name;

    // 映射事件名称到EventType
    if (strstr(event_id, "motion_shake_violently")) return EventType::MOTION_SHAKE_VIOLENTLY;
    if (strstr(event_id, "motion_upside_down")) return EventType::MOTION_UPSIDE_DOWN;
    if (strstr(event_id, "motion_shake")) return EventType::MOTION_SHAKE;
    if (strstr(event_id, "motion_pickup")) return EventType::MOTION_PICKUP;
    if (strstr(event_id, "touch_tap")) return EventType::TOUCH_TAP;
    if (strstr(event_id, "touch_long_press")) return EventType::TOUCH_LONG_PRESS;
    if (strstr(event_id, "touch_cradled")) return EventType::TOUCH_CRADLED;
    if (strstr(event_id, "touch_tickled")) return EventType::TOUCH_TICKLED;
    if (strstr(event_id, "idle_1min")) return EventType::IDLE_1MIN;

    // state_expression 事件不映射到 EventType（它们通过状态名称处理）
    if (strstr(event_name, "state_expression/")) {
        ESP_LOGD(TAG, "State expression event (handled separately): %s", event_name);
        return EventType::MOTION_NONE;
    }

    ESP_LOGW(TAG, "Unknown event name: %s", event_name);
    return EventType::MOTION_NONE;
}

// 加载单个事件模板
void LocalResponseController::LoadEventTemplate(const char* event_name, cJSON* event_config) {
    // 检查是否是状态表达式事件
    if (strstr(event_name, "state_expression/")) {
        LoadStateTemplate(event_name, event_config);
        return;
    }

    if (template_count_ >= MAX_TEMPLATES) {
        ESP_LOGW(TAG, "Template array full, skipping %s", event_name);
        return;
    }

    EventType event_type = ParseEventName(event_name);
    if (event_type == EventType::MOTION_NONE) {
        return;  // 未识别的事件，跳过
    }

    // 创建模板（复制事件名到字符串池，避免cJSON释放后指针失效）
    int priority = 2;  // 默认优先级
    const char* persistent_name = AllocateString(event_name);
    ResponseTemplate& tmpl = templates_[template_count_++] = ResponseTemplate(persistent_name, event_type, priority);

    ESP_LOGI(TAG, "Loading template[%d]: %s", template_count_ - 1, event_name);

    // 加载can_interrupt配置
    LoadCanInterruptStates(event_config, tmpl);

    // 加载响应组件
    LoadResponseComponents(event_config, event_name, tmpl);
}

// 加载响应组件（振动、动作、动画、音频）
void LocalResponseController::LoadResponseComponents(cJSON* event_config, const char* event_name, ResponseTemplate& tmpl) {
    // 1. 加载振动配置
    cJSON* vibration_cfg = cJSON_GetObjectItem(event_config, "vibration");
    if (vibration_cfg) {
        cJSON* enabled = cJSON_GetObjectItem(vibration_cfg, "enabled");
        if (!enabled || cJSON_IsTrue(enabled)) {
            cJSON* pattern = cJSON_GetObjectItem(vibration_cfg, "pattern");
            if (pattern && cJSON_IsString(pattern)) {
                vibration_id_t vib_id = ParseVibrationPattern(pattern->valuestring);
                tmpl.AddBaseComponent(ResponseComponent::CreateVibration(vib_id));
                ESP_LOGI(TAG, "  + Vibration: %s", pattern->valuestring);
            }
        }
    }

    // 2. 加载动作配置
    cJSON* motion_cfg = cJSON_GetObjectItem(event_config, "motion");
    if (motion_cfg) {
        cJSON* enabled = cJSON_GetObjectItem(motion_cfg, "enabled");
        if (!enabled || cJSON_IsTrue(enabled)) {
            cJSON* action = cJSON_GetObjectItem(motion_cfg, "action");
            if (action && cJSON_IsString(action)) {
                motion_id_t motion_id = ParseMotionAction(action->valuestring);
                tmpl.AddBaseComponent(ResponseComponent::CreateMotion(motion_id));
                ESP_LOGI(TAG, "  + Motion: %s", action->valuestring);
            }
        }
    }

    // 3. 加载动画配置
    cJSON* animation_cfg = cJSON_GetObjectItem(event_config, "animation");
    if (animation_cfg) {
        cJSON* enabled = cJSON_GetObjectItem(animation_cfg, "enabled");
        if (!enabled || cJSON_IsTrue(enabled)) {
            cJSON* name = cJSON_GetObjectItem(animation_cfg, "name");
            cJSON* count = cJSON_GetObjectItem(animation_cfg, "count");
            int loop_count = count ? count->valueint : 1;

            // 从配置文件读取动画名称，如果未指定则使用事件ID（不含路径前缀）
            const char* animation_name_src;
            if (name && cJSON_IsString(name)) {
                animation_name_src = name->valuestring;
                ESP_LOGI(TAG, "  [DEBUG] Animation name from JSON: '%s' (cJSON ptr=%p)", animation_name_src, (void*)animation_name_src);
            } else {
                const char* last_slash = strrchr(event_name, '/');
                animation_name_src = last_slash ? (last_slash + 1) : event_name;
                ESP_LOGI(TAG, "  [DEBUG] Animation name from event path: '%s' (ptr=%p)", animation_name_src, (void*)animation_name_src);
            }

            // 分配持久化字符串存储（避免cJSON_Delete后指针失效）
            const char* animation_name = AllocateString(animation_name_src);
            if (animation_name) {
                tmpl.AddBaseComponent(ResponseComponent::CreateAnimation(animation_name, loop_count));
                ESP_LOGI(TAG, "  + Animation: %s (loop=%d, pool ptr=%p)", animation_name, loop_count, (void*)animation_name);
            }
        }
    }

    // 4. 加载音频配置
    cJSON* sound_cfg = cJSON_GetObjectItem(event_config, "sound");
    if (sound_cfg) {
        cJSON* enabled = cJSON_GetObjectItem(sound_cfg, "enabled");
        if (!enabled || cJSON_IsTrue(enabled)) {
            cJSON* name = cJSON_GetObjectItem(sound_cfg, "name");
            cJSON* volume = cJSON_GetObjectItem(sound_cfg, "volume");
            int vol = volume ? volume->valueint : 100;

            // 从配置文件读取音频名称，如果未指定则使用事件ID（不含路径前缀）
            const char* audio_name_src;
            if (name && cJSON_IsString(name)) {
                audio_name_src = name->valuestring;
                ESP_LOGI(TAG, "  [DEBUG] Audio name from JSON: '%s' (cJSON ptr=%p)", audio_name_src, (void*)audio_name_src);
            } else {
                const char* last_slash = strrchr(event_name, '/');
                audio_name_src = last_slash ? (last_slash + 1) : event_name;
                ESP_LOGI(TAG, "  [DEBUG] Audio name from event path: '%s' (ptr=%p)", audio_name_src, (void*)audio_name_src);
            }

            // 分配持久化字符串存储（避免cJSON_Delete后指针失效）
            const char* audio_name = AllocateString(audio_name_src);
            if (audio_name) {
                tmpl.AddBaseComponent(ResponseComponent::CreateAudio(audio_name, vol));
                ESP_LOGI(TAG, "  + Audio: %s (volume=%d, pool ptr=%p)", audio_name, vol, (void*)audio_name);
            }
        }
    }
}

// 解析振动模式字符串
vibration_id_t LocalResponseController::ParseVibrationPattern(const char* pattern_str) {
    if (strcmp(pattern_str, "VIBRATION_SHORT_BUZZ") == 0) return VIBRATION_SHORT_BUZZ;
    if (strcmp(pattern_str, "VIBRATION_PURR_SHORT") == 0) return VIBRATION_PURR_SHORT;
    if (strcmp(pattern_str, "VIBRATION_PURR_PATTERN") == 0) return VIBRATION_PURR_PATTERN;
    if (strcmp(pattern_str, "VIBRATION_GENTLE_HEARTBEAT") == 0) return VIBRATION_GENTLE_HEARTBEAT;
    if (strcmp(pattern_str, "VIBRATION_STRUGGLE_PATTERN") == 0) return VIBRATION_STRUGGLE_PATTERN;
    if (strcmp(pattern_str, "VIBRATION_SHARP_BUZZ") == 0) return VIBRATION_SHARP_BUZZ;
    if (strcmp(pattern_str, "VIBRATION_TREMBLE_PATTERN") == 0) return VIBRATION_TREMBLE_PATTERN;
    if (strcmp(pattern_str, "VIBRATION_GIGGLE_PATTERN") == 0) return VIBRATION_GIGGLE_PATTERN;
    if (strcmp(pattern_str, "VIBRATION_HEARTBEAT_STRONG") == 0) return VIBRATION_HEARTBEAT_STRONG;
    if (strcmp(pattern_str, "VIBRATION_ERRATIC_STRONG") == 0) return VIBRATION_ERRATIC_STRONG;
    ESP_LOGW(TAG, "Unknown vibration pattern: %s", pattern_str);
    return VIBRATION_SHORT_BUZZ;  // 默认
}

// 解析动作字符串
motion_id_t LocalResponseController::ParseMotionAction(const char* action_str) {
    if (strcmp(action_str, "MOTION_HAPPY_WIGGLE") == 0) return MOTION_HAPPY_WIGGLE;
    if (strcmp(action_str, "MOTION_SHAKE_HEAD") == 0) return MOTION_SHAKE_HEAD;
    if (strcmp(action_str, "MOTION_NUZZLE_FORWARD") == 0) return MOTION_NUZZLE_FORWARD;
    if (strcmp(action_str, "MOTION_RELAX_COMPLETELY") == 0) return MOTION_RELAX_COMPLETELY;
    if (strcmp(action_str, "MOTION_EXCITED_JIGGLE") == 0) return MOTION_EXCITED_JIGGLE;
    if (strcmp(action_str, "MOTION_CURIOUS_PEEK_LEFT") == 0) return MOTION_CURIOUS_PEEK_LEFT;
    if (strcmp(action_str, "MOTION_CURIOUS_PEEK_RIGHT") == 0) return MOTION_CURIOUS_PEEK_RIGHT;
    if (strcmp(action_str, "MOTION_BODY_SHIVER") == 0) return MOTION_BODY_SHIVER;
    if (strcmp(action_str, "MOTION_STRUGGLE_TWIST") == 0) return MOTION_STRUGGLE_TWIST;
    if (strcmp(action_str, "MOTION_TICKLE_TWIST_DANCE") == 0) return MOTION_TICKLE_TWIST_DANCE;
    if (strcmp(action_str, "MOTION_RELAX_TO_CENTER") == 0) return MOTION_RELAX_TO_CENTER;
    ESP_LOGW(TAG, "Unknown motion action: %s", action_str);
    return MOTION_RELAX_COMPLETELY;  // 默认
}

// 解析设备状态字符串
DeviceState LocalResponseController::ParseDeviceState(const char* state_str) {
    if (strcmp(state_str, "idle") == 0) return kDeviceStateIdle;
    if (strcmp(state_str, "listening") == 0) return kDeviceStateListening;
    if (strcmp(state_str, "speaking") == 0) return kDeviceStateSpeaking;
    if (strcmp(state_str, "connecting") == 0) return kDeviceStateConnecting;
    if (strcmp(state_str, "upgrading") == 0) return kDeviceStateUpgrading;
    if (strcmp(state_str, "error") == 0) return kDeviceStateFatalError;
    if (strcmp(state_str, "starting") == 0) return kDeviceStateStarting;
    if (strcmp(state_str, "activating") == 0) return kDeviceStateActivating;
    if (strcmp(state_str, "audio_testing") == 0) return kDeviceStateAudioTesting;
    if (strcmp(state_str, "wifi_configuring") == 0) return kDeviceStateWifiConfiguring;

    ESP_LOGW(TAG, "Unknown device state: %s", state_str);
    return kDeviceStateIdle;  // 默认
}


// 加载 can_interrupt 状态列表
void LocalResponseController::LoadCanInterruptStates(cJSON* event_config, ResponseTemplate& tmpl) {
    cJSON* can_interrupt = cJSON_GetObjectItem(event_config, "can_interrupt");
    if (!can_interrupt || !cJSON_IsArray(can_interrupt)) {
        // 没有配置can_interrupt，默认允许所有状态（向后兼容）
        tmpl.can_interrupt_all = true;
        ESP_LOGD(TAG, "  No can_interrupt config, allowing all states");
        return;
    }

    int array_size = cJSON_GetArraySize(can_interrupt);
    ESP_LOGI(TAG, "  + can_interrupt: %d states", array_size);

    for (int i = 0; i < array_size; ++i) {
        cJSON* state_item = cJSON_GetArrayItem(can_interrupt, i);
        if (!cJSON_IsString(state_item)) continue;

        const char* state_str = state_item->valuestring;

        // 特殊处理 "all"
        if (strcmp(state_str, "all") == 0) {
            tmpl.can_interrupt_all = true;
            ESP_LOGI(TAG, "    - all (can interrupt any state)");
            return;  // 设置了all就不需要继续解析
        }

        // 解析具体状态
        DeviceState state = ParseDeviceState(state_str);
        if (tmpl.can_interrupt_count < ResponseTemplate::MAX_INTERRUPT_STATES) {
            tmpl.can_interrupt_states[tmpl.can_interrupt_count++] = state;
            ESP_LOGI(TAG, "    - %s (state=%d)", state_str, static_cast<int>(state));
        } else {
            ESP_LOGW(TAG, "    - %s: interrupt states array full!", state_str);
        }
    }
}

// 字符串池管理 - 分配持久化字符串存储
// 原因：接口函数需要const std::string&，会从const char*构造std::string
// 如果const char*指向已释放的cJSON内存，会读取到乱码
const char* LocalResponseController::AllocateString(const char* str) {
    if (!str) return nullptr;

    if (name_pool_index_ >= MAX_NAME_POOL_SIZE) {
        ESP_LOGE(TAG, "❌ Name pool exhausted! Cannot allocate string: %s", str);
        return nullptr;
    }

    size_t len = strlen(str);
    if (len >= MAX_NAME_LENGTH) {
        ESP_LOGW(TAG, "⚠️ String too long (truncating): %s", str);
        len = MAX_NAME_LENGTH - 1;
    }

    char* buffer = name_pool_[name_pool_index_++];
    strncpy(buffer, str, MAX_NAME_LENGTH - 1);
    buffer[MAX_NAME_LENGTH - 1] = '\0';

    ESP_LOGD(TAG, "[StringPool] Allocated '%s' at index %u (ptr=%p)", buffer, name_pool_index_ - 1, (void*)buffer);
    return buffer;
}

// ==================== 状态响应辅助函数 ====================

StateResponseTemplate* LocalResponseController::FindStateTemplate(DeviceState state) {
    for (size_t i = 0; i < state_template_count_; i++) {
        if (state_templates_[i].state == state) {
            return &state_templates_[i];
        }
    }
    return nullptr;
}

StateResponseTemplate* LocalResponseController::FindOrCreateStateTemplate(DeviceState state) {
    StateResponseTemplate* existing = FindStateTemplate(state);
    if (existing) {
        return existing;
    }

    if (state_template_count_ >= MAX_STATE_TEMPLATES) {
        ESP_LOGW(TAG, "State template array full!");
        return nullptr;
    }

    StateResponseTemplate* new_tmpl = &state_templates_[state_template_count_++];
    new_tmpl->state = state;
    return new_tmpl;
}

DeviceState LocalResponseController::ParseStateName(const char* event_name) {
    // 从 "state_expression/idle/idle_q1" 中提取状态 "idle"
    const char* first_slash = strchr(event_name, '/');
    if (!first_slash) return kDeviceStateUnknown;

    const char* second_slash = strchr(first_slash + 1, '/');
    if (!second_slash) return kDeviceStateUnknown;

    // 提取中间的状态名称
    size_t state_name_len = second_slash - (first_slash + 1);
    char state_name[32] = {0};
    if (state_name_len >= sizeof(state_name)) {
        return kDeviceStateUnknown;
    }
    strncpy(state_name, first_slash + 1, state_name_len);
    state_name[state_name_len] = '\0';

    // 映射状态名称
    if (strcmp(state_name, "idle") == 0) return kDeviceStateIdle;
    if (strcmp(state_name, "listening") == 0) return kDeviceStateListening;
    if (strcmp(state_name, "speaking") == 0) return kDeviceStateSpeaking;

    return kDeviceStateUnknown;
}

void LocalResponseController::LoadStateTemplate(const char* event_name, cJSON* event_config) {
    DeviceState state = ParseStateName(event_name);
    if (state == kDeviceStateUnknown) {
        ESP_LOGW(TAG, "Unknown state in event: %s", event_name);
        return;
    }

    // 只处理 idle 和 listening 状态
    if (state != kDeviceStateIdle && state != kDeviceStateListening) {
        ESP_LOGD(TAG, "Skipping non-idle/listening state: %s", event_name);
        return;
    }

    // 从事件名称中提取象限 (例如 "idle_q1" -> 象限0)
    const char* last_slash = strrchr(event_name, '/');
    const char* event_id = last_slash ? (last_slash + 1) : event_name;

    // 提取象限编号
    const char* q_pos = strstr(event_id, "_q");
    if (!q_pos) {
        ESP_LOGW(TAG, "No quadrant suffix found in: %s", event_name);
        return;
    }

    int quadrant_num = atoi(q_pos + 2);  // +2 to skip "_q"
    if (quadrant_num < 1 || quadrant_num > 4) {
        ESP_LOGW(TAG, "Invalid quadrant number in: %s", event_name);
        return;
    }

    EmotionQuadrant quadrant = static_cast<EmotionQuadrant>(quadrant_num - 1);
    int quadrant_idx = static_cast<int>(quadrant);

    ESP_LOGI(TAG, "Loading state template: %s (state=%d, quadrant=%d)",
             event_name, static_cast<int>(state), quadrant_idx);

    // 查找或创建状态模板
    StateResponseTemplate* state_tmpl = FindOrCreateStateTemplate(state);
    if (!state_tmpl) {
        ESP_LOGE(TAG, "Failed to create state template for state: %d", static_cast<int>(state));
        return;
    }

    // 加载动画配置（状态响应主要是动画）
    cJSON* animation_cfg = cJSON_GetObjectItem(event_config, "animation");
    if (animation_cfg) {
        cJSON* enabled = cJSON_GetObjectItem(animation_cfg, "enabled");
        if (!enabled || cJSON_IsTrue(enabled)) {
            cJSON* name = cJSON_GetObjectItem(animation_cfg, "name");
            cJSON* count = cJSON_GetObjectItem(animation_cfg, "count");
            int loop_count = count ? count->valueint : -1;  // 默认无限循环

            const char* animation_name_src;
            if (name && cJSON_IsString(name)) {
                animation_name_src = name->valuestring;
            } else {
                animation_name_src = event_id;
            }

            const char* animation_name = AllocateString(animation_name_src);
            if (animation_name) {
                state_tmpl->quadrant_responses[quadrant_idx] =
                    ResponseComponent::CreateAnimation(animation_name, loop_count);
                state_tmpl->has_response[quadrant_idx] = true;
                ESP_LOGI(TAG, "  + State animation: %s (loop=%d)", animation_name, loop_count);
            }
        }
    }

    // 可选：加载音频配置
    cJSON* sound_cfg = cJSON_GetObjectItem(event_config, "sound");
    if (sound_cfg) {
        cJSON* enabled = cJSON_GetObjectItem(sound_cfg, "enabled");
        if (!enabled || cJSON_IsTrue(enabled)) {
            cJSON* name = cJSON_GetObjectItem(sound_cfg, "name");
            cJSON* volume = cJSON_GetObjectItem(sound_cfg, "volume");
            int vol = volume ? volume->valueint : 100;

            if (name && cJSON_IsString(name)) {
                const char* audio_name = AllocateString(name->valuestring);
                if (audio_name) {
                    // 注意：如果同时有动画和音频，这里会覆盖动画
                    // 实际使用中可以考虑支持多个组件
                    ESP_LOGI(TAG, "  + State audio: %s (volume=%d) [Note: overrides animation]",
                             audio_name, vol);
                    // 暂时不覆盖，只打印日志
                }
            }
        }
    }
}

// 调试接口实现
void LocalResponseController::ListTemplates() const {
    ESP_LOGI(TAG, "=== Response Templates (%u) ===", template_count_);
    for (size_t i = 0; i < template_count_; ++i) {
        const auto& t = templates_[i];
        size_t qv_cnt = 0;
        for (int q = 0; q < 4; ++q) qv_cnt += t.quadrant_variants[q].count;

        ESP_LOGI(TAG, "- %s (Event: %d, Priority: %d, Base: %u, Quadrant comps: %u)",
                 t.name ? t.name : "(null)",
                 static_cast<int>(t.trigger_event),
                 t.priority,
                 t.base_component_count,
                 qv_cnt);
    }
}

void LocalResponseController::TestResponse(EventType event_type, EmotionQuadrant quadrant) {
    ESP_LOGI(TAG, "🧪 Testing response for event %d in quadrant %d", 
             static_cast<int>(event_type), static_cast<int>(quadrant));
    
    Event test_event;
    test_event.type = event_type;
    test_event.timestamp_us = esp_timer_get_time();
    
    // 临时设置情感状态用于测试
    auto& emotion_engine = EmotionEngine::GetInstance();
    float test_v = 0.0f, test_a = 0.0f;
    switch (quadrant) {
        case EmotionQuadrant::POSITIVE_HIGH_AROUSAL: test_v = 0.5f; test_a = 0.5f; break;
        case EmotionQuadrant::POSITIVE_LOW_AROUSAL: test_v = 0.5f; test_a = -0.5f; break;
        case EmotionQuadrant::NEGATIVE_HIGH_AROUSAL: test_v = -0.5f; test_a = 0.5f; break;
        case EmotionQuadrant::NEGATIVE_LOW_AROUSAL: test_v = -0.5f; test_a = -0.5f; break;
    }
    emotion_engine.SetState(test_v, test_a);
    
    ProcessEvent(test_event);
}
