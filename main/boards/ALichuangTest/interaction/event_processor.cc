#include "event_processor.h"
#include "event_engine.h"  // 现在包含完整定义
#include <esp_log.h>
#include <algorithm>

#define TAG "EventProcessor"

// 辅助函数：获取策略名称
static const char* GetStrategyName(EventProcessingStrategy strategy) {
    switch (strategy) {
        case EventProcessingStrategy::IMMEDIATE: return "IMMEDIATE";
        case EventProcessingStrategy::DEBOUNCE: return "DEBOUNCE";
        case EventProcessingStrategy::THROTTLE: return "THROTTLE";
        case EventProcessingStrategy::QUEUE: return "QUEUE";
        case EventProcessingStrategy::MERGE: return "MERGE";
        case EventProcessingStrategy::COOLDOWN: return "COOLDOWN";
        default: return "UNKNOWN";
    }
}

EventProcessor::EventProcessor() {
    // 设置默认策略为立即处理
    default_config_.strategy = EventProcessingStrategy::IMMEDIATE;
    default_config_.interval_ms = 0;
    ESP_LOGI(TAG, "EventProcessor created with default strategy IMMEDIATE");
}

EventProcessor::~EventProcessor() {
    // 清理队列中的事件
    while (!event_queue_.empty()) {
        Event* event = (Event*)event_queue_.front();
        delete event;
        event_queue_.pop();
    }
    
    // 清理pending事件
    for (auto& pair : event_states_) {
        if (pair.second.pending_event) {
            delete (Event*)pair.second.pending_event;
            pair.second.pending_event = nullptr;
        }
    }
    
    ESP_LOGI(TAG, "EventProcessor destroyed, memory cleaned");
}

void EventProcessor::ConfigureEventType(EventType type, const EventProcessingConfig& config) {
    event_states_[(int)type].config = config;
    ESP_LOGI(TAG, "Configured event type %d with strategy %d, interval %ldms", 
            (int)type, (int)config.strategy, config.interval_ms);
}

void EventProcessor::SetDefaultStrategy(const EventProcessingConfig& config) {
    default_config_ = config;
    ESP_LOGI(TAG, "Set default strategy to %d with interval %ldms", 
            (int)config.strategy, config.interval_ms);
}

bool EventProcessor::ProcessEvent(const Event& event, Event& processed_event) {
    // 获取或创建事件状态
    auto& state = event_states_[(int)event.type];
    
    // 输出接收到的事件
    ESP_LOGI(TAG, "[接收] Event type %d, strategy: %s", 
            (int)event.type, 
            GetStrategyName(state.config.strategy));
    
    // 如果没有配置，使用默认配置
    if (state.config.strategy == EventProcessingStrategy::IMMEDIATE && 
        state.config.interval_ms == 0 && 
        state.last_process_time == 0) {
        state.config = default_config_;
    }
    
    // 更新统计
    state.stats.received_count++;
    
    // 根据策略处理事件
    bool should_process = false;
    processed_event = event;  // 默认使用原事件
    
    switch (state.config.strategy) {
        case EventProcessingStrategy::IMMEDIATE:
            should_process = ProcessImmediate(processed_event, state);
            break;
        case EventProcessingStrategy::DEBOUNCE:
            should_process = ProcessDebounce(processed_event, state);
            break;
        case EventProcessingStrategy::THROTTLE:
            should_process = ProcessThrottle(processed_event, state);
            break;
        case EventProcessingStrategy::QUEUE:
            should_process = ProcessQueue(processed_event, state);
            break;
        case EventProcessingStrategy::MERGE:
            should_process = ProcessMerge(processed_event, state);
            break;
        case EventProcessingStrategy::COOLDOWN:
            should_process = ProcessCooldown(processed_event, state);
            break;
    }
    
    if (should_process) {
        state.stats.processed_count++;
        state.last_process_time = esp_timer_get_time();
        ESP_LOGI(TAG, "[处理] Event type %d processed (total processed: %ld, dropped: %ld, merged: %ld)", 
                (int)event.type, state.stats.processed_count, 
                state.stats.dropped_count, state.stats.merged_count);
    } else {
        state.stats.dropped_count++;
        ESP_LOGW(TAG, "[丢弃] Event type %d dropped by %s strategy (total dropped: %ld)", 
                (int)event.type, GetStrategyName(state.config.strategy), 
                state.stats.dropped_count);
    }
    
    return should_process;
}

