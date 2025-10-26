# 步骤4：测试验证

## 目标

本阶段进行全面的功能、性能和稳定性测试,确保Flash资源迁移后所有功能正常工作。

## 任务清单

- [ ] 4.1 功能测试
- [ ] 4.2 性能测试
- [ ] 4.3 内存稳定性测试
- [ ] 4.4 压力测试
- [ ] 4.5 回归测试

---

## 4.1 功能测试

### 4.1.1 GIF动画播放测试

#### 测试矩阵

| 分类 | 动画名称 | 文件路径 | 预期结果 | 实际结果 |
|------|---------|---------|---------|---------|
| **紧急事件** | motion_shake_violently | emergency/... | 显示剧烈摇晃动画 | [ ] |
| | motion_upside_down | emergency/... | 显示倒置警告动画 | [ ] |
| **拾起动作** | motion_pickup_q1~q4 | interaction/... | 显示4个象限拾起动画 | [ ] |
| **摇晃动作** | motion_shake_q1~q4 | interaction/... | 显示4个象限摇晃动画 | [ ] |
| **触摸-抱持** | touch_cradled_q1~q4 | interaction/... | 显示抱持反馈动画 | [ ] |
| **触摸-长按** | touch_long_press_q1~q4 | interaction/... | 显示长按反馈动画 | [ ] |
| **触摸-点击** | touch_tap_q1~q4 | interaction/... | 显示点击反馈动画 | [ ] |
| **触摸-挠痒** | touch_tickled_q1~q4 | interaction/... | 显示挠痒反馈动画 | [ ] |
| **空闲状态** | idle_q1~q4 | state_expression/idle/... | 显示4个象限空闲动画 | [ ] |
| **聆听状态** | listening_q1~q4 | state_expression/listening/... | 显示4个象限聆听动画 | [ ] |
| **说话情感** | calm | state_expression/speaking/... | 显示平静表情 | [ ] |
| | happy | state_expression/speaking/... | 显示开心表情 | [ ] |
| | sad | state_expression/speaking/... | 显示悲伤表情 | [ ] |
| | angry | state_expression/speaking/... | 显示生气表情 | [ ] |
| | scared | state_expression/speaking/... | 显示害怕表情 | [ ] |
| | curious | state_expression/speaking/... | 显示好奇表情 | [ ] |
| | shy | state_expression/speaking/... | 显示害羞表情 | [ ] |
| | content | state_expression/speaking/... | 显示满足表情 | [ ] |
| **系统** | system_boot_up | system/... | 启动动画正常播放 | [ ] |

#### 测试方法

**方法1：通过日志触发（推荐）**

在设备端添加测试命令（临时代码）：

```cpp
// 在ALichuangTest.cc中添加测试函数
void ALichuangTest::TestAllAnimations() {
    auto* display = dynamic_cast<AnimaDisplay*>(GetDisplay());
    if (!display) {
        ESP_LOGE(TAG, "Display is not AnimaDisplay");
        return;
    }

    const char* test_animations[] = {
        "motion_shake_violently", "motion_upside_down",
        "motion_pickup_q1", "motion_pickup_q2", "motion_pickup_q3", "motion_pickup_q4",
        "touch_tap_q1", "touch_tap_q2", "touch_tap_q3", "touch_tap_q4",
        "calm", "happy", "sad", "angry", "scared", "curious", "shy", "content",
        "idle_q1", "listening_q1"
    };

    for (const char* anim : test_animations) {
        ESP_LOGI(TAG, "Testing animation: %s", anim);
        display->SetAnima(anim, 2);  // 播放2次
        vTaskDelay(pdMS_TO_TICKS(3000));  // 等待3秒
    }

    ESP_LOGI(TAG, "All animations tested");
}

// 在构造函数末尾调用
TestAllAnimations();
```

**方法2：通过交互触发**

- 剧烈摇晃设备 → 应显示 `motion_shake_violently`
- 倒置设备 → 应显示 `motion_upside_down`
- 触摸屏幕 → 应显示 `touch_tap_qX`
- 说话（触发TTS） → 应根据情感显示对应表情

#### 验证标准

每个动画应满足：
- [x] 从Flash加载（日志显示 "Loading GIF from Flash"）
- [x] 正确显示在屏幕上
- [x] 动画流畅无卡顿
- [x] 循环次数正确
- [x] 切换到下一个动画无延迟

### 4.1.2 OGG音频播放测试

