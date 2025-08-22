# MCP本地响应控制功能开发TODO

## 概述
本文档描述基于MCP协议实现云端大模型调用本地响应功能的分步实现计划，利用ALichuangTest板的直流马达、振动马达和屏幕动画能力，为大模型提供丰富的本地交互响应接口。

## 开发原则
- ✅ 每个阶段可独立编译和测试
- ✅ 参考esp-hi板的MCP实现模式
- ✅ 充分利用现有的skills功能（Motion, Vibration, Animation）
- ✅ 提供直观的函数命名和参数设计
- ✅ 每步都有具体的验证方法
- ✅ 问题即时修复，不累积到后续阶段

---

## Phase 1: MCP工具注册基础框架 [⏱️ 45分钟]

### 1.1 分析现有MCP集成点
- [ ] 检查ALichuangTest是否已包含mcp_server.h
- [ ] 确认McpServer::GetInstance()可用性
- [ ] 查看现有的MCP工具注册位置

### 1.2 创建MCP工具初始化函数
- [ ] 在ALichuangTest类中添加`InitializeMcpTools()`方法
- [ ] 在构造函数中调用MCP工具初始化
- [ ] 创建基础的工具注册框架

### 1.3 验证MCP服务器可用性
```cpp
void ALichuangTest::InitializeMcpTools() {
    auto& mcp_server = McpServer::GetInstance();
    ESP_LOGI(TAG, "MCP Server available, registering local response tools...");
    
    // 测试基础工具注册
    mcp_server.AddTool("self.test.ping", "测试MCP连接", PropertyList(), 
        [this](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI(TAG, "MCP Test: Ping received!");
            return "Pong from ALichuangTest";
        });
}
```

### 测试点 1.1
**验证**: 编译通过，在MCP客户端调用`self.test.ping`能收到"Pong"响应

---

## Phase 2: 直流马达动作控制接口 [⏱️ 1小时]

### 2.1 实现基础身体动作控制
- [ ] 注册`self.body.basic_motion` - 基础身体动作
- [ ] 支持动作：happy_wiggle, shake_head, nuzzle_forward, relax_completely
- [ ] 添加动作描述和参数验证

### 2.2 实现情绪表达动作
- [ ] 注册`self.body.emotion_motion` - 情绪动作表达
- [ ] 支持情绪：happy, angry, shy, curious, nervous, excited
- [ ] 每种情绪映射到对应的Motion动作ID

### 2.3 实现精确角度控制
- [ ] 注册`self.body.angle_control` - 精确角度控制
- [ ] 支持角度范围：-90到+90度
- [ ] 支持速度控制：slow, medium, fast

```cpp
// 基础身体动作控制
mcp_server.AddTool("self.body.basic_motion", 
    "控制身体做基础动作。可用动作：\n"
    "happy_wiggle: 开心摇摆\n"
    "shake_head: 摇头表示不同意\n" 
    "nuzzle_forward: 向前蹭表示亲昵\n"
    "relax_completely: 完全放松\n"
    "stop: 停止当前动作", 
    PropertyList({
        Property("action", kPropertyTypeString)
    }), [this](const PropertyList& properties) -> ReturnValue {
        const std::string& action = properties["action"].value<std::string>();
        
        if (!motion_skill_) {
            return ReturnValue(false, "Motion system not available");
        }
        
        if (action == "happy_wiggle") {
            motion_skill_->Perform(MOTION_HAPPY_WIGGLE);
        } else if (action == "shake_head") {
            motion_skill_->Perform(MOTION_SHAKE_HEAD);
        } else if (action == "nuzzle_forward") {
            motion_skill_->Perform(MOTION_NUZZLE_FORWARD);
        } else if (action == "relax_completely") {
            motion_skill_->Perform(MOTION_RELAX_COMPLETELY);
        } else if (action == "stop") {
            motion_skill_->Stop();
        } else {
            return ReturnValue(false, "Unknown action: " + action);
        }
        
        ESP_LOGI(TAG, "Body motion executed: %s", action.c_str());
        return ReturnValue(true, "Action " + action + " executed successfully");
    });

// 情绪表达动作
mcp_server.AddTool("self.body.emotion_motion",
    "根据情绪表达相应的身体动作。支持情绪：\n"
    "happy: 开心时的动作\n"
    "angry: 愤怒时的动作\n"
    "shy: 害羞时的动作\n"
    "curious: 好奇时的动作\n"
    "nervous: 紧张时的动作\n"
    "excited: 兴奋时的动作",
    PropertyList({
        Property("emotion", kPropertyTypeString)
    }), [this](const PropertyList& properties) -> ReturnValue {
        const std::string& emotion = properties["emotion"].value<std::string>();
        
        if (!motion_skill_) {
            return ReturnValue(false, "Motion system not available");
        }
        
        motion_id_t motion_id;
        if (emotion == "happy") {
            motion_id = MOTION_HAPPY_WIGGLE;
        } else if (emotion == "angry") {
            motion_id = MOTION_SHAKE_HEAD;
        } else if (emotion == "shy") {
            motion_id = MOTION_DODGE_SUBTLE;
        } else if (emotion == "curious") {
            motion_id = MOTION_CURIOUS_PEEK_LEFT; // 或根据随机选择左右
        } else if (emotion == "nervous") {
            motion_id = MOTION_TENSE_UP;
        } else if (emotion == "excited") {
            motion_id = MOTION_EXCITED_JIGGLE;
        } else {
            return ReturnValue(false, "Unknown emotion: " + emotion);
        }
        
        motion_skill_->Perform(motion_id);
        ESP_LOGI(TAG, "Emotion motion executed: %s", emotion.c_str());
        return ReturnValue(true, "Emotion " + emotion + " expressed successfully");
    });

// 精确角度控制
mcp_server.AddTool("self.body.angle_control",
    "控制身体转到精确角度。参数说明：\n"
    "angle: 目标角度，范围-90到90度\n"
    "speed: 转动速度 (slow/medium/fast)",
    PropertyList({
        Property("angle", kPropertyTypeNumber, -90, 90),
        Property("speed", kPropertyTypeString)
    }), [this](const PropertyList& properties) -> ReturnValue {
        if (!motion_skill_) {
            return ReturnValue(false, "Motion system not available");
        }
        
        float angle = properties["angle"].value<double>();
        std::string speed_str = properties["speed"].value<std::string>();
        
        motion_speed_t speed = MOTION_SPEED_MEDIUM;
        if (speed_str == "slow") speed = MOTION_SPEED_SLOW;
        else if (speed_str == "fast") speed = MOTION_SPEED_FAST;
        
        motion_skill_->SetAngle(angle, speed);
        
        ESP_LOGI(TAG, "Angle control: %.1f degrees at %s speed", angle, speed_str.c_str());
        return ReturnValue(true, "Moved to " + std::to_string(angle) + " degrees");
    });
```

