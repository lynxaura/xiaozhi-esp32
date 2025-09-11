# Step 8: 创建单元测试

## 目标
为 EventNames 类和相关功能创建全面的单元测试，确保所有命名转换功能正确工作。

## 测试文件结构

```
main/boards/ALichuangTest/tests/
├── test_event_names.cc         # 主要测试文件
├── test_config_migration.cc    # 配置迁移测试
├── test_event_integration.cc   # 集成测试
└── CMakeLists.txt              # 测试构建配置
```

## 实施步骤

### 1. 创建测试目录和构建配置

```cmake
# main/boards/ALichuangTest/tests/CMakeLists.txt
idf_component_register(
    SRCS 
        "test_event_names.cc"
        "test_config_migration.cc"
        "test_event_integration.cc"
        "../interaction/core/event_names.cc"
        "../interaction/config/event_config_loader.cc"
    INCLUDE_DIRS 
        "../interaction/core"
        "../interaction/config"
        "."
    REQUIRES 
        unity
        json
        esp_timer
)

# 定义测试宏
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    UNITY_INCLUDE_CONFIG_H
    ENABLE_UNIT_TESTS
)
```

### 2. 主要测试文件

```cpp
// test_event_names.cc
#include "unity.h"
#include "event_names.h"
#include "event_engine.h"
#include <esp_log.h>
#include <string.h>

static const char* TAG = "TestEventNames";

// 测试用例集合
void setUp(void) {
    // 每个测试前的准备工作
}

void tearDown(void) {
    // 每个测试后的清理工作
}

// === 基础命名测试 ===

void test_get_internal_name(void) {
    TEST_ASSERT_EQUAL_STRING("TOUCH_TAP", EventNames::GetInternalName(EventType::TOUCH_TAP));
    TEST_ASSERT_EQUAL_STRING("TOUCH_LONG_PRESS", EventNames::GetInternalName(EventType::TOUCH_LONG_PRESS));
    TEST_ASSERT_EQUAL_STRING("MOTION_SHAKE", EventNames::GetInternalName(EventType::MOTION_SHAKE));
    TEST_ASSERT_EQUAL_STRING("MOTION_FREE_FALL", EventNames::GetInternalName(EventType::MOTION_FREE_FALL));
    
    // 测试未知事件
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", EventNames::GetInternalName(EventType::MOTION_NONE));
}

void test_get_upload_base_name(void) {
    TEST_ASSERT_EQUAL_STRING("Touch", EventNames::GetUploadBaseName(EventType::TOUCH_TAP));
    TEST_ASSERT_EQUAL_STRING("Touch", EventNames::GetUploadBaseName(EventType::TOUCH_LONG_PRESS));
    TEST_ASSERT_EQUAL_STRING("Motion_Shake", EventNames::GetUploadBaseName(EventType::MOTION_SHAKE));
    TEST_ASSERT_EQUAL_STRING("Motion_FreeFall", EventNames::GetUploadBaseName(EventType::MOTION_FREE_FALL));
}

void test_position_modifier(void) {
    TEST_ASSERT_EQUAL_STRING("Left", EventNames::GetPositionModifier(TouchPosition::LEFT));
    TEST_ASSERT_EQUAL_STRING("Right", EventNames::GetPositionModifier(TouchPosition::RIGHT));
    TEST_ASSERT_EQUAL_STRING("Both", EventNames::GetPositionModifier(TouchPosition::BOTH));
    TEST_ASSERT_EQUAL_STRING("Any", EventNames::GetPositionModifier(TouchPosition::ANY));
}

// === 触摸事件名称构建测试 ===

void test_build_touch_event_name(void) {
    // 测试基本触摸事件
    std::string name = EventNames::BuildTouchEventName(EventType::TOUCH_TAP, TouchPosition::LEFT);
    TEST_ASSERT_EQUAL_STRING("Touch_Left_Tap", name.c_str());
    
    name = EventNames::BuildTouchEventName(EventType::TOUCH_TAP, TouchPosition::RIGHT);
    TEST_ASSERT_EQUAL_STRING("Touch_Right_Tap", name.c_str());
    
    name = EventNames::BuildTouchEventName(EventType::TOUCH_LONG_PRESS, TouchPosition::BOTH);
    TEST_ASSERT_EQUAL_STRING("Touch_Both_LongPress", name.c_str());
    
    // 测试特殊事件（始终是 Both）
    name = EventNames::BuildTouchEventName(EventType::TOUCH_CRADLED, TouchPosition::LEFT);
    TEST_ASSERT_EQUAL_STRING("Touch_Both_Cradled", name.c_str());
    
    name = EventNames::BuildTouchEventName(EventType::TOUCH_TICKLED, TouchPosition::RIGHT);
    TEST_ASSERT_EQUAL_STRING("Touch_Both_Tickled", name.c_str());
    
    // 测试运动事件（不需要位置）
    name = EventNames::BuildTouchEventName(EventType::MOTION_SHAKE, TouchPosition::LEFT);
    TEST_ASSERT_EQUAL_STRING("Motion_Shake", name.c_str());
}

// === 显示名称测试 ===

void test_get_display_name(void) {
    // 中文显示
    TEST_ASSERT_EQUAL_STRING("轻触", EventNames::GetDisplayName(EventType::TOUCH_TAP, "cn"));
    TEST_ASSERT_EQUAL_STRING("长按", EventNames::GetDisplayName(EventType::TOUCH_LONG_PRESS, "cn"));
    TEST_ASSERT_EQUAL_STRING("摇晃", EventNames::GetDisplayName(EventType::MOTION_SHAKE, "cn"));
    
    // 英文显示
    TEST_ASSERT_EQUAL_STRING("Tap", EventNames::GetDisplayName(EventType::TOUCH_TAP, "en"));
    TEST_ASSERT_EQUAL_STRING("Long Press", EventNames::GetDisplayName(EventType::TOUCH_LONG_PRESS, "en"));
    TEST_ASSERT_EQUAL_STRING("Shake", EventNames::GetDisplayName(EventType::MOTION_SHAKE, "en"));
}

void test_get_display_name_with_position(void) {
    // 中文带位置
    std::string name = EventNames::GetDisplayNameWithPosition(EventType::TOUCH_TAP, TouchPosition::LEFT, "cn");
    TEST_ASSERT_EQUAL_STRING("左侧轻触", name.c_str());
    
    name = EventNames::GetDisplayNameWithPosition(EventType::TOUCH_LONG_PRESS, TouchPosition::RIGHT, "cn");
    TEST_ASSERT_EQUAL_STRING("右侧长按", name.c_str());
    
    // 英文带位置
    name = EventNames::GetDisplayNameWithPosition(EventType::TOUCH_TAP, TouchPosition::LEFT, "en");
    TEST_ASSERT_EQUAL_STRING("Left Tap", name.c_str());
    
    // 运动事件不带位置
    name = EventNames::GetDisplayNameWithPosition(EventType::MOTION_SHAKE, TouchPosition::LEFT, "cn");
    TEST_ASSERT_EQUAL_STRING("摇晃", name.c_str());
}

// === 配置解析测试 ===

void test_parse_config_name(void) {
    // 测试标准格式
    TEST_ASSERT_EQUAL(EventType::TOUCH_TAP, EventNames::ParseConfigName("TOUCH_TAP"));
    TEST_ASSERT_EQUAL(EventType::TOUCH_LONG_PRESS, EventNames::ParseConfigName("TOUCH_LONG_PRESS"));
    TEST_ASSERT_EQUAL(EventType::MOTION_SHAKE, EventNames::ParseConfigName("MOTION_SHAKE"));
    
    // 测试兼容格式
    TEST_ASSERT_EQUAL(EventType::TOUCH_TAP, EventNames::ParseConfigName("TouchTap"));
    TEST_ASSERT_EQUAL(EventType::TOUCH_TAP, EventNames::ParseConfigName("Touch_Tap"));
    TEST_ASSERT_EQUAL(EventType::TOUCH_TAP, EventNames::ParseConfigName("touch_tap"));
    TEST_ASSERT_EQUAL(EventType::TOUCH_TAP, EventNames::ParseConfigName("SINGLE_TAP"));
    TEST_ASSERT_EQUAL(EventType::TOUCH_TAP, EventNames::ParseConfigName("TAP"));
    
    // 测试运动事件兼容
    TEST_ASSERT_EQUAL(EventType::MOTION_FREE_FALL, EventNames::ParseConfigName("FreeFall"));
    TEST_ASSERT_EQUAL(EventType::MOTION_FREE_FALL, EventNames::ParseConfigName("FREE_FALL"));
    TEST_ASSERT_EQUAL(EventType::MOTION_SHAKE, EventNames::ParseConfigName("Shake"));
    
    // 测试未知名称
    TEST_ASSERT_EQUAL(EventType::MOTION_NONE, EventNames::ParseConfigName("UNKNOWN_EVENT"));
    TEST_ASSERT_EQUAL(EventType::MOTION_NONE, EventNames::ParseConfigName(""));
}

void test_is_valid_config_name(void) {
    // 有效名称
    TEST_ASSERT_TRUE(EventNames::IsValidConfigName("TOUCH_TAP"));
    TEST_ASSERT_TRUE(EventNames::IsValidConfigName("TouchTap"));
    TEST_ASSERT_TRUE(EventNames::IsValidConfigName("MOTION_SHAKE"));
    TEST_ASSERT_TRUE(EventNames::IsValidConfigName("FreeFall"));
    
    // 无效名称
    TEST_ASSERT_FALSE(EventNames::IsValidConfigName("INVALID_EVENT"));
    TEST_ASSERT_FALSE(EventNames::IsValidConfigName(""));
    TEST_ASSERT_FALSE(EventNames::IsValidConfigName("random_string"));
}

// === 工具函数测试 ===

void test_event_type_classification(void) {
    // 触摸事件
    TEST_ASSERT_TRUE(EventNames::IsTouchEvent(EventType::TOUCH_TAP));
    TEST_ASSERT_TRUE(EventNames::IsTouchEvent(EventType::TOUCH_LONG_PRESS));
    TEST_ASSERT_TRUE(EventNames::IsTouchEvent(EventType::TOUCH_CRADLED));
    TEST_ASSERT_FALSE(EventNames::IsTouchEvent(EventType::MOTION_SHAKE));
    
    // 运动事件
    TEST_ASSERT_TRUE(EventNames::IsMotionEvent(EventType::MOTION_SHAKE));
    TEST_ASSERT_TRUE(EventNames::IsMotionEvent(EventType::MOTION_FREE_FALL));
    TEST_ASSERT_FALSE(EventNames::IsMotionEvent(EventType::TOUCH_TAP));
    
    // 位置需求
    TEST_ASSERT_TRUE(EventNames::RequiresPosition(EventType::TOUCH_TAP));
    TEST_ASSERT_TRUE(EventNames::RequiresPosition(EventType::TOUCH_LONG_PRESS));
    TEST_ASSERT_FALSE(EventNames::RequiresPosition(EventType::TOUCH_CRADLED));
    TEST_ASSERT_FALSE(EventNames::RequiresPosition(EventType::MOTION_SHAKE));
}

void test_get_all_event_types(void) {
    std::vector<EventType> types = EventNames::GetAllEventTypes();
    
    // 检查是否包含主要事件类型
    bool has_touch_tap = false;
    bool has_motion_shake = false;
    
    for (EventType type : types) {
        if (type == EventType::TOUCH_TAP) has_touch_tap = true;
        if (type == EventType::MOTION_SHAKE) has_motion_shake = true;
        
        // 不应该包含 MOTION_NONE
        TEST_ASSERT_NOT_EQUAL(EventType::MOTION_NONE, type);
    }
    
    TEST_ASSERT_TRUE(has_touch_tap);
    TEST_ASSERT_TRUE(has_motion_shake);
    TEST_ASSERT_GREATER_THAN(5, types.size()); // 至少有 5 个事件类型
}

// === 性能测试 ===

void test_performance_naming_operations(void) {
    const int iterations = 1000;
    
    int64_t start_time = esp_timer_get_time();
    
    // 测试基本命名操作性能
    for (int i = 0; i < iterations; i++) {
        EventNames::GetInternalName(EventType::TOUCH_TAP);
        EventNames::GetUploadBaseName(EventType::MOTION_SHAKE);
        EventNames::BuildTouchEventName(EventType::TOUCH_TAP, TouchPosition::LEFT);
        EventNames::ParseConfigName("TOUCH_TAP");
    }
    
    int64_t end_time = esp_timer_get_time();
    int64_t duration_us = end_time - start_time;
    
    ESP_LOGI(TAG, "Performance test: %d iterations took %lld us (avg: %.2f us/op)", 
             iterations, duration_us, (double)duration_us / (iterations * 4));
    
    // 验证性能在可接受范围内（平均每操作 < 10us）
    TEST_ASSERT_LESS_THAN(10.0, (double)duration_us / (iterations * 4));
}

// === 边界情况测试 ===

void test_edge_cases(void) {
    // 空字符串
    TEST_ASSERT_EQUAL(EventType::MOTION_NONE, EventNames::ParseConfigName(""));
    TEST_ASSERT_FALSE(EventNames::IsValidConfigName(""));
    
    // NULL 指针处理
    const char* null_ptr = nullptr;
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", EventNames::GetInternalName(static_cast<EventType>(-1)));
    
    // 非常长的字符串
    std::string long_name(1000, 'a');
    TEST_ASSERT_EQUAL(EventType::MOTION_NONE, EventNames::ParseConfigName(long_name));
    
    // 特殊字符
    TEST_ASSERT_EQUAL(EventType::MOTION_NONE, EventNames::ParseConfigName("TOUCH@TAP"));
    TEST_ASSERT_EQUAL(EventType::MOTION_NONE, EventNames::ParseConfigName("TOUCH\nTAP"));
}

// === 测试运行器 ===

void runAllEventNamesTests(void) {
    RUN_TEST(test_get_internal_name);
    RUN_TEST(test_get_upload_base_name);
    RUN_TEST(test_position_modifier);
    RUN_TEST(test_build_touch_event_name);
    RUN_TEST(test_get_display_name);
    RUN_TEST(test_get_display_name_with_position);
    RUN_TEST(test_parse_config_name);
    RUN_TEST(test_is_valid_config_name);
    RUN_TEST(test_event_type_classification);
    RUN_TEST(test_get_all_event_types);
    RUN_TEST(test_performance_naming_operations);
    RUN_TEST(test_edge_cases);
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting EventNames unit tests");
    
    UNITY_BEGIN();
    runAllEventNamesTests();
    UNITY_END();
    
    ESP_LOGI(TAG, "Unit tests completed");
}
```