bool EventProcessor::ProcessImmediate(Event& event, EventState& state) {
    // 立即处理所有事件
    return true;
}

bool EventProcessor::ProcessDebounce(Event& event, EventState& state) {
    const int64_t now_us = esp_timer_get_time();
    const int64_t interval_us = (int64_t)state.config.interval_ms * 1000;

    // interval=0 视为立即通过
    if (interval_us <= 0) {
        state.last_process_time = now_us;
        if (state.pending_event) {
            delete (Event*)state.pending_event;
            state.pending_event = nullptr;
        }
        state.has_pending = false;
        state.pending_count = 0;
        return true;
    }

    bool ready = false;

    // 若"上一次记录的事件"距离现在已超过防抖区间，则先输出上一条
    if (state.has_pending &&
        (now_us - state.last_trigger_time) >= interval_us) {
        event = *(Event*)state.pending_event;
        delete (Event*)state.pending_event;
        state.pending_event = nullptr;
        state.has_pending = false;
        state.pending_count = 0;
        state.last_process_time = now_us;
        ready = true; // 这次调用产出的是"上一条"
    }

    // 记录/更新当前这条为"待输出"
    if (state.has_pending) {
        *(Event*)state.pending_event = event;  // 覆盖为最新
        state.pending_count++;
    } else {
        state.pending_event = new Event(event);   // 首条进入
        state.has_pending = true;
        state.pending_count = 1;
    }
    state.last_trigger_time = now_us;

    ESP_LOGD(TAG, "[DEBOUNCE] pending=%d, count=%lu, interval_ms=%lu, ready=%d",
             state.has_pending, (unsigned long)state.pending_count,
             (unsigned long)state.config.interval_ms, (int)ready);

    return ready;
}

bool EventProcessor::ProcessThrottle(Event& event, EventState& state) {
    int64_t current_time = esp_timer_get_time();
    int64_t time_since_last = (current_time - state.last_process_time) / 1000;
    
    // 检查是否在节流时间内
    if (time_since_last < state.config.interval_ms) {
        ESP_LOGD(TAG, "[THROTTLE] Event throttled, %lldms remaining", 
                state.config.interval_ms - time_since_last);
        return false;  // 还在节流期，丢弃事件
    }
    
    ESP_LOGD(TAG, "[THROTTLE] Event allowed after %lldms", time_since_last);
    
    return true;
}

bool EventProcessor::ProcessQueue(Event& event, EventState& state) {
    // 入队
    if (event_queue_.size() < state.config.max_queue_size) {
        event_queue_.push(new Event(event));
        ESP_LOGD(TAG, "[QUEUE] enqueued, size=%zu", event_queue_.size());
    } else {
        state.stats.dropped_count++;
        ESP_LOGW(TAG, "[QUEUE] full, drop. dropped=%ld", state.stats.dropped_count);
        return false;
    }

    const int64_t now_us = esp_timer_get_time();
    const int64_t interval_us = (int64_t)state.config.interval_ms * 1000;

    // interval=0 直接弹出
    if (interval_us <= 0) {
        if (!event_queue_.empty()) {
            Event* queued_event = (Event*)event_queue_.front();
            event = *queued_event;
            delete queued_event;
            event_queue_.pop();
            state.last_process_time = now_us;
            ESP_LOGD(TAG, "[QUEUE] popped(immediate), size=%zu", event_queue_.size());
            return true;
        }
        return false;
    }

    // 基于 last_process_time 做"节流式"出队
    if ((now_us - state.last_process_time) >= interval_us && !event_queue_.empty()) {
        Event* queued_event = (Event*)event_queue_.front();
        event = *queued_event;
        delete queued_event;
        event_queue_.pop();
        state.last_process_time = now_us;
        ESP_LOGD(TAG, "[QUEUE] popped, size=%zu", event_queue_.size());
        return true;
    }

    return false;
}