### 测试点 2.1
**测试方法**: 
```bash
# MCP客户端调用测试
self.body.basic_motion({"action": "happy_wiggle"})
self.body.emotion_motion({"emotion": "happy"}) 
self.body.angle_control({"angle": 45, "speed": "medium"})
```
**期望**: 设备执行对应的身体动作，返回成功确认消息

---

## Phase 3: 振动反馈控制接口 [⏱️ 45分钟]

### 3.1 实现基础振动模式
- [ ] 注册`self.haptic.basic_vibration` - 基础振动反馈
- [ ] 支持模式：short_buzz, purr, heartbeat, sharp_buzz
- [ ] 添加振动强度和时长说明

### 3.2 实现情绪振动反馈
- [ ] 注册`self.haptic.emotion_vibration` - 情绪振动反馈
- [ ] 映射情绪到对应振动模式
- [ ] 与身体动作形成配合

```cpp
// 基础振动控制
mcp_server.AddTool("self.haptic.basic_vibration",
    "控制振动马达产生触觉反馈。可用模式：\n"
    "short_buzz: 短促确认振动\n"
    "purr: 咕噜咕噜声振动\n"
    "heartbeat: 心跳节奏振动\n"
    "sharp_buzz: 尖锐提醒振动\n"
    "giggle: 笑声振动\n"
    "tremble: 颤抖振动\n"
    "stop: 停止振动",
    PropertyList({
        Property("pattern", kPropertyTypeString)
    }), [this](const PropertyList& properties) -> ReturnValue {
        const std::string& pattern = properties["pattern"].value<std::string>();
        
        if (!vibration_skill_) {
            return ReturnValue(false, "Vibration system not available");
        }
        
        vibration_id_t vibration_id;
        if (pattern == "short_buzz") {
            vibration_id = VIBRATION_SHORT_BUZZ;
        } else if (pattern == "purr") {
            vibration_id = VIBRATION_PURR_PATTERN;
        } else if (pattern == "heartbeat") {
            vibration_id = VIBRATION_GENTLE_HEARTBEAT;
        } else if (pattern == "sharp_buzz") {
            vibration_id = VIBRATION_SHARP_BUZZ;
        } else if (pattern == "giggle") {
            vibration_id = VIBRATION_GIGGLE_PATTERN;
        } else if (pattern == "tremble") {
            vibration_id = VIBRATION_TREMBLE_PATTERN;
        } else if (pattern == "stop") {
            vibration_skill_->Stop();
            return ReturnValue(true, "Vibration stopped");
        } else {
            return ReturnValue(false, "Unknown vibration pattern: " + pattern);
        }
        
        vibration_skill_->Play(vibration_id);
        ESP_LOGI(TAG, "Vibration executed: %s", pattern.c_str());
        return ReturnValue(true, "Vibration pattern " + pattern + " started");
    });

// 情绪振动反馈
mcp_server.AddTool("self.haptic.emotion_vibration", 
    "根据情绪产生相应的振动反馈。支持情绪：\n"
    "happy: 开心的振动\n"
    "excited: 兴奋的振动\n"
    "comfort: 舒适的振动\n"
    "alert: 警觉的振动\n"
    "sad: 悲伤的振动\n"
    "scared: 害怕的振动",
    PropertyList({
        Property("emotion", kPropertyTypeString)
    }), [this](const PropertyList& properties) -> ReturnValue {
        const std::string& emotion = properties["emotion"].value<std::string>();
        
        if (!vibration_skill_) {
            return ReturnValue(false, "Vibration system not available");
        }
        
        vibration_skill_->PlayForEmotion(emotion);
        ESP_LOGI(TAG, "Emotion vibration: %s", emotion.c_str());
        return ReturnValue(true, "Emotion vibration " + emotion + " triggered");
    });
```