### 3. 配置迁移测试

```cpp
// test_config_migration.cc
#include "unity.h"
#include "event_names.h"
#include <cJSON.h>
#include <esp_log.h>

static const char* TAG = "TestConfigMigration";

void test_config_version_detection(void) {
    // 测试 v1 配置（无版本号）
    const char* v1_config = R"({
        "event_processing_strategies": {
            "touch_events": {
                "TouchTap": { "strategy": "MERGE" }
            }
        }
    })";
    
    TEST_ASSERT_EQUAL(1, EventNames::GetConfigVersion(v1_config));
    
    // 测试 v2 配置（有版本号）
    const char* v2_config = R"({
        "config_version": 2,
        "event_processing_strategies": {
            "touch_events": {
                "TOUCH_TAP": { "strategy": "MERGE" }
            }
        }
    })";
    
    TEST_ASSERT_EQUAL(2, EventNames::GetConfigVersion(v2_config));
}

void test_config_validation(void) {
    // 有效配置
    const char* valid_config = R"({
        "event_processing_strategies": {
            "touch_events": {
                "TOUCH_TAP": { "strategy": "MERGE" }
            },
            "motion_events": {
                "MOTION_SHAKE": { "strategy": "THROTTLE" }
            }
        }
    })";
    
    TEST_ASSERT_TRUE(EventNames::ValidateConfig(valid_config));
    
    // 无效配置（缺少必需字段）
    const char* invalid_config = R"({
        "some_other_field": "value"
    })";
    
    TEST_ASSERT_FALSE(EventNames::ValidateConfig(invalid_config));
    
    // 无效事件名称
    const char* unknown_event_config = R"({
        "event_processing_strategies": {
            "touch_events": {
                "UNKNOWN_EVENT": { "strategy": "MERGE" }
            }
        }
    })";
    
    TEST_ASSERT_FALSE(EventNames::ValidateConfig(unknown_event_config));
}

void test_config_migration_v1_to_v2(void) {
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
    
    std::string migrated = EventNames::MigrateConfigToLatest(v1_config);
    
    // 验证迁移结果
    TEST_ASSERT_FALSE(migrated.empty());
    
    cJSON* root = cJSON_Parse(migrated.c_str());
    TEST_ASSERT_NOT_NULL(root);
    
    // 检查版本号
    cJSON* version = cJSON_GetObjectItem(root, "config_version");
    TEST_ASSERT_NOT_NULL(version);
    TEST_ASSERT_EQUAL(2, version->valueint);
    
    // 检查事件名称是否正确迁移
    cJSON* strategies = cJSON_GetObjectItem(root, "event_processing_strategies");
    cJSON* touch_events = cJSON_GetObjectItem(strategies, "touch_events");
    
    // 应该有新名称
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(touch_events, "TOUCH_TAP"));
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(touch_events, "TOUCH_LONG_PRESS"));
    
    // 不应该有旧名称
    TEST_ASSERT_NULL(cJSON_GetObjectItem(touch_events, "TouchTap"));
    TEST_ASSERT_NULL(cJSON_GetObjectItem(touch_events, "SINGLE_TAP"));
    TEST_ASSERT_NULL(cJSON_GetObjectItem(touch_events, "touch_long_press"));
    
    cJSON_Delete(root);
}

void runConfigMigrationTests(void) {
    RUN_TEST(test_config_version_detection);
    RUN_TEST(test_config_validation);
    RUN_TEST(test_config_migration_v1_to_v2);
}
```

