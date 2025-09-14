#include "event_config_loader.h"
#include "../core/emotion_engine.h"
#include <esp_log.h>
#include <cJSON.h>
#include <fstream>
#include <sstream>

#define TAG "EventConfigLoader"

// 静态成员初始化
std::map<std::string, EventResponse> EventConfigLoader::response_map_;

// 默认配置（嵌入式版本）- 包含所有支持的事件
const char* DefaultEventConfig::GetDefaultConfig() {
    return R"({
        "events": {
            "MOTION_FREE_FALL": {
                "detection": {
                    "threshold_g": 0.3,
                    "min_duration_ms": 200
                },
                "processing": {
                    "strategy": "IMMEDIATE"
                },
                "va_impact": {
                    "valence": -0.8,
                    "arousal": 0.9
                }
            },
            "MOTION_SHAKE_VIOLENTLY": {
                "detection": {
                    "threshold_g": 3.0
                },
                "processing": {
                    "strategy": "IMMEDIATE"
                },
                "va_impact": {
                    "valence": -0.3,
                    "arousal": 0.7
                }
            },
            "MOTION_FLIP": {
                "detection": {
                    "threshold_deg_s": 400.0
                },
                "processing": {
                    "strategy": "THROTTLE",
                    "interval_ms": 2000
                },
                "va_impact": {
                    "valence": 0.2,
                    "arousal": 0.4
                }
            },
            "MOTION_SHAKE": {
                "detection": {
                    "normal_threshold_g": 1.5
                },
                "processing": {
                    "strategy": "THROTTLE",
                    "interval_ms": 2000
                },
                "va_impact": {
                    "valence": 0.1,
                    "arousal": 0.3
                }
            },
            "MOTION_PICKUP": {
                "detection": {
                    "threshold_g": 0.15,
                    "stable_threshold_g": 0.05,
                    "stable_count": 5,
                    "min_duration_ms": 300
                },
                "processing": {
                    "strategy": "THROTTLE",
                    "interval_ms": 1000
                },
                "va_impact": {
                    "valence": 0.05,
                    "arousal": 0.2
                }
            },
            "MOTION_UPSIDE_DOWN": {
                "detection": {
                    "threshold_g": -0.8,
                    "stable_count": 10
                },
                "processing": {
                    "strategy": "THROTTLE",
                    "interval_ms": 3000
                },
                "va_impact": {
                    "valence": -0.2,
                    "arousal": 0.3
                }
            },
            "TOUCH_TAP": {
                "detection": {
                    "max_duration_ms": 500,
                    "debounce_time_ms": 30
                },
                "processing": {
                    "strategy": "THROTTLE",
                    "interval_ms": 300
                },
                "va_impact": {
                    "valence": 0.1,
                    "arousal": 0.1
                }
            },
            "TOUCH_DOUBLE_TAP": {
                "detection": {
                    "tap_interval_ms": 300
                },
                "processing": {
                    "strategy": "COOLDOWN",
                    "interval_ms": 1000
                },
                "va_impact": {
                    "valence": 0.15,
                    "arousal": 0.15
                }
            },
            "TOUCH_LONG_PRESS": {
                "detection": {
                    "min_duration_ms": 600
                },
                "processing": {
                    "strategy": "COOLDOWN",
                    "interval_ms": 1000
                },
                "va_impact": {
                    "valence": 0.2,
                    "arousal": -0.2
                }
            },
            "TOUCH_CRADLED": {
                "detection": {
                    "min_duration_ms": 2000
                },
                "processing": {
                    "strategy": "THROTTLE",
                    "interval_ms": 5000
                },
                "va_impact": {
                    "valence": 0.3,
                    "arousal": -0.4
                }
            },
            "TOUCH_TICKLED": {
                "detection": {
                    "window_ms": 2000,
                    "min_touches": 4
                },
                "processing": {
                    "strategy": "COOLDOWN",
                    "interval_ms": 3000
                },
                "va_impact": {
                    "valence": 0.4,
                    "arousal": 0.5
                }
            }
        },
        "global_settings": {
            "batch_upload": {
                "enabled": true,
                "window_ms": 500,
                "max_batch_size": 10
            },
            "default_processing": {
                "strategy": "IMMEDIATE",
                "interval_ms": 0
            },
            "debug": {
                "motion_debug_enabled": false
            }
        }
    })";
}

bool EventConfigLoader::LoadFromFile(const std::string& filepath, EventEngine* engine) {
    // 尝试从文件系统读取配置
    FILE* file = fopen(filepath.c_str(), "r");
    if (!file) {
        return false;  // 返回false让调用者决定是否加载默认配置
    }
    
    // 获取文件大小
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // 读取文件内容
    char* json_data = new char[file_size + 1];
    fread(json_data, 1, file_size, file);
    json_data[file_size] = '\0';
    fclose(file);
    
    // 解析JSON
    bool result = ParseJsonConfig(json_data, engine);
    delete[] json_data;
    
    if (!result) {
        return false;  // 返回false让调用者决定是否加载默认配置
    }
    
    ESP_LOGI(TAG, "Event config loaded from SD card");
    return true;
}