### 测试点 3.1
**测试方法**: 调用不同振动模式，验证振动马达响应正确
**期望**: 设备产生对应模式的振动，返回成功确认

---

## Phase 4: 屏幕情绪动画控制接口 [⏱️ 1小时15分钟]

### 4.1 实现情绪动画控制
- [ ] 注册`self.display.show_emotion` - 显示情绪动画
- [ ] 支持情绪：neutral, happy, angry, sad, surprised, laughing
- [ ] 集成现有的AnimaDisplay和情绪图片系统

### 4.2 实现动画播放控制
- [ ] 注册`self.display.animation_control` - 动画播放控制
- [ ] 支持操作：start, stop, pause, set_speed
- [ ] 控制情绪动画的播放状态

### 4.3 实现屏幕绘制控制
- [ ] 注册`self.display.draw_image` - 自定义图像显示
- [ ] 支持在屏幕指定位置绘制简单图形或文本

```cpp
// 情绪动画控制
mcp_server.AddTool("self.display.show_emotion",
    "在屏幕上显示情绪动画。支持情绪：\n"
    "neutral: 中性表情\n"
    "happy: 开心表情\n" 
    "angry: 愤怒表情\n"
    "sad: 悲伤表情\n"
    "surprised: 惊讶表情\n"
    "laughing: 大笑表情\n"
    "thinking: 思考表情",
    PropertyList({
        Property("emotion", kPropertyTypeString),
        Property("duration", kPropertyTypeInteger, 0, 30000) // 可选，持续时间(ms)
    }), [this](const PropertyList& properties) -> ReturnValue {
        const std::string& emotion = properties["emotion"].value<std::string>();
        
        auto display = GetDisplay();
        if (!display) {
            return ReturnValue(false, "Display system not available");
        }
        
        // 设置情绪，触发动画变更
        SetCurrentEmotion(emotion);
        display->SetEmotion(emotion.c_str());
        
        // 可选的持续时间控制
        if (properties.find("duration") != properties.end()) {
            int duration_ms = properties["duration"].value<int>();
            if (duration_ms > 0) {
                // 创建定时器，在指定时间后恢复neutral
                // TODO: 实现定时器逻辑
            }
        }
        
        ESP_LOGI(TAG, "Emotion animation: %s", emotion.c_str());
        return ReturnValue(true, "Emotion " + emotion + " displayed");
    });

// 动画播放控制
mcp_server.AddTool("self.display.animation_control",
    "控制屏幕动画播放。支持操作：\n"
    "start: 开始播放当前情绪动画\n"
    "stop: 停止动画播放\n"
    "set_speed: 设置动画播放速度",
    PropertyList({
        Property("action", kPropertyTypeString),
        Property("speed", kPropertyTypeInteger, 10, 500) // 可选，播放间隔(ms)
    }), [this](const PropertyList& properties) -> ReturnValue {
        const std::string& action = properties["action"].value<std::string>();
        
        if (action == "start") {
            // 开始播放动画（实际上AnimaDisplay一直在播放）
            ESP_LOGI(TAG, "Animation playback started");
            return ReturnValue(true, "Animation started");
        } else if (action == "stop") {
            // 停止到neutral状态
            SetCurrentEmotion("neutral");
            ESP_LOGI(TAG, "Animation playback stopped");
            return ReturnValue(true, "Animation stopped");
        } else if (action == "set_speed") {
            if (properties.find("speed") != properties.end()) {
                int speed_ms = properties["speed"].value<int>();
                // TODO: 实现动画速度控制逻辑
                ESP_LOGI(TAG, "Animation speed set to %d ms", speed_ms);
                return ReturnValue(true, "Speed set to " + std::to_string(speed_ms) + "ms");
            } else {
                return ReturnValue(false, "Speed parameter required for set_speed action");
            }
        } else {
            return ReturnValue(false, "Unknown action: " + action);
        }
    });
```

### 测试点 4.1
**测试方法**: 调用情绪动画显示，验证屏幕显示对应情绪图片
**期望**: 屏幕显示正确的情绪动画，动画播放流畅

---

## Phase 5: 复合动作控制接口 [⏱️ 1小时]

### 5.1 实现同步表达动作
- [ ] 注册`self.express.emotion` - 综合情绪表达
- [ ] 同时控制身体动作、振动反馈和屏幕动画
- [ ] 创建情绪表达的最佳组合

