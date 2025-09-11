# Step 4: 更新 event_uploader.cc 使用 EventNames

## 目标
使用 EventNames 类统一事件上传器中的命名格式，特别是处理触摸事件的位置信息。

## 文件位置
`main/boards/ALichuangTest/interaction/upload/event_uploader.cc`

## 当前问题

```cpp
// 当前的硬编码名称生成
std::string EventUploader::GetEventTypeString(EventType type, TouchPosition position) {
    switch (type) {
        case EventType::TOUCH_TAP:
            switch (position) {
                case TouchPosition::LEFT: return "Touch_Left_Tap";
                case TouchPosition::RIGHT: return "Touch_Right_Tap";
                case TouchPosition::BOTH: return "Touch_Both_Tap";
                default: return "Touch_Tap";
            }
        case EventType::TOUCH_LONG_PRESS:
            switch (position) {
                case TouchPosition::LEFT: return "Touch_Left_LongPress";
                case TouchPosition::RIGHT: return "Touch_Right_LongPress";
                case TouchPosition::BOTH: return "Touch_Both_LongPress";
                default: return "Touch_LongPress";
            }
        // ... 更多 case
    }
}
```

## 实施步骤

### 1. 添加 event_names.h 头文件

```cpp
#include "../core/event_names.h"  // 添加
#include "event_uploader.h"
#include <esp_log.h>
#include <cJSON.h>
// ... 其他包含
```

### 2. 简化 GetEventTypeString 函数

```cpp
std::string EventUploader::GetEventTypeString(EventType type, TouchPosition position) {
    // 旧代码：大量的 switch-case
    // 新代码：使用 EventNames
    
    if (EventNames::IsTouchEvent(type)) {
        // 触摸事件需要处理位置
        return EventNames::BuildTouchEventName(type, position);
    } else {
        // 运动事件直接返回名称
        return EventNames::GetUploadBaseName(type);
    }
}
```

### 3. 更新中文描述生成

```cpp
std::string EventUploader::GetEventDescription(EventType type, TouchPosition position) {
    // 旧代码：硬编码的中文描述
    // 新代码：使用 EventNames 的本地化支持
    
    if (EventNames::IsTouchEvent(type) && EventNames::RequiresPosition(type)) {
        return EventNames::GetDisplayNameWithPosition(type, position, "cn");
    } else {
        return EventNames::GetDisplayName(type, "cn");
    }
}
```

### 4. 更新 PrepareEventData 函数

```cpp
void EventUploader::PrepareEventData(const Event& event, cJSON* json) {
    // 获取事件类型字符串
    std::string event_type_str;
    std::string event_desc;
    
    if (EventNames::IsTouchEvent(event.type)) {
        // 触摸事件：从事件数据中获取位置
        TouchPosition pos = event.data.touch_data.position;
        event_type_str = EventNames::BuildTouchEventName(event.type, pos);
        event_desc = EventNames::GetDisplayNameWithPosition(event.type, pos, "cn");
    } else {
        // 其他事件
        event_type_str = EventNames::GetUploadBaseName(event.type);
        event_desc = EventNames::GetDisplayName(event.type, "cn");
    }
    
    // 添加到 JSON
    cJSON_AddStringToObject(json, "event_type", event_type_str.c_str());
    cJSON_AddStringToObject(json, "description", event_desc.c_str());
    cJSON_AddNumberToObject(json, "timestamp", event.timestamp);
    
    // 添加持续时间（如果有）
    if (event.type == EventType::TOUCH_TAP || 
        event.type == EventType::TOUCH_LONG_PRESS ||
        event.type == EventType::TOUCH_CRADLED) {
        cJSON_AddNumberToObject(json, "duration_ms", event.data.touch_data.duration_ms);
    } else if (event.type == EventType::TOUCH_TICKLED) {
        // TICKLED 事件使用固定窗口时间
        cJSON_AddNumberToObject(json, "duration_ms", 2000);
    } else {
        // 运动事件被视为瞬时事件
        cJSON_AddNumberToObject(json, "duration_ms", 0);
    }
}
```

### 5. 移除硬编码的名称映射

```cpp
// 删除所有硬编码的 switch-case 语句
// 删除以下类似代码：
// switch (type) {
//     case EventType::TOUCH_TAP:
//         return "Touch_Tap";
//     case EventType::MOTION_SHAKE:
//         return "Motion_Shake";
//     // ...
// }
```

### 6. 更新批量上传逻辑

```cpp
void EventUploader::UploadBatch(const std::vector<Event>& events) {
    cJSON* root = cJSON_CreateObject();
    cJSON* events_array = cJSON_CreateArray();
    
    for (const auto& event : events) {
        cJSON* event_json = cJSON_CreateObject();
        
        // 使用统一的命名系统
        PrepareEventData(event, event_json);
        
        cJSON_AddItemToArray(events_array, event_json);
    }
    
    cJSON_AddItemToObject(root, "events", events_array);
    cJSON_AddNumberToObject(root, "batch_size", events.size());
    cJSON_AddNumberToObject(root, "timestamp", esp_timer_get_time() / 1000);
    
    // 发送数据
    char* json_str = cJSON_PrintUnformatted(root);
    SendToServer(json_str);
    
    cJSON_Delete(root);
    free(json_str);
}
```

## 验证步骤

### 测试上传数据格式

```cpp
void TestEventUploader() {
    Event test_event;
    
    // 测试触摸事件
    test_event.type = EventType::TOUCH_TAP;
    test_event.data.touch_data.position = TouchPosition::LEFT;
    test_event.data.touch_data.duration_ms = 100;
    
    std::string name = EventUploader::GetEventTypeString(
        test_event.type, 
        test_event.data.touch_data.position
    );
    
    // 验证输出为 "Touch_Left_Tap"
    assert(name == "Touch_Left_Tap");
    
    // 测试运动事件
    test_event.type = EventType::MOTION_SHAKE;
    name = EventUploader::GetEventTypeString(test_event.type, TouchPosition::ANY);
    
    // 验证输出为 "Motion_Shake"
    assert(name == "Motion_Shake");
}
```

### 检查上传的 JSON 格式

```json
{
    "events": [
        {
            "event_type": "Touch_Left_Tap",
            "description": "左侧轻触",
            "timestamp": 1234567890,
            "duration_ms": 100
        },
        {
            "event_type": "Motion_FreeFall",
            "description": "自由落体",
            "timestamp": 1234567900,
            "duration_ms": 0
        }
    ],
    "batch_size": 2,
    "timestamp": 1234567900
}
```

## 回滚方案

如果出现问题：
1. 恢复原有的 GetEventTypeString 实现
2. 恢复硬编码的 switch-case 逻辑
3. 移除 event_names.h 包含

## 注意事项

1. **保持上传格式稳定**
   - 确保生成的名称格式与服务器端期望一致
   - Touch_[Position]_[Action] 格式
   - Motion_[Action] 格式

2. **持续时间处理**
   - TICKLED 事件固定 2000ms
   - 运动事件为 0ms
   - 其他触摸事件使用实际持续时间

3. **性能优化**
   - EventNames 使用静态查找，性能影响可忽略
   - 避免重复构建字符串

## 完成标准

- [ ] 代码编译通过
- [ ] 上传数据格式正确
- [ ] 触摸位置处理正确
- [ ] 中文描述正确
- [ ] 批量上传正常

## 下一步
完成 event_uploader.cc 更新后，继续 [05_update_event_config_loader.md](05_update_event_config_loader.md) 更新配置加载器。