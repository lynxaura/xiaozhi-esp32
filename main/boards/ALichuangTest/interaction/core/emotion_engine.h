#ifndef ALICHUANGTEST_EMOTION_ENGINE_H
#define ALICHUANGTEST_EMOTION_ENGINE_H

#include "esp_timer.h"
#include "esp_log.h"
#include <map>
#include <string>
#include <functional>

// 前向声明
struct Event;
enum class EventType;

// 情感象限定义
enum class EmotionQuadrant {
    POSITIVE_HIGH_AROUSAL,    // 积极高激活 (V>0, A>0)
    POSITIVE_LOW_AROUSAL,     // 积极低激活 (V>0, A<=0)
    NEGATIVE_HIGH_AROUSAL,    // 消极高激活 (V<=0, A>0)
    NEGATIVE_LOW_AROUSAL      // 消极低激活 (V<=0, A<=0)
};

// 事件对情感的影响
struct EventImpact {
    float delta_valence;
    float delta_arousal;
    
    EventImpact() : delta_valence(0.0f), delta_arousal(0.0f) {}
    EventImpact(float v, float a) : delta_valence(v), delta_arousal(a) {}
};

// 情感引擎类 - 基于二维情感空间的状态管理系统
class EmotionEngine {
public:
    // 单例模式
    static EmotionEngine& GetInstance();
    
    // 初始化
    void Initialize();
    
    // 事件驱动的情感更新
    void OnEvent(const Event& event);
    
    // 直接设置情感状态（用于云端控制）
    void SetState(float valence, float arousal);
    
    // 查询接口
    EmotionQuadrant GetQuadrant() const;
    std::pair<float, float> GetCoordinates() const;
    float GetValence() const { return current_valence_; }
    float GetArousal() const { return current_arousal_; }
    
    // 配置接口
    void SetDecayEnabled(bool enabled);
    void SetDecayRate(float rate);
    void SetBaseline(float v, float a);
    
    // 调试接口
    void PrintCurrentState() const;
    
    // 事件上报回调类型
    using EmotionReportCallback = std::function<void(const Event& event, float valence, float arousal)>;
    
    // 设置情感状态上报回调
    void SetEmotionReportCallback(EmotionReportCallback callback);
    
private:
    EmotionEngine();
    ~EmotionEngine();
    
    // 单例实例
    static EmotionEngine* instance_;
    
    // 当前情感坐标
    float current_valence_;
    float current_arousal_;
    
    // 基线值（衰减目标）
    float baseline_valence_;
    float baseline_arousal_;
    
    // 时间衰减配置
    esp_timer_handle_t decay_timer_;
    bool decay_enabled_;
    float slow_decay_rate_;   // 慢衰减速率（每秒）
    float fast_decay_rate_;   // 快衰减速率（每秒）
    int64_t last_event_time_; // 最后一次事件发生时间（微秒）
    int64_t fast_decay_threshold_us_; // 快衰减阈值时间（15秒，微秒）
    bool initialized_;
    
    // 事件影响映射表
    std::map<EventType, EventImpact> event_impact_map_;
    
    // 事件上报回调
    EmotionReportCallback emotion_report_callback_;
    
    // 内部方法
    void UpdateState(float delta_v, float delta_a);
    void ClampValues();
    void ProcessDecay();
    void InitializeEventImpactMap();
    
    // 静态回调函数（用于ESP定时器）
    static void DecayTimerCallback(void* arg);
};

#endif // ALICHUANGTEST_EMOTION_ENGINE_H