### 5.2 实现交互响应动作
- [ ] 注册`self.interact.respond` - 交互响应动作
- [ ] 支持回应类型：agree, disagree, acknowledge, celebrate, comfort
- [ ] 每种回应使用不同的动作组合

### 5.3 实现状态表达动作
- [ ] 注册`self.state.indicate` - 状态指示动作
- [ ] 支持状态：thinking, listening, processing, ready, busy
- [ ] 通过动作组合反映设备当前状态

```cpp
// 综合情绪表达
mcp_server.AddTool("self.express.emotion",
    "综合表达情绪，同时控制身体动作、振动反馈和屏幕动画。支持情绪：\n"
    "joy: 喜悦 - 开心摇摆 + 笑声振动 + 开心动画\n"
    "excitement: 兴奋 - 激动抖动 + 强烈振动 + 兴奋动画\n"
    "sadness: 悲伤 - 低垂动作 + 心跳振动 + 悲伤动画\n"
    "anger: 愤怒 - 摇头 + 尖锐振动 + 愤怒动画\n"
    "surprise: 惊讶 - 快速转向 + 短促振动 + 惊讶动画\n"
    "affection: 亲昵 - 向前蹭 + 咕噜振动 + 开心动画",
    PropertyList({
        Property("emotion", kPropertyTypeString),
        Property("intensity", kPropertyTypeInteger, 1, 5) // 强度等级
    }), [this](const PropertyList& properties) -> ReturnValue {
        const std::string& emotion = properties["emotion"].value<std::string>();
        int intensity = properties.find("intensity") != properties.end() ? 
                       properties["intensity"].value<int>() : 3;
        
        bool success = true;
        std::string error_msg = "";
        
        // 根据情绪执行组合动作
        if (emotion == "joy") {
            // 身体动作
            if (motion_skill_) motion_skill_->Perform(MOTION_HAPPY_WIGGLE);
            // 振动反馈
            if (vibration_skill_) vibration_skill_->Play(VIBRATION_GIGGLE_PATTERN);
            // 屏幕动画
            SetCurrentEmotion("happy");
        } else if (emotion == "excitement") {
            if (motion_skill_) motion_skill_->Perform(MOTION_EXCITED_JIGGLE);
            if (vibration_skill_) vibration_skill_->Play(VIBRATION_ERRATIC_STRONG);
            SetCurrentEmotion("happy");
        } else if (emotion == "sadness") {
            if (motion_skill_) motion_skill_->Perform(MOTION_RELAX_COMPLETELY);
            if (vibration_skill_) vibration_skill_->Play(VIBRATION_GENTLE_HEARTBEAT);
            SetCurrentEmotion("sad");
        } else if (emotion == "anger") {
            if (motion_skill_) motion_skill_->Perform(MOTION_SHAKE_HEAD);
            if (vibration_skill_) vibration_skill_->Play(VIBRATION_SHARP_BUZZ);
            SetCurrentEmotion("angry");
        } else if (emotion == "surprise") {
            if (motion_skill_) motion_skill_->Perform(MOTION_QUICK_TURN_LEFT);
            if (vibration_skill_) vibration_skill_->Play(VIBRATION_SHORT_BUZZ);
            SetCurrentEmotion("surprised");
        } else if (emotion == "affection") {
            if (motion_skill_) motion_skill_->Perform(MOTION_NUZZLE_FORWARD);
            if (vibration_skill_) vibration_skill_->Play(VIBRATION_PURR_PATTERN);
            SetCurrentEmotion("happy");
        } else {
            return ReturnValue(false, "Unknown emotion: " + emotion);
        }
        
        ESP_LOGI(TAG, "Complex emotion expression: %s (intensity: %d)", 
                emotion.c_str(), intensity);
        return ReturnValue(true, "Emotion " + emotion + " expressed comprehensively");
    });

// 交互响应动作
mcp_server.AddTool("self.interact.respond",
    "执行交互响应动作。支持响应类型：\n"
    "agree: 同意 - 点头动作 + 确认振动\n"
    "disagree: 不同意 - 摇头动作 + 否定振动\n"
    "acknowledge: 理解确认 - 轻微摇摆 + 短促振动\n"
    "celebrate: 庆祝 - 兴奋动作 + 欢快振动\n"
    "comfort: 安慰 - 轻柔动作 + 心跳振动",
    PropertyList({
        Property("response_type", kPropertyTypeString)
    }), [this](const PropertyList& properties) -> ReturnValue {
        const std::string& response_type = properties["response_type"].value<std::string>();
        
        if (response_type == "agree") {
            if (motion_skill_) motion_skill_->Perform(MOTION_NUZZLE_FORWARD);
            if (vibration_skill_) vibration_skill_->Play(VIBRATION_SHORT_BUZZ);
        } else if (response_type == "disagree") {
            if (motion_skill_) motion_skill_->Perform(MOTION_SHAKE_HEAD);
            if (vibration_skill_) vibration_skill_->Play(VIBRATION_SHARP_BUZZ);
        } else if (response_type == "acknowledge") {
            if (motion_skill_) motion_skill_->Perform(MOTION_HAPPY_WIGGLE);
            if (vibration_skill_) vibration_skill_->Play(VIBRATION_SHORT_BUZZ);
        } else if (response_type == "celebrate") {
            if (motion_skill_) motion_skill_->Perform(MOTION_EXCITED_JIGGLE);
            if (vibration_skill_) vibration_skill_->Play(VIBRATION_GIGGLE_PATTERN);
            SetCurrentEmotion("happy");
        } else if (response_type == "comfort") {
            if (motion_skill_) motion_skill_->Perform(MOTION_RELAX_COMPLETELY);
            if (vibration_skill_) vibration_skill_->Play(VIBRATION_GENTLE_HEARTBEAT);
            SetCurrentEmotion("neutral");
        } else {
            return ReturnValue(false, "Unknown response type: " + response_type);
        }
        
        ESP_LOGI(TAG, "Interaction response: %s", response_type.c_str());
        return ReturnValue(true, "Response " + response_type + " executed");
    });
```

