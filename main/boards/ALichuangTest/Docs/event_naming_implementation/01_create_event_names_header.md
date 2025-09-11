# Step 1: 创建 event_names.h 头文件

## 目标
创建统一的事件命名管理系统头文件，定义 EventNames 类接口。

## 文件位置
`main/boards/ALichuangTest/interaction/core/event_names.h`

## 实施步骤

### 1. 创建头文件

```cpp
#ifndef ALICHUANGTEST_EVENT_NAMES_H
#define ALICHUANGTEST_EVENT_NAMES_H

#include "event_engine.h"
#include <string>
#include <map>

// 触摸位置枚举（从 event_engine.h 移动或重用）
enum class TouchPosition {
    LEFT,
    RIGHT,
    BOTH,
    ANY
};

class EventNames {
public:
    // 事件名称映射结构
    struct EventNameMapping {
        EventType type;
        const char* internal_name;      // 内部标准名称 (SCREAMING_SNAKE_CASE)
        const char* upload_base;        // 上传基础名称 (不含位置)
        const char* display_cn;         // 中文显示名称
        const char* display_en;         // 英文显示名称
        bool requires_position;         // 是否需要位置信息
    };
    
    // === 基础命名接口 ===
    
    // 获取内部标准名称 (用于日志、调试)
    static const char* GetInternalName(EventType type);
    
    // 获取上传基础名称 (不含位置信息)
    static const char* GetUploadBaseName(EventType type);
    
    // === 触摸事件位置处理 ===
    
    // 构建完整的触摸事件上传名称 (含位置)
    static std::string BuildTouchEventName(EventType type, TouchPosition pos);
    
    // 获取位置修饰符
    static const char* GetPositionModifier(TouchPosition pos);
    
    // === 显示名称本地化 ===
    
    // 获取本地化显示名称
    static const char* GetDisplayName(EventType type, const char* lang = "cn");
    
    // 获取带位置的显示名称
    static std::string GetDisplayNameWithPosition(EventType type, 
                                                  TouchPosition pos, 
                                                  const char* lang = "cn");
    
    // === 配置文件兼容性 ===
    
    // 从配置名称解析事件类型 (支持多种格式)
    static EventType ParseConfigName(const std::string& config_name);
    
    // 检查配置名称是否有效
    static bool IsValidConfigName(const std::string& name);
    
    // 获取标准配置名称 (用于写入新配置)
    static const char* GetConfigName(EventType type);
    
    // === 工具函数 ===
    
    // 判断事件类型
    static bool IsTouchEvent(EventType type);
    static bool IsMotionEvent(EventType type);
    static bool RequiresPosition(EventType type);
    
    // 获取所有支持的事件类型
    static std::vector<EventType> GetAllEventTypes();
    
    // === 配置迁移支持 ===
    
    // 配置格式版本
    static int GetConfigVersion(const char* json_content);
    
    // 迁移到最新格式
    static std::string MigrateConfigToLatest(const char* json_content);
    
    // 验证配置完整性
    static bool ValidateConfig(const char* json_content);
    
private:
    // 静态映射表
    static const EventNameMapping s_mappings[];
    
    // 配置名称别名表 (支持向后兼容)
    static const std::map<std::string, EventType> s_config_aliases;
    
    // 查找映射
    static const EventNameMapping* FindMapping(EventType type);
    
    // 初始化别名表
    static void InitializeAliases();
};

#endif // ALICHUANGTEST_EVENT_NAMES_H
```

## 检查项

### 编译检查
- [ ] 头文件保护宏正确
- [ ] 包含必要的头文件
- [ ] 无循环依赖

### 接口设计
- [ ] 提供所有必要的命名转换接口
- [ ] 支持触摸位置处理
- [ ] 支持配置兼容性
- [ ] 支持本地化

### 兼容性
- [ ] 与现有 EventType 枚举兼容
- [ ] 支持多种配置格式解析
- [ ] 提供迁移接口

## 依赖说明

### 需要的头文件
- `event_engine.h` - EventType 枚举定义
- `<string>` - std::string
- `<map>` - std::map
- `<vector>` - std::vector

### 被依赖方
- emotion_engine.cc
- event_uploader.cc
- event_config_loader.cc

## 注意事项

1. **命名规范一致性**
   - 内部名称使用 SCREAMING_SNAKE_CASE
   - 上传名称使用 Category_Action 格式
   - 触摸事件添加位置：Touch_[Position]_[Action]

2. **向后兼容性**
   - 保留对旧配置格式的解析支持
   - 提供别名机制

3. **性能考虑**
   - 使用静态映射表
   - 避免运行时字符串构造

## 测试要点

1. 所有 EventType 都有对应的映射
2. 触摸事件位置组合正确
3. 配置名称解析支持多格式
4. 本地化名称正确返回

## 下一步
完成头文件后，继续 [02_create_event_names_implementation.md](02_create_event_names_implementation.md) 实现具体功能。