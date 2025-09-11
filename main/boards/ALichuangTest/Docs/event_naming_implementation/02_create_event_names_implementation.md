# Step 2: 创建 event_names.cc 实现文件

## 目标
实现 EventNames 类的所有功能，包括映射表定义和各种转换逻辑。

## 文件位置
`main/boards/ALichuangTest/interaction/core/event_names.cc`

## 实施步骤

### 1. 创建实现文件框架

```cpp
#include "event_names.h"
#include <algorithm>
#include <cstring>
#include <esp_log.h>

#define TAG "EventNames"

// 静态映射表定义
const EventNames::EventNameMapping EventNames::s_mappings[] = {
    // === 触摸事件 (需要位置信息) ===
    {EventType::TOUCH_TAP, "TOUCH_TAP", "Touch", "轻触", "Tap", true},
    {EventType::TOUCH_DOUBLE_TAP, "TOUCH_DOUBLE_TAP", "Touch", "双击", "Double Tap", true},
    {EventType::TOUCH_LONG_PRESS, "TOUCH_LONG_PRESS", "Touch", "长按", "Long Press", true},
    {EventType::TOUCH_CRADLED, "TOUCH_CRADLED", "Touch", "托抱", "Cradled", false}, // 始终是Both
    {EventType::TOUCH_TICKLED, "TOUCH_TICKLED", "Touch", "挠痒", "Tickled", false}, // 始终是Both
    
    // === 运动事件 (不需要位置信息) ===
    {EventType::MOTION_FREE_FALL, "MOTION_FREE_FALL", "Motion_FreeFall", "自由落体", "Free Fall", false},
    {EventType::MOTION_SHAKE, "MOTION_SHAKE", "Motion_Shake", "摇晃", "Shake", false},
    {EventType::MOTION_SHAKE_VIOLENTLY, "MOTION_SHAKE_VIOLENTLY", "Motion_ShakeViolently", "剧烈摇晃", "Shake Violently", false},
    {EventType::MOTION_FLIP, "MOTION_FLIP", "Motion_Flip", "翻转", "Flip", false},
    {EventType::MOTION_PICKUP, "MOTION_PICKUP", "Motion_Pickup", "拿起", "Pickup", false},
    {EventType::MOTION_UPSIDE_DOWN, "MOTION_UPSIDE_DOWN", "Motion_UpsideDown", "倒置", "Upside Down", false},
    
    // 结束标记
    {EventType::MOTION_NONE, nullptr, nullptr, nullptr, nullptr, false}
};
```

### 2. 实现配置别名表

```cpp
// 配置名称别名表 (支持多种格式)
const std::map<std::string, EventType> EventNames::s_config_aliases = {
    // === 标准格式 (新) ===
    {"TOUCH_TAP", EventType::TOUCH_TAP},
    {"TOUCH_DOUBLE_TAP", EventType::TOUCH_DOUBLE_TAP},
    {"TOUCH_LONG_PRESS", EventType::TOUCH_LONG_PRESS},
    {"TOUCH_CRADLED", EventType::TOUCH_CRADLED},
    {"TOUCH_TICKLED", EventType::TOUCH_TICKLED},
    
    {"MOTION_FREE_FALL", EventType::MOTION_FREE_FALL},
    {"MOTION_SHAKE", EventType::MOTION_SHAKE},
    {"MOTION_SHAKE_VIOLENTLY", EventType::MOTION_SHAKE_VIOLENTLY},
    {"MOTION_FLIP", EventType::MOTION_FLIP},
    {"MOTION_PICKUP", EventType::MOTION_PICKUP},
    {"MOTION_UPSIDE_DOWN", EventType::MOTION_UPSIDE_DOWN},
    
    // === 兼容旧格式 ===
    // 触摸事件旧名称
    {"TouchTap", EventType::TOUCH_TAP},
    {"Touch_Tap", EventType::TOUCH_TAP},
    {"TAP", EventType::TOUCH_TAP},
    {"SINGLE_TAP", EventType::TOUCH_TAP},
    
    {"TouchLongPress", EventType::TOUCH_LONG_PRESS},
    {"Touch_Long_Press", EventType::TOUCH_LONG_PRESS},
    {"HOLD", EventType::TOUCH_LONG_PRESS},
    {"LONG_PRESS", EventType::TOUCH_LONG_PRESS},
    
    // 运动事件旧名称
    {"FreeFall", EventType::MOTION_FREE_FALL},
    {"FREE_FALL", EventType::MOTION_FREE_FALL},
    
    {"Shake", EventType::MOTION_SHAKE},
    {"SHAKE", EventType::MOTION_SHAKE},
    
    {"ShakeViolently", EventType::MOTION_SHAKE_VIOLENTLY},
    {"SHAKE_VIOLENTLY", EventType::MOTION_SHAKE_VIOLENTLY},
    
    // === 小写格式兼容 ===
    {"touch_tap", EventType::TOUCH_TAP},
    {"touch_long_press", EventType::TOUCH_LONG_PRESS},
    {"motion_shake", EventType::MOTION_SHAKE},
    {"motion_free_fall", EventType::MOTION_FREE_FALL}
};
```

