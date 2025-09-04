#include "emotion_engine.h"
#include "event_engine.h" 
#include <cmath>
#include <algorithm>

static const char* TAG = "EmotionEngine";

// 静态成员定义
EmotionEngine* EmotionEngine::instance_ = nullptr;

EmotionEngine& EmotionEngine::GetInstance() {
    if (instance_ == nullptr) {
        instance_ = new EmotionEngine();
    }
    return *instance_;
}

EmotionEngine::EmotionEngine() 
    : current_valence_(0.2f),
      current_arousal_(0.2f),
      baseline_valence_(0.2f),
      baseline_arousal_(0.2f),
      decay_timer_(nullptr),
      decay_enabled_(true),
      slow_decay_rate_(0.01f),   // 慢衰减：0.01每秒
      fast_decay_rate_(0.05f),   // 快衰减：0.05每秒
      last_event_time_(0),
      fast_decay_threshold_us_(15 * 1000 * 1000),  // 15秒转换为微秒
      initialized_(false) {
}

EmotionEngine::~EmotionEngine() {
    if (decay_timer_ != nullptr) {
        esp_timer_stop(decay_timer_);
        esp_timer_delete(decay_timer_);
        decay_timer_ = nullptr;
    }
}

void EmotionEngine::Initialize() {
    if (initialized_) {
        return;
    }
    
    ESP_LOGI(TAG, "Initializing Emotion Engine");
    
    // 初始化事件影响映射表
    InitializeEventImpactMap();
    
    // 创建衰减定时器（10秒间隔）
    esp_timer_create_args_t timer_args = {
        .callback = DecayTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "emotion_decay"
    };
    
    esp_err_t ret = esp_timer_create(&timer_args, &decay_timer_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create decay timer: %s", esp_err_to_name(ret));
        return;
    }
    
    // 启动定时器（1秒间隔用于精确的双阶段衰减控制）
    if (decay_enabled_) {
        ret = esp_timer_start_periodic(decay_timer_, 1 * 1000 * 1000); // 1秒
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start decay timer: %s", esp_err_to_name(ret));
        }
    }
    
    // 初始化事件时间为当前时间
    last_event_time_ = esp_timer_get_time();
    
    initialized_ = true;
    ESP_LOGI(TAG, "Emotion Engine initialized with baseline V=%.2f, A=%.2f", 
             baseline_valence_, baseline_arousal_);
    ESP_LOGI(TAG, "Dual-stage decay: slow=%.3f/s (0-15s), fast=%.3f/s (>15s)", 
             slow_decay_rate_, fast_decay_rate_);
    PrintCurrentState();
}

void EmotionEngine::InitializeEventImpactMap() {
    // 运动事件的情感影响
    event_impact_map_[EventType::MOTION_FREE_FALL] = EventImpact(-0.8f, +0.9f);    // 恐惧
    event_impact_map_[EventType::MOTION_SHAKE_VIOLENTLY] = EventImpact(-0.3f, +0.7f); // 晕眩
    event_impact_map_[EventType::MOTION_FLIP] = EventImpact(+0.2f, +0.4f);          // 好玩
    event_impact_map_[EventType::MOTION_SHAKE] = EventImpact(+0.1f, +0.3f);         // 轻微兴奋
    event_impact_map_[EventType::MOTION_PICKUP] = EventImpact(+0.05f, +0.2f);       // 被关注
    event_impact_map_[EventType::MOTION_UPSIDE_DOWN] = EventImpact(-0.2f, +0.3f);   // 不适
    
    // 触摸事件的情感影响
    event_impact_map_[EventType::TOUCH_TAP] = EventImpact(+0.1f, +0.1f);           // 友好互动
    event_impact_map_[EventType::TOUCH_LONG_PRESS] = EventImpact(+0.3f, -0.1f);     // 温暖抚摸
    event_impact_map_[EventType::TOUCH_CRADLED] = EventImpact(+0.5f, -0.3f);       // 被呵护
    event_impact_map_[EventType::TOUCH_TICKLED] = EventImpact(+0.4f, +0.6f);        // 被逗乐
    
    // 音频事件的情感影响（预留）
    event_impact_map_[EventType::AUDIO_WAKE_WORD] = EventImpact(+0.1f, +0.3f);      // 被唤醒
    event_impact_map_[EventType::AUDIO_SPEAKING] = EventImpact(0.0f, +0.2f);        // 表达中
    event_impact_map_[EventType::AUDIO_LISTENING] = EventImpact(0.0f, -0.1f);       // 倾听中
}

void EmotionEngine::OnEvent(const Event& event) {
    if (!initialized_) {
        return;
    }
    
    // 记录事件发生时间
    last_event_time_ = esp_timer_get_time();
    
    auto it = event_impact_map_.find(event.type);
    if (it != event_impact_map_.end()) {
        const EventImpact& impact = it->second;
        
        ESP_LOGD(TAG, "Event impact: type=%d, ΔV=%.2f, ΔA=%.2f", 
                 (int)event.type, impact.delta_valence, impact.delta_arousal);
        
        // 更新情感状态
        UpdateState(impact.delta_valence, impact.delta_arousal);
        PrintCurrentState();
        
        // 上报情感状态到云端
        if (emotion_report_callback_) {
            emotion_report_callback_(event, current_valence_, current_arousal_);
            ESP_LOGD(TAG, "Emotion state reported: [V=%.2f, A=%.2f]", 
                     current_valence_, current_arousal_);
        }
    }
}

void EmotionEngine::UpdateState(float delta_v, float delta_a) {
    current_valence_ += delta_v;
    current_arousal_ += delta_a;
    ClampValues();
}