### 测试点 5.1
**测试方法**: 调用复合动作，验证身体、振动、屏幕的协调表达
**期望**: 多种输出方式协调工作，产生统一的表达效果

---

## Phase 6: 设备状态查询接口 [⏱️ 30分钟]

### 6.1 实现动作状态查询
- [ ] 注册`self.status.motion` - 查询动作系统状态
- [ ] 返回当前是否正在执行动作
- [ ] 返回当前角度位置

### 6.2 实现系统状态查询
- [ ] 注册`self.status.system` - 查询系统状态
- [ ] 返回各个子系统的可用性
- [ ] 返回当前情绪状态

```cpp
// 动作状态查询
mcp_server.AddTool("self.status.motion", "查询身体动作系统状态", PropertyList(),
    [this](const PropertyList& properties) -> ReturnValue {
        if (!motion_skill_) {
            return ReturnValue("Motion system not available");
        }
        
        bool is_busy = motion_skill_->IsBusy();
        // 注意：需要在Motion类中添加获取当前角度的方法
        // float current_angle = motion_skill_->GetCurrentAngle();
        
        cJSON* status = cJSON_CreateObject();
        cJSON_AddBoolToObject(status, "is_busy", is_busy);
        cJSON_AddStringToObject(status, "status", is_busy ? "moving" : "idle");
        // cJSON_AddNumberToObject(status, "current_angle", current_angle);
        
        char* json_str = cJSON_Print(status);
        std::string result(json_str);
        cJSON_free(json_str);
        cJSON_Delete(status);
        
        return ReturnValue(result);
    });

// 事件系统状态查询
mcp_server.AddTool("self.status.events", "查询事件系统状态，包括触摸、运动检测和事件统计", PropertyList(),
    [this](const PropertyList& properties) -> ReturnValue {
        if (!event_engine_) {
            return ReturnValue("Event engine not available");
        }
        
        cJSON* status = cJSON_CreateObject();
        
        // 实时状态查询（基于event_engine.cc实现）
        cJSON_AddBoolToObject(status, "left_touched", event_engine_->IsLeftTouched());
        cJSON_AddBoolToObject(status, "right_touched", event_engine_->IsRightTouched());
        cJSON_AddBoolToObject(status, "picked_up", event_engine_->IsPickedUp());
        cJSON_AddBoolToObject(status, "upside_down", event_engine_->IsUpsideDown());
        
        // 事件处理统计（利用EventProcessor的统计功能）
        cJSON* event_stats = cJSON_CreateObject();
        
        // 获取各类事件的处理统计
        auto touch_stats = event_engine_->GetEventStats(EventType::TOUCH_TAP);
        cJSON* touch_stats_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(touch_stats_obj, "total_count", touch_stats.total_count);
        cJSON_AddNumberToObject(touch_stats_obj, "processed_count", touch_stats.processed_count);
        cJSON_AddNumberToObject(touch_stats_obj, "dropped_count", touch_stats.dropped_count);
        cJSON_AddNumberToObject(touch_stats_obj, "last_event_time", touch_stats.last_event_time_us / 1000); // 转换为ms
        cJSON_AddItemToObject(event_stats, "touch_tap", touch_stats_obj);
        
        auto motion_stats = event_engine_->GetEventStats(EventType::MOTION_SHAKE);
        cJSON* motion_stats_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(motion_stats_obj, "total_count", motion_stats.total_count);
        cJSON_AddNumberToObject(motion_stats_obj, "processed_count", motion_stats.processed_count);
        cJSON_AddNumberToObject(motion_stats_obj, "dropped_count", motion_stats.dropped_count);
        cJSON_AddNumberToObject(motion_stats_obj, "last_event_time", motion_stats.last_event_time_us / 1000);
        cJSON_AddItemToObject(event_stats, "motion_shake", motion_stats_obj);
        
        cJSON_AddItemToObject(status, "event_statistics", event_stats);
        
        // 事件引擎健康状态
        cJSON_AddBoolToObject(status, "motion_engine_available", event_engine_->motion_engine_ != nullptr);
        cJSON_AddBoolToObject(status, "touch_engine_available", event_engine_->touch_engine_ != nullptr);
        cJSON_AddBoolToObject(status, "event_processor_available", event_engine_->event_processor_ != nullptr);
        
        char* json_str = cJSON_Print(status);
        std::string result(json_str);
        cJSON_free(json_str);
        cJSON_Delete(status);
        
        return ReturnValue(result);
    });

// 系统状态查询
mcp_server.AddTool("self.status.system", "查询设备系统状态", PropertyList(),
    [this](const PropertyList& properties) -> ReturnValue {
        cJSON* status = cJSON_CreateObject();
        
        // 子系统可用性
        cJSON_AddBoolToObject(status, "motion_available", motion_skill_ != nullptr);
        cJSON_AddBoolToObject(status, "vibration_available", vibration_skill_ != nullptr);
        cJSON_AddBoolToObject(status, "display_available", GetDisplay() != nullptr);
        cJSON_AddBoolToObject(status, "event_engine_available", event_engine_ != nullptr);
        
        // 当前状态
        std::string current_emotion = GetCurrentEmotion();
        cJSON_AddStringToObject(status, "current_emotion", current_emotion.c_str());
        
        // 动作状态
        if (motion_skill_) {
            cJSON_AddBoolToObject(status, "motion_busy", motion_skill_->IsBusy());
        }
        
        // 触摸状态
        if (event_engine_) {
            cJSON_AddBoolToObject(status, "left_touched", event_engine_->IsLeftTouched());
            cJSON_AddBoolToObject(status, "right_touched", event_engine_->IsRightTouched());
            cJSON_AddBoolToObject(status, "picked_up", event_engine_->IsPickedUp());
            cJSON_AddBoolToObject(status, "upside_down", event_engine_->IsUpsideDown());
        }
        
        char* json_str = cJSON_Print(status);
        std::string result(json_str);
        cJSON_free(json_str);
        cJSON_Delete(status);
        
        return ReturnValue(result);
    });
```

