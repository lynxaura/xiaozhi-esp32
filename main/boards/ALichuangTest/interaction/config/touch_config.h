#ifndef TOUCH_CONFIG_H
#define TOUCH_CONFIG_H

#include <cstdint>

// 触摸检测配置结构 - 默认值通过JSON配置文件定义
struct TouchDetectionConfig {
    uint32_t tap_max_duration_ms;      // 单击最大持续时间
    uint32_t hold_min_duration_ms;     // 长按最小持续时间
    uint32_t cradled_min_duration_ms;  // 摇篮模式最小持续时间
    uint32_t tickled_window_ms;        // 挠痒检测窗口
    uint32_t tickled_min_touches;      // 挠痒最小触摸次数
    uint32_t debounce_time_ms;         // 消抖时间
    float touch_threshold_ratio;       // 触摸阈值比例

    // 构造函数 - 所有值初始化为0，实际值由JSON配置加载
    TouchDetectionConfig()
        : tap_max_duration_ms(0)
        , hold_min_duration_ms(0)
        , cradled_min_duration_ms(0)
        , tickled_window_ms(0)
        , tickled_min_touches(0)
        , debounce_time_ms(0)
        , touch_threshold_ratio(0.0f) {}
};

// 触摸配置加载器
class TouchConfigLoader {
public:
    // 从JSON文件加载配置
    static bool LoadFromFile(const char* filepath, TouchDetectionConfig& config);
    
    // 从嵌入的默认配置加载
    static TouchDetectionConfig LoadDefaults();
    
    // 从JSON字符串解析配置
    static bool ParseFromJson(const char* json_str, TouchDetectionConfig& config);
};

#endif // TOUCH_CONFIG_H