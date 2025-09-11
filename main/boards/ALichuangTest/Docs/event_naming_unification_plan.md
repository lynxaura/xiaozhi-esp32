# 事件命名统一化策略

本文档提供ALichuangTest板载事件系统的命名统一化策略，解决现有系统中存在的命名不一致问题，建立清晰的命名规范体系。

## 现状分析

### 当前命名不一致问题

1. **枚举值命名不统一**
   - `TouchEventType`: `SINGLE_TAP`, `LONG_PRESS` 
   - `EventType`: `TOUCH_TAP`, `TOUCH_LONG_PRESS`
   - 底层与统一层命名不匹配

2. **事件映射完整性问题**
   - **触摸事件映射缺失**：
     - `TouchEventType` 中没有 `DOUBLE_TAP`，但 `EventType` 中有 `TOUCH_DOUBLE_TAP`
     - `RELEASE` 事件映射到 `MOTION_NONE`，导致不会上传
   - **运动事件映射完整**：所有 `MotionEventType` 都有对应的 `EventType` 映射

3. **上传字符串格式不规范**
   - 触摸事件：`Touch_Left_Tap` vs `Touch_Both_LongPress`
   - 运动事件：`Motion_FreeFall` vs `Motion_ShakeViolently` (PascalCase不一致)

4. **配置键名与代码不匹配**
   - 配置文件使用字符串键，但与枚举值对应关系不明确
   - 缺乏统一的键名规范

## 统一命名规范

### 命名规范标准

| 用途 | 格式 | 示例 |
|------|------|------|
| 枚举值 | SCREAMING_SNAKE_CASE | `TOUCH_LONG_PRESS`, `MOTION_SHAKE` |
| 配置键名 | 与枚举值一致 | `"TOUCH_TAP"`, `"MOTION_FREE_FALL"` |
| 上传字符串（触摸） | Touch_[Position]_[Action] | `"Touch_Left_Tap"`, `"Touch_Both_LongPress"` |
| 上传字符串（运动） | Motion_[Action] | `"Motion_FreeFall"`, `"Motion_ShakeViolently"` |
| 自然语言描述 | AI友好的中文描述 | `"主人轻轻拍了我的左侧"`, `"主人用力摇晃我"` |

### 详细规范说明

#### 1. 枚举值命名 (SCREAMING_SNAKE_CASE)

**触摸事件枚举**:
```cpp
enum class TouchEventType {
    NONE,
    SINGLE_TAP,        // 保持现有命名
    DOUBLE_TAP,        // 预留，与SINGLE_TAP对称
    LONG_PRESS,        // 改名：HOLD → LONG_PRESS
    RELEASE,           // 保持现有命名
    CRADLED,           // 保持现有命名
    TICKLED,           // 保持现有命名
};
```

**统一事件枚举** (已规范):
```cpp
enum class EventType {
    MOTION_FREE_FALL,      // 保持现有命名
    MOTION_SHAKE_VIOLENTLY,// 保持现有命名
    MOTION_SHAKE,          // 保持现有命名
    TOUCH_TAP,             // 保持现有命名
    TOUCH_LONG_PRESS,      // 保持现有命名
    TOUCH_CRADLED,         // 保持现有命名
    TOUCH_TICKLED,         // 保持现有命名
};
```

#### 2. 上传字符串命名

**触摸事件上传格式**: `Touch_[Position]_[Action]`
- Position: `Left`, `Right`, `Both` (PascalCase)
- Action: `Tap`, `LongPress`, `Cradled`, `Tickled` (PascalCase)

示例:
```
Touch_Left_Tap
Touch_Right_LongPress  
Touch_Both_Cradled
Touch_Both_Tickled
```

**运动事件上传格式**: `Motion_[Action]`
- Action: 使用PascalCase，保持动作语义清晰

示例:
```
Motion_FreeFall
Motion_ShakeViolently
Motion_Shake
Motion_Flip
Motion_PickUp
Motion_UpsideDown
```

#### 3. 配置键名命名

配置文件中的键名直接使用枚举值的字符串形式：

```json
{
  "event_processing": {
    "TOUCH_TAP": {
      "enabled": true,
      "debounce_ms": 50
    },
    "MOTION_FREE_FALL": {
      "enabled": true,
      "threshold": 0.3
    }
  }
}
```

