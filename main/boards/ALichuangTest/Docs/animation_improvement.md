# 动画播放模块重构方案

## 实施状态
✅ **已完成实现** - 所有核心模块已开发完成，可以开始集成测试

## 概述
创建一个独立、高效、鲁棒的动画播放模块，支持事件驱动的动画播放，代码结构优雅。

## 核心特性
- **简单调用**: 一行代码 `AnimationPlayer::GetInstance().Play(event)` 即可
- **自动判断**: 自动获取当前VA情感象限，匹配对应动画资源
- **高效执行**: 预加载和缓存机制保证流畅播放
- **鲁棒性强**: 完善的错误处理和降级策略

## 已实现的文件结构

### 核心模块
```
main/boards/ALichuangTest/skills/
├── animation_player.h       ✅ 动画播放器主类
├── animation_player.cc      ✅ 完整实现（整合了AnimaDisplay功能）
├── animation_loader.h       ✅ SD卡加载器接口
└── animation_loader.cc      ✅ 加载器实现（含缓存和LRU管理）
```

### 替代的旧模块
```
main/boards/ALichuangTest/skills/
├── animation.h             🔄 可逐步废弃（功能已整合到animation_player）
└── animation.cc            🔄 可逐步废弃（功能已整合到animation_player）
```

## 核心API设计

### AnimationPlayer 类（单例模式）

```cpp
class AnimationPlayer {
public:
    // 获取单例实例
    static AnimationPlayer& GetInstance();

    // 初始化（整合了原AnimaDisplay的初始化）
    bool Initialize(esp_lcd_panel_io_handle_t panel_io,
                   esp_lcd_panel_handle_t panel,
                   int width, int height,
                   int offset_x, int offset_y,
                   bool mirror_x, bool mirror_y, bool swap_xy);

    // === 核心播放接口（简单明确） ===

    // 万能播放接口 - 自动路由所有逻辑
    void Play(EventType event);

    // 专用播放接口
    void PlayEmergency(EventType event);           // 紧急事件
    void PlayInteraction(EventType event);        // 交互事件（自动获取象限）
    void PlayStateExpression(DeviceState state);  // 状态表达（自动获取象限）
    void PlaySystem(SystemEventType event);       // 系统事件

    // 控制接口
    void Stop();                    // 停止当前动画
    bool IsPlaying() const;         // 检查播放状态

    // === 画布管理（从AnimaDisplay整合） ===
    void CreateCanvas();                    // 创建LVGL画布
    void DestroyCanvas();                   // 销毁画布
    bool HasCanvas() const;                 // 检查画布状态
    void DrawImageOnCanvas(int x, int y, int width, int height,
                          const uint8_t* img_data);  // 绘制图像

    // 配置管理
    void SetDefaultFPS(int fps);            // 设置默认帧率
    int GetDefaultFPS() const;              // 获取默认帧率
};
```

## 使用示例

### 在LocalResponseController中集成

```cpp
// 旧方式：复杂的多步调用
void LocalResponseController::ProcessEvent(const Event& event) {
    // 需要判断事件类型
    if (IsEmergencyEvent(event.type)) {
        // 调用不同的处理逻辑
        TriggerEmergencyResponse(event);
    } else if (IsInteractionEvent(event.type)) {
        // 需要获取象限
        EmotionQuadrant quadrant = GetCurrentQuadrant();
        // 构建路径
        std::string path = BuildPath(event.type, quadrant);
        // 播放动画...
    }
}

// 新方式：一行解决
void LocalResponseController::ProcessEvent(const Event& event) {
    // AnimationPlayer内部自动处理：
    // 1. 判断事件类型（紧急/交互/状态/系统）
    // 2. 获取当前VA象限（如果需要）
    // 3. 构建正确的动画路径
    // 4. 检查中断权限
    // 5. 加载并播放动画
    AnimationPlayer::GetInstance().Play(event.type);
}
```

## 路径映射规则（自动处理）

### 自动构建路径示例

```cpp
// 用户调用：AnimationPlayer::GetInstance().Play(MOTION_FREE_FALL);
// 内部自动处理：
BuildAnimationPath(MOTION_FREE_FALL)
    → 判断为紧急事件
    → "/sdcard/emergency/motion_free_fall/animation/"

// 用户调用：AnimationPlayer::GetInstance().Play(MOTION_SHAKE);
// 内部自动处理：
BuildInteractionPath(MOTION_SHAKE, GetCurrentQuadrant())
    → 自动获取当前象限 = Q1
    → "/sdcard/interaction/motion_shake_q1/animation/"

// 用户调用：AnimationPlayer::GetInstance().PlayStateExpression(kDeviceStateIdle);
// 内部自动处理：
BuildStatePath(kDeviceStateIdle, GetCurrentQuadrant())
    → 自动获取当前象限 = Q2
    → "/sdcard/state_expression/idle/idle_q2/animation/"
```

