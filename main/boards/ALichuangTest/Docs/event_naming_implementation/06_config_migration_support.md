# Step 6: 添加配置文件迁移支持

## 目标
在 event_names.cc 中实现配置文件版本检测和自动迁移功能，确保旧配置文件能够平滑升级。

## 实施步骤

### 1. 定义配置版本常量

```cpp
// 在 event_names.cc 顶部添加
#define CURRENT_CONFIG_VERSION 2
#define MIN_SUPPORTED_VERSION 1

// 版本历史：
// v1: 初始版本，使用混合格式事件名称
// v2: 统一格式，使用 EventNames 系统
```

### 2. 实现配置版本检测

```cpp
int EventNames::GetConfigVersion(const char* json_content) {
    cJSON* root = cJSON_Parse(json_content);
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse JSON for version check");
        return 1; // 假设是v1
    }
    
    cJSON* version = cJSON_GetObjectItem(root, "config_version");
    int config_version = 1; // 默认版本
    
    if (version && cJSON_IsNumber(version)) {
        config_version = version->valueint;
    } else {
        // 检查特征来判断版本
        cJSON* strategies = cJSON_GetObjectItem(root, "event_processing_strategies");
        if (strategies) {
            cJSON* touch_events = cJSON_GetObjectItem(strategies, "touch_events");
            if (touch_events) {
                // 检查是否使用旧格式事件名称
                if (cJSON_GetObjectItem(touch_events, "TouchTap") ||
                    cJSON_GetObjectItem(touch_events, "SINGLE_TAP") ||
                    cJSON_GetObjectItem(touch_events, "touch_tap")) {
                    config_version = 1;
                } else if (cJSON_GetObjectItem(touch_events, "TOUCH_TAP")) {
                    config_version = 2;
                }
            }
        }
    }
    
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Detected config version: %d", config_version);
    return config_version;
}
```

### 3. 实现配置验证功能

```cpp
bool EventNames::ValidateConfig(const char* json_content) {
    cJSON* root = cJSON_Parse(json_content);
    if (!root) {
        ESP_LOGE(TAG, "Invalid JSON format");
        return false;
    }
    
    bool valid = true;
    std::vector<std::string> errors;
    
    // 检查必需的顶层字段
    if (!cJSON_GetObjectItem(root, "event_processing_strategies")) {
        errors.push_back("Missing 'event_processing_strategies' section");
        valid = false;
    }
    
    // 检查事件处理策略
    cJSON* strategies = cJSON_GetObjectItem(root, "event_processing_strategies");
    if (strategies) {
        // 验证触摸事件
        cJSON* touch_events = cJSON_GetObjectItem(strategies, "touch_events");
        if (touch_events) {
            cJSON* event = NULL;
            cJSON_ArrayForEach(event, touch_events) {
                if (event->string) {
                    EventType type = ParseConfigName(event->string);
                    if (type == EventType::MOTION_NONE) {
                        errors.push_back("Unknown touch event: " + std::string(event->string));
                        valid = false;
                    } else if (!IsTouchEvent(type)) {
                        errors.push_back("Non-touch event in touch_events: " + std::string(event->string));
                        valid = false;
                    }
                }
            }
        }
        
        // 验证运动事件
        cJSON* motion_events = cJSON_GetObjectItem(strategies, "motion_events");
        if (motion_events) {
            cJSON* event = NULL;
            cJSON_ArrayForEach(event, motion_events) {
                if (event->string) {
                    EventType type = ParseConfigName(event->string);
                    if (type == EventType::MOTION_NONE) {
                        errors.push_back("Unknown motion event: " + std::string(event->string));
                        valid = false;
                    } else if (!IsMotionEvent(type)) {
                        errors.push_back("Non-motion event in motion_events: " + std::string(event->string));
                        valid = false;
                    }
                }
            }
        }
    }
    
    // 记录所有错误
    for (const auto& error : errors) {
        ESP_LOGE(TAG, "Config validation error: %s", error.c_str());
    }
    
    cJSON_Delete(root);
    return valid;
}
```

### 4. 实现配置迁移功能