#### 4. 自然语言描述命名 (event_text)

用于 AI/LLM 理解的自然语言描述，通过 `GenerateEventText()` 函数生成：

**触摸事件描述**:
```cpp
// 实际实现示例
case EventType::TOUCH_TAP:
    switch (position) {
        case TouchPosition::LEFT: return "主人轻轻拍了我的左侧";
        case TouchPosition::RIGHT: return "主人轻轻拍了我的右侧";
        case TouchPosition::BOTH: return "主人同时拍了我的两侧";
    }
case EventType::TOUCH_LONG_PRESS:
    return "主人长时间按住了我的" + position_name;
case EventType::TOUCH_CRADLED:
    return "主人温柔地抱着我";
```

**运动事件描述**:
```cpp
case EventType::MOTION_SHAKE: return "主人轻轻摇了摇我";
case EventType::MOTION_SHAKE_VIOLENTLY: return "主人用力摇晃我";
case EventType::MOTION_FREE_FALL: return "我在自由下落";
```

**用途说明**：
- 上传到云端供 AI 服务理解用户行为
- 情感引擎根据自然语言生成合适的情感反应
- 与 `event_type` (机器可读) 形成互补的描述体系

## 事件映射分析

### 触摸事件映射状态

| TouchEventType | EventType | 映射状态 | 上传状态 |
|----------------|-----------|----------|----------|
| `SINGLE_TAP` | `TOUCH_TAP` | ✅ 已映射 | ✅ 正常上传 |
| `LONG_PRESS` | `TOUCH_LONG_PRESS` | ✅ 已映射 | ✅ 正常上传 |
| `RELEASE` | `MOTION_NONE` | ❌ 映射错误 | ❌ 不会上传 |
| `CRADLED` | `TOUCH_CRADLED` | ✅ 已映射 | ✅ 正常上传 |
| `TICKLED` | `TOUCH_TICKLED` | ✅ 已映射 | ✅ 正常上传 |
| **不存在** | `TOUCH_DOUBLE_TAP` | ❌ 孤立事件 | ❌ 永远不会触发 |

### 运动事件映射状态

| MotionEventType | EventType | 映射状态 | 上传状态 |
|-----------------|-----------|----------|----------|
| `NONE` | `MOTION_NONE` | ✅ 已映射 | ❌ 不上传 |
| `FREE_FALL` | `MOTION_FREE_FALL` | ✅ 已映射 | ✅ 正常上传 |
| `SHAKE_VIOLENTLY` | `MOTION_SHAKE_VIOLENTLY` | ✅ 已映射 | ✅ 正常上传 |
| `FLIP` | `MOTION_FLIP` | ✅ 已映射 | ✅ 正常上传 |
| `SHAKE` | `MOTION_SHAKE` | ✅ 已映射 | ✅ 正常上传 |
| `PICKUP` | `MOTION_PICKUP` | ✅ 已映射 | ✅ 正常上传 |
| `UPSIDE_DOWN` | `MOTION_UPSIDE_DOWN` | ✅ 已映射 | ✅ 正常上传 |

### 关键发现
1. **运动事件映射完美**：所有底层事件都有对应的统一事件，映射关系清晰
2. **触摸事件存在问题**：
   - `TOUCH_DOUBLE_TAP` 是孤立事件，没有来源
   - `RELEASE` 事件被错误映射，不会上传
   - 这表明触摸事件系统的设计不如运动事件系统完整

## 实施策略

### 分层改进方案

采用**分层命名规范**而非集中式命名管理系统，避免增加系统复杂度：

#### 1. 底层传感器层
- 保持现有的 `TouchEventType` 和 `MotionEventType` 枚举
- 仅在必要时进行最小化调整（如 `LONG_PRESS` 命名统一）

#### 2. 统一事件层  
- `EventType` 枚举已基本规范，保持现状
- 确保转换函数 `ConvertTouchEventType` 和 `ConvertMotionEventType` 的映射正确

#### 3. 上传层
- 严格按照上传字符串格式规范实现 `GetEventTypeString`
- 确保所有触摸事件包含位置信息
- 统一运动事件的PascalCase格式