#### 测试矩阵

| 音频名称 | 文件路径 | 触发场景 | 预期音效 | 实际结果 |
|---------|---------|---------|---------|---------|
| motion_shake_violently | emergency/... | 剧烈摇晃 | 警告音 | [ ] |
| talk_calm | state_expression/speaking/... | 说话（平静） | 平静语调 | [ ] |
| talk_happy | state_expression/speaking/... | 说话（开心） | 欢快语调 | [ ] |
| ... | ... | ... | ... | [ ] |

#### 测试方法

```cpp
// 测试代码
void ALichuangTest::TestAllAudios() {
    auto& app = Application::GetInstance();

    const char* test_audios[] = {
        "motion_shake_violently",
        "talk_calm", "talk_happy", "talk_sad",
        // ... 其他音频
    };

    for (const char* audio : test_audios) {
        ESP_LOGI(TAG, "Testing audio: %s", audio);
        app.PlaySoundOGGFile(audio, 80);  // 音量80%
        vTaskDelay(pdMS_TO_TICKS(2000));  // 等待2秒
    }
}
```

#### 验证标准

- [x] 从Flash加载（日志显示 "Playing OGG from Flash"）
- [x] 音频正常播放
- [x] 音质清晰无噪音
- [x] 音量控制正常
- [x] 播放完成后无杂音

### 4.1.3 联合测试（GIF+OGG）

测试动画和音效同步播放：

```cpp
void TestAnimationWithSound() {
    auto& app = Application::GetInstance();
    auto* display = dynamic_cast<AnimaDisplay*>(Board::GetInstance().GetDisplay());

    // 测试：紧急事件（GIF + OGG同时播放）
    display->SetAnima("motion_shake_violently", -1);  // 循环播放GIF
    app.PlaySoundOGGFile("motion_shake_violently", 100);  // 播放音效

    vTaskDelay(pdMS_TO_TICKS(5000));

    // 验证内存是否正常
    ESP_LOGI(TAG, "Free heap after combined playback: %lu bytes",
             esp_get_free_heap_size());
}
```

**验证标准**：
- [x] GIF和OGG同时加载无冲突
- [x] 内存峰值 <400KB
- [x] 播放流畅无卡顿

---

## 4.2 性能测试

### 4.2.1 加载延迟测试

#### 测试代码

```cpp
void MeasureLoadingTime() {
    auto& assets = *Board::GetInstance().GetLynxAssets();

    // 测试GIF加载延迟
    {
        void* data = nullptr;
        size_t size = 0;

        uint64_t start = esp_timer_get_time();
        bool success = assets.GetResource("emergency/motion_shake_violently/motion_shake_violently",
                                         data, size);
        uint64_t elapsed = esp_timer_get_time() - start;

        ESP_LOGI(TAG, "GIF load time: %llu μs (%.3f ms), success=%d",
                 elapsed, elapsed / 1000.0, success);
    }

    // 测试OGG加载延迟
    {
        void* data = nullptr;
        size_t size = 0;

        uint64_t start = esp_timer_get_time();
        bool success = assets.GetResource("emergency/motion_shake_violently/motion_shake_violently",
                                         data, size);
        uint64_t elapsed = esp_timer_get_time() - start;

        ESP_LOGI(TAG, "OGG load time: %llu μs (%.3f ms), success=%d",
                 elapsed, elapsed / 1000.0, success);
    }
}
```

#### 性能目标

| 指标 | 目标值 | 实际值 | 通过? |
|------|-------|--------|-------|
| **GIF加载延迟** | <5ms | ___ μs | [ ] |
| **OGG加载延迟** | <1ms | ___ μs | [ ] |
| **动画切换延迟** | <10ms | ___ ms | [ ] |

### 4.2.2 内存使用测试

#### 测试代码

```cpp
void MeasureMemoryUsage() {
    auto* display = dynamic_cast<AnimaDisplay*>(Board::GetInstance().GetDisplay());

    // 基线内存
    size_t baseline = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Baseline free heap: %zu bytes", baseline);

    // 加载GIF
    display->SetAnima("emergency/motion_shake_violently", 1);
    vTaskDelay(pdMS_TO_TICKS(500));  // 等待GIF解码完成

    size_t after_gif = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Free heap after GIF load: %zu bytes", after_gif);
    ESP_LOGI(TAG, "GIF memory consumption: %zu bytes", baseline - after_gif);

    // 清理并等待
    display->SetAnima("idle_q1", 1);
    vTaskDelay(pdMS_TO_TICKS(500));

    size_t after_cleanup = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Free heap after cleanup: %zu bytes", after_cleanup);
    ESP_LOGI(TAG, "Memory leak: %zu bytes", baseline - after_cleanup);
}
```