```cpp
std::string EventNames::MigrateConfigToLatest(const char* json_content) {
    int current_version = GetConfigVersion(json_content);
    
    if (current_version >= CURRENT_CONFIG_VERSION) {
        ESP_LOGI(TAG, "Config is already at latest version");
        return std::string(json_content);
    }
    
    if (current_version < MIN_SUPPORTED_VERSION) {
        ESP_LOGE(TAG, "Config version %d is too old, minimum supported: %d", 
                 current_version, MIN_SUPPORTED_VERSION);
        return "";
    }
    
    std::string result = json_content;
    
    // 逐版本迁移
    for (int version = current_version; version < CURRENT_CONFIG_VERSION; version++) {
        switch (version) {
            case 1:
                result = MigrateFromV1ToV2(result);
                ESP_LOGI(TAG, "Migrated config from v1 to v2");
                break;
            // 未来版本的迁移逻辑...
            default:
                ESP_LOGW(TAG, "No migration path from version %d", version);
                break;
        }
    }
    
    return result;
}
```

### 5. 实现 V1 到 V2 的迁移

```cpp
std::string EventNames::MigrateFromV1ToV2(const std::string& v1_config) {
    cJSON* root = cJSON_Parse(v1_config.c_str());
    if (!root) {
        return "";
    }
    
    // 事件名称映射表（旧 -> 新）
    static const std::map<std::string, std::string> name_migration = {
        // 触摸事件
        {"TouchTap", "TOUCH_TAP"},
        {"Touch_Tap", "TOUCH_TAP"},
        {"touch_tap", "TOUCH_TAP"},
        {"SINGLE_TAP", "TOUCH_TAP"},
        {"TAP", "TOUCH_TAP"},
        
        {"TouchLongPress", "TOUCH_LONG_PRESS"},
        {"Touch_Long_Press", "TOUCH_LONG_PRESS"},
        {"touch_long_press", "TOUCH_LONG_PRESS"},
        {"HOLD", "TOUCH_LONG_PRESS"},
        {"LONG_PRESS", "TOUCH_LONG_PRESS"},
        
        // 运动事件
        {"FreeFall", "MOTION_FREE_FALL"},
        {"FREE_FALL", "MOTION_FREE_FALL"},
        {"Motion_FreeFall", "MOTION_FREE_FALL"},
        
        {"Shake", "MOTION_SHAKE"},
        {"SHAKE", "MOTION_SHAKE"},
        {"Motion_Shake", "MOTION_SHAKE"},
        
        {"ShakeViolently", "MOTION_SHAKE_VIOLENTLY"},
        {"SHAKE_VIOLENTLY", "MOTION_SHAKE_VIOLENTLY"},
    };
    
    // 迁移事件处理策略
    cJSON* strategies = cJSON_GetObjectItem(root, "event_processing_strategies");
    if (strategies) {
        MigrateEventNames(cJSON_GetObjectItem(strategies, "touch_events"), name_migration);
        MigrateEventNames(cJSON_GetObjectItem(strategies, "motion_events"), name_migration);
    }
    
    // 添加版本号
    cJSON_AddNumberToObject(root, "config_version", CURRENT_CONFIG_VERSION);
    
    // 生成新的 JSON
    char* json_str = cJSON_Print(root);
    std::string result = json_str;
    
    cJSON_Delete(root);
    free(json_str);
    
    return result;
}

void EventNames::MigrateEventNames(cJSON* events_obj, 
                                   const std::map<std::string, std::string>& name_map) {
    if (!events_obj) return;
    
    // 收集需要迁移的项目
    std::vector<std::pair<std::string, cJSON*>> to_migrate;
    
    cJSON* event = NULL;
    cJSON_ArrayForEach(event, events_obj) {
        if (event->string) {
            auto it = name_map.find(event->string);
            if (it != name_map.end()) {
                to_migrate.push_back({it->second, cJSON_Duplicate(event, true)});
            }
        }
    }
    
    // 删除旧名称，添加新名称
    for (const auto& item : to_migrate) {
        // 删除旧项目
        cJSON_DeleteItemFromObject(events_obj, item.second->string);
        
        // 修改名称并添加新项目
        free(item.second->string);
        item.second->string = cJSON_strdup(item.first.c_str());
        cJSON_AddItemToObject(events_obj, item.first.c_str(), item.second);
        
        ESP_LOGD(TAG, "Migrated event name: %s -> %s", 
                 item.second->string, item.first.c_str());
    }
}
```

### 6. 添加配置备份功能

```cpp
bool EventNames::BackupConfig(const std::string& original_path) {
    std::string backup_path = original_path + ".backup." + 
                             std::to_string(esp_timer_get_time() / 1000000);
    
    FILE* src = fopen(original_path.c_str(), "r");
    if (!src) {
        ESP_LOGE(TAG, "Cannot open source file for backup: %s", original_path.c_str());
        return false;
    }
    
    FILE* dst = fopen(backup_path.c_str(), "w");
    if (!dst) {
        fclose(src);
        ESP_LOGE(TAG, "Cannot create backup file: %s", backup_path.c_str());
        return false;
    }
    
    // 复制文件内容
    char buffer[1024];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        fwrite(buffer, 1, bytes, dst);
    }
    
    fclose(src);
    fclose(dst);
    
    ESP_LOGI(TAG, "Config backed up to: %s", backup_path.c_str());
    return true;
}
```

## 验证步骤

### 测试版本检测

```cpp
void TestVersionDetection() {
    // 测试 v1 配置（无版本号）
    const char* v1_config = R"({
        "event_processing_strategies": {
            "touch_events": {
                "TouchTap": { "strategy": "MERGE" }
            }
        }
    })";
    
    assert(EventNames::GetConfigVersion(v1_config) == 1);
    
    // 测试 v2 配置（有版本号）
    const char* v2_config = R"({
        "config_version": 2,
        "event_processing_strategies": {
            "touch_events": {
                "TOUCH_TAP": { "strategy": "MERGE" }
            }
        }
    })";
    
    assert(EventNames::GetConfigVersion(v2_config) == 2);
}
```

### 测试配置迁移

```cpp
void TestConfigMigration() {
    const char* v1_config = R"({
        "event_processing_strategies": {
            "touch_events": {
                "TouchTap": { "strategy": "MERGE" },
                "SINGLE_TAP": { "strategy": "IMMEDIATE" }
            },
            "motion_events": {
                "FreeFall": { "strategy": "IMMEDIATE" }
            }
        }
    })";
    
    std::string migrated = EventNames::MigrateConfigToLatest(v1_config);
    
    // 验证迁移结果
    cJSON* root = cJSON_Parse(migrated.c_str());
    assert(root != nullptr);
    
    cJSON* version = cJSON_GetObjectItem(root, "config_version");
    assert(version && version->valueint == CURRENT_CONFIG_VERSION);
    
    // 验证事件名称已更新
    cJSON* touch_events = cJSON_GetObjectItem(
        cJSON_GetObjectItem(root, "event_processing_strategies"),
        "touch_events"
    );
    
    assert(cJSON_GetObjectItem(touch_events, "TOUCH_TAP") != nullptr);
    assert(cJSON_GetObjectItem(touch_events, "TouchTap") == nullptr);
    
    cJSON_Delete(root);
}
```

## 注意事项

1. **数据安全**
   - 始终备份原配置文件
   - 迁移失败时能够恢复
   - 提供详细的错误日志

2. **兼容性**
   - 支持多个版本的迁移路径
   - 向前兼容新版本
   - 优雅处理未知字段

3. **性能考虑**
   - 迁移是一次性操作
   - 可以接受较高的内存使用
   - 优先考虑正确性

## 完成标准

- [ ] 版本检测功能正常
- [ ] 配置验证功能完整
- [ ] 迁移功能正确处理所有事件名称
- [ ] 备份机制可靠
- [ ] 错误处理完善

## 下一步
完成配置迁移支持后，继续 [07_sd_card_compatibility.md](07_sd_card_compatibility.md) 实现 SD 卡配置兼容性。