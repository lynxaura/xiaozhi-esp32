# Step 11: 更新所有相关文档

## 目标
更新所有与事件命名相关的文档，反映新的统一命名系统，提供完整的开发和维护指南。

## 文档更新列表

### 1. 更新现有文档
- [event_name_mapping.md](../event_name_mapping.md)
- [Sprint.md](../Sprint.md) （如果存在）
- [README.md](../../README.md) （板级文档）

### 2. 创建新文档
- API 文档
- 开发者指南
- 迁移指南
- 最佳实践

## 实施步骤

### 1. 更新 event_name_mapping.md

```markdown
# 事件命名统一化映射对照表

本文档描述了 ALichuangTest 板载交互系统中的统一事件命名系统。

## 系统概述

使用 EventNames 类作为单一事实来源（Single Source of Truth），管理所有事件命名转换。

### 架构图

```
传感器层     事件引擎     EventNames     上传层     显示层
├─TouchEventType → EventType → 统一管理 → 上传格式 → 本地化显示
└─MotionEventType                          │
                                         └─配置兼容
```

## 命名规范

### 标准格式

| 类型 | 格式 | 示例 |
|------|------|------|
| 枚举值 | SCREAMING_SNAKE_CASE | `TOUCH_TAP`, `MOTION_SHAKE` |
| 配置键名 | 与枚举值一致 | `"TOUCH_TAP"`, `"MOTION_FREE_FALL"` |
| 上传格式（触摸） | Touch_[Position]_[Action] | `"Touch_Left_Tap"`, `"Touch_Right_LongPress"` |
| 上传格式（运动） | Motion_[Action] | `"Motion_FreeFall"`, `"Motion_ShakeViolently"` |
| 显示名称 | 本地化友好 | `"左侧轻触"`, `"Left Tap"` |

## 完整事件映射表

### 触摸事件

| EventType | 内部名称 | 上传名称（左侧） | 上传名称（右侧） | 上传名称（双侧） | 中文显示 | 英文显示 |
|-----------|---------|--------------|--------------|----------------|---------|----------|
| TOUCH_TAP | TOUCH_TAP | Touch_Left_Tap | Touch_Right_Tap | Touch_Both_Tap | 轻触 | Tap |
| TOUCH_LONG_PRESS | TOUCH_LONG_PRESS | Touch_Left_LongPress | Touch_Right_LongPress | Touch_Both_LongPress | 长按 | Long Press |
| TOUCH_DOUBLE_TAP | TOUCH_DOUBLE_TAP | Touch_Left_DoubleTap | Touch_Right_DoubleTap | Touch_Both_DoubleTap | 双击 | Double Tap |
| TOUCH_CRADLED | TOUCH_CRADLED | - | - | Touch_Both_Cradled | 托抱 | Cradled |
| TOUCH_TICKLED | TOUCH_TICKLED | - | - | Touch_Both_Tickled | 挠痒 | Tickled |

### 运动事件

| EventType | 内部名称 | 上传名称 | 中文显示 | 英文显示 |
|-----------|---------|---------|---------|----------|
| MOTION_FREE_FALL | MOTION_FREE_FALL | Motion_FreeFall | 自由落体 | Free Fall |
| MOTION_SHAKE | MOTION_SHAKE | Motion_Shake | 摇晃 | Shake |
| MOTION_SHAKE_VIOLENTLY | MOTION_SHAKE_VIOLENTLY | Motion_ShakeViolently | 剧烈摇晃 | Shake Violently |
| MOTION_FLIP | MOTION_FLIP | Motion_Flip | 翻转 | Flip |
| MOTION_PICKUP | MOTION_PICKUP | Motion_Pickup | 拿起 | Pickup |
| MOTION_UPSIDE_DOWN | MOTION_UPSIDE_DOWN | Motion_UpsideDown | 倒置 | Upside Down |

## 兼容性支持

### 配置文件兼容性

EventNames 系统支持多种配置格式：

```json
// 新格式（推荐）
{
    "event_processing_strategies": {
        "touch_events": {
            "TOUCH_TAP": { "strategy": "MERGE" }
        }
    }
}

