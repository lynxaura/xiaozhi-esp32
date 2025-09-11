# Step 9: 执行集成测试

## 目标
执行全面的集成测试，验证整个事件命名系统在真实环境中的工作情况。

## 测试类型

### 1. 系统集成测试
- 完整的事件处理流程测试
- 多组件协同工作测试
- 配置加载和应用测试

### 2. 真实硬件测试
- ESP32-S3 实际硬件测试
- SD 卡配置文件测试
- 事件上传测试

### 3. 性能集成测试
- 高频事件处理测试
- 内存使用监控
- 延迟测量

## 实施步骤

### 1. 创建集成测试框架

```cpp
// test_integration_framework.h
#ifndef TEST_INTEGRATION_FRAMEWORK_H
#define TEST_INTEGRATION_FRAMEWORK_H

#include "event_engine.h"
#include "event_names.h"
#include "event_config_loader.h"
#include "event_uploader.h"
#include <vector>
#include <functional>

class IntegrationTestFramework {
public:
    struct TestResult {
        std::string test_name;
        bool passed;
        std::string error_message;
        int64_t duration_us;
    };
    
    struct SystemState {
        EventEngine* event_engine;
        EventUploader* event_uploader;
        std::vector<Event> captured_events;
        std::vector<std::string> uploaded_data;
    };
    
    // 测试管理
    static void SetupTestEnvironment();
    static void TeardownTestEnvironment();
    static void ResetSystemState();
    
    // 测试执行
    static TestResult RunTest(const std::string& name, std::function<bool()> test_func);
    static std::vector<TestResult> RunAllTests();
    
    // 事件模拟
    static void SimulateTouchEvent(EventType type, TouchPosition pos, uint32_t duration_ms = 0);
    static void SimulateMotionEvent(EventType type);
    static void SimulateEventSequence(const std::vector<Event>& events);
    
    // 数据捕获
    static void StartEventCapture();
    static void StopEventCapture();
    static std::vector<Event> GetCapturedEvents();
    static std::vector<std::string> GetUploadedData();
    
    // 验证工具
    static bool VerifyEventNaming(const Event& event);
    static bool VerifyUploadFormat(const std::string& json_data);
    static bool VerifyConfigCompatibility(const std::string& config);
    
private:
    static SystemState system_state_;
    static bool test_environment_ready_;
};

#endif // TEST_INTEGRATION_FRAMEWORK_H
```

### 2. 实现集成测试框架

