# Step 10: 性能验证

## 目标
对事件命名系统进行全面的性能验证，确保系统在各种负载情况下都能正常工作。

## 性能指标目标

### 响应时间指标
- 单次命名转换：< 10μs
- 触摸事件名称构建：< 50μs
- 配置名称解析：< 100μs
- 完整事件处理链：< 1ms

### 吞吐量指标
- 并发事件处理：> 1000 events/sec
- 批量命名转换：> 10000 ops/sec
- 配置加载频率：> 100 configs/sec

### 资源使用指标
- 额外内存开销：< 4KB
- CPU 使用率增加：< 5%
- 无内存泄漏

## 实施步骤

### 1. 创建性能测试框架

```cpp
// performance_test_framework.h
#ifndef PERFORMANCE_TEST_FRAMEWORK_H
#define PERFORMANCE_TEST_FRAMEWORK_H

#include <esp_timer.h>
#include <vector>
#include <string>
#include <functional>

class PerformanceTestFramework {
public:
    struct PerformanceMetrics {
        std::string test_name;
        double avg_time_us;
        double min_time_us;
        double max_time_us;
        double std_deviation_us;
        double ops_per_second;
        size_t memory_used_bytes;
        bool passed;
    };
    
    struct TestConfig {
        int iterations = 10000;
        int warmup_iterations = 1000;
        double timeout_ms = 10000.0;
        bool measure_memory = true;
        bool detailed_stats = true;
    };
    
    // 性能测试执行
    static PerformanceMetrics RunPerformanceTest(
        const std::string& name,
        std::function<void()> test_func,
        const TestConfig& config = TestConfig{});
    
    static PerformanceMetrics RunThroughputTest(
        const std::string& name,
        std::function<void()> test_func,
        int duration_seconds = 10);
    
    // 内存性能测试
    static size_t MeasureMemoryUsage(std::function<void()> test_func);
    static bool CheckMemoryLeaks(std::function<void()> test_func, int iterations = 1000);
    
    // 并发性能测试
    static PerformanceMetrics RunConcurrentTest(
        const std::string& name,
        std::function<void()> test_func,
        int thread_count,
        int iterations_per_thread);
    
    // 统计工具
    static double CalculateStandardDeviation(const std::vector<double>& values);
    static void PrintMetrics(const PerformanceMetrics& metrics);
    static bool ValidateMetrics(const PerformanceMetrics& metrics, 
                               double max_avg_time_us,
                               double min_ops_per_sec = 0);
    
private:
    static size_t GetFreeHeapSize();
    static void WarmupCPU();
};

#endif // PERFORMANCE_TEST_FRAMEWORK_H
```

### 2. 实现性能测试框架