bool EventProcessor::ProcessMerge(Event& event, EventState& state) {
    const int64_t now_us = esp_timer_get_time();
    const int64_t win_us = (int64_t)state.config.merge_window_ms * 1000;

    // merge_window=0 视为不合并，直接通过
    if (win_us <= 0) {
        state.last_process_time = now_us;
        if (state.pending_event) {
            delete (Event*)state.pending_event;
            state.pending_event = nullptr;
        }
        state.has_pending = false;
        state.pending_count = 0;
        return true;
    }

    const bool in_window =
        state.has_pending &&
        (now_us - state.last_trigger_time) < win_us;

    if (in_window) {
        // 合并到已有 pending
        MergeEvents(*(Event*)state.pending_event, event);
        state.pending_count++;
        state.stats.merged_count++;
        state.last_trigger_time = now_us;
        ESP_LOGD(TAG, "[MERGE] merged, count=%lu", (unsigned long)state.pending_count);
        return false; // 仍在窗口内，不输出
    }

    // 不在窗口：如果之前有 pending，先把它输出
    if (state.has_pending) {
        event = *(Event*)state.pending_event;
        delete (Event*)state.pending_event;
        state.pending_event = nullptr;
        state.has_pending = false;
        state.pending_count = 0;
        state.last_process_time = now_us;
        ESP_LOGD(TAG, "[MERGE] window closed -> flush");
        return true;  // 产出上一窗口的汇总事件
    }

    // 开启新窗口：当前事件成为 pending
    state.pending_event = new Event(event);
    state.has_pending = true;
    state.pending_count = 1;
    state.last_trigger_time = now_us;
    ESP_LOGD(TAG, "[MERGE] window started");
    return false;
}

bool EventProcessor::ProcessCooldown(Event& event, EventState& state) {
    int64_t current_time = esp_timer_get_time();
    int64_t time_since_last = (current_time - state.last_process_time) / 1000;
    
    // 检查是否在冷却期
    if (time_since_last < state.config.interval_ms) {
        ESP_LOGI(TAG, "[COOLDOWN] Event in cooldown, %lldms remaining", 
                state.config.interval_ms - time_since_last);
        return false;
    }
    
    ESP_LOGD(TAG, "[COOLDOWN] Event allowed after %lldms cooldown", time_since_last);
    
    return true;
}

void EventProcessor::MergeEvents(Event& existing, const Event& new_event) {
    // 合并逻辑：根据事件类型决定如何合并
    if (existing.type == new_event.type) {
        // 对于触摸事件，增加计数
        if (existing.type == EventType::TOUCH_TAP) {
            // 使用touch_data.y存储点击次数（原本是持续时间）
            existing.data.touch_data.y++;
            ESP_LOGD(TAG, "Merged tap event, count: %d", existing.data.touch_data.y);
        }
        // 可以根据需要添加其他事件类型的合并逻辑
    }
}

bool EventProcessor::GetNextQueuedEvent(Event& out) {
    if (event_queue_.empty()) return false;
    Event* queued_event = (Event*)event_queue_.front();
    out = *queued_event;
    delete queued_event;
    event_queue_.pop();
    ESP_LOGD(TAG, "[QUEUE] manual pop, size=%zu", event_queue_.size());
    return true;
}

void EventProcessor::ClearEventQueue(EventType type) {
    if (event_queue_.empty()) return;

    std::queue<void*> kept;           // 保留的事件
    size_t removed = 0;

    while (!event_queue_.empty()) {
        Event* front = (Event*)event_queue_.front();
        if (front->type != type) {
            // 保留这个事件
            kept.push(front);
        } else {
            delete front;  // 删除匹配类型的事件
            removed++;
        }
        event_queue_.pop();
    }

    event_queue_.swap(kept);
    ESP_LOGI(TAG, "[QUEUE] cleared type=%d, removed=%zu, remain=%zu",
             static_cast<int>(type), removed, event_queue_.size());
}

void EventProcessor::ClearEventQueueAll() {
    while (!event_queue_.empty()) {
        Event* front = (Event*)event_queue_.front();
        delete front;
        event_queue_.pop();
    }
    ESP_LOGI(TAG, "[QUEUE] cleared all, remain=%zu", event_queue_.size());
}

bool EventProcessor::IsInCooldown(EventType type) const {
    auto it = event_states_.find((int)type);
    if (it == event_states_.end()) {
        return false;
    }
    
    int64_t current_time = esp_timer_get_time();
    return (current_time - it->second.last_process_time) < (it->second.config.interval_ms * 1000);
}

EventProcessor::EventStats EventProcessor::GetStats(EventType type) const {
    auto it = event_states_.find((int)type);
    if (it != event_states_.end()) {
        return it->second.stats;
    }
    return EventStats{0, 0, 0, 0, 0};
}