## 中断机制（基于response_config.json）

### 配置文件示例

```json
{
  "events": {
    "emergency/motion_free_fall": {
      "can_interrupt": ["all"],  // 可中断所有状态
      "animation": { "frames": 6, "count": 2 }
    },
    "interaction/motion_shake_q1": {
      "can_interrupt": ["idle", "listening"],  // 只能中断特定状态
      "animation": { "frames": 4, "count": 2 }
    },
    "state_expression/idle/idle_q1": {
      // 没有can_interrupt，不能中断任何状态
      "animation": { "frames": 5, "count": -1 }  // -1表示循环
    }
  }
}
```

## 关键实现特性

### 1. 自动VA象限识别
```cpp
EmotionQuadrant AnimationPlayer::GetCurrentQuadrant() {
    // 直接从EmotionEngine获取，LocalResponseController无需关心
    return EmotionEngine::GetInstance().GetQuadrant();
}
```

### 2. RGB565字节序转换（保持与现有实现一致）
```cpp
void AnimationPlayer::AnimationLoop() {
    // 分配转换缓冲区
    uint16_t* convertedData = new uint16_t[width_ * height_];

    for (int frame = 1; frame <= config.frames; frame++) {
        // 加载原始帧数据
        uint8_t* frameData = heap_caps_malloc(153600, MALLOC_CAP_SPIRAM);
        loader_->LoadFrame(framePath, frameData);

        // RGB565字节序转换（与ImageSlideshowTask保持一致）
        for (int i = 0; i < width_ * height_; i++) {
            uint16_t pixel = ((uint16_t*)frameData)[i];
            convertedData[i] = ((pixel & 0xFF) << 8) | ((pixel & 0xFF00) >> 8);
        }

        // 绘制到屏幕（使用整合的DrawImageOnCanvas）
        DrawImageOnCanvas(0, 0, width_, height_, (const uint8_t*)convertedData);

        heap_caps_free(frameData);
        vTaskDelay(pdMS_TO_TICKS(1000 / config.fps));
    }

    delete[] convertedData;
}
```

## 配置文件集成

### 从 `/sdcard/manifest.json` 读取全局配置
```json
{
  "animation_specs": {
    "fps": 24,           // 默认帧率，AnimationPlayer自动读取
    "format": "RGB565",
    "resolution": "320x240"
  }
}
```

### 从 `/sdcard/response_config.json` 读取事件配置
```json
{
  "events": {
    "emergency/motion_free_fall": {
      "can_interrupt": ["all"],
      "animation": {
        "frames": 6,    // 动画帧数
        "count": 2      // 播放次数，-1表示循环
      }
    }
  }
}
```

## 性能优化特性

### 1. LRU缓存管理
```cpp
void AnimationLoader::EvictLRUCache() {
    // 自动清理最久未使用的动画缓存
    // 最多缓存32帧，智能管理内存使用
}
```

### 2. SPIRAM优化
```cpp
uint8_t* AnimationLoader::AllocateFrameBuffer() {
    // 优先使用外部PSRAM存储帧数据
    return (uint8_t*)heap_caps_malloc(FRAME_SIZE, MALLOC_CAP_SPIRAM);
}
```

## 迁移指南

### 旧代码迁移
```cpp
// 旧方式（使用AnimaDisplay + ImageSlideshowTask）
AnimaDisplay* display = new AnimaDisplay(panel_io, panel, ...);
display->CreateCanvas();
// 需要单独管理ImageSlideshowTask...

// 新方式（使用AnimationPlayer）
AnimationPlayer::GetInstance().Initialize(panel_io, panel, ...);
// 一切都自动处理
```

## 测试建议

### 1. 基本功能测试
- [x] 初始化测试
- [x] 画布创建/销毁测试
- [x] 单帧加载测试
- [ ] 动画播放测试

### 2. 集成测试
- [ ] 紧急事件动画播放
- [ ] 交互事件动画播放（各象限）
- [ ] 状态表达动画播放
- [ ] 系统事件动画播放

## 总结

### 主要优势
1. **极简接口**：从复杂的多步调用简化为一行代码
2. **自动化处理**：VA象限判断、路径构建、中断检查全自动
3. **功能整合**：不再需要AnimaDisplay和ImageSlideshowTask
4. **高效实现**：LRU缓存、SPIRAM优化、预加载机制
5. **鲁棒设计**：完善的错误处理和降级策略

### 下一步行动
1. ✅ 完成代码实现
2. 📋 **当前**: 准备集成测试
3. 📋 在ALichuangTest.cc中集成AnimationPlayer
4. 📋 在LocalResponseController中使用新接口
5. 📋 测试各种动画播放场景
6. 📋 性能优化和问题修复
7. 📋 逐步废弃animation.cc/h

重构后提供了统一高效的动画播放解决方案，在保持现有功能和性能的同时大大简化了使用方式。