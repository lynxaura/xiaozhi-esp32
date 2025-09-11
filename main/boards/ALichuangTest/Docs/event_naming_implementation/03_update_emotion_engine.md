# Step 3: 更新 emotion_engine.cc 事件映射

## 目标
统一 emotion_engine.cc 中的事件名称映射，使用 EventNames 类提供的标准名称。

## 文件位置
`main/boards/ALichuangTest/interaction/core/emotion_engine.cc`

## 当前问题

```cpp
// 当前的不一致映射
static const std::map<EventType, std::string> EVENT_TO_STRING_MAP = {
    { EventType::TOUCH_TAP, "Touch_Tap" },           // Mixed case
    { EventType::TOUCH_LONG_PRESS, "TOUCH_LONG_PRESS" }, // ALL CAPS
    { EventType::TOUCH_CRADLED, "TOUCH_CRADLED" },       // ALL CAPS
    { EventType::TOUCH_TICKLED, "TOUCH_TICKLED" },       // ALL CAPS
    { EventType::MOTION_FREE_FALL, "MOTION_FREE_FALL" }, // Consistent
    // ...
};
```

## 实施步骤

### 1. 添加 event_names.h 头文件包含

```cpp
#include "event_names.h"  // 添加这行
#include "emotion_engine.h"
#include <esp_log.h>
// ... 其他包含
```

### 2. 替换硬编码的事件映射表

#### 选项 A: 完全移除静态映射表（推荐）

```cpp
// 删除原有的 EVENT_TO_STRING_MAP
// static const std::map<EventType, std::string> EVENT_TO_STRING_MAP = { ... };

// 修改使用映射表的地方
void EmotionEngine::LogEventWithEmotion(EventType event_type) {
    // 旧代码：
    // auto it = EVENT_TO_STRING_MAP.find(event_type);
    // if (it != EVENT_TO_STRING_MAP.end()) {
    //     ESP_LOGI(TAG, "Event: %s, Current Emotion: %s", 
    //              it->second.c_str(), GetCurrentEmotionString());
    // }
    
    // 新代码：
    const char* event_name = EventNames::GetInternalName(event_type);
    ESP_LOGI(TAG, "Event: %s, Current Emotion: %s", 
             event_name, GetCurrentEmotionString());
}
```

#### 选项 B: 保留映射表但使用 EventNames 生成

```cpp
// 如果必须保留映射表结构
static std::map<EventType, std::string> BuildEventStringMap() {
    std::map<EventType, std::string> map;
    auto event_types = EventNames::GetAllEventTypes();
    
    for (auto type : event_types) {
        map[type] = EventNames::GetInternalName(type);
    }
    
    return map;
}

static const std::map<EventType, std::string> EVENT_TO_STRING_MAP = BuildEventStringMap();
```

### 3. 更新事件处理函数

```cpp
void EmotionEngine::HandleEvent(const Event& event) {
    // 使用 EventNames 获取事件描述
    const char* event_name = EventNames::GetInternalName(event.type);
    
    // 如果是触摸事件，可能需要位置信息
    if (EventNames::IsTouchEvent(event.type)) {
        std::string full_name = EventNames::BuildTouchEventName(
            event.type, 
            event.data.touch_data.position
        );
        ESP_LOGI(TAG, "Handling touch event: %s", full_name.c_str());
    } else {
        ESP_LOGI(TAG, "Handling event: %s", event_name);
    }
    
    // 原有的情感更新逻辑...
    UpdateEmotionBasedOnEvent(event);
}
```

### 4. 更新调试输出

```cpp
void EmotionEngine::DebugPrintEventInfo(EventType type) {
    ESP_LOGI(TAG, "Event Info:");
    ESP_LOGI(TAG, "  Internal Name: %s", EventNames::GetInternalName(type));
    ESP_LOGI(TAG, "  Config Name: %s", EventNames::GetConfigName(type));
    ESP_LOGI(TAG, "  Display (CN): %s", EventNames::GetDisplayName(type, "cn"));
    ESP_LOGI(TAG, "  Display (EN): %s", EventNames::GetDisplayName(type, "en"));
    ESP_LOGI(TAG, "  Is Touch: %s", EventNames::IsTouchEvent(type) ? "Yes" : "No");
    ESP_LOGI(TAG, "  Is Motion: %s", EventNames::IsMotionEvent(type) ? "Yes" : "No");
}
```

### 5. 更新 CMakeLists.txt 依赖

```cmake
# 确保 emotion_engine.cc 能找到 event_names.h
target_include_directories(${COMPONENT_LIB} PRIVATE
    interaction/core
)
```

## 验证步骤

### 编译检查
```bash
idf.py build
```

### 运行时日志验证
```
// 期望看到统一的事件名称格式
I (12345) EmotionEngine: Event: TOUCH_TAP, Current Emotion: happy
I (12346) EmotionEngine: Event: MOTION_SHAKE, Current Emotion: surprised
I (12347) EmotionEngine: Handling touch event: Touch_Left_Tap
```

## 回滚方案

如果出现问题：
1. 恢复原有的 EVENT_TO_STRING_MAP
2. 移除 event_names.h 包含
3. 恢复原有的事件名称获取逻辑

## 注意事项

1. **保持功能不变**
   - 只改变名称格式，不改变业务逻辑
   - 情感转换规则保持不变

2. **日志格式**
   - 确保日志输出格式一致
   - 方便调试和问题追踪

3. **性能影响**
   - EventNames 使用静态查找，性能影响最小
   - 避免在热路径中频繁调用

## 测试要点

1. 所有事件类型都能正确记录
2. 触摸事件位置信息正确
3. 情感状态转换正常
4. 日志输出格式统一

## 完成标准

- [ ] 代码编译通过
- [ ] 事件名称格式统一
- [ ] 日志输出正确
- [ ] 情感功能正常
- [ ] 无性能退化

## 下一步
完成 emotion_engine.cc 更新后，继续 [04_update_event_uploader.md](04_update_event_uploader.md) 更新事件上传器。