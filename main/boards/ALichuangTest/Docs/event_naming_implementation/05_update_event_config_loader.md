# Step 5: 更新 event_config_loader.cc 兼容性

## 目标
使用 EventNames 类替换硬编码的事件类型解析，支持多种配置格式，确保向后兼容性。

## 文件位置
`main/boards/ALichuangTest/interaction/config/event_config_loader.cc`

## 当前问题

```cpp
// 当前的硬编码解析
EventType EventConfigLoader::ParseEventType(const std::string& type_str) {
    if (type_str == "TOUCH_TAP") return EventType::TOUCH_TAP;
    if (type_str == "TOUCH_LONG_PRESS") return EventType::TOUCH_LONG_PRESS;
    if (type_str == "MOTION_SHAKE") return EventType::MOTION_SHAKE;
    // ... 更多硬编码
    return EventType::MOTION_NONE;
}
```

## 实施步骤

### 1. 添加 event_names.h 头文件

```cpp
#include "../core/event_names.h"  // 添加
#include "event_config_loader.h"
#include <esp_log.h>
#include <cJSON.h>
// ... 其他包含
```

### 2. 替换 ParseEventType 函数

```cpp
EventType EventConfigLoader::ParseEventType(const std::string& type_str) {
    // 旧代码：大量的 if 语句
    // 新代码：使用 EventNames 支持多格式
    
    EventType type = EventNames::ParseConfigName(type_str);
    
    if (type == EventType::MOTION_NONE) {
        ESP_LOGW(TAG, "Unknown event type in config: %s", type_str.c_str());
    } else {
        ESP_LOGD(TAG, "Parsed event type: %s -> %s", 
                 type_str.c_str(), 
                 EventNames::GetInternalName(type));
    }
    
    return type;
}
```

### 3. 增强配置文件验证

```cpp
bool EventConfigLoader::ValidateConfigFile(const char* json_content) {
    // 使用 EventNames 提供的验证功能
    if (!EventNames::ValidateConfig(json_content)) {
        ESP_LOGE(TAG, "Config validation failed");
        return false;
    }
    
    // 解析 JSON 并检查所有事件名称
    cJSON* root = cJSON_Parse(json_content);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return false;
    }
    
    bool valid = true;
    
    // 检查触摸事件配置
    cJSON* touch_events = cJSON_GetObjectItem(
        cJSON_GetObjectItem(root, "event_processing_strategies"), 
        "touch_events"
    );
    
    if (touch_events) {
        cJSON* event = NULL;
        cJSON_ArrayForEach(event, touch_events) {
            if (event->string && !EventNames::IsValidConfigName(event->string)) {
                ESP_LOGW(TAG, "Unknown event name in config: %s", event->string);
                valid = false;
            }
        }
    }
    
    // 检查运动事件配置
    cJSON* motion_events = cJSON_GetObjectItem(
        cJSON_GetObjectItem(root, "event_processing_strategies"), 
        "motion_events"
    );
    
    if (motion_events) {
        cJSON* event = NULL;
        cJSON_ArrayForEach(event, motion_events) {
            if (event->string && !EventNames::IsValidConfigName(event->string)) {
                ESP_LOGW(TAG, "Unknown event name in config: %s", event->string);
                valid = false;
            }
        }
    }
    
    cJSON_Delete(root);
    return valid;
}
```

### 4. 添加配置迁移支持

```cpp
bool EventConfigLoader::LoadFromFile(const std::string& filepath, EventEngine* engine) {
    // 读取文件
    FILE* file = fopen(filepath.c_str(), "r");
    if (!file) {
        ESP_LOGW(TAG, "Config file not found: %s, using default config", filepath.c_str());
        return LoadFromEmbedded(engine);
    }
    
    // 读取文件内容
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char* json_data = new char[file_size + 1];
    fread(json_data, 1, file_size, file);
    json_data[file_size] = '\0';
    fclose(file);
    
    // 检查配置版本并迁移（如果需要）
    int config_version = EventNames::GetConfigVersion(json_data);
    ESP_LOGI(TAG, "Config file version: %d", config_version);
    
    std::string processed_json;
    if (config_version < CURRENT_CONFIG_VERSION) {
        ESP_LOGI(TAG, "Migrating config from version %d to %d", 
                 config_version, CURRENT_CONFIG_VERSION);
        processed_json = EventNames::MigrateConfigToLatest(json_data);
        
        // 可选：将迁移后的配置写回文件
        if (ENABLE_AUTO_MIGRATION_SAVE) {
            SaveMigratedConfig(filepath, processed_json);
        }
    } else {
        processed_json = json_data;
    }
    
    // 解析配置
    bool result = ParseJsonConfig(processed_json.c_str(), engine);
    delete[] json_data;
    
    if (!result) {
        ESP_LOGW(TAG, "Failed to parse config file, using default config");
        return LoadFromEmbedded(engine);
    }
    
    ESP_LOGI(TAG, "Loaded event config from file: %s", filepath.c_str());
    return true;
}
```

### 5. 更新默认配置生成