```cpp
// performance_test_framework.cc
#include "performance_test_framework.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <cmath>

static const char* TAG = "PerformanceTest";

PerformanceTestFramework::PerformanceMetrics 
PerformanceTestFramework::RunPerformanceTest(
    const std::string& name,
    std::function<void()> test_func,
    const TestConfig& config) {
    
    ESP_LOGI(TAG, "Starting performance test: %s", name.c_str());
    
    PerformanceMetrics metrics;
    metrics.test_name = name;
    
    std::vector<double> times;
    times.reserve(config.iterations);
    
    size_t initial_memory = 0;
    if (config.measure_memory) {
        initial_memory = GetFreeHeapSize();
    }
    
    // 预热阶段
    ESP_LOGI(TAG, "Warming up (%d iterations)...", config.warmup_iterations);
    for (int i = 0; i < config.warmup_iterations; i++) {
        test_func();
        if (i % 100 == 0) {
            vTaskDelay(1); // 防止看门狗复位
        }
    }
    
    WarmupCPU();
    
    // 正式测试
    ESP_LOGI(TAG, "Running test (%d iterations)...", config.iterations);
    
    int64_t total_start = esp_timer_get_time();
    
    for (int i = 0; i < config.iterations; i++) {
        int64_t start = esp_timer_get_time();
        test_func();
        int64_t end = esp_timer_get_time();
        
        double duration_us = (double)(end - start);
        times.push_back(duration_us);
        
        // 超时检查
        if ((end - total_start) / 1000.0 > config.timeout_ms) {
            ESP_LOGW(TAG, "Test timeout after %d iterations", i + 1);
            break;
        }
        
        if (i % 1000 == 0 && i > 0) {
            vTaskDelay(1); // 适当让出 CPU
        }
    }
    
    int64_t total_end = esp_timer_get_time();
    double total_time_us = (double)(total_end - total_start);
    
    // 计算统计数据
    if (!times.empty()) {
        std::sort(times.begin(), times.end());
        
        metrics.min_time_us = times.front();
        metrics.max_time_us = times.back();
        
        double sum = 0;
        for (double time : times) {
            sum += time;
        }
        metrics.avg_time_us = sum / times.size();
        
        metrics.std_deviation_us = CalculateStandardDeviation(times);
        metrics.ops_per_second = 1000000.0 / metrics.avg_time_us;
        
        // 性能标准验证
        metrics.passed = (metrics.avg_time_us < 1000.0); // 默认 < 1ms
    }
    
    // 内存使用测量
    if (config.measure_memory) {
        size_t final_memory = GetFreeHeapSize();
        if (final_memory < initial_memory) {
            metrics.memory_used_bytes = initial_memory - final_memory;
        }
    }
    
    PrintMetrics(metrics);
    return metrics;
}

PerformanceTestFramework::PerformanceMetrics 
PerformanceTestFramework::RunThroughputTest(
    const std::string& name,
    std::function<void()> test_func,
    int duration_seconds) {
    
    ESP_LOGI(TAG, "Starting throughput test: %s (%d seconds)", 
             name.c_str(), duration_seconds);
    
    PerformanceMetrics metrics;
    metrics.test_name = name + "_Throughput";
    
    size_t initial_memory = GetFreeHeapSize();
    
    int64_t start_time = esp_timer_get_time();
    int64_t end_time = start_time + (duration_seconds * 1000000LL);
    
    int operations = 0;
    std::vector<double> interval_ops;
    
    int64_t last_check = start_time;
    
    while (esp_timer_get_time() < end_time) {
        test_func();
        operations++;
        
        // 每秒记录一次吐吐量
        int64_t current_time = esp_timer_get_time();
        if (current_time - last_check >= 1000000) { // 1秒
            double ops_this_second = 1000000.0 / (current_time - last_check) * 
                                   (operations - interval_ops.size() * 1000);
            interval_ops.push_back(ops_this_second);
            last_check = current_time;
        }
        
        if (operations % 100 == 0) {
            vTaskDelay(1); // 防止看门狗
        }
    }
    
    int64_t actual_duration = esp_timer_get_time() - start_time;
    
    metrics.ops_per_second = (double)operations * 1000000.0 / actual_duration;
    metrics.avg_time_us = actual_duration / (double)operations;
    
    if (!interval_ops.empty()) {
        metrics.min_time_us = *std::min_element(interval_ops.begin(), interval_ops.end());
        metrics.max_time_us = *std::max_element(interval_ops.begin(), interval_ops.end());
        metrics.std_deviation_us = CalculateStandardDeviation(interval_ops);
    }
    
    // 内存使用
    size_t final_memory = GetFreeHeapSize();
    if (final_memory < initial_memory) {
        metrics.memory_used_bytes = initial_memory - final_memory;
    }
    
    metrics.passed = (metrics.ops_per_second > 1000.0); // 默认 > 1000 ops/sec
    
    ESP_LOGI(TAG, "Throughput test completed: %.2f ops/sec (%d operations in %lld us)", 
             metrics.ops_per_second, operations, actual_duration);
    
    return metrics;
}

bool PerformanceTestFramework::CheckMemoryLeaks(
    std::function<void()> test_func, int iterations) {
    
    ESP_LOGI(TAG, "Checking for memory leaks (%d iterations)...", iterations);
    
    size_t initial_memory = GetFreeHeapSize();
    
    // 运行多次测试
    for (int i = 0; i < iterations; i++) {
        test_func();
        
        if (i % 100 == 0) {
            vTaskDelay(1);
        }
    }
    
    // 等待垃圾回收
    vTaskDelay(pdMS_TO_TICKS(100));
    
    size_t final_memory = GetFreeHeapSize();
    
    if (final_memory < initial_memory) {
        size_t leaked = initial_memory - final_memory;
        ESP_LOGW(TAG, "Potential memory leak detected: %d bytes", leaked);
        return false;
    }
    
    ESP_LOGI(TAG, "No memory leaks detected");
    return true;
}

void PerformanceTestFramework::PrintMetrics(const PerformanceMetrics& metrics) {
    ESP_LOGI(TAG, "Performance Results for: %s", metrics.test_name.c_str());
    ESP_LOGI(TAG, "  Average time: %.2f μs", metrics.avg_time_us);
    ESP_LOGI(TAG, "  Min time: %.2f μs", metrics.min_time_us);
    ESP_LOGI(TAG, "  Max time: %.2f μs", metrics.max_time_us);
    ESP_LOGI(TAG, "  Std deviation: %.2f μs", metrics.std_deviation_us);
    ESP_LOGI(TAG, "  Operations/sec: %.2f", metrics.ops_per_second);
    ESP_LOGI(TAG, "  Memory used: %d bytes", metrics.memory_used_bytes);
    ESP_LOGI(TAG, "  Result: %s", metrics.passed ? "PASSED" : "FAILED");
}

double PerformanceTestFramework::CalculateStandardDeviation(
    const std::vector<double>& values) {
    
    if (values.size() < 2) return 0.0;
    
    double sum = 0;
    for (double value : values) {
        sum += value;
    }
    double mean = sum / values.size();
    
    double sq_sum = 0;
    for (double value : values) {
        sq_sum += (value - mean) * (value - mean);
    }
    
    return std::sqrt(sq_sum / (values.size() - 1));
}

size_t PerformanceTestFramework::GetFreeHeapSize() {
    return esp_get_free_heap_size();
}

void PerformanceTestFramework::WarmupCPU() {
    // CPU 预热，稳定时钟频率
    volatile int dummy = 0;
    for (int i = 0; i < 10000; i++) {
        dummy += i * i;
    }
}
```