### 测试点 6.1
**测试方法**: 查询系统状态，验证返回的JSON格式和内容正确性
**期望**: 返回完整的系统状态信息，格式正确

---

## Phase 7: 板级集成和测试 [⏱️ 45分钟]

### 7.1 完整集成到ALichuangTest
- [ ] 在构造函数中调用`InitializeMcpTools()`
- [ ] 确保在所有skills初始化之后调用
- [ ] 添加错误处理和日志记录

### 7.2 MCP工具测试验证
- [ ] 验证所有工具正确注册
- [ ] 测试参数验证功能
- [ ] 测试返回值格式

```cpp
// 在ALichuangTest构造函数中的完整集成
ALichuangTest() : boot_button_(BOOT_BUTTON_GPIO) {
    // ... 现有初始化代码 ...
    
    InitializeVibration();
    InitializeMotion(); 
    InitializeInteractionSystem();
    
    StartVibrationTask();
    StartMotionTask();
    
    // 所有skills初始化完成后，初始化MCP工具
    InitializeMcpTools();
    
    ESP_LOGI(TAG, "ALichuangTest board initialization complete with MCP tools");
}

void ALichuangTest::InitializeMcpTools() {
    ESP_LOGI(TAG, "Initializing MCP local response tools...");
    
    try {
        auto& mcp_server = McpServer::GetInstance();
        
        // 注册所有工具
        RegisterMotionTools(mcp_server);
        RegisterVibrationTools(mcp_server);
        RegisterDisplayTools(mcp_server);
        RegisterComplexExpressionTools(mcp_server);
        RegisterStatusTools(mcp_server);
        
        ESP_LOGI(TAG, "✅ All MCP tools registered successfully");
        
        // 记录已注册的工具数量
        ESP_LOGI(TAG, "MCP Local Response System Ready - Available Tools:");
        ESP_LOGI(TAG, "  - Body Motion Control (3 tools)");
        ESP_LOGI(TAG, "  - Haptic Feedback (2 tools)");
        ESP_LOGI(TAG, "  - Display Animation (2 tools)");
        ESP_LOGI(TAG, "  - Complex Expression (2 tools)");
        ESP_LOGI(TAG, "  - Status Query (2 tools)");
        ESP_LOGI(TAG, "  Total: 11 tools available for LLM interaction");
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Failed to initialize MCP tools: %s", e.what());
    }
}
```

### 测试点 7.1
**完整端到端测试**:
1. 编译并烧录固件
2. 连接MCP客户端
3. 调用每个工具验证功能
4. 测试组合动作的效果
5. 验证错误处理机制

---

## Phase 8: 高级功能和事件集成 [⏱️ 1小时30分钟]

### 8.1 实现事件驱动响应
- [ ] 注册`self.events.configure_response` - 配置事件响应
- [ ] 利用EventEngine的callback系统
- [ ] 支持为不同事件类型设置自动响应动作
- [ ] 集成EventProcessor的处理策略（防抖、节流、冷却）

### 8.2 实现动作序列控制
- [ ] 注册`self.sequence.execute` - 执行动作序列
- [ ] 支持定义多个动作的执行顺序
- [ ] 支持动作间的延时控制
- [ ] 可被事件中断或触发