### 3. 实现核心功能

```cpp
// 查找映射
const EventNames::EventNameMapping* EventNames::FindMapping(EventType type) {
    for (const auto& mapping : s_mappings) {
        if (mapping.type == type) {
            return &mapping;
        }
    }
    return nullptr;
}

// 获取内部标准名称
const char* EventNames::GetInternalName(EventType type) {
    const auto* mapping = FindMapping(type);
    return mapping ? mapping->internal_name : "UNKNOWN";
}

// 获取上传基础名称
const char* EventNames::GetUploadBaseName(EventType type) {
    const auto* mapping = FindMapping(type);
    return mapping ? mapping->upload_base : "Unknown";
}

// 获取位置修饰符
const char* EventNames::GetPositionModifier(TouchPosition pos) {
    switch (pos) {
        case TouchPosition::LEFT: return "Left";
        case TouchPosition::RIGHT: return "Right";
        case TouchPosition::BOTH: return "Both";
        case TouchPosition::ANY: return "Any";
        default: return "";
    }
}

// 构建触摸事件完整名称
std::string EventNames::BuildTouchEventName(EventType type, TouchPosition pos) {
    const auto* mapping = FindMapping(type);
    if (!mapping) {
        return "Unknown";
    }
    
    // 运动事件直接返回上传名称
    if (!mapping->requires_position) {
        return mapping->upload_base;
    }
    
    // 触摸事件需要添加位置和动作
    std::string result = mapping->upload_base;
    result += "_";
    result += GetPositionModifier(pos);
    result += "_";
    
    // 根据事件类型添加动作
    switch (type) {
        case EventType::TOUCH_TAP:
            result += "Tap";
            break;
        case EventType::TOUCH_DOUBLE_TAP:
            result += "DoubleTap";
            break;
        case EventType::TOUCH_LONG_PRESS:
            result += "LongPress";
            break;
        case EventType::TOUCH_CRADLED:
            result = "Touch_Both_Cradled"; // 特殊处理
            break;
        case EventType::TOUCH_TICKLED:
            result = "Touch_Both_Tickled"; // 特殊处理
            break;
        default:
            result += "Unknown";
    }
    
    return result;
}

// 获取显示名称
const char* EventNames::GetDisplayName(EventType type, const char* lang) {
    const auto* mapping = FindMapping(type);
    if (!mapping) {
        return "Unknown";
    }
    
    if (strcmp(lang, "cn") == 0) {
        return mapping->display_cn;
    } else {
        return mapping->display_en;
    }
}

// 获取带位置的显示名称
std::string EventNames::GetDisplayNameWithPosition(EventType type, 
                                                   TouchPosition pos, 
                                                   const char* lang) {
    const auto* mapping = FindMapping(type);
    if (!mapping) {
        return "Unknown";
    }
    
    // 不需要位置的事件
    if (!mapping->requires_position) {
        return GetDisplayName(type, lang);
    }
    
    // 构建带位置的显示名称
    std::string result;
    if (strcmp(lang, "cn") == 0) {
        // 中文格式：位置+动作
        switch (pos) {
            case TouchPosition::LEFT: result = "左侧"; break;
            case TouchPosition::RIGHT: result = "右侧"; break;
            case TouchPosition::BOTH: result = "双侧"; break;
            default: result = "";
        }
        result += mapping->display_cn;
    } else {
        // 英文格式：Position + Action
        result = GetPositionModifier(pos);
        if (!result.empty()) {
            result += " ";
        }
        result += mapping->display_en;
    }
    
    return result;
}

// 从配置名称解析事件类型
EventType EventNames::ParseConfigName(const std::string& config_name) {
    auto it = s_config_aliases.find(config_name);
    if (it != s_config_aliases.end()) {
        return it->second;
    }
    
    ESP_LOGW(TAG, "Unknown config name: %s", config_name.c_str());
    return EventType::MOTION_NONE;
}

// 检查配置名称是否有效
bool EventNames::IsValidConfigName(const std::string& name) {
    return s_config_aliases.find(name) != s_config_aliases.end();
}

// 获取标准配置名称
const char* EventNames::GetConfigName(EventType type) {
    return GetInternalName(type); // 使用内部名称作为标准配置名称
}

// 判断是否是触摸事件
bool EventNames::IsTouchEvent(EventType type) {
    return type >= EventType::TOUCH_TAP && type <= EventType::TOUCH_TICKLED;
}

// 判断是否是运动事件
bool EventNames::IsMotionEvent(EventType type) {
    return type >= EventType::MOTION_FREE_FALL && type <= EventType::MOTION_UPSIDE_DOWN;
}

// 判断是否需要位置信息
bool EventNames::RequiresPosition(EventType type) {
    const auto* mapping = FindMapping(type);
    return mapping ? mapping->requires_position : false;
}

// 获取所有支持的事件类型
std::vector<EventType> EventNames::GetAllEventTypes() {
    std::vector<EventType> types;
    for (const auto& mapping : s_mappings) {
        if (mapping.type != EventType::MOTION_NONE) {
            types.push_back(mapping.type);
        }
    }
    return types;
}
```