void EmotionEngine::ClampValues() {
    current_valence_ = std::max(-1.0f, std::min(1.0f, current_valence_));
    current_arousal_ = std::max(-1.0f, std::min(1.0f, current_arousal_));
}

void EmotionEngine::SetState(float valence, float arousal) {
    current_valence_ = valence;
    current_arousal_ = arousal;
    ClampValues();
    PrintCurrentState();
}


EmotionQuadrant EmotionEngine::GetQuadrant() const {
    if (current_valence_ > 0.0f) {
        return (current_arousal_ > 0.0f) ? EmotionQuadrant::POSITIVE_HIGH_AROUSAL : EmotionQuadrant::POSITIVE_LOW_AROUSAL;
    } else {
        return (current_arousal_ > 0.0f) ? EmotionQuadrant::NEGATIVE_HIGH_AROUSAL : EmotionQuadrant::NEGATIVE_LOW_AROUSAL;
    }
}


std::pair<float, float> EmotionEngine::GetCoordinates() const {
    return std::make_pair(current_valence_, current_arousal_);
}



void EmotionEngine::SetDecayEnabled(bool enabled) {
    decay_enabled_ = enabled;
    
    if (decay_timer_ != nullptr) {
        if (enabled) {
            esp_timer_start_periodic(decay_timer_, 1 * 1000 * 1000); // 1秒
        } else {
            esp_timer_stop(decay_timer_);
        }
    }
    
    ESP_LOGI(TAG, "Decay %s", enabled ? "enabled" : "disabled");
}

void EmotionEngine::SetDecayRate(float rate) {
    // 为了兼容性，同时设置慢衰减和快衰减速率
    slow_decay_rate_ = std::max(0.0f, std::min(1.0f, rate));
    fast_decay_rate_ = std::max(0.0f, std::min(1.0f, rate * 5.0f)); // 快衰减是慢衰减的5倍
    ESP_LOGI(TAG, "Decay rates set: slow=%.3f/s, fast=%.3f/s", slow_decay_rate_, fast_decay_rate_);
}

void EmotionEngine::SetBaseline(float v, float a) {
    baseline_valence_ = std::max(-1.0f, std::min(1.0f, v));
    baseline_arousal_ = std::max(-1.0f, std::min(1.0f, a));
    ESP_LOGI(TAG, "Baseline set to V=%.2f, A=%.2f", baseline_valence_, baseline_arousal_);
}

void EmotionEngine::SetEmotionReportCallback(EmotionReportCallback callback) {
    emotion_report_callback_ = callback;
    ESP_LOGI(TAG, "Emotion report callback %s", callback ? "registered" : "cleared");
}

void EmotionEngine::ProcessDecay() {
    if (!decay_enabled_) {
        return;
    }
    
    int64_t current_time = esp_timer_get_time();
    int64_t time_since_last_event = current_time - last_event_time_;
    
    // 选择衰减速率：15秒内用慢衰减，超过15秒用快衰减
    float decay_rate_per_second;
    const char* decay_type;
    
    if (time_since_last_event < fast_decay_threshold_us_) {
        decay_rate_per_second = slow_decay_rate_;  // 0.01每秒
        decay_type = "slow";
    } else {
        decay_rate_per_second = fast_decay_rate_;  // 0.05每秒  
        decay_type = "fast";
    }
    
    // 计算向基线的衰减量
    float v_diff = baseline_valence_ - current_valence_;
    float a_diff = baseline_arousal_ - current_arousal_;
    
    // 应用衰减（每秒调用一次，直接使用每秒速率）
    float v_decay_amount = v_diff > 0 ? 
        std::min(v_diff, decay_rate_per_second) : 
        std::max(v_diff, -decay_rate_per_second);
    float a_decay_amount = a_diff > 0 ? 
        std::min(a_diff, decay_rate_per_second) : 
        std::max(a_diff, -decay_rate_per_second);
    
    float old_v = current_valence_;
    float old_a = current_arousal_;
    
    current_valence_ += v_decay_amount;
    current_arousal_ += a_decay_amount;
    
    // 防止越过基线
    ClampValues();
    
    // 如果状态发生了变化，记录调试信息
    if (std::abs(v_decay_amount) > 0.001f || std::abs(a_decay_amount) > 0.001f) {
        ESP_LOGD(TAG, "%s decay (%.1fs since event): V=%.2f→%.2f, A=%.2f→%.2f", 
                 decay_type, time_since_last_event / 1000000.0f,
                 old_v, current_valence_, old_a, current_arousal_);
    }
}

void EmotionEngine::PrintCurrentState() const {
    EmotionQuadrant quadrant = GetQuadrant();
    
    const char* quadrant_name = "";
    switch (quadrant) {
        case EmotionQuadrant::POSITIVE_HIGH_AROUSAL:
            quadrant_name = "POSITIVE_HIGH_AROUSAL";
            break;
        case EmotionQuadrant::POSITIVE_LOW_AROUSAL:
            quadrant_name = "POSITIVE_LOW_AROUSAL";
            break;
        case EmotionQuadrant::NEGATIVE_HIGH_AROUSAL:
            quadrant_name = "NEGATIVE_HIGH_AROUSAL";
            break;
        case EmotionQuadrant::NEGATIVE_LOW_AROUSAL:
            quadrant_name = "NEGATIVE_LOW_AROUSAL";
            break;
    }
    
    ESP_LOGI(TAG, "Emotion State: [V=%.2f, A=%.2f] (%s)", 
             current_valence_, current_arousal_, quadrant_name);
}

// 静态定时器回调函数
void EmotionEngine::DecayTimerCallback(void* arg) {
    EmotionEngine* engine = static_cast<EmotionEngine*>(arg);
    if (engine != nullptr) {
        engine->ProcessDecay();
    }
}