### 3. EventNames 性能测试用例

```cpp
// event_names_performance_tests.cc
#include "performance_test_framework.h"
#include "event_names.h"
#include <esp_log.h>
#include <vector>
#include <random>

static const char* TAG = "EventNamesPerformanceTest";

// === 基础命名操作性能测试 ===

void test_get_internal_name_performance() {
    std::vector<EventType> test_events = {
        EventType::TOUCH_TAP,
        EventType::TOUCH_LONG_PRESS,
        EventType::MOTION_SHAKE,
        EventType::MOTION_FREE_FALL
    };
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, test_events.size() - 1);
    
    auto test_func = [&]() {
        EventType type = test_events[dis(gen)];
        volatile const char* name = EventNames::GetInternalName(type);
        (void)name; // 避免编译器优化
    };
    
    PerformanceTestFramework::TestConfig config;
    config.iterations = 100000;
    
    auto metrics = PerformanceTestFramework::RunPerformanceTest(
        "GetInternalName", test_func, config);
    
    // 验证性能要求：< 10μs
    assert(metrics.avg_time_us < 10.0);
    assert(metrics.passed);
}

void test_build_touch_event_name_performance() {
    std::vector<EventType> touch_events = {
        EventType::TOUCH_TAP,
        EventType::TOUCH_LONG_PRESS,
        EventType::TOUCH_DOUBLE_TAP
    };
    
    std::vector<TouchPosition> positions = {
        TouchPosition::LEFT,
        TouchPosition::RIGHT,
        TouchPosition::BOTH
    };
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> event_dis(0, touch_events.size() - 1);
    std::uniform_int_distribution<> pos_dis(0, positions.size() - 1);
    
    auto test_func = [&]() {
        EventType type = touch_events[event_dis(gen)];
        TouchPosition pos = positions[pos_dis(gen)];
        std::string name = EventNames::BuildTouchEventName(type, pos);
        // 使用结果防止优化
        volatile size_t len = name.length();
        (void)len;
    };
    
    PerformanceTestFramework::TestConfig config;
    config.iterations = 50000;
    
    auto metrics = PerformanceTestFramework::RunPerformanceTest(
        "BuildTouchEventName", test_func, config);
    
    // 验证性能要求：< 50μs
    assert(metrics.avg_time_us < 50.0);
    assert(metrics.passed);
}

void test_parse_config_name_performance() {
    std::vector<std::string> config_names = {
        "TOUCH_TAP",
        "TouchTap",
        "touch_tap",
        "SINGLE_TAP",
        "MOTION_SHAKE",
        "FreeFall",
        "MOTION_FREE_FALL"
    };
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, config_names.size() - 1);
    
    auto test_func = [&]() {
        const std::string& name = config_names[dis(gen)];
        volatile EventType type = EventNames::ParseConfigName(name);
        (void)type;
    };
    
    PerformanceTestFramework::TestConfig config;
    config.iterations = 50000;
    
    auto metrics = PerformanceTestFramework::RunPerformanceTest(
        "ParseConfigName", test_func, config);
    
    // 验证性能要求：< 100μs
    assert(metrics.avg_time_us < 100.0);
    assert(metrics.passed);
}

// === 吐吐量测试 ===

void test_naming_operations_throughput() {
    std::vector<EventType> events = {
        EventType::TOUCH_TAP, EventType::TOUCH_LONG_PRESS,
        EventType::MOTION_SHAKE, EventType::MOTION_FREE_FALL
    };
    
    std::vector<TouchPosition> positions = {
        TouchPosition::LEFT, TouchPosition::RIGHT, TouchPosition::BOTH
    };
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> event_dis(0, events.size() - 1);
    std::uniform_int_distribution<> pos_dis(0, positions.size() - 1);
    
    auto test_func = [&]() {
        EventType type = events[event_dis(gen)];
        TouchPosition pos = positions[pos_dis(gen)];
        
        // 模拟完整的命名操作流程
        const char* internal = EventNames::GetInternalName(type);
        std::string upload_name = EventNames::BuildTouchEventName(type, pos);
        const char* display = EventNames::GetDisplayName(type, "cn");
        
        // 使用结果防止优化
        volatile size_t total_len = strlen(internal) + upload_name.length() + strlen(display);
        (void)total_len;
    };
    
    auto metrics = PerformanceTestFramework::RunThroughputTest(
        "CompleteNamingOperations", test_func, 10);
    
    // 验证吐吐量要求：> 10000 ops/sec
    assert(metrics.ops_per_second > 10000.0);
    assert(metrics.passed);
}

// === 内存性能测试 ===

void test_memory_usage() {
    ESP_LOGI(TAG, "Testing memory usage for naming operations");
    
    auto test_func = []() {
        // 执行大量命名操作
        for (int i = 0; i < 1000; i++) {
            EventNames::GetInternalName(EventType::TOUCH_TAP);
            EventNames::BuildTouchEventName(EventType::TOUCH_TAP, TouchPosition::LEFT);
            EventNames::ParseConfigName("TOUCH_TAP");
            EventNames::GetDisplayName(EventType::MOTION_SHAKE, "cn");
        }
    };
    
    size_t memory_used = PerformanceTestFramework::MeasureMemoryUsage(test_func);
    
    ESP_LOGI(TAG, "Memory used by 4000 naming operations: %d bytes", memory_used);
    
    // 验证内存使用要求：< 4KB
    assert(memory_used < 4096);
}

void test_memory_leaks() {
    ESP_LOGI(TAG, "Testing for memory leaks");
    
    auto test_func = []() {
        // 模拟完整的事件处理流程
        std::string name = EventNames::BuildTouchEventName(
            EventType::TOUCH_TAP, TouchPosition::LEFT);
        EventType type = EventNames::ParseConfigName("TOUCH_TAP");
        const char* internal = EventNames::GetInternalName(type);
        
        // 使用结果
        volatile size_t len = name.length() + strlen(internal);
        (void)len;
    };
    
    bool no_leaks = PerformanceTestFramework::CheckMemoryLeaks(test_func, 10000);
    assert(no_leaks);
}

// === 并发性能测试 ===

void test_concurrent_naming_operations() {
    ESP_LOGI(TAG, "Testing concurrent naming operations");
    
    auto test_func = []() {
        // 在多线程环境下模拟命名操作
        EventNames::GetInternalName(EventType::TOUCH_TAP);
        EventNames::BuildTouchEventName(EventType::TOUCH_LONG_PRESS, TouchPosition::RIGHT);
        EventNames::ParseConfigName("MOTION_SHAKE");
    };
    
    // 模拟 4 个并发任务，每个执行 1000 次操作
    auto metrics = PerformanceTestFramework::RunConcurrentTest(
        "ConcurrentNaming", test_func, 4, 1000);
    
    // 验证并发性能
    assert(metrics.passed);
    assert(metrics.avg_time_us < 100.0);
}
```