#### 4. 配置层
- 配置键名直接使用 `EventType` 枚举的字符串表示
- 建立配置键名与枚举值的一一对应关系

### 兼容性考虑

#### 向后兼容
- 现有的上传字符串格式已经相对规范，主要做微调
- 配置文件格式保持稳定，仅标准化键名

#### 渐进式改进
1. **阶段1**: 文档化现有命名规范
2. **阶段2**: 修复明显的不一致问题
3. **阶段3**: 建立验证机制防止命名漂移

## 验证机制

### 编译时检查
```cpp
// 确保所有 EventType 都有对应的上传字符串
static_assert(GetEventTypeString(EventType::TOUCH_TAP) != "");
static_assert(GetEventTypeString(EventType::MOTION_SHAKE) != "");
```

### 单元测试
```cpp
// 验证命名格式一致性
TEST(EventNaming, TouchEventFormat) {
    Event event;
    event.type = EventType::TOUCH_TAP;
    event.data.touch_data.position = TouchPosition::LEFT;
    
    std::string name = GetEventTypeString(event);
    EXPECT_EQ(name, "Touch_Left_Tap");
    EXPECT_THAT(name, MatchesRegex("^Touch_(Left|Right|Both)_[A-Z][a-zA-Z]*$"));
}

TEST(EventNaming, MotionEventFormat) {
    Event event;
    event.type = EventType::MOTION_FREE_FALL;
    
    std::string name = GetEventTypeString(event);
    EXPECT_EQ(name, "Motion_FreeFall");
    EXPECT_THAT(name, MatchesRegex("^Motion_[A-Z][a-zA-Z]*$"));
}
```

### 运行时验证
```cpp
void ValidateEventNaming() {
    // 验证所有枚举值都有对应的命名
    for (int i = 0; i < static_cast<int>(EventType::MAX_EVENT_TYPE); i++) {
        EventType type = static_cast<EventType>(i);
        std::string name = GetEventTypeString(Event{type});
        ESP_LOGW_IF(name.empty(), "EventType %d missing upload name", i);
    }
}
```

## 实施检查清单

### 代码更新
- [ ] **映射完整性修复**：
  - [ ] 决定是否在 `TouchEventType` 中添加 `DOUBLE_TAP` 支持
  - [ ] 或者从 `EventType` 中移除孤立的 `TOUCH_DOUBLE_TAP`
  - [ ] 决定是否启用 `RELEASE` 事件的上传（当前映射到 `MOTION_NONE`）
- [ ] **命名一致性**：
  - [x] 确认 `TouchEventType::HOLD` 已重命名为 `LONG_PRESS`
  - [ ] 检查 `GetEventTypeString` 函数的输出格式一致性
  - [ ] 验证所有触摸事件都正确包含位置信息
  - [ ] 统一运动事件上传字符串的PascalCase格式

### 配置更新
- [ ] 标准化配置文件中的事件键名
- [ ] 确保配置键名与 `EventType` 枚举一一对应
- [ ] 更新示例配置文件

### 测试验证
- [ ] 添加命名格式一致性测试
- [ ] 实施编译时命名完整性检查
- [ ] 建立运行时命名验证机制

### 文档维护
- [ ] 更新事件映射文档
- [ ] 建立命名规范参考文档
- [ ] 创建新事件添加指南

## 长期维护策略

### 防止命名漂移
1. **代码审查检查项**：新增事件时必须遵循命名规范
2. **自动化测试**：CI/CD 中包含命名一致性测试
3. **文档同步**：事件变更时同步更新映射文档

### 扩展性考虑
1. **新事件类型**：预留 `AUDIO_*` 和 `SYSTEM_*` 事件的命名空间
2. **多语言支持**：建立本地化显示名称的扩展机制
3. **版本兼容**：为未来的命名格式变更预留兼容性处理

## 参考实施

### 优先级
1. **高优先级**：修复现有的明显命名不一致
2. **中优先级**：建立验证机制防止回退
3. **低优先级**：添加多语言显示名称支持

### 预期效果
- 消除事件命名的歧义和不一致
- 提高代码的可维护性和可读性
- 建立清晰的命名规范，便于新功能扩展
- 减少因命名不规范导致的调试困难

---

*本文档作为ALichuangTest事件系统命名统一化的指导文档，应定期更新以反映系统的演进。*