```cpp
// test_integration_framework.cc
#include "test_integration_framework.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>

static const char* TAG = "IntegrationTest";

IntegrationTestFramework::SystemState IntegrationTestFramework::system_state_;
bool IntegrationTestFramework::test_environment_ready_ = false;

void IntegrationTestFramework::SetupTestEnvironment() {
    ESP_LOGI(TAG, "Setting up integration test environment");
    
    // 初始化系统组件
    system_state_.event_engine = new EventEngine();
    system_state_.event_uploader = new EventUploader();
    
    // 加载测试配置
    const char* test_config = R"({
        "config_version": 2,
        "event_processing_strategies": {
            "touch_events": {
                "TOUCH_TAP": {
                    "strategy": "IMMEDIATE",
                    "interval_ms": 0
                },
                "TOUCH_LONG_PRESS": {
                    "strategy": "IMMEDIATE",
                    "interval_ms": 0
                }
            },
            "motion_events": {
                "MOTION_SHAKE": {
                    "strategy": "IMMEDIATE",
                    "interval_ms": 0
                }
            }
        }
    })";
    
    EventConfigLoader::ParseJsonConfig(test_config, system_state_.event_engine);
    
    // 设置事件捕获
    system_state_.event_engine->SetEventCallback([](const Event& event) {
        system_state_.captured_events.push_back(event);
    });
    
    // 设置上传捕获
    system_state_.event_uploader->SetUploadCallback([](const std::string& data) {
        system_state_.uploaded_data.push_back(data);
    });
    
    test_environment_ready_ = true;
    ESP_LOGI(TAG, "Integration test environment ready");
}

void IntegrationTestFramework::TeardownTestEnvironment() {
    if (test_environment_ready_) {
        delete system_state_.event_engine;
        delete system_state_.event_uploader;
        
        system_state_ = SystemState{};
        test_environment_ready_ = false;
        
        ESP_LOGI(TAG, "Integration test environment cleaned up");
    }
}

void IntegrationTestFramework::ResetSystemState() {
    if (test_environment_ready_) {
        system_state_.captured_events.clear();
        system_state_.uploaded_data.clear();
    }
}

IntegrationTestFramework::TestResult IntegrationTestFramework::RunTest(
    const std::string& name, std::function<bool()> test_func) {
    
    TestResult result;
    result.test_name = name;
    
    ESP_LOGI(TAG, "Running test: %s", name.c_str());
    
    ResetSystemState();
    
    int64_t start_time = esp_timer_get_time();
    
    try {
        result.passed = test_func();
        if (result.passed) {
            ESP_LOGI(TAG, "Test PASSED: %s", name.c_str());
        } else {
            ESP_LOGW(TAG, "Test FAILED: %s", name.c_str());
        }
    } catch (const std::exception& e) {
        result.passed = false;
        result.error_message = e.what();
        ESP_LOGE(TAG, "Test EXCEPTION: %s - %s", name.c_str(), e.what());
    }
    
    int64_t end_time = esp_timer_get_time();
    result.duration_us = end_time - start_time;
    
    return result;
}

void IntegrationTestFramework::SimulateTouchEvent(EventType type, TouchPosition pos, uint32_t duration_ms) {
    Event event;
    event.type = type;
    event.timestamp = esp_timer_get_time() / 1000;
    event.data.touch_data.position = pos;
    event.data.touch_data.duration_ms = duration_ms;
    
    system_state_.event_engine->ProcessEvent(event);
}

void IntegrationTestFramework::SimulateMotionEvent(EventType type) {
    Event event;
    event.type = type;
    event.timestamp = esp_timer_get_time() / 1000;
    
    system_state_.event_engine->ProcessEvent(event);
}

bool IntegrationTestFramework::VerifyEventNaming(const Event& event) {
    // 验证事件命名的一致性
    const char* internal_name = EventNames::GetInternalName(event.type);
    if (strcmp(internal_name, "UNKNOWN") == 0) {
        return false;
    }
    
    // 验证上传名称
    if (EventNames::IsTouchEvent(event.type)) {
        std::string upload_name = EventNames::BuildTouchEventName(
            event.type, event.data.touch_data.position);
        if (upload_name.empty() || upload_name == "Unknown") {
            return false;
        }
    }
    
    return true;
}

bool IntegrationTestFramework::VerifyUploadFormat(const std::string& json_data) {
    cJSON* root = cJSON_Parse(json_data.c_str());
    if (!root) {
        return false;
    }
    
    // 检查必需字段
    bool valid = true;
    
    if (!cJSON_GetObjectItem(root, "event_type")) {
        valid = false;
    }
    
    if (!cJSON_GetObjectItem(root, "timestamp")) {
        valid = false;
    }
    
    // 检查事件名称格式
    cJSON* event_type = cJSON_GetObjectItem(root, "event_type");
    if (event_type && cJSON_IsString(event_type)) {
        std::string name = event_type->valuestring;
        
        // 触摸事件应该有格式：Touch_[Position]_[Action]
        if (name.find("Touch_") == 0) {
            if (name.find("_Left_") == std::string::npos &&
                name.find("_Right_") == std::string::npos &&
                name.find("_Both_") == std::string::npos) {
                valid = false;
            }
        }
        // 运动事件应该有格式：Motion_[Action]
        else if (name.find("Motion_") != 0) {
            valid = false;
        }
    }
    
    cJSON_Delete(root);
    return valid;
}
```

### 3. 系统集成测试用例