### 8.3 实现情境模式
- [ ] 注册`self.mode.set` - 设置设备情境模式
- [ ] 支持模式：companion, guard, entertainment, learning
- [ ] 每种模式有不同的默认行为和响应风格
- [ ] 影响事件处理的敏感度和响应强度

### 8.4 实现个性化配置
- [ ] 注册`self.config.personality` - 配置设备个性
- [ ] 支持个性参数：活泼度、敏感度、响应强度
- [ ] 影响所有动作的执行风格和事件响应阈值

```cpp
// 事件驱动响应配置（基于event_engine.cc的callback系统）
mcp_server.AddTool("self.events.configure_response",
    "配置设备对特定事件的自动响应。基于EventEngine的callback机制：\n"
    "event_type: 事件类型 (touch_tap/motion_shake/motion_pickup等)\n"
    "response_motion: 响应的身体动作 (可选)\n"
    "response_vibration: 响应的振动模式 (可选)\n"
    "response_emotion: 响应的情绪动画 (可选)\n"
    "enabled: 是否启用此响应",
    PropertyList({
        Property("event_type", kPropertyTypeString),
        Property("response_motion", kPropertyTypeString),
        Property("response_vibration", kPropertyTypeString), 
        Property("response_emotion", kPropertyTypeString),
        Property("enabled", kPropertyTypeBool)
    }), [this](const PropertyList& properties) -> ReturnValue {
        const std::string& event_type = properties["event_type"].value<std::string>();
        bool enabled = properties["enabled"].value<bool>();
        
        if (!event_engine_) {
            return ReturnValue(false, "Event engine not available");
        }
        
        // 将字符串转换为EventType枚举
        EventType type = EventType::MOTION_NONE;
        if (event_type == "touch_tap") type = EventType::TOUCH_TAP;
        else if (event_type == "motion_shake") type = EventType::MOTION_SHAKE;
        else if (event_type == "motion_pickup") type = EventType::MOTION_PICKUP;
        else if (event_type == "motion_flip") type = EventType::MOTION_FLIP;
        else {
            return ReturnValue(false, "Unknown event type: " + event_type);
        }
        
        if (enabled) {
            // 注册事件回调（利用EventEngine的RegisterCallback方法）
            event_engine_->RegisterCallback(type, [this, properties](const Event& event) {
                // 执行配置的响应动作
                if (properties.find("response_motion") != properties.end()) {
                    std::string motion = properties["response_motion"].value<std::string>();
                    // 执行对应的motion动作
                }
                if (properties.find("response_vibration") != properties.end()) {
                    std::string vibration = properties["response_vibration"].value<std::string>();
                    // 执行对应的vibration动作
                }
                if (properties.find("response_emotion") != properties.end()) {
                    std::string emotion = properties["response_emotion"].value<std::string>();
                    SetCurrentEmotion(emotion);
                }
                ESP_LOGI(TAG, "Auto-response triggered for event: %s", event_type.c_str());
            });
            
            ESP_LOGI(TAG, "Event response configured: %s", event_type.c_str());
            return ReturnValue(true, "Event response " + event_type + " configured");
        } else {
            // TODO: 实现取消特定事件回调的逻辑
            ESP_LOGI(TAG, "Event response disabled: %s", event_type.c_str());
            return ReturnValue(true, "Event response " + event_type + " disabled");
        }
    });

// 动作序列控制
mcp_server.AddTool("self.sequence.execute", 
    "执行一系列连续动作。actions参数是动作数组，每个动作包含：\n"
    "type: 动作类型 (motion/vibration/emotion)\n"
    "action: 具体动作名称\n"
    "delay: 执行前延时(ms)\n"
    "duration: 持续时间(ms，可选)\n"
    "可通过事件系统中断或被触发",
    PropertyList({
        Property("actions", kPropertyTypeArray)
    }), [this](const PropertyList& properties) -> ReturnValue {
        // TODO: 实现动作序列解析和执行逻辑
        // 这需要创建一个异步任务来按顺序执行动作
        // 可以利用EventEngine的事件系统来实现序列中断和状态管理
        ESP_LOGI(TAG, "Action sequence execution requested");
        return ReturnValue(true, "Action sequence started");
    });

// 情境模式设置  
mcp_server.AddTool("self.mode.set",
    "设置设备情境模式，影响整体行为风格：\n"
    "companion: 陪伴模式 - 温和亲切的响应\n"
    "guard: 守护模式 - 警觉敏锐的响应\n" 
    "entertainment: 娱乐模式 - 活泼有趣的响应\n"
    "learning: 学习模式 - 专注认真的响应",
    PropertyList({
        Property("mode", kPropertyTypeString)
    }), [this](const PropertyList& properties) -> ReturnValue {
        const std::string& mode = properties["mode"].value<std::string>();
        
        // TODO: 保存模式设置，影响后续动作的执行风格
        ESP_LOGI(TAG, "Device mode set to: %s", mode.c_str());
        return ReturnValue(true, "Mode " + mode + " activated");
    });
```