// 旧格式兼容
{
    "event_processing_strategies": {
        "touch_events": {
            "TouchTap": { "strategy": "MERGE" },     // 支持
            "touch_tap": { "strategy": "MERGE" },    // 支持
            "SINGLE_TAP": { "strategy": "MERGE" }   // 支持
        }
    }
}
```

### 支持的别名映射

| 标准名称 | 支持的别名 |
|----------|----------|
| TOUCH_TAP | TouchTap, Touch_Tap, touch_tap, SINGLE_TAP, TAP |
| TOUCH_LONG_PRESS | TouchLongPress, Touch_Long_Press, touch_long_press, HOLD, LONG_PRESS |
| MOTION_FREE_FALL | FreeFall, FREE_FALL, Motion_FreeFall |
| MOTION_SHAKE | Shake, SHAKE, Motion_Shake |

## 使用示例

### 基本命名转换

```cpp
#include "event_names.h"

// 获取内部名称
const char* name = EventNames::GetInternalName(EventType::TOUCH_TAP);
// 返回: "TOUCH_TAP"

// 构建触摸事件上传名称
std::string upload_name = EventNames::BuildTouchEventName(
    EventType::TOUCH_TAP, TouchPosition::LEFT);
// 返回: "Touch_Left_Tap"

// 获取本地化显示名称
std::string display = EventNames::GetDisplayNameWithPosition(
    EventType::TOUCH_TAP, TouchPosition::LEFT, "cn");
// 返回: "左侧轻触"
```

### 配置解析

```cpp
// 解析配置名称（支持多格式）
EventType type1 = EventNames::ParseConfigName("TOUCH_TAP");     // 标准格式
EventType type2 = EventNames::ParseConfigName("TouchTap");      // 旧格式
EventType type3 = EventNames::ParseConfigName("touch_tap");     // 小写格式
// 全部返回: EventType::TOUCH_TAP

// 检查配置名称有效性
bool valid = EventNames::IsValidConfigName("TOUCH_TAP");  // true
bool invalid = EventNames::IsValidConfigName("UNKNOWN");  // false
```

### 配置迁移

```cpp
// 检查配置版本
int version = EventNames::GetConfigVersion(json_content);

// 迁移到最新版本
std::string migrated = EventNames::MigrateConfigToLatest(old_config);

// 验证配置有效性
bool valid = EventNames::ValidateConfig(config_content);
```

## 性能特性

- **单次命名转换**: < 10μs
- **触摸事件名称构建**: < 50μs
- **配置名称解析**: < 100μs
- **内存开销**: < 4KB
- **无内存泄漏**: 使用静态映射表，线程安全

## 注意事项

1. **线程安全**: EventNames 提供的所有操作都是线程安全的
2. **性能优化**: 使用静态映射表，无运行时开销
3. **向后兼容**: 支持多种旧格式，不破坏现有配置
4. **错误处理**: 未知事件返回默认值，记录警告日志

## 相关文件

- [event_names.h](../interaction/core/event_names.h) - 接口定义
- [event_names.cc](../interaction/core/event_names.cc) - 实现代码
- [event_config_loader.cc](../interaction/config/event_config_loader.cc) - 配置加载
- [event_uploader.cc](../interaction/upload/event_uploader.cc) - 事件上传
- [emotion_engine.cc](../interaction/core/emotion_engine.cc) - 情感引擎
```

### 2. 创建 API 文档