```cpp
// integration_test_cases.cc
#include "test_integration_framework.h"
#include "unity.h"

// === 系统集成测试 ===

bool test_complete_touch_event_flow() {
    // 模拟触摸事件
    IntegrationTestFramework::SimulateTouchEvent(
        EventType::TOUCH_TAP, TouchPosition::LEFT, 100);
    
    // 等待事件处理
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // 检查事件是否被捕获
    auto captured = IntegrationTestFramework::GetCapturedEvents();
    if (captured.size() != 1) {
        return false;
    }
    
    // 验证事件命名
    if (!IntegrationTestFramework::VerifyEventNaming(captured[0])) {
        return false;
    }
    
    // 检查上传数据
    auto uploaded = IntegrationTestFramework::GetUploadedData();
    if (uploaded.size() != 1) {
        return false;
    }
    
    // 验证上传格式
    if (!IntegrationTestFramework::VerifyUploadFormat(uploaded[0])) {
        return false;
    }
    
    // 检查具体内容
    cJSON* root = cJSON_Parse(uploaded[0].c_str());
    cJSON* event_type = cJSON_GetObjectItem(root, "event_type");
    
    bool result = (strcmp(event_type->valuestring, "Touch_Left_Tap") == 0);
    
    cJSON_Delete(root);
    return result;
}

bool test_multiple_event_types() {
    // 模拟多种事件类型
    IntegrationTestFramework::SimulateTouchEvent(EventType::TOUCH_TAP, TouchPosition::LEFT);
    IntegrationTestFramework::SimulateTouchEvent(EventType::TOUCH_LONG_PRESS, TouchPosition::RIGHT);
    IntegrationTestFramework::SimulateMotionEvent(EventType::MOTION_SHAKE);
    IntegrationTestFramework::SimulateMotionEvent(EventType::MOTION_FREE_FALL);
    
    vTaskDelay(pdMS_TO_TICKS(50));
    
    auto captured = IntegrationTestFramework::GetCapturedEvents();
    if (captured.size() != 4) {
        return false;
    }
    
    auto uploaded = IntegrationTestFramework::GetUploadedData();
    if (uploaded.size() != 4) {
        return false;
    }
    
    // 验证所有事件的命名
    for (const auto& event : captured) {
        if (!IntegrationTestFramework::VerifyEventNaming(event)) {
            return false;
        }
    }
    
    // 验证所有上传数据的格式
    for (const auto& data : uploaded) {
        if (!IntegrationTestFramework::VerifyUploadFormat(data)) {
            return false;
        }
    }
    
    return true;
}

bool test_config_loading_and_application() {
    // 测试不同版本的配置加载
    const char* v1_config = R"({
        "event_processing_strategies": {
            "touch_events": {
                "TouchTap": { "strategy": "MERGE" },
                "SINGLE_TAP": { "strategy": "IMMEDIATE" }
            }
        }
    })";
    
    // 迁移配置
    std::string migrated = EventNames::MigrateConfigToLatest(v1_config);
    if (migrated.empty()) {
        return false;
    }
    
    // 验证迁移后的配置可以正常加载
    EventEngine test_engine;
    if (!EventConfigLoader::ParseJsonConfig(migrated.c_str(), &test_engine)) {
        return false;
    }
    
    return true;
}

// === 性能集成测试 ===

bool test_high_frequency_events() {
    const int event_count = 1000;
    
    int64_t start_time = esp_timer_get_time();
    
    // 生成大量事件
    for (int i = 0; i < event_count; i++) {
        TouchPosition pos = (i % 2 == 0) ? TouchPosition::LEFT : TouchPosition::RIGHT;
        IntegrationTestFramework::SimulateTouchEvent(EventType::TOUCH_TAP, pos);
        
        if (i % 100 == 0) {
            vTaskDelay(pdMS_TO_TICKS(1)); // 适当的延迟
        }
    }
    
    int64_t end_time = esp_timer_get_time();
    int64_t duration_us = end_time - start_time;
    
    ESP_LOGI("PerformanceTest", "Processed %d events in %lld us (avg: %.2f us/event)", 
             event_count, duration_us, (double)duration_us / event_count);
    
    // 验证性能要求（平均每个事件 < 100us）
    if ((double)duration_us / event_count > 100.0) {
        return false;
    }
    
    // 检查所有事件都被正确处理
    auto captured = IntegrationTestFramework::GetCapturedEvents();
    return captured.size() >= event_count * 0.95; // 允许 5% 的丢失
}

bool test_memory_usage() {
    // 记录初始内存使用
    size_t initial_free = esp_get_free_heap_size();
    
    // 进行大量命名操作
    for (int i = 0; i < 10000; i++) {
        EventNames::GetInternalName(EventType::TOUCH_TAP);
        EventNames::BuildTouchEventName(EventType::TOUCH_TAP, TouchPosition::LEFT);
        EventNames::ParseConfigName("TOUCH_TAP");
    }
    
    // 检查内存泄漏
    size_t final_free = esp_get_free_heap_size();
    size_t memory_used = initial_free - final_free;
    
    ESP_LOGI("MemoryTest", "Memory used: %d bytes", memory_used);
    
    // 内存使用应该在合理范围内（< 1KB）
    return memory_used < 1024;
}

// === SD 卡配置测试 ===

bool test_sd_card_config_compatibility() {
    // 创建模拟 SD 卡配置文件
    const char* sd_config_path = "/tmp/test_sd_config.json";
    
    const char* config_content = R"({
        "config_version": 1,
        "event_processing_strategies": {
            "touch_events": {
                "TouchTap": { "strategy": "MERGE" },
                "touch_long_press": { "strategy": "COOLDOWN" }
            },
            "motion_events": {
                "FreeFall": { "strategy": "IMMEDIATE" }
            }
        }
    })";
    
    // 写入文件
    FILE* f = fopen(sd_config_path, "w");
    if (!f) return false;
    
    fwrite(config_content, 1, strlen(config_content), f);
    fclose(f);
    
    // 测试加载
    EventEngine test_engine;
    bool success = EventConfigLoader::LoadFromFile(sd_config_path, &test_engine);
    
    // 清理
    remove(sd_config_path);
    
    return success;
}
```