```cpp
const char* DefaultEventConfig::GetDefaultConfig() {
    // 使用 EventNames 获取标准名称
    static std::string default_config;
    
    if (default_config.empty()) {
        cJSON* root = cJSON_CreateObject();
        cJSON* strategies = cJSON_CreateObject();
        cJSON* touch_events = cJSON_CreateObject();
        cJSON* motion_events = cJSON_CreateObject();
        
        // 添加触摸事件默认配置
        cJSON* tap_config = cJSON_CreateObject();
        cJSON_AddStringToObject(tap_config, "strategy", "MERGE");
        cJSON_AddNumberToObject(tap_config, "merge_window_ms", 1500);
        cJSON_AddNumberToObject(tap_config, "interval_ms", 500);
        cJSON_AddItemToObject(touch_events, 
                             EventNames::GetConfigName(EventType::TOUCH_TAP), 
                             tap_config);
        
        cJSON* long_press_config = cJSON_CreateObject();
        cJSON_AddStringToObject(long_press_config, "strategy", "COOLDOWN");
        cJSON_AddNumberToObject(long_press_config, "interval_ms", 1000);
        cJSON_AddItemToObject(touch_events, 
                             EventNames::GetConfigName(EventType::TOUCH_LONG_PRESS), 
                             long_press_config);
        
        // 添加运动事件默认配置
        cJSON* shake_config = cJSON_CreateObject();
        cJSON_AddStringToObject(shake_config, "strategy", "THROTTLE");
        cJSON_AddNumberToObject(shake_config, "interval_ms", 2000);
        cJSON_AddItemToObject(motion_events, 
                             EventNames::GetConfigName(EventType::MOTION_SHAKE), 
                             shake_config);
        
        cJSON* fall_config = cJSON_CreateObject();
        cJSON_AddStringToObject(fall_config, "strategy", "IMMEDIATE");
        cJSON_AddBoolToObject(fall_config, "allow_interrupt", true);
        cJSON_AddItemToObject(motion_events, 
                             EventNames::GetConfigName(EventType::MOTION_FREE_FALL), 
                             fall_config);
        
        // 组装 JSON
        cJSON_AddItemToObject(strategies, "touch_events", touch_events);
        cJSON_AddItemToObject(strategies, "motion_events", motion_events);
        cJSON_AddItemToObject(root, "event_processing_strategies", strategies);
        
        // 添加版本信息
        cJSON_AddNumberToObject(root, "config_version", CURRENT_CONFIG_VERSION);
        
        char* json_str = cJSON_Print(root);
        default_config = json_str;
        
        cJSON_Delete(root);
        free(json_str);
    }
    
    return default_config.c_str();
}
```

### 6. 添加配置迁移辅助函数

```cpp
void EventConfigLoader::SaveMigratedConfig(const std::string& filepath, 
                                           const std::string& config) {
    // 备份原文件
    std::string backup_path = filepath + ".backup";
    rename(filepath.c_str(), backup_path.c_str());
    
    // 写入新配置
    FILE* file = fopen(filepath.c_str(), "w");
    if (file) {
        fwrite(config.c_str(), 1, config.length(), file);
        fclose(file);
        ESP_LOGI(TAG, "Migrated config saved to: %s", filepath.c_str());
        ESP_LOGI(TAG, "Original config backed up to: %s", backup_path.c_str());
    } else {
        ESP_LOGE(TAG, "Failed to save migrated config");
        // 恢复原文件
        rename(backup_path.c_str(), filepath.c_str());
    }
}
```

## 验证步骤

### 测试多格式解析

```cpp
void TestConfigParsing() {
    // 测试标准格式
    assert(EventConfigLoader::ParseEventType("TOUCH_TAP") == EventType::TOUCH_TAP);
    
    // 测试旧格式兼容
    assert(EventConfigLoader::ParseEventType("TouchTap") == EventType::TOUCH_TAP);
    assert(EventConfigLoader::ParseEventType("SINGLE_TAP") == EventType::TOUCH_TAP);
    assert(EventConfigLoader::ParseEventType("touch_tap") == EventType::TOUCH_TAP);
    
    // 测试未知类型
    assert(EventConfigLoader::ParseEventType("UNKNOWN_EVENT") == EventType::MOTION_NONE);
}
```

### 测试配置文件加载

```bash
# 创建测试配置文件
echo '{
    "event_processing_strategies": {
        "touch_events": {
            "TouchTap": { "strategy": "MERGE" },
            "TOUCH_TAP": { "strategy": "MERGE" },
            "touch_tap": { "strategy": "MERGE" }
        }
    }
}' > /sdcard/test_config.json

# 加载并验证
```

## 回滚方案

如果出现问题：
1. 恢复原有的 ParseEventType 实现
2. 移除配置验证和迁移逻辑
3. 恢复硬编码的事件名称

## 注意事项

1. **向后兼容性**
   - 必须支持现有 SD 卡上的配置文件
   - 支持多种命名格式
   - 提供平滑的迁移路径

2. **配置验证**
   - 检查所有事件名称是否有效
   - 提供清晰的错误信息
   - 失败时回退到默认配置

3. **性能考虑**
   - 配置加载不是频繁操作
   - 可以接受较小的性能开销

## 完成标准

- [ ] 代码编译通过
- [ ] 支持多种配置格式
- [ ] 现有配置文件正常加载
- [ ] 配置验证正常工作
- [ ] 迁移功能正常（可选）

## 下一步
完成 event_config_loader.cc 更新后，继续 [06_config_migration_support.md](06_config_migration_support.md) 实现配置迁移支持。