```markdown
# EventNames API 文档

## 类概述

EventNames 类提供了统一的事件命名管理接口，支持命名转换、配置解析和本地化显示。

## 公共接口

### 基础命名接口

#### GetInternalName()
```cpp
static const char* GetInternalName(EventType type);
```
**功能**: 获取事件的内部标准名称

**参数**:
- `type`: 事件类型

**返回值**: 内部名称字符串（SCREAMING_SNAKE_CASE 格式）

**示例**:
```cpp
const char* name = EventNames::GetInternalName(EventType::TOUCH_TAP);
// 返回: "TOUCH_TAP"
```

#### GetUploadBaseName()
```cpp
static const char* GetUploadBaseName(EventType type);
```
**功能**: 获取事件的上传基础名称（不包含位置信息）

### 触摸事件处理

#### BuildTouchEventName()
```cpp
static std::string BuildTouchEventName(EventType type, TouchPosition pos);
```
**功能**: 构建完整的触摸事件上传名称

**参数**:
- `type`: 触摸事件类型
- `pos`: 触摸位置

**返回值**: 完整的上传名称（Touch_[Position]_[Action] 格式）

**特殊处理**:
- TOUCH_CRADLED 和 TOUCH_TICKLED 始终返回 Both 位置
- 非触摸事件直接返回基础名称

#### GetPositionModifier()
```cpp
static const char* GetPositionModifier(TouchPosition pos);
```
**功能**: 获取位置修饰符

### 显示名称本地化

#### GetDisplayName()
```cpp
static const char* GetDisplayName(EventType type, const char* lang = "cn");
```
**功能**: 获取本地化显示名称

**参数**:
- `type`: 事件类型
- `lang`: 语言代码（"cn" 或 "en"）

#### GetDisplayNameWithPosition()
```cpp
static std::string GetDisplayNameWithPosition(EventType type, 
                                              TouchPosition pos, 
                                              const char* lang = "cn");
```
**功能**: 获取带位置信息的显示名称

### 配置文件支持

#### ParseConfigName()
```cpp
static EventType ParseConfigName(const std::string& config_name);
```
**功能**: 从配置名称解析事件类型（支持多种格式）

**支持的格式**:
- 标准格式: "TOUCH_TAP"
- 大小写变体: "TouchTap", "touch_tap"
- 别名: "SINGLE_TAP", "TAP", "HOLD"

#### IsValidConfigName()
```cpp
static bool IsValidConfigName(const std::string& name);
```
**功能**: 检查配置名称是否有效

#### GetConfigName()
```cpp
static const char* GetConfigName(EventType type);
```
**功能**: 获取标准配置名称（用于写入新配置）

### 工具函数

#### IsTouchEvent()
```cpp
static bool IsTouchEvent(EventType type);
```
**功能**: 判断是否为触摸事件

#### IsMotionEvent()
```cpp
static bool IsMotionEvent(EventType type);
```
**功能**: 判断是否为运动事件

#### RequiresPosition()
```cpp
static bool RequiresPosition(EventType type);
```
**功能**: 判断是否需要位置信息

#### GetAllEventTypes()
```cpp
static std::vector<EventType> GetAllEventTypes();
```
**功能**: 获取所有支持的事件类型

### 配置迁移支持

#### GetConfigVersion()
```cpp
static int GetConfigVersion(const char* json_content);
```
**功能**: 检测配置文件版本

#### MigrateConfigToLatest()
```cpp
static std::string MigrateConfigToLatest(const char* json_content);
```
**功能**: 将配置迁移到最新版本

#### ValidateConfig()
```cpp
static bool ValidateConfig(const char* json_content);
```
**功能**: 验证配置文件的有效性

## 线程安全性

EventNames 类的所有公共方法都是线程安全的：
- 使用静态常量数据
- 无全局状态修改
- 只读操作，无竞争条件

## 性能特性

- 所有操作都基于静态映射表
- 无堆内存分配（除 std::string 返回值）
- O(1) 或 O(n) 时间复杂度（n 为事件类型数量）

## 错误处理

- 未知事件类型返回 "UNKNOWN" 或 EventType::MOTION_NONE
- 无效参数返回默认值
- 记录警告日志但不崩溃

## 使用最佳实践

1. **缓存结果**: 对于频繁调用的操作，考虑缓存结果
2. **错误检查**: 始终检查返回值的有效性
3. **日志监控**: 关注警告日志，及时处理未知事件
4. **版本管理**: 使用配置迁移功能处理版本升级
```