### 4. 测试运行器

```cpp
// run_integration_tests.cc
#include "test_integration_framework.h"
#include "unity.h"
#include <esp_log.h>

static const char* TAG = "IntegrationTestRunner";

extern bool test_complete_touch_event_flow();
extern bool test_multiple_event_types();
extern bool test_config_loading_and_application();
extern bool test_high_frequency_events();
extern bool test_memory_usage();
extern bool test_sd_card_config_compatibility();

std::vector<IntegrationTestFramework::TestResult> RunAllIntegrationTests() {
    std::vector<IntegrationTestFramework::TestResult> results;
    
    ESP_LOGI(TAG, "Starting integration test suite");
    
    // 设置测试环境
    IntegrationTestFramework::SetupTestEnvironment();
    
    // 系统集成测试
    results.push_back(IntegrationTestFramework::RunTest(
        "Complete Touch Event Flow", test_complete_touch_event_flow));
    
    results.push_back(IntegrationTestFramework::RunTest(
        "Multiple Event Types", test_multiple_event_types));
    
    results.push_back(IntegrationTestFramework::RunTest(
        "Config Loading and Application", test_config_loading_and_application));
    
    // 性能测试
    results.push_back(IntegrationTestFramework::RunTest(
        "High Frequency Events", test_high_frequency_events));
    
    results.push_back(IntegrationTestFramework::RunTest(
        "Memory Usage", test_memory_usage));
    
    // SD 卡测试
    results.push_back(IntegrationTestFramework::RunTest(
        "SD Card Config Compatibility", test_sd_card_config_compatibility));
    
    // 清理测试环境
    IntegrationTestFramework::TeardownTestEnvironment();
    
    // 统计结果
    int passed = 0;
    int failed = 0;
    int64_t total_duration = 0;
    
    for (const auto& result : results) {
        if (result.passed) {
            passed++;
        } else {
            failed++;
        }
        total_duration += result.duration_us;
    }
    
    ESP_LOGI(TAG, "Integration test suite completed");
    ESP_LOGI(TAG, "Results: %d passed, %d failed", passed, failed);
    ESP_LOGI(TAG, "Total duration: %lld us", total_duration);
    
    return results;
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting Event Naming Integration Tests");
    
    auto results = RunAllIntegrationTests();
    
    // 输出详细结果
    for (const auto& result : results) {
        ESP_LOGI(TAG, "Test: %s - %s (Duration: %lld us)", 
                 result.test_name.c_str(),
                 result.passed ? "PASSED" : "FAILED",
                 result.duration_us);
        
        if (!result.passed && !result.error_message.empty()) {
            ESP_LOGE(TAG, "Error: %s", result.error_message.c_str());
        }
    }
    
    ESP_LOGI(TAG, "Integration tests completed");
}
```

### 5. 自动化测试脚本

```bash
#!/bin/bash
# scripts/run_integration_tests.sh

set -e

echo "Starting Event Naming Integration Tests"
echo "======================================="

# 设置测试环境
exporn INTEGRATION_TEST=1

# 编译测试
echo "Building integration tests..."
idf.py build

# 检查是否有硬件连接
if idf.py monitor --print_filter="*" --no_reset 2>&1 | grep -q "Waiting for device"; then
    echo "No device connected, running in simulation mode"
    # 这里可以添加模拟器逻辑
else
    echo "Device found, running on hardware"
    idf.py flash monitor
fi

echo "Integration tests completed"
```

## 验证标准

### 功能验证
- [ ] 完整的事件处理流程正常
- [ ] 所有事件类型命名正确
- [ ] 上传数据格式符合规范
- [ ] 配置文件兼容性正常

### 性能验证
- [ ] 单个事件处理时间 < 100μs
- [ ] 高频事件处理无丢失
- [ ] 内存使用在合理范围内
- [ ] 无内存泄漏

### 稳定性验证
- [ ] 连续运行 1 小时无故障
- [ ] SD 卡拔插不影响系统稳定性
- [ ] 各种错误情况处理正确

## 完成标准

- [ ] 所有集成测试通过
- [ ] 性能指标达标
- [ ] 稳定性测试通过
- [ ] 真实硬件测试正常
- [ ] 自动化测试流程完整

## 下一步
完成集成测试执行后，继续 [10_performance_validation.md](10_performance_validation.md) 执行性能验证。