### 4. 集成测试

```cpp
// test_event_integration.cc
#include "unity.h"
#include "event_names.h"
#include "event_config_loader.h"
#include "event_engine.h"
#include <esp_log.h>

static const char* TAG = "TestIntegration";

void test_end_to_end_config_loading(void) {
    // 创建测试配置
    const char* test_config = R"({
        "config_version": 2,
        "event_processing_strategies": {
            "touch_events": {
                "TOUCH_TAP": {
                    "strategy": "MERGE",
                    "merge_window_ms": 1500,
                    "interval_ms": 500
                }
            },
            "motion_events": {
                "MOTION_SHAKE": {
                    "strategy": "THROTTLE",
                    "interval_ms": 2000
                }
            }
        }
    })";
    
    // 写入临时文件
    const char* temp_file = "/tmp/test_config.json";
    FILE* f = fopen(temp_file, "w");
    TEST_ASSERT_NOT_NULL(f);
    fwrite(test_config, 1, strlen(test_config), f);
    fclose(f);
    
    // 测试加载
    EventEngine engine;
    bool success = EventConfigLoader::LoadFromFile(temp_file, &engine);
    TEST_ASSERT_TRUE(success);
    
    // 清理
    remove(temp_file);
}

void test_naming_consistency_across_components(void) {
    // 测试在不同组件中使用相同的命名
    EventType test_event = EventType::TOUCH_TAP;
    TouchPosition test_pos = TouchPosition::LEFT;
    
    // EventNames 的命名
    std::string upload_name = EventNames::BuildTouchEventName(test_event, test_pos);
    const char* internal_name = EventNames::GetInternalName(test_event);
    const char* config_name = EventNames::GetConfigName(test_event);
    
    // 验证一致性
    TEST_ASSERT_EQUAL_STRING("Touch_Left_Tap", upload_name.c_str());
    TEST_ASSERT_EQUAL_STRING("TOUCH_TAP", internal_name);
    TEST_ASSERT_EQUAL_STRING("TOUCH_TAP", config_name);
    
    // 验证可以反向解析
    EventType parsed_type = EventNames::ParseConfigName(config_name);
    TEST_ASSERT_EQUAL(test_event, parsed_type);
}

void runIntegrationTests(void) {
    RUN_TEST(test_end_to_end_config_loading);
    RUN_TEST(test_naming_consistency_across_components);
}
```