### 3. 创建开发者指南

```markdown
# 事件命名系统开发者指南

## 快速开始

### 1. 环境设置

```cpp
// 在你的源文件中包含
#include "event_names.h"
```

### 2. 基本使用

```cpp
// 获取事件的各种名称
EventType event = EventType::TOUCH_TAP;

// 内部名称（用于日志、调试）
const char* internal = EventNames::GetInternalName(event);
ESP_LOGI(TAG, "Processing event: %s", internal);

// 上传名称（用于服务器通信）
std::string upload = EventNames::BuildTouchEventName(event, TouchPosition::LEFT);
send_to_server(upload);

// 显示名称（用于用户界面）
std::string display = EventNames::GetDisplayNameWithPosition(
    event, TouchPosition::LEFT, "cn");
show_to_user(display);
```

### 3. 配置处理

```cpp
// 从配置文件解析事件名称
std::string config_name = "TOUCH_TAP";  // 或 "TouchTap", "touch_tap" 等
EventType type = EventNames::ParseConfigName(config_name);

if (type != EventType::MOTION_NONE) {
    // 有效的事件类型
    configure_event_processing(type);
} else {
    ESP_LOGW(TAG, "Unknown event type in config: %s", config_name.c_str());
}
```

## 常见任务

### 添加新的事件类型

1. **更新枚举定义** (`event_engine.h`):
```cpp
enum class EventType {
    // 现有事件...
    TOUCH_NEW_GESTURE,  // 新增的事件
    // ...
};
```

2. **更新映射表** (`event_names.cc`):
```cpp
const EventNames::EventNameMapping EventNames::s_mappings[] = {
    // 现有映射...
    {EventType::TOUCH_NEW_GESTURE, "TOUCH_NEW_GESTURE", "Touch", "新手势", "New Gesture", true},
    // ...
};
```

3. **更新别名表** (如需要):
```cpp
const std::map<std::string, EventType> EventNames::s_config_aliases = {
    // 现有别名...
    {"NEW_GESTURE", EventType::TOUCH_NEW_GESTURE},
    {"TouchNewGesture", EventType::TOUCH_NEW_GESTURE},
    // ...
};
```

4. **更新触摸事件构建逻辑** (如果是触摸事件):
```cpp
std::string EventNames::BuildTouchEventName(EventType type, TouchPosition pos) {
    // 在 switch 语句中添加新 case
    switch (type) {
        // 现有 case...
        case EventType::TOUCH_NEW_GESTURE:
            result += "NewGesture";
            break;
        // ...
    }
}
```

### 处理配置兼容性

```cpp
// 检查配置是否需要迁移
int version = EventNames::GetConfigVersion(config_json);
if (version < CURRENT_CONFIG_VERSION) {
    // 备份原配置
    std::string backup_path = original_path + ".backup";
    backup_config(original_path, backup_path);
    
    // 迁移配置
    std::string migrated = EventNames::MigrateConfigToLatest(config_json);
    if (!migrated.empty()) {
        save_config(original_path, migrated);
        ESP_LOGI(TAG, "Config migrated from v%d to v%d", version, CURRENT_CONFIG_VERSION);
    }
}
```

### 错误处理最佳实践

```cpp
// 始终检查返回值
const char* name = EventNames::GetInternalName(event_type);
if (strcmp(name, "UNKNOWN") == 0) {
    ESP_LOGW(TAG, "Unknown event type: %d", (int)event_type);
    // 处理未知事件的逻辑
    return handle_unknown_event();
}

// 验证配置名称
if (!EventNames::IsValidConfigName(config_name)) {
    ESP_LOGE(TAG, "Invalid config name: %s", config_name.c_str());
    return false;
}

