# IMU传感器和震动马达使用说明

本系统为lichuang-dev开发板实现了完整的IMU传感器和震动马达功能。

## 功能模块

### 1. QMI8658 IMU传感器驱动 (qmi8658.h/cc)
- I2C地址：0x6A
- 支持加速度计和陀螺仪数据读取
- 可配置量程和采样率
- 温度传感器读取

### 2. 摇晃检测 (shake_detector.h/cc)
- 基于加速度计数据的摇晃检测
- 可配置检测阈值和参数
- 支持回调函数通知

### 3. 震动马达驱动 (vibration_motor.h/cc)
- 控制GPIO10引脚的震动马达
- `motor(hz, time)`函数：设置频率和持续时间
- 基于PWM的精确控制

### 4. 震动模式 (vibration_patterns.h/cc)
- 预设震动模式：心跳、短震动、长震动等
- 支持自定义震动模式
- 顺序播放支持

### 5. 情感反馈系统 (emotion_feedback.h/cc)
- 高级情感表达：开心、兴奋、平静等
- 摇晃检测集成
- 队列管理和异步播放

## 使用方法

### 基本使用
```cpp
// 在lichuang_dev_board.cc中已经集成
auto* board = static_cast<LichuangDevBoard*>(Board::GetInstance());
EmotionFeedback* emotion = board->GetEmotionFeedback();

// 触发开心情感
emotion->Happy();

// 触发其他情感
emotion->Excited();
emotion->Calm();
emotion->Alert();
```

### 自定义震动模式
```cpp
// 定义自定义情感
std::vector<std::string> custom_patterns = {"heartbeat", "pulse", "short_buzz"};
emotion->DefineEmotion("celebration", custom_patterns);

// 使用自定义情感
emotion->TriggerEmotion("celebration");
```

### 队列模式
```cpp
// 排队多个情感
emotion->QueueEmotion("happy");
emotion->QueueEmotion("excited");
emotion->QueueEmotion("calm");
```

### 摇晃检测
```cpp
// 摇晃检测已默认启用
// 摇晃时会自动触发"happy"情感

// 自定义摇晃响应
emotion->SetShakeResponseEmotion("surprised");

// 设置回调
emotion->SetShakeDetectedCallback([]() {
    ESP_LOGI("APP", "Device was shaken!");
});
```

## 硬件连接

- IMU传感器QMI8658：I2C地址0x6A，连接到SDA/SCL引脚
- 震动马达：连接到GPIO10

## 预设情感列表

1. **happy**: 心跳 + 短震动
2. **excited**: 三重敲击 + 脉冲 + 短震动  
3. **calm**: 波浪模式
4. **alert**: 警报模式
5. **sad**: 脉冲模式
6. **angry**: 错误 + 警报模式
7. **surprised**: 双击模式
8. **love**: 双重心跳模式

## 预设震动模式

- **heartbeat**: 心跳节奏 (强-弱-停顿)
- **short_buzz**: 快速震动
- **long_buzz**: 持续震动
- **double_tap**: 双击
- **triple_tap**: 三击
- **pulse**: 节奏脉冲
- **wave**: 渐变波浪
- **alert**: 紧急警报
- **success**: 成功反馈
- **error**: 错误反馈

## 编译说明

所有相关文件已添加到CMakeLists.txt的common源文件中，会自动编译。需要确保：

1. FreeRTOS已启用
2. I2C驱动已配置
3. PWM/LEDC驱动已启用

## 故障排除

1. **IMU无响应**: 检查I2C连接和地址
2. **马达不震动**: 检查GPIO10连接和电源
3. **编译错误**: 确保所有头文件路径正确

## 性能说明

- IMU采样率：50Hz
- 震动频率范围：0-1000Hz
- 内存使用：约8KB RAM
- CPU使用：轻量级，不影响主任务