### 4. 配置加载性能测试

```cpp
// config_performance_tests.cc
#include "performance_test_framework.h"
#include "event_config_loader.h"
#include "event_names.h"
#include <esp_log.h>

static const char* TAG = "ConfigPerformanceTest";

void test_config_parsing_performance() {
    const char* test_config = R"({
        "config_version": 2,
        "event_processing_strategies": {
            "touch_events": {
                "TOUCH_TAP": { "strategy": "MERGE", "interval_ms": 500 },
                "TOUCH_LONG_PRESS": { "strategy": "COOLDOWN", "interval_ms": 1000 },
                "TOUCH_DOUBLE_TAP": { "strategy": "IMMEDIATE" }
            },
            "motion_events": {
                "MOTION_SHAKE": { "strategy": "THROTTLE", "interval_ms": 2000 },
                "MOTION_FREE_FALL": { "strategy": "IMMEDIATE" }
            }
        }
    })";
    
    auto test_func = [&]() {
        EventEngine engine;
        bool success = EventConfigLoader::ParseJsonConfig(test_config, &engine);
        assert(success);
    };
    
    PerformanceTestFramework::TestConfig config;
    config.iterations = 1000;
    
    auto metrics = PerformanceTestFramework::RunPerformanceTest(
        "ConfigParsing", test_func, config);
    
    // 验证性能要求：< 5ms
    assert(metrics.avg_time_us < 5000.0);
    assert(metrics.passed);
}

void test_config_migration_performance() {
    const char* v1_config = R"({
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
    })";
    
    auto test_func = [&]() {
        std::string migrated = EventNames::MigrateConfigToLatest(v1_config);
        assert(!migrated.empty());
        
        // 验证迁移结果
        bool valid = EventNames::ValidateConfig(migrated.c_str());
        assert(valid);
    };
    
    PerformanceTestFramework::TestConfig config;
    config.iterations = 100;
    
    auto metrics = PerformanceTestFramework::RunPerformanceTest(
        "ConfigMigration", test_func, config);
    
    // 验证性能要求：< 10ms
    assert(metrics.avg_time_us < 10000.0);
    assert(metrics.passed);
}

void test_config_validation_performance() {
    const char* complex_config = R"({
        "config_version": 2,
        "event_processing_strategies": {
            "touch_events": {
                "TOUCH_TAP": { "strategy": "MERGE" },
                "TOUCH_LONG_PRESS": { "strategy": "COOLDOWN" },
                "TOUCH_DOUBLE_TAP": { "strategy": "IMMEDIATE" },
                "TOUCH_CRADLED": { "strategy": "IMMEDIATE" },
                "TOUCH_TICKLED": { "strategy": "IMMEDIATE" }
            },
            "motion_events": {
                "MOTION_SHAKE": { "strategy": "THROTTLE" },
                "MOTION_FREE_FALL": { "strategy": "IMMEDIATE" },
                "MOTION_FLIP": { "strategy": "THROTTLE" },
                "MOTION_PICKUP": { "strategy": "DEBOUNCE" },
                "MOTION_UPSIDE_DOWN": { "strategy": "IMMEDIATE" },
                "MOTION_SHAKE_VIOLENTLY": { "strategy": "THROTTLE" }
            }
        }
    })";
    
    auto test_func = [&]() {
        bool valid = EventNames::ValidateConfig(complex_config);
        assert(valid);
    };
    
    PerformanceTestFramework::TestConfig config;
    config.iterations = 1000;
    
    auto metrics = PerformanceTestFramework::RunPerformanceTest(
        "ConfigValidation", test_func, config);
    
    // 验证性能要求：< 2ms
    assert(metrics.avg_time_us < 2000.0);
    assert(metrics.passed);
}
```