### 4. 添加到 CMakeLists.txt

```cmake
# 在 main/boards/ALichuangTest/CMakeLists.txt 中添加
set(BOARD_SOURCES
    ...
    interaction/core/event_names.cc
    ...
)
```

## 检查项

### 功能完整性
- [ ] 所有 EventType 都有映射
- [ ] 触摸事件位置处理正确
- [ ] 配置别名表完整
- [ ] 本地化支持完整

### 兼容性
- [ ] 支持所有旧格式名称
- [ ] 大小写兼容
- [ ] 特殊事件处理正确

### 性能
- [ ] 使用静态映射表
- [ ] 避免不必要的字符串拷贝
- [ ] 查找效率优化

## 测试用例

```cpp
// 测试示例
void TestEventNames() {
    // 测试内部名称
    assert(strcmp(EventNames::GetInternalName(EventType::TOUCH_TAP), "TOUCH_TAP") == 0);
    
    // 测试触摸事件构建
    assert(EventNames::BuildTouchEventName(EventType::TOUCH_TAP, TouchPosition::LEFT) == "Touch_Left_Tap");
    
    // 测试配置解析
    assert(EventNames::ParseConfigName("TOUCH_TAP") == EventType::TOUCH_TAP);
    assert(EventNames::ParseConfigName("TouchTap") == EventType::TOUCH_TAP); // 旧格式
    assert(EventNames::ParseConfigName("touch_tap") == EventType::TOUCH_TAP); // 小写
    
    // 测试显示名称
    assert(strcmp(EventNames::GetDisplayName(EventType::MOTION_SHAKE, "cn"), "摇晃") == 0);
}
```

## 注意事项

1. **字符串生命周期**
   - 静态字符串直接返回指针
   - 动态生成的字符串返回 std::string

2. **错误处理**
   - 未知类型返回默认值
   - 记录警告日志

3. **线程安全**
   - 只读操作，线程安全
   - 无全局状态修改

## 下一步
完成实现文件后，继续 [03_update_emotion_engine.md](03_update_emotion_engine.md) 更新 emotion_engine.cc。