#### 内存目标

| 指标 | 目标值 | 实际值 | 通过? |
|------|-------|--------|-------|
| **单个GIF峰值** | <350KB | ___ KB | [ ] |
| **单个OGG峰值** | <10KB | ___ KB | [ ] |
| **GIF+OGG峰值** | <360KB | ___ KB | [ ] |
| **内存泄漏** | <1KB/次 | ___ KB | [ ] |

---

## 4.3 内存稳定性测试

### 4.3.1 长时间运行测试

#### 测试代码

```cpp
void LongRunningTest() {
    auto* display = dynamic_cast<AnimaDisplay*>(Board::GetInstance().GetDisplay());
    auto& app = Application::GetInstance();

    const char* animations[] = {"happy", "sad", "angry", "calm"};
    const char* audios[] = {"talk_happy", "talk_sad", "talk_angry", "talk_calm"};

    size_t min_heap = esp_get_free_heap_size();
    size_t max_heap = min_heap;

    for (int i = 0; i < 1000; i++) {  // 1000次迭代
        int idx = i % 4;

        // 切换动画
        display->SetAnima(animations[idx], 1);
        vTaskDelay(pdMS_TO_TICKS(500));

        // 播放音效
        app.PlaySoundOGGFile(audios[idx], 80);
        vTaskDelay(pdMS_TO_TICKS(1000));

        // 监控内存
        size_t current_heap = esp_get_free_heap_size();
        if (current_heap < min_heap) min_heap = current_heap;
        if (current_heap > max_heap) max_heap = current_heap;

        // 每100次报告一次
        if (i % 100 == 0) {
            ESP_LOGI(TAG, "[%d/1000] Current: %zu, Min: %zu, Max: %zu, Delta: %zu",
                     i, current_heap, min_heap, max_heap, max_heap - min_heap);
        }
    }

    ESP_LOGI(TAG, "Long running test completed");
    ESP_LOGI(TAG, "Final - Min: %zu, Max: %zu, Delta: %zu",
             min_heap, max_heap, max_heap - min_heap);
}
```

#### 稳定性目标

| 指标 | 目标值 | 实际值 | 通过? |
|------|-------|--------|-------|
| **最小堆内存** | >100KB | ___ KB | [ ] |
| **内存波动** | <±30KB | ___ KB | [ ] |
| **总内存泄漏** | <10KB | ___ KB | [ ] |
| **无崩溃运行** | 1000次 | ___ 次 | [ ] |

---

## 4.4 压力测试

### 4.4.1 快速切换测试

模拟用户快速交互：

```cpp
void RapidSwitchingTest() {
    auto* display = dynamic_cast<AnimaDisplay*>(Board::GetInstance().GetDisplay());

    const char* animations[] = {
        "happy", "sad", "angry", "scared",
        "touch_tap_q1", "touch_tap_q2",
        "idle_q1", "listening_q1"
    };

    ESP_LOGI(TAG, "Starting rapid switching test...");

    for (int i = 0; i < 100; i++) {
        for (const char* anim : animations) {
            display->SetAnima(anim, 1);
            vTaskDelay(pdMS_TO_TICKS(100));  // 仅100ms切换间隔
        }

        if (i % 10 == 0) {
            size_t free_heap = esp_get_free_heap_size();
            ESP_LOGI(TAG, "[%d/100] Free heap: %zu bytes", i, free_heap);
        }
    }

    ESP_LOGI(TAG, "Rapid switching test completed");
}
```

**验证标准**：
- [x] 无崩溃或重启
- [x] 画面无花屏
- [x] 内存无持续下降
- [x] CPU负载正常

### 4.4.2 并发加载测试

模拟多个组件同时访问Flash：