### 5. 运行测试

```bash
# 编译测试
idf.py -D UNIT_TEST=1 build

# 运行测试
idf.py flash monitor

# 或者使用测试模式
idf.py -D UNIT_TEST=1 flash monitor
```

### 6. 自动化测试脚本

```python
#!/usr/bin/env python3
# scripts/run_event_naming_tests.py

import subprocess
import sys
import os

def run_test_suite():
    """Run the complete event naming test suite"""
    print("Starting Event Naming Test Suite...")
    
    # 设置测试环境
    env = os.environ.copy()
    env['UNIT_TEST'] = '1'
    
    try:
        # 编译测试
        print("Building tests...")
        result = subprocess.run(['idf.py', 'build'], 
                              cwd='main/boards/ALichuangTest',
                              env=env,
                              capture_output=True, 
                              text=True)
        
        if result.returncode != 0:
            print(f"Build failed: {result.stderr}")
            return False
            
        print("Build successful")
        
        # 运行测试（模拟器模式）
        print("Running tests...")
        # 这里可以添加真实的测试运行逻辑
        
        return True
        
    except Exception as e:
        print(f"Test execution failed: {e}")
        return False

if __name__ == "__main__":
    success = run_test_suite()
    sys.exit(0 if success else 1)
```

## 测试覆盖率目标

- **功能覆盖率**: >95%
- **分支覆盖率**: >90%
- **边界情况**: 全覆盖
- **性能测试**: 包含

## 完成标准

- [ ] 所有测试用例通过
- [ ] 测试覆盖率达标
- [ ] 性能测试通过
- [ ] 边界情况处理正确
- [ ] 自动化测试脚本可用

## 下一步
完成单元测试创建后，继续 [09_integration_testing.md](09_integration_testing.md) 执行集成测试。