### 测试点 8.1
**测试复杂交互场景**:
- 测试事件驱动响应：触摸后自动执行响应动作
- 测试连续动作执行：验证序列播放和中断机制
- 验证不同情境模式的效果差异
- 测试个性化参数对动作风格和事件响应的影响
- 验证事件统计和状态查询的准确性

---

## 验证清单

### MCP工具注册验证
- [ ] 所有工具成功注册到MCP服务器
- [ ] 工具描述清晰准确
- [ ] 参数类型和范围定义正确
- [ ] 错误处理机制完善

### 功能验证
- [ ] 直流马达动作控制正常
- [ ] 振动马达反馈正常
- [ ] 屏幕动画显示正常
- [ ] 复合动作协调执行
- [ ] 状态查询返回准确

### 集成验证  
- [ ] 与现有系统无冲突
- [ ] MCP协议通信正常
- [ ] 多个工具可同时工作
- [ ] 错误不会影响其他功能

### 性能验证
- [ ] 动作执行响应及时
- [ ] 系统稳定性良好
- [ ] 内存使用合理
- [ ] CPU占用正常

---

## 使用示例

### 基础动作控制
```json
// 让设备做开心摇摆
{"tool": "self.body.basic_motion", "params": {"action": "happy_wiggle"}}

// 控制设备转到45度角
{"tool": "self.body.angle_control", "params": {"angle": 45, "speed": "medium"}}
```

### 情绪表达
```json
// 表达喜悦情绪（综合动作）
{"tool": "self.express.emotion", "params": {"emotion": "joy", "intensity": 4}}

// 显示愤怒动画
{"tool": "self.display.show_emotion", "params": {"emotion": "angry"}}
```

### 交互响应
```json
// 表示同意
{"tool": "self.interact.respond", "params": {"response_type": "agree"}}

// 庆祝动作
{"tool": "self.interact.respond", "params": {"response_type": "celebrate"}}
```

### 状态查询
```json
// 查询系统状态
{"tool": "self.status.system", "params": {}}

// 查询动作状态  
{"tool": "self.status.motion", "params": {}}

// 查询事件系统状态（包括触摸、运动检测、事件统计）
{"tool": "self.status.events", "params": {}}
```

### 事件驱动响应
```json
// 配置触摸时自动摇摆
{"tool": "self.events.configure_response", "params": {
  "event_type": "touch_tap", 
  "response_motion": "happy_wiggle",
  "response_vibration": "short_buzz",
  "enabled": true
}}

// 配置设备被拿起时显示惊讶表情
{"tool": "self.events.configure_response", "params": {
  "event_type": "motion_pickup",
  "response_emotion": "surprised",
  "enabled": true
}}
```

---

## 大模型应用场景

### 1. 情感陪伴场景
**用户**: "我今天心情不好"
**大模型**: 调用`self.express.emotion`表达关心 + `self.interact.respond`提供安慰

### 2. 互动游戏场景  
**用户**: "我们来玩个游戏吧"
**大模型**: 调用`self.display.show_emotion`显示兴奋 + `self.express.emotion`表达期待

### 3. 学习辅助场景
**用户**: "答对了！"
**大模型**: 调用`self.interact.respond("celebrate")`庆祝 + `self.haptic.basic_vibration("giggle")`

### 4. 状态反馈场景
**大模型**: 思考时调用`self.state.indicate("thinking")` + 回答时调用`self.state.indicate("ready")`

### 5. 智能事件响应场景
**用户**: 触摸设备
**系统**: 自动触发配置的响应动作（基于EventEngine回调）
**大模型**: 通过`self.status.events`查询触摸统计，调整后续交互策略

### 6. 复杂交互场景
**用户**: "陪我玩一会儿"
**大模型**: 
1. 配置事件响应：`self.events.configure_response`（触摸→开心动作）
2. 设置娱乐模式：`self.mode.set("entertainment")`
3. 表达兴奋：`self.express.emotion("excitement")`
4. 定期查询状态：`self.status.events`了解用户互动频率

---

## 时间估算

- Phase 1-2: 2小时（MCP框架 + 直流马达控制）
- Phase 3-4: 2小时（振动控制 + 屏幕动画）  
- Phase 5-6: 2小时（复合动作 + 事件状态查询）
- Phase 7: 0.75小时（集成测试）
- Phase 8: 1.5小时（事件集成 + 高级功能）
- **总计**: 约8.25小时

建议分3-4个开发周期完成，每个周期2-3小时，确保充分测试每个阶段的功能。

---

## 完成标准

✅ 所有MCP工具成功注册并可调用  
✅ 直流马达22种动作可通过MCP控制  
✅ 振动马达10种模式可通过MCP控制  
✅ 屏幕情绪动画可通过MCP控制  
✅ 复合动作协调执行效果良好  
✅ 状态查询返回准确信息  
✅ 大模型可以流畅调用本地响应功能  
✅ 系统稳定性和性能满足要求  
✅ 代码review通过  
✅ 文档更新完成