// 验证配置文件
if (!EventNames::ValidateConfig(config_content)) {
    ESP_LOGE(TAG, "Config validation failed");
    // 使用默认配置或报告错误
    return load_default_config();
}
```

## 性能优化技巧

### 1. 避免不必要的字符串创建

```cpp
// 好的做法：缓存结果
static std::string cached_name;
if (cached_name.empty()) {
    cached_name = EventNames::BuildTouchEventName(type, pos);
}
use_name(cached_name);

// 不好的做法：每次都创建
for (int i = 0; i < 1000; i++) {
    std::string name = EventNames::BuildTouchEventName(type, pos);  // 每次都创建
    use_name(name);
}
```

### 2. 使用静态字符串接口

```cpp
// 高效：返回静态字符串指针
const char* name = EventNames::GetInternalName(type);
ESP_LOGI(TAG, "Event: %s", name);

// 低效：不必要的字符串复制
std::string name = std::string(EventNames::GetInternalName(type));
```

### 3. 批量操作优化

```cpp
// 对于需要处理大量事件的情况
void process_events_batch(const std::vector<Event>& events) {
    // 预先获取所有需要的名称
    std::unordered_map<EventType, std::string> name_cache;
    
    for (const auto& event : events) {
        if (name_cache.find(event.type) == name_cache.end()) {
            if (EventNames::IsTouchEvent(event.type)) {
                name_cache[event.type] = EventNames::BuildTouchEventName(
                    event.type, event.touch_position);
            } else {
                name_cache[event.type] = EventNames::GetUploadBaseName(event.type);
            }
        }
        
        process_single_event(event, name_cache[event.type]);
    }
}
```

## 调试和测试

### 启用调试日志

```cpp
// 在编译时定义
#define ENABLE_EVENT_NAMING_DEBUG 1

#if ENABLE_EVENT_NAMING_DEBUG
void debug_event_naming(EventType type, TouchPosition pos) {
    ESP_LOGD(TAG, "Event naming debug:");
    ESP_LOGD(TAG, "  Type: %s", EventNames::GetInternalName(type));
    ESP_LOGD(TAG, "  Upload: %s", EventNames::BuildTouchEventName(type, pos).c_str());
    ESP_LOGD(TAG, "  Display (CN): %s", EventNames::GetDisplayNameWithPosition(type, pos, "cn").c_str());
    ESP_LOGD(TAG, "  Display (EN): %s", EventNames::GetDisplayNameWithPosition(type, pos, "en").c_str());
}
#endif
```

### 单元测试示例

```cpp
#include "unity.h"
#include "event_names.h"

void test_touch_event_naming() {
    // 测试基本功能
    std::string name = EventNames::BuildTouchEventName(
        EventType::TOUCH_TAP, TouchPosition::LEFT);
    TEST_ASSERT_EQUAL_STRING("Touch_Left_Tap", name.c_str());
    
    // 测试特殊情况
    name = EventNames::BuildTouchEventName(
        EventType::TOUCH_CRADLED, TouchPosition::LEFT);
    TEST_ASSERT_EQUAL_STRING("Touch_Both_Cradled", name.c_str());
}

void test_config_compatibility() {
    // 测试多格式支持
    TEST_ASSERT_EQUAL(EventType::TOUCH_TAP, 
                     EventNames::ParseConfigName("TOUCH_TAP"));
    TEST_ASSERT_EQUAL(EventType::TOUCH_TAP, 
                     EventNames::ParseConfigName("TouchTap"));
    TEST_ASSERT_EQUAL(EventType::TOUCH_TAP, 
                     EventNames::ParseConfigName("touch_tap"));
}
```

## 常见问题解决

### Q: 为什么上传名称和内部名称不一样？
A: 为了支持不同的使用场景：
- 内部名称遵循 C++ 命名约定
- 上传名称为了与服务器 API 兼容
- 显示名称为了用户友好

### Q: 如何处理未知的事件类型？
A: EventNames 会返回默认值并记录警告日志，你应该：
1. 检查返回值的有效性
2. 关注警告日志
3. 实现合适的错误处理逻辑

### Q: 配置文件中的事件名称大小写不一致怎么办？
A: EventNames 自动支持多种格式，不需要手动处理。如果需要，可以使用 MigrateConfigToLatest() 将配置迁移到最新格式。

## 相关资源

- [API 文档](event_names_api.md)
- [EventNames 单元测试](../tests/test_event_names.cc)
- [ESP-IDF 日志系统文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/log.html)
```

### 4. 创建迁移指南

```markdown
# 事件命名系统迁移指南

## 概述

本指南帮助开发者从原有的分散式事件命名系统迁移到新的统一 EventNames 系统。

## 迁移检查清单

### Phase 1: 准备工作
- [ ] 备份现有代码
- [ ] 备份 SD 卡配置文件
- [ ] 阅读新系统文档
- [ ] 准备测试环境

### Phase 2: 逐步替换
- [ ] 更新 emotion_engine.cc
- [ ] 更新 event_uploader.cc
- [ ] 更新 event_config_loader.cc
- [ ] 测试每个更改

### Phase 3: 配置迁移
- [ ] 迁移 SD 卡配置文件
- [ ] 验证配置加载
- [ ] 测试向后兼容性

### Phase 4: 验证和清理
- [ ] 运行完整测试套件
- [ ] 性能测试
- [ ] 清理旧代码
- [ ] 更新文档

## 逐步迁移指南

### Step 1: emotion_engine.cc 迁移

**更改前**:
```cpp
static const std::map<EventType, std::string> EVENT_TO_STRING_MAP = {
    { EventType::TOUCH_TAP, "Touch_Tap" },
    { EventType::TOUCH_LONG_PRESS, "TOUCH_LONG_PRESS" },
    // ...
};
```

**更改后**:
```cpp
#include "event_names.h"

// 移除硬编码映射表
void EmotionEngine::LogEventWithEmotion(EventType event_type) {
    const char* event_name = EventNames::GetInternalName(event_type);
    ESP_LOGI(TAG, "Event: %s, Current Emotion: %s", 
             event_name, GetCurrentEmotionString());
}
```

**验证步骤**:
1. 编译项目：`idf.py build`
2. 检查日志输出格式是否一致
3. 验证所有事件类型都能正确显示

### Step 2: event_uploader.cc 迁移

**更改前**:
```cpp
std::string EventUploader::GetEventTypeString(EventType type, TouchPosition position) {
    switch (type) {
        case EventType::TOUCH_TAP:
            switch (position) {
                case TouchPosition::LEFT: return "Touch_Left_Tap";
                // ...
            }
        // ...
    }
}
```

**更改后**:
```cpp
#include "../core/event_names.h"

std::string EventUploader::GetEventTypeString(EventType type, TouchPosition position) {
    if (EventNames::IsTouchEvent(type)) {
        return EventNames::BuildTouchEventName(type, position);
    } else {
        return EventNames::GetUploadBaseName(type);
    }
}
```

**验证步骤**:
1. 测试所有触摸事件的上传名称
2. 验证上传数据格式符合服务器期望
3. 检查特殊事件（CRADLED, TICKLED）的处理

### Step 3: event_config_loader.cc 迁移

**更改前**:
```cpp
EventType EventConfigLoader::ParseEventType(const std::string& type_str) {
    if (type_str == "TOUCH_TAP") return EventType::TOUCH_TAP;
    // ... 更多硬编码
}
```

**更改后**:
```cpp
#include "../core/event_names.h"

EventType EventConfigLoader::ParseEventType(const std::string& type_str) {
    EventType type = EventNames::ParseConfigName(type_str);
    if (type == EventType::MOTION_NONE) {
        ESP_LOGW(TAG, "Unknown event type in config: %s", type_str.c_str());
    }
    return type;
}
```

**验证步骤**:
1. 测试现有配置文件能正常加载
2. 测试多种格式的事件名称
3. 验证错误处理逻辑

## 配置文件迁移

### 自动迁移

使用 EventNames::MigrateConfigToLatest() 自动迁移：

```cpp
// 读取旧配置
std::string old_config = read_config_from_sd("/sdcard/event_config.json");

// 检查版本
int version = EventNames::GetConfigVersion(old_config.c_str());
if (version < CURRENT_CONFIG_VERSION) {
    // 备份原文件
    write_config_to_sd("/sdcard/event_config.json.backup", old_config);
    
    // 迁移配置
    std::string new_config = EventNames::MigrateConfigToLatest(old_config.c_str());
    if (!new_config.empty()) {
        write_config_to_sd("/sdcard/event_config.json", new_config);
        ESP_LOGI(TAG, "Config migrated from v%d to v%d", version, CURRENT_CONFIG_VERSION);
    }
}
```

### 手动迁移示例

**v1 配置文件**:
```json
{
    "event_processing_strategies": {
        "touch_events": {
            "TouchTap": { "strategy": "MERGE" },
            "SINGLE_TAP": { "strategy": "IMMEDIATE" },
            "touch_long_press": { "strategy": "COOLDOWN" }
        },
        "motion_events": {
            "FreeFall": { "strategy": "IMMEDIATE" },
            "Shake": { "strategy": "THROTTLE" }
        }
    }
}
```

**迁移后的 v2 配置文件**:
```json
{
    "config_version": 2,
    "event_processing_strategies": {
        "touch_events": {
            "TOUCH_TAP": { "strategy": "MERGE" },
            "TOUCH_LONG_PRESS": { "strategy": "COOLDOWN" }
        },
        "motion_events": {
            "MOTION_FREE_FALL": { "strategy": "IMMEDIATE" },
            "MOTION_SHAKE": { "strategy": "THROTTLE" }
        }
    }
}
```

## 常见问题解决

### 问题 1: 编译错误
**错误**: `error: 'EventNames' was not declared in this scope`

**解决**:
1. 检查是否包含了 `event_names.h`
2. 检查 CMakeLists.txt 中是否添加了 `event_names.cc`
3. 检查头文件路径是否正确

### 问题 2: 上传名称格式不正确
**现象**: 服务器收到的事件名称与期望不符

**解决**:
1. 检查是否使用了 `BuildTouchEventName()` 而非 `GetUploadBaseName()`
2. 验证触摸位置参数是否正确
3. 检查特殊事件的处理逻辑

### 问题 3: 配置文件无法加载
**现象**: SD 卡上的旧配置文件无法正常加载

**解决**:
1. 使用 `EventNames::ValidateConfig()` 检查配置格式
2. 查看错误日志，确定具体问题
3. 尝试手动迁移或使用默认配置

## 回滚计划

如果遇到严重问题需要回滚：

1. **保留原有代码**：使用 git 分支进行迁移
2. **恢复步骤**：
   ```bash
   git checkout main
   git branch -D migration_branch
   ```
3. **恢复配置**：从备份恢复 SD 卡配置文件

## 迁移后验证

完成迁移后，进行以下验证：

1. **功能验证**：所有事件类型都能正确处理
2. **性能验证**：运行性能测试套件
3. **兼容性验证**：测试各种配置格式
4. **稳定性验证**：连续运行 24 小时

## 相关资源

- [EventNames API 文档](event_names_api.md)
- [开发者指南](developer_guide.md)
- [单元测试](../tests/)
- [性能测试](../tests/performance/)
```

## 完成标准

- [ ] 所有现有文档已更新
- [ ] API 文档完整准确
- [ ] 开发者指南易于理解
- [ ] 迁移指南提供明确步骤
- [ ] 所有示例代码可用
- [ ] 文档互相关联一致

## 总结

通过完成这些文档更新，开发者将能够：

1. **快速上手**：通过开发者指南快速学会使用 EventNames
2. **安全迁移**：通过迁移指南平滑从旧系统升级
3. **正确使用**：通过 API 文档理解所有接口的用法
4. **解决问题**：通过最佳实践和 FAQ 快速解决常见问题

这套文档系统将保障事件命名系统的长期可维护性和可扩展性。