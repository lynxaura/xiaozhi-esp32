# Step 7: SD卡配置兼容性处理

## 目标
增强 SD 卡配置文件的兼容性处理，支持自动迁移和格式检测，确保现有 SD 卡上的配置文件能够正常工作。

## 实施步骤

### 1. 更新文件加载流程

```cpp
// 在 event_config_loader.cc 中更新 LoadFromFile 函数
bool EventConfigLoader::LoadFromFile(const std::string& filepath, EventEngine* engine) {
    ESP_LOGI(TAG, "Loading config from: %s", filepath.c_str());
    
    // 步骤 1: 检查文件是否存在
    FILE* file = fopen(filepath.c_str(), "r");
    if (!file) {
        ESP_LOGW(TAG, "Config file not found: %s", filepath.c_str());
        
        // 尝试查找备用文件
        std::string backup_path = filepath + ".backup";
        if (TryLoadBackupConfig(backup_path, engine)) {
            ESP_LOGI(TAG, "Loaded from backup config: %s", backup_path.c_str());
            return true;
        }
        
        ESP_LOGW(TAG, "No backup found, using default config");
        return LoadFromEmbedded(engine);
    }
    
    // 步骤 2: 读取文件内容
    std::string json_content = ReadFileContent(file);
    fclose(file);
    
    if (json_content.empty()) {
        ESP_LOGE(TAG, "Failed to read config file content");
        return LoadFromEmbedded(engine);
    }
    
    // 步骤 3: 验证配置文件格式
    if (!EventNames::ValidateConfig(json_content.c_str())) {
        ESP_LOGW(TAG, "Config validation failed, attempting repair");
        
        // 尝试修复配置
        std::string repaired = RepairConfig(json_content);
        if (!repaired.empty() && EventNames::ValidateConfig(repaired.c_str())) {
            json_content = repaired;
            ESP_LOGI(TAG, "Config repaired successfully");
        } else {
            ESP_LOGE(TAG, "Config repair failed, using default");
            BackupCorruptedConfig(filepath, json_content);
            return LoadFromEmbedded(engine);
        }
    }
    
    // 步骤 4: 检查版本并迁移
    int config_version = EventNames::GetConfigVersion(json_content.c_str());
    ESP_LOGI(TAG, "Config version: %d (current: %d)", config_version, CURRENT_CONFIG_VERSION);
    
    if (config_version < CURRENT_CONFIG_VERSION) {
        // 备份原文件
        if (!EventNames::BackupConfig(filepath)) {
            ESP_LOGW(TAG, "Failed to backup config, proceeding anyway");
        }
        
        // 迁移配置
        std::string migrated = EventNames::MigrateConfigToLatest(json_content.c_str());
        if (migrated.empty()) {
            ESP_LOGE(TAG, "Config migration failed");
            return LoadFromEmbedded(engine);
        }
        
        json_content = migrated;
        ESP_LOGI(TAG, "Config migrated from v%d to v%d", config_version, CURRENT_CONFIG_VERSION);
        
        // 可选：保存迁移后的配置
        if (ENABLE_AUTO_SAVE_MIGRATED_CONFIG) {
            SaveMigratedConfig(filepath, json_content);
        }
    }
    
    // 步骤 5: 解析和应用配置
    bool result = ParseJsonConfig(json_content.c_str(), engine);
    if (!result) {
        ESP_LOGE(TAG, "Failed to apply config, using default");
        return LoadFromEmbedded(engine);
    }
    
    ESP_LOGI(TAG, "Successfully loaded config from: %s", filepath.c_str());
    return true;
}
```

### 2. 实现辅助函数

```cpp
std::string EventConfigLoader::ReadFileContent(FILE* file) {
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size <= 0 || file_size > MAX_CONFIG_FILE_SIZE) {
        ESP_LOGE(TAG, "Invalid file size: %ld", file_size);
        return "";
    }
    
    std::vector<char> buffer(file_size + 1);
    size_t bytes_read = fread(buffer.data(), 1, file_size, file);
    buffer[bytes_read] = '\0';
    
    return std::string(buffer.data());
}

bool EventConfigLoader::TryLoadBackupConfig(const std::string& backup_path, EventEngine* engine) {
    FILE* backup_file = fopen(backup_path.c_str(), "r");
    if (!backup_file) {
        return false;
    }
    
    std::string backup_content = ReadFileContent(backup_file);
    fclose(backup_file);
    
    if (backup_content.empty()) {
        return false;
    }
    
    // 验证备份文件
    if (!EventNames::ValidateConfig(backup_content.c_str())) {
        ESP_LOGW(TAG, "Backup config is also invalid");
        return false;
    }
    
    // 应用备份配置
    return ParseJsonConfig(backup_content.c_str(), engine);
}

void EventConfigLoader::BackupCorruptedConfig(const std::string& filepath, 
                                              const std::string& content) {
    std::string corrupted_path = filepath + ".corrupted." + 
                                std::to_string(esp_timer_get_time() / 1000000);
    
    FILE* corrupted_file = fopen(corrupted_path.c_str(), "w");
    if (corrupted_file) {
        fwrite(content.c_str(), 1, content.length(), corrupted_file);
        fclose(corrupted_file);
        ESP_LOGI(TAG, "Corrupted config backed up to: %s", corrupted_path.c_str());
    }
}
```

### 3. 实现配置修复功能

```cpp
std::string EventConfigLoader::RepairConfig(const std::string& config) {
    // 尝试修复常见的 JSON 格式问题
    std::string repaired = config;
    
    // 修复 1：移除非法字符
    repaired = RemoveInvalidCharacters(repaired);
    
    // 修复 2：修复缺失的逗号
    repaired = FixMissingCommas(repaired);
    
    // 修复 3：修复括号匹配
    repaired = FixBracketMatching(repaired);
    
    // 修复 4：替换过时的事件名称
    repaired = ReplaceObsoleteEventNames(repaired);
    
    return repaired;
}

std::string EventConfigLoader::RemoveInvalidCharacters(const std::string& config) {
    std::string result;
    result.reserve(config.size());
    
    for (char c : config) {
        // 保留可打印字符和空白字符
        if (std::isprint(c) || std::isspace(c)) {
            result += c;
        }
    }
    
    return result;
}

std::string EventConfigLoader::ReplaceObsoleteEventNames(const std::string& config) {
    std::string result = config;
    
    // 常见的过时名称替换
    static const std::vector<std::pair<std::string, std::string>> replacements = {
        {"\"TouchTap\"", "\"TOUCH_TAP\""},
        {"\"Touch_Tap\"", "\"TOUCH_TAP\""},
        {"\"SINGLE_TAP\"", "\"TOUCH_TAP\""},
        {"\"TouchLongPress\"", "\"TOUCH_LONG_PRESS\""},
        {"\"HOLD\"", "\"TOUCH_LONG_PRESS\""},
        {"\"FreeFall\"", "\"MOTION_FREE_FALL\""},
        {"\"Shake\"", "\"MOTION_SHAKE\""},
    };
    
    for (const auto& replacement : replacements) {
        size_t pos = 0;
        while ((pos = result.find(replacement.first, pos)) != std::string::npos) {
            result.replace(pos, replacement.first.length(), replacement.second);
            pos += replacement.second.length();
            ESP_LOGD(TAG, "Replaced: %s -> %s", 
                     replacement.first.c_str(), replacement.second.c_str());
        }
    }
    
    return result;
}
```

### 4. 实现 SD 卡目录扫描

```cpp
bool EventConfigLoader::FindAndLoadFromSDCard(EventEngine* engine) {
    // SD 卡可能的挂载点
    std::vector<std::string> sd_mount_points = {
        "/sdcard",
        "/mnt/sdcard", 
        "/sd"
    };
    
    // 可能的配置文件名
    std::vector<std::string> config_filenames = {
        "event_config.json",
        "xiaozhi_config.json",
        "alichuang_config.json",
        "config.json"
    };
    
    for (const auto& mount_point : sd_mount_points) {
        for (const auto& filename : config_filenames) {
            std::string full_path = mount_point + "/" + filename;
            
            if (access(full_path.c_str(), F_OK) == 0) {
                ESP_LOGI(TAG, "Found config file: %s", full_path.c_str());
                
                if (LoadFromFile(full_path, engine)) {
                    ESP_LOGI(TAG, "Successfully loaded from SD card: %s", full_path.c_str());
                    return true;
                } else {
                    ESP_LOGW(TAG, "Failed to load from: %s", full_path.c_str());
                }
            }
        }
    }
    
    ESP_LOGI(TAG, "No valid config found on SD card");
    return false;
}
```

### 5. 添加配置文件监控

```cpp
class SDCardConfigMonitor {
public:
    static void StartMonitoring(EventEngine* engine) {
        // 创建监控任务
        xTaskCreate(MonitorTask, "sd_config_monitor", 4096, engine, 5, &monitor_task_handle_);
    }
    
    static void StopMonitoring() {
        if (monitor_task_handle_) {
            vTaskDelete(monitor_task_handle_);
            monitor_task_handle_ = nullptr;
        }
    }
    
private:
    static TaskHandle_t monitor_task_handle_;
    
    static void MonitorTask(void* param) {
        EventEngine* engine = static_cast<EventEngine*>(param);
        time_t last_check = 0;
        
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(5000)); // 每 5 秒检查一次
            
            // 检查配置文件是否有更新
            std::string config_path = "/sdcard/event_config.json";
            struct stat file_stat;
            
            if (stat(config_path.c_str(), &file_stat) == 0) {
                if (file_stat.st_mtime > last_check) {
                    ESP_LOGI(TAG, "Config file updated, reloading...");
                    
                    if (EventConfigLoader::LoadFromFile(config_path, engine)) {
                        ESP_LOGI(TAG, "Config reloaded successfully");
                    } else {
                        ESP_LOGW(TAG, "Failed to reload config");
                    }
                    
                    last_check = file_stat.st_mtime;
                }
            }
        }
    }
};

TaskHandle_t SDCardConfigMonitor::monitor_task_handle_ = nullptr;
```

### 6. 更新主加载逻辑

```cpp
void EventEngine::InitializeConfig() {
    ESP_LOGI(TAG, "Initializing event configuration...");
    
    // 优先级：
    // 1. SD 卡配置文件
    // 2. 内置默认配置
    
    if (EventConfigLoader::FindAndLoadFromSDCard(this)) {
        ESP_LOGI(TAG, "Loaded config from SD card");
        
        // 启动 SD 卡监控（可选）
        if (ENABLE_SD_CARD_MONITORING) {
            SDCardConfigMonitor::StartMonitoring(this);
        }
    } else {
        ESP_LOGI(TAG, "Loading default embedded config");
        EventConfigLoader::LoadFromEmbedded(this);
    }
    
    ESP_LOGI(TAG, "Event configuration initialized");
}
```

## 测试用例

### 模拟不同版本的配置文件

```cpp
void TestSDCardCompatibility() {
    // 测试 v1 配置文件
    const char* v1_config = R"({
        "event_processing_strategies": {
            "touch_events": {
                "TouchTap": { "strategy": "MERGE" },
                "SINGLE_TAP": { "strategy": "IMMEDIATE" }
            }
        }
    })";
    
    // 写入 SD 卡
    FILE* f = fopen("/sdcard/event_config.json", "w");
    fwrite(v1_config, 1, strlen(v1_config), f);
    fclose(f);
    
    // 测试加载
    EventEngine engine;
    bool success = EventConfigLoader::LoadFromFile("/sdcard/event_config.json", &engine);
    assert(success);
    
    // 验证迁移后的配置
    // ...
}
```

## 注意事项

1. **SD 卡可靠性**
   - SD 卡可能随时移除
   - 文件系统可能損坏
   - 需要健壮的错误处理

2. **向后兼容性**
   - 保持支持老版本配置
   - 提供平滑的升级路径
   - 不破坏现有用户配置

3. **性能考虑**
   - SD 卡读取比内存慢
   - 避免频繁文件 I/O
   - 监控功能可选

## 完成标准

- [ ] SD 卡配置文件自动发现
- [ ] 多版本配置兼容
- [ ] 损坏配置文件修复
- [ ] 备份和恢复机制
- [ ] 配置热更新（可选）

## 下一步
完成 SD 卡兼容性处理后，继续 [08_create_unit_tests.md](08_create_unit_tests.md) 创建单元测试。