```cpp
void ConcurrentAccessTest() {
    auto& assets = *Board::GetInstance().GetLynxAssets();

    // 创建10个任务同时读取资源
    for (int i = 0; i < 10; i++) {
        xTaskCreate([](void* param) {
            auto& assets = *(lynxaura::LynxAssets*)param;
            int task_id = (int)pvTaskGetThreadLocalStoragePointer(nullptr, 0);

            for (int j = 0; j < 50; j++) {
                void* data = nullptr;
                size_t size = 0;

                bool success = assets.GetResource(
                    "emergency/motion_shake_violently/motion_shake_violently",
                    data, size);

                if (!success) {
                    ESP_LOGE("Task%d", "Failed to load resource at iteration %d", task_id, j);
                }

                vTaskDelay(pdMS_TO_TICKS(10));
            }

            vTaskDelete(nullptr);
        }, "TestTask", 4096, &assets, 5, nullptr);
    }

    vTaskDelay(pdMS_TO_TICKS(10000));  // 等待所有任务完成
    ESP_LOGI(TAG, "Concurrent access test completed");
}
```

**验证标准**：
- [x] 所有任务成功完成
- [x] 无数据竞争或损坏
- [x] 无死锁

---

## 4.5 回归测试

### 4.5.1 核心功能验证

确保迁移后原有功能未受影响：

| 功能模块 | 测试项 | 预期结果 | 实际结果 |
|---------|-------|---------|---------|
| **WiFi连接** | 连接配置WiFi | 成功连接 | [ ] |
| **语音识别** | 说"你好小智" | 唤醒成功 | [ ] |
| **TTS播放** | 触发语音回复 | 正常播放 | [ ] |
| **触摸交互** | 点击屏幕 | 触发动画+音效 | [ ] |
| **运动检测** | 摇晃设备 | 触发动画+音效 | [ ] |
| **情感识别** | LLM返回emotion | 显示对应表情 | [ ] |
| **MCP工具** | 调用设备控制 | 功能正常 | [ ] |
| **OTA升级** | 触发固件更新 | 升级成功 | [ ] |

### 4.5.2 边界条件测试

| 测试项 | 操作 | 预期结果 | 实际结果 |
|-------|------|---------|---------|
| **资源不存在** | 加载不存在的GIF | 日志警告，使用默认 | [ ] |
| **资源名称错误** | 使用错误名称 | 日志警告，不崩溃 | [ ] |
| **CRC校验失败** | 人为损坏Flash | 警告但继续运行 | [ ] |
| **mmap失败** | 模拟内存不足 | 降级处理，不崩溃 | [ ] |

---

## 测试报告模板

### 测试环境

- **设备型号**：ALichuangTest
- **固件版本**：v2.0-flash
- **测试日期**：2025-10-XX
- **测试人员**：XXX

### 测试结果汇总

| 测试类别 | 通过率 | 失败项 | 备注 |
|---------|-------|--------|------|
| 功能测试 | __/103 | | |
| 性能测试 | __/6 | | |
| 稳定性测试 | __/4 | | |
| 压力测试 | __/2 | | |
| 回归测试 | __/10 | | |
| **总计** | __/125 | | |

### 性能数据

| 指标 | 目标值 | 实际值 | 对比SD卡 |
|------|-------|--------|---------|
| GIF加载延迟 | <5ms | ___ μs | 快___倍 |
| OGG加载延迟 | <1ms | ___ μs | 快___倍 |
| 内存峰值 | <350KB | ___ KB | 节省___KB |
| 内存波动 | <±30KB | ___ KB | 减少___% |

### 发现的问题

1. **问题描述**：xxx
   - 严重程度：[严重/中等/轻微]
   - 复现步骤：xxx
   - 解决方案：xxx

2. ...

### 结论

- [ ] 所有关键测试通过，建议发布
- [ ] 存在次要问题，修复后可发布
- [ ] 存在严重问题，需进一步修复

---

## 完成检查清单

- [ ] **功能测试**
  - [ ] 所有73个GIF动画播放正常
  - [ ] 所有30个OGG音频播放正常
  - [ ] GIF+OGG联合播放正常

- [ ] **性能测试**
  - [ ] 加载延迟达标（GIF <5ms, OGG <1ms）
  - [ ] 内存使用达标（峰值 <350KB）

- [ ] **稳定性测试**
  - [ ] 长时间运行无崩溃（1000次迭代）
  - [ ] 内存泄漏 <10KB

- [ ] **压力测试**
  - [ ] 快速切换无问题（100次×8种动画）
  - [ ] 并发访问无冲突

- [ ] **回归测试**
  - [ ] 核心功能未受影响
  - [ ] 边界条件处理正确

**所有检查项通过后，继续 [步骤5：部署发布](05_DEPLOYMENT.md)**

---

**下一步**：[步骤5：部署发布](05_DEPLOYMENT.md)