bool EventConfigLoader::LoadFromEmbedded(EventEngine* engine) {
    const char* default_config = DefaultEventConfig::GetDefaultConfig();
    bool result = ParseJsonConfig(default_config, engine);
    if (result) {
        ESP_LOGI(TAG, "Event config loaded from embedded defaults");
    }
    return result;
}

bool EventConfigLoader::ParseJsonConfig(const char* json_data, EventEngine* engine) {
    cJSON* root = cJSON_Parse(json_data);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON config");
        return false;
    }
    
    // 1. 解析全局设置
    cJSON* global_settings = cJSON_GetObjectItem(root, "global_settings");
    if (global_settings) {
        // 解析批量上传配置
        cJSON* batch_upload = cJSON_GetObjectItem(global_settings, "batch_upload");
        if (batch_upload) {
            // 创建兼容的JSON结构供LoadUploadConfig使用
            cJSON* upload_config = cJSON_CreateObject();
            cJSON* event_upload_config = cJSON_CreateObject();
            
            cJSON* enabled = cJSON_GetObjectItem(batch_upload, "enabled");
            if (enabled) {
                cJSON_AddBoolToObject(event_upload_config, "batch_upload_enabled", cJSON_IsTrue(enabled));
            }
            
            cJSON* window_ms = cJSON_GetObjectItem(batch_upload, "window_ms");
            if (window_ms) {
                cJSON_AddNumberToObject(event_upload_config, "batch_window_ms", window_ms->valueint);
            }
            
            cJSON* max_size = cJSON_GetObjectItem(batch_upload, "max_batch_size");
            if (max_size) {
                cJSON_AddNumberToObject(event_upload_config, "max_batch_size", max_size->valueint);
            }
            
            cJSON_AddItemToObject(upload_config, "event_upload_config", event_upload_config);
            engine->LoadUploadConfig(upload_config);
            cJSON_Delete(upload_config);
        }
        
        // 解析默认处理策略
        cJSON* default_processing = cJSON_GetObjectItem(global_settings, "default_processing");
        if (default_processing) {
            EventProcessingConfig config;
            
            cJSON* strategy = cJSON_GetObjectItem(default_processing, "strategy");
            if (strategy) {
                config.strategy = ParseStrategy(strategy->valuestring);
            }
            
            cJSON* interval = cJSON_GetObjectItem(default_processing, "interval_ms");
            if (interval) {
                config.interval_ms = interval->valueint;
            }
            
            engine->SetDefaultProcessingStrategy(config);
        }
    }
    
    // 2. 解析事件配置（统一处理所有事件）
    cJSON* events = cJSON_GetObjectItem(root, "events");
    if (events) {
        // 获取EmotionEngine实例
        EmotionEngine& emotion_engine = EmotionEngine::GetInstance();
        
        cJSON* event = NULL;
        cJSON_ArrayForEach(event, events) {
            if (!event->string) continue;
            
            const char* event_name = event->string;
            EventType event_type = ParseEventType(event_name);
            
            if (event_type == EventType::MOTION_NONE) {
                ESP_LOGW(TAG, "Unknown event type: %s, skipping", event_name);
                continue;
            }
            
            ESP_LOGD(TAG, "Processing event config: %s", event_name);
            
            // 2.1 解析处理策略
            cJSON* processing = cJSON_GetObjectItem(event, "processing");
            if (processing) {
                EventProcessingConfig config;
                
                cJSON* strategy = cJSON_GetObjectItem(processing, "strategy");
                if (strategy && cJSON_IsString(strategy)) {
                    config.strategy = ParseStrategy(strategy->valuestring);
                }
                
                cJSON* interval = cJSON_GetObjectItem(processing, "interval_ms");
                if (interval && cJSON_IsNumber(interval)) {
                    config.interval_ms = interval->valueint;
                }
                
                cJSON* merge_window = cJSON_GetObjectItem(processing, "merge_window_ms");
                if (merge_window && cJSON_IsNumber(merge_window)) {
                    config.merge_window_ms = merge_window->valueint;
                }
                
                cJSON* max_queue = cJSON_GetObjectItem(processing, "max_queue_size");
                if (max_queue && cJSON_IsNumber(max_queue)) {
                    config.max_queue_size = max_queue->valueint;
                }
                
                engine->ConfigureEventProcessing(event_type, config);
                ESP_LOGD(TAG, "Processing strategy configured for %s", event_name);
            }

            // 2.1.1 特殊处理：IDLE_1MIN事件的空闲阈值配置
            if (event_type == EventType::IDLE_1MIN) {
                cJSON* detection = cJSON_GetObjectItem(event, "detection");
                if (detection) {
                    cJSON* idle_duration = cJSON_GetObjectItem(detection, "idle_duration_ms");
                    if (idle_duration && cJSON_IsNumber(idle_duration)) {
                        engine->SetIdleThreshold(idle_duration->valueint);
                        ESP_LOGI(TAG, "Idle threshold configured: %dms", idle_duration->valueint);
                    }
                }
            }

            // 2.2 解析VA影响
            cJSON* va_impact = cJSON_GetObjectItem(event, "va_impact");
            if (va_impact) {
                cJSON* valence = cJSON_GetObjectItem(va_impact, "valence");
                cJSON* arousal = cJSON_GetObjectItem(va_impact, "arousal");
                
                if (valence && arousal && cJSON_IsNumber(valence) && cJSON_IsNumber(arousal)) {
                    emotion_engine.SetEventImpact(event_type, 
                                                  valence->valuedouble, 
                                                  arousal->valuedouble);
                    ESP_LOGI(TAG, "VA impact configured for %s: V=%.2f, A=%.2f", 
                             event_name, valence->valuedouble, arousal->valuedouble);
                }
            }
        }
    }
    
    // 3. 传递完整的JSON给motion_engine和multitouch_engine进行检测参数配置
    // 这两个引擎会从events节点下读取各自需要的detection参数
    engine->UpdateMotionEngineConfig(root);
    engine->UpdateMultitouchEngineConfig(root);
    
    cJSON_Delete(root);
    return true;
}