### 5. 性能测试运行器

```cpp
// run_performance_tests.cc
#include "performance_test_framework.h"
#include <esp_log.h>
#include <vector>

static const char* TAG = "PerformanceTestRunner";

// 外部测试函数声明
extern void test_get_internal_name_performance();
extern void test_build_touch_event_name_performance();
extern void test_parse_config_name_performance();
extern void test_naming_operations_throughput();
extern void test_memory_usage();
extern void test_memory_leaks();
extern void test_concurrent_naming_operations();
extern void test_config_parsing_performance();
extern void test_config_migration_performance();
extern void test_config_validation_performance();

struct PerformanceTestCase {
    std::string name;
    std::function<void()> test_func;
    bool is_critical; // 关键测试，必须通过
};

std::vector<PerformanceTestCase> GetAllPerformanceTests() {
    return {
        // EventNames 基础性能测试
        {"GetInternalName Performance", test_get_internal_name_performance, true},
        {"BuildTouchEventName Performance", test_build_touch_event_name_performance, true},
        {"ParseConfigName Performance", test_parse_config_name_performance, true},
        
        // 吐吐量测试
        {"Naming Operations Throughput", test_naming_operations_throughput, true},
        
        // 内存性能测试
        {"Memory Usage", test_memory_usage, true},
        {"Memory Leaks", test_memory_leaks, true},
        
        // 并发性能测试
        {"Concurrent Naming Operations", test_concurrent_naming_operations, false},
        
        // 配置性能测试
        {"Config Parsing Performance", test_config_parsing_performance, true},
        {"Config Migration Performance", test_config_migration_performance, false},
        {"Config Validation Performance", test_config_validation_performance, true},
    };
}

bool RunAllPerformanceTests() {
    auto test_cases = GetAllPerformanceTests();
    
    ESP_LOGI(TAG, "Starting performance test suite (%d tests)", test_cases.size());
    
    int passed = 0;
    int failed = 0;
    int critical_failed = 0;
    
    for (const auto& test_case : test_cases) {
        ESP_LOGI(TAG, "Running: %s", test_case.name.c_str());
        
        try {
            test_case.test_func();
            passed++;
            ESP_LOGI(TAG, "PASSED: %s", test_case.name.c_str());
        } catch (const std::exception& e) {
            failed++;
            ESP_LOGE(TAG, "FAILED: %s - %s", test_case.name.c_str(), e.what());
            
            if (test_case.is_critical) {
                critical_failed++;
            }
        } catch (...) {
            failed++;
            ESP_LOGE(TAG, "FAILED: %s - Unknown exception", test_case.name.c_str());
            
            if (test_case.is_critical) {
                critical_failed++;
            }
        }
    }
    
    ESP_LOGI(TAG, "Performance test suite completed");
    ESP_LOGI(TAG, "Results: %d passed, %d failed", passed, failed);
    ESP_LOGI(TAG, "Critical failures: %d", critical_failed);
    
    // 关键测试必须全部通过
    return critical_failed == 0;
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting Event Naming Performance Tests");
    
    bool success = RunAllPerformanceTests();
    
    if (success) {
        ESP_LOGI(TAG, "All critical performance tests PASSED");
    } else {
        ESP_LOGE(TAG, "Some critical performance tests FAILED");
    }
    
    ESP_LOGI(TAG, "Performance tests completed");
}
```

### 6. 性能报告生成

```cpp
// performance_report.cc
#include "performance_test_framework.h"
#include <fstream>
#include <sstream>
#include <iomanip>

class PerformanceReportGenerator {
public:
    static void GenerateReport(const std::vector<PerformanceTestFramework::PerformanceMetrics>& results) {
        std::stringstream report;
        
        // 报告头部
        report << "# Event Naming System Performance Report\n\n";
        report << "Generated on: " << GetCurrentTimestamp() << "\n\n";
        
        // 性能概述
        GeneratePerformanceSummary(report, results);
        
        // 详细结果
        GenerateDetailedResults(report, results);
        
        // 性能分析
        GeneratePerformanceAnalysis(report, results);
        
        // 建议
        GenerateRecommendations(report, results);
        
        // 写入文件
        std::ofstream file("/tmp/performance_report.md");
        file << report.str();
        file.close();
        
        ESP_LOGI("PerformanceReport", "Report generated: /tmp/performance_report.md");
    }
    
private:
    static void GeneratePerformanceSummary(std::stringstream& report, 
                                          const std::vector<PerformanceTestFramework::PerformanceMetrics>& results) {
        report << "## Performance Summary\n\n";
        report << "| Test | Avg Time (μs) | Ops/Sec | Memory (bytes) | Status |\n";
        report << "|------|-------------|---------|----------------|--------|\n";
        
        for (const auto& result : results) {
            report << "| " << result.test_name
                   << " | " << std::fixed << std::setprecision(2) << result.avg_time_us
                   << " | " << std::fixed << std::setprecision(0) << result.ops_per_second
                   << " | " << result.memory_used_bytes
                   << " | " << (result.passed ? "✅ PASS" : "❌ FAIL")
                   << " |\n";
        }
        
        report << "\n";
    }
    
    static std::string GetCurrentTimestamp() {
        // 返回当前时间戳
        return "2024-01-01 12:00:00"; // 占位符
    }
    
    // ... 其他报告生成函数
};
```

## 性能优化建议

### 如果性能不达标：

1. **优化查找算法**
   - 使用哈希表替换线性查找
   - 实现二分查找算法

2. **减少字符串操作**
   - 使用字符串池
   - 避免不必要的字符串复制

3. **内存优化**
   - 使用栈内存替代堆内存
   - 实现对象池

## 完成标准

- [ ] 所有关键性能测试通过
- [ ] 响应时间达标
- [ ] 吐吐量达标
- [ ] 内存使用在合理范围内
- [ ] 无内存泄漏
- [ ] 性能报告生成

## 下一步
完成性能验证后，继续 [11_update_documentation.md](11_update_documentation.md) 更新所有相关文档。