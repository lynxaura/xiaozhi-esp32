# 事件命名统一化执行计划

## 背景与问题

当前 ALichuangTest 板载交互系统存在事件命名不一致的问题，在不同层次使用了不同的命名规范：

### 现状分析

| 层级 | 文件位置 | 命名示例 | 问题 |
|------|---------|---------|------|
| 传感器层 | multitouch_engine.h | `SINGLE_TAP`, `HOLD` | 与最终输出不一致 |
| 事件引擎层 | event_engine.h | `TOUCH_TAP`, `TOUCH_LONG_PRESS` | 转换逻辑分散 |
| 情感引擎层 | emotion_engine.cc | `"Touch_Tap"`, `"TOUCH_LONG_PRESS"` | 格式混乱 |
| 上传层 | event_uploader.cc | `"Touch_Left_Tap"`, `"Motion_FreeFall"` | 命名风格不统一 |

### 核心问题
1. **命名格式不一致**：混用了 SCREAMING_SNAKE_CASE、PascalCase、Mixed_Case
2. **映射逻辑分散**：每个组件都有自己的命名转换逻辑
3. **维护困难**：添加新事件需要在多处修改
4. **配置兼容性风险**：SD卡配置依赖硬编码的字符串

## 解决方案架构

### 核心设计：集中式命名管理系统

创建 `event_names.h/.cc` 作为单一事实来源（Single Source of Truth），管理所有事件命名映射。

```cpp
class EventNames {
public:
    // 获取各种格式的事件名称
    static const char* GetInternalName(EventType type);
    static const char* GetUploadName(EventType type);
    static std::string BuildTouchEventName(EventType type, TouchPosition pos);
    
    // 配置文件兼容性
    static EventType ParseConfigName(const std::string& name);
    static bool ValidateConfigFile(const char* json_content);
    
    // 显示名称本地化
    static const char* GetDisplayName(EventType type, const char* lang);
};
```

### 命名规范标准

| 用途 | 格式 | 示例 |
|------|------|------|
| 枚举值 | SCREAMING_SNAKE_CASE | `TOUCH_LONG_PRESS`, `MOTION_SHAKE` |
| 配置键名 | 与枚举值一致 | `"TOUCH_TAP"`, `"MOTION_FREE_FALL"` |
| 上传字符串（触摸） | Touch_[Position]_[Action] | `"Touch_Left_Tap"`, `"Touch_Both_LongPress"` |
| 上传字符串（运动） | Motion_[Action] | `"Motion_FreeFall"`, `"Motion_ShakeViolently"` |
| 显示名称 | 本地化友好 | `"左侧轻触"`, `"Left Tap"` |

## 实施计划

### 第一阶段：基础架构（第1-2天）

#### 1. 创建 event_names.h
```cpp
// interaction/core/event_names.h
#ifndef EVENT_NAMES_H
#define EVENT_NAMES_H

#include "event_engine.h"
#include <string>
#include <map>

// 触摸位置枚举
enum class TouchPosition {
    LEFT,
    RIGHT,
    BOTH,
    ANY
};

class EventNames {
public:
    // 基础命名映射
    struct EventNameMapping {
        EventType type;
        const char* internal_name;      // 内部标准名称
        const char* upload_base;        // 上传基础名称
        const char* display_cn;         // 中文显示
        const char* display_en;         // 英文显示
    };
    
    // 获取事件名称
    static const char* GetInternalName(EventType type);
    static const char* GetUploadName(EventType type);
    
    // 触摸事件位置处理
    static std::string BuildTouchEventName(EventType type, TouchPosition pos);
    static std::string GetDisplayNameWithPosition(EventType type, TouchPosition pos, const char* lang = "cn");
    
    // 配置文件兼容性
    static EventType ParseConfigName(const std::string& config_name);
    static bool IsValidConfigName(const std::string& name);
    
    // 工具函数
    static bool IsTouchEvent(EventType type);
    static bool IsMotionEvent(EventType type);
    
private:
    static const EventNameMapping s_mappings[];
    static std::map<std::string, EventType> s_config_aliases;
};

#endif // EVENT_NAMES_H
```

#### 2. 创建 event_names.cc
实现所有映射逻辑，包括：
- 基础事件名称映射表
- 触摸位置组合逻辑
- 配置名称解析（支持多格式）
- 显示名称本地化

### 第二阶段：组件更新（第3-5天）

#### 3. 更新 emotion_engine.cc
- 替换硬编码的事件名称字符串
- 使用 `EventNames::GetInternalName()` 获取统一名称
- 确保所有事件使用一致的格式

#### 4. 更新 event_uploader.cc
- 使用 `EventNames::BuildTouchEventName()` 生成触摸事件名称
- 使用 `EventNames::GetUploadName()` 获取其他事件名称
- 统一上传数据格式

#### 5. 更新 event_config_loader.cc
- 替换 `ParseEventType()` 使用 `EventNames::ParseConfigName()`
- 添加配置格式验证
- 支持新旧格式兼容

### 第三阶段：配置兼容性（第6-7天）

#### 6. 配置文件迁移支持
```cpp
// 在 event_names.cc 中添加
class ConfigMigration {
public:
    // 检查配置格式版本
    static int GetConfigVersion(const char* json_content);
    
    // 迁移旧格式到新格式
    static std::string MigrateToLatestFormat(const char* old_json);
    
    // 验证配置完整性
    static bool ValidateConfig(const char* json_content);
};
```

#### 7. SD卡配置处理
- 修改 `EventConfigLoader::LoadFromFile()` 支持自动格式检测
- 读取时兼容多种格式
- 写入时使用标准格式

### 第四阶段：测试验证（第8-10天）

#### 8. 单元测试
创建 `test_event_names.cc`：
- 测试所有事件类型的命名映射
- 验证触摸位置组合逻辑
- 测试配置名称解析
- 验证新旧格式兼容性

#### 9. 集成测试
- 测试完整的事件处理流程
- 验证上传数据格式
- 测试SD卡配置加载
- 确保向后兼容性

### 第五阶段：文档完善（第11天）

#### 10. 更新文档
- 更新 `event_name_mapping.md` 反映新的统一命名
- 创建迁移指南
- 更新API文档

## 向后兼容性保证

### 配置文件兼容
```cpp
// 支持多种命名格式
static const std::map<std::string, EventType> config_aliases = {
    // 新格式（标准）
    {"TOUCH_TAP", EventType::TOUCH_TAP},
    {"TOUCH_LONG_PRESS", EventType::TOUCH_LONG_PRESS},
    
    // 兼容旧格式（如果存在）
    {"TouchTap", EventType::TOUCH_TAP},
    {"touch_tap", EventType::TOUCH_TAP},
    
    // 其他可能的别名
    {"TAP", EventType::TOUCH_TAP},
    {"HOLD", EventType::TOUCH_LONG_PRESS},
};
```

### 迁移策略
1. **阶段1**：支持读取所有格式，写入使用新格式
2. **阶段2**：提供迁移工具，帮助用户转换配置
3. **阶段3**：在未来版本中逐步废弃旧格式支持

## 风险评估与缓解

### 风险点
1. **配置兼容性**：SD卡上的现有配置可能无法识别
   - 缓解：提供完整的向后兼容支持
   
2. **第三方集成**：外部系统可能依赖现有命名
   - 缓解：保持上传格式稳定，仅内部统一
   
3. **性能影响**：额外的映射查找可能影响性能
   - 缓解：使用静态映射表，编译时优化

## 实施检查清单

- [ ] 创建 event_names.h/.cc 文件
- [ ] 实现基础映射功能
- [ ] 添加触摸位置处理逻辑
- [ ] 实现配置兼容性层
- [ ] 更新 emotion_engine.cc
- [ ] 更新 event_uploader.cc  
- [ ] 更新 event_config_loader.cc
- [ ] 添加单元测试
- [ ] 执行集成测试
- [ ] 更新相关文档
- [ ] 代码审查
- [ ] 性能测试
- [ ] 向后兼容性验证

## 成功标准

1. **一致性**：所有组件使用统一的命名系统
2. **兼容性**：现有配置文件继续正常工作
3. **可维护性**：新事件添加只需在一处修改
4. **性能**：无明显性能下降（<1ms延迟增加）
5. **测试覆盖**：>90%的代码覆盖率

## 时间线

| 阶段 | 任务 | 预计时间 | 依赖 |
|------|------|---------|------|
| 1 | 基础架构搭建 | 2天 | 无 |
| 2 | 组件更新 | 3天 | 阶段1 |
| 3 | 配置兼容性 | 2天 | 阶段2 |
| 4 | 测试验证 | 3天 | 阶段3 |
| 5 | 文档完善 | 1天 | 阶段4 |

总计：11个工作日

## 附录：命名示例对照表

### 触摸事件

| EventType | 内部名称 | 上传名称（左） | 上传名称（右） | 上传名称（双侧） |
|-----------|---------|--------------|--------------|----------------|
| TOUCH_TAP | TOUCH_TAP | Touch_Left_Tap | Touch_Right_Tap | Touch_Both_Tap |
| TOUCH_LONG_PRESS | TOUCH_LONG_PRESS | Touch_Left_LongPress | Touch_Right_LongPress | Touch_Both_LongPress |
| TOUCH_DOUBLE_TAP | TOUCH_DOUBLE_TAP | Touch_Left_DoubleTap | Touch_Right_DoubleTap | Touch_Both_DoubleTap |
| TOUCH_CRADLED | TOUCH_CRADLED | - | - | Touch_Both_Cradled |
| TOUCH_TICKLED | TOUCH_TICKLED | - | - | Touch_Both_Tickled |

### 运动事件

| EventType | 内部名称 | 上传名称 | 中文显示 |
|-----------|---------|---------|---------|
| MOTION_FREE_FALL | MOTION_FREE_FALL | Motion_FreeFall | 自由落体 |
| MOTION_SHAKE | MOTION_SHAKE | Motion_Shake | 摇晃 |
| MOTION_SHAKE_VIOLENTLY | MOTION_SHAKE_VIOLENTLY | Motion_ShakeViolently | 剧烈摇晃 |
| MOTION_FLIP | MOTION_FLIP | Motion_Flip | 翻转 |
| MOTION_PICKUP | MOTION_PICKUP | Motion_Pickup | 拿起 |
| MOTION_UPSIDE_DOWN | MOTION_UPSIDE_DOWN | Motion_UpsideDown | 倒置 |

## 总结

本计划通过创建集中式的事件命名管理系统，解决了当前存在的命名不一致问题。方案确保了向后兼容性，提供了清晰的迁移路径，并为未来的扩展奠定了良好基础。通过分阶段实施，可以最小化风险，确保系统稳定性。