EventProcessingStrategy EventConfigLoader::ParseStrategy(const std::string& strategy) {
    if (strategy == "IMMEDIATE") return EventProcessingStrategy::IMMEDIATE;
    if (strategy == "DEBOUNCE") return EventProcessingStrategy::DEBOUNCE;
    if (strategy == "THROTTLE") return EventProcessingStrategy::THROTTLE;
    if (strategy == "QUEUE") return EventProcessingStrategy::QUEUE;
    if (strategy == "MERGE") return EventProcessingStrategy::MERGE;
    if (strategy == "COOLDOWN") return EventProcessingStrategy::COOLDOWN;
    
    ESP_LOGW(TAG, "Unknown strategy: %s, using IMMEDIATE", strategy.c_str());
    return EventProcessingStrategy::IMMEDIATE;
}

EventType EventConfigLoader::ParseEventType(const std::string& type_str) {
    // 运动事件
    if (type_str == "MOTION_FREE_FALL") return EventType::MOTION_FREE_FALL;
    if (type_str == "MOTION_SHAKE_VIOLENTLY") return EventType::MOTION_SHAKE_VIOLENTLY;
    if (type_str == "MOTION_FLIP") return EventType::MOTION_FLIP;
    if (type_str == "MOTION_SHAKE") return EventType::MOTION_SHAKE;
    if (type_str == "MOTION_PICKUP") return EventType::MOTION_PICKUP;
    if (type_str == "MOTION_UPSIDE_DOWN") return EventType::MOTION_UPSIDE_DOWN;
    
    // 触摸事件
    if (type_str == "TOUCH_TAP") return EventType::TOUCH_TAP;
    if (type_str == "TOUCH_DOUBLE_TAP") return EventType::TOUCH_DOUBLE_TAP;
    if (type_str == "TOUCH_LONG_PRESS") return EventType::TOUCH_LONG_PRESS;
    if (type_str == "TOUCH_CRADLED") return EventType::TOUCH_CRADLED;
    if (type_str == "TOUCH_TICKLED") return EventType::TOUCH_TICKLED;
    if (type_str == "TOUCH_HOLD") return EventType::TOUCH_HOLD;
    if (type_str == "TOUCH_RELEASE") return EventType::TOUCH_RELEASE;
    
    // 音频事件已移除 - 目前未实际使用
    
    // 特殊事件
    if (type_str == "IDLE_1MIN") return EventType::IDLE_1MIN;
    
    ESP_LOGW(TAG, "Unknown event type: %s", type_str.c_str());
    return EventType::MOTION_NONE;
}

EventResponse EventConfigLoader::GetResponseForEvent(EventType type, const Event& event) {
    // 根据事件类型和数据获取响应
    if (type == EventType::TOUCH_TAP) {
        // 检查是左侧还是右侧
        std::string key;
        switch (event.data.touch_data.position) {
            case TouchPosition::LEFT: key = "tap_left"; break;
            case TouchPosition::RIGHT: key = "tap_right"; break;
            case TouchPosition::BOTH: key = "tap_both"; break;
            default: key = "tap_any"; break;
        }
        auto it = response_map_.find(key);
        if (it != response_map_.end()) {
            return it->second;
        }
    }
    
    // 返回默认响应
    return EventResponse("", "", "neutral");
}

EventResponse EventConfigLoader::GetMultiTapResponse(int tap_count) {
    std::string key = "multi_" + std::to_string(tap_count) + "_taps";
    auto it = response_map_.find(key);
    if (it != response_map_.end()) {
        return it->second;
    }
    
    // 返回默认响应
    return EventResponse("", "", "neutral");
}

bool EventConfigLoader::CheckSpecialPattern(const std::vector<Event>& recent_events) {
    // TODO: 实现特殊模式检测
    // 例如：检查是否是左右交替点击等
    return false;
}