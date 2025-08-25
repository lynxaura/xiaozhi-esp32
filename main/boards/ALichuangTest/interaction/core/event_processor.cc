#include "event_processor.h"
#include "event_engine.h" // For Event struct full definition
#include <esp_log.h>
#include <algorithm>
#include <memory> // For std::make_unique

#define TAG "EventProcessor"

// Helper function to get strategy name for logging
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
    default_config_.strategy = EventProcessingStrategy::IMMEDIATE;
    default_config_.interval_ms = 0;
    ESP_LOGI(TAG, "EventProcessor created with default strategy IMMEDIATE");
}

EventProcessor::~EventProcessor() {
    // No manual memory management needed.
    // std::unique_ptr in the map and queue will handle everything automatically.
    ESP_LOGI(TAG, "EventProcessor destroyed, memory cleaned up by smart pointers");
}

void EventProcessor::ConfigureEventType(EventType type, const EventProcessingConfig& config) {
    event_states_[(int)type].config = config;
    ESP_LOGI(TAG, "Configured event type %d with strategy %s, interval %ldms", 
            (int)type, GetStrategyName(config.strategy), config.interval_ms);
}

void EventProcessor::SetDefaultStrategy(const EventProcessingConfig& config) {
    default_config_ = config;
    ESP_LOGI(TAG, "Set default strategy to %s with interval %ldms", 
            GetStrategyName(config.strategy), config.interval_ms);
}

bool EventProcessor::ProcessEvent(const Event& event, Event& processed_event) {
    auto& state = event_states_[(int)event.type];
    
    ESP_LOGD(TAG, "[接收] Event type %d, strategy: %s", 
            (int)event.type, 
            GetStrategyName(state.config.strategy));
    
    if (state.config.strategy == EventProcessingStrategy::IMMEDIATE && 
        state.config.interval_ms == 0 && 
        state.last_process_time == 0) {
        state.config = default_config_;
    }
    
    state.stats.received_count++;
    
    bool should_process = false;
    processed_event = event;
    
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
        ESP_LOGI(TAG, "[处理] Event type %d processed (total processed: %lu, dropped: %lu, merged: %lu)", 
                (int)processed_event.type, (unsigned long)state.stats.processed_count, 
                (unsigned long)state.stats.dropped_count, (unsigned long)state.stats.merged_count);
    } else {
        state.stats.dropped_count++;
        ESP_LOGD(TAG, "[丢弃] Event type %d dropped by %s strategy (total dropped: %lu)", 
                (int)event.type, GetStrategyName(state.config.strategy), 
                (unsigned long)state.stats.dropped_count);
    }
    
    return should_process;
}

bool EventProcessor::ProcessImmediate(Event& event, EventState& state) {
    return true;
}

bool EventProcessor::ProcessDebounce(Event& event, EventState& state) {
    const int64_t now_us = esp_timer_get_time();
    const int64_t interval_us = (int64_t)state.config.interval_ms * 1000;

    if (interval_us <= 0) {
        state.pending_event.reset(); // Clear any pending event
        state.has_pending = false;
        state.pending_count = 0;
        return true;
    }

    bool ready = false;

    if (state.has_pending && (now_us - state.last_trigger_time) >= interval_us) {
        event = *state.pending_event; // Copy data from the pending event
        state.pending_event.reset();  // Free the memory
        state.has_pending = false;
        state.pending_count = 0;
        state.last_process_time = now_us;
        ready = true;
    }

    if (state.has_pending) {
        *state.pending_event = event; // Update the existing event
        state.pending_count++;
    } else {
        state.pending_event = std::make_unique<Event>(event); // Create new pending event
        state.has_pending = true;
        state.pending_count = 1;
    }
    state.last_trigger_time = now_us;

    ESP_LOGD(TAG, "[DEBOUNCE] pending=%d, count=%lu, ready=%d",
             (int)state.has_pending, (unsigned long)state.pending_count, (int)ready);

    return ready;
}

bool EventProcessor::ProcessThrottle(Event& event, EventState& state) {
    int64_t current_time = esp_timer_get_time();
    int64_t time_since_last = (current_time - state.last_process_time) / 1000;
    
    if (time_since_last < state.config.interval_ms) {
        return false;
    }
    
    return true;
}

bool EventProcessor::ProcessQueue(Event& event, EventState& state) {
    if (event_queue_.size() < state.config.max_queue_size) {
        event_queue_.push(std::make_unique<Event>(event));
        ESP_LOGD(TAG, "[QUEUE] enqueued, size=%zu", event_queue_.size());
    } else {
        state.stats.dropped_count++;
        ESP_LOGW(TAG, "[QUEUE] full, dropping event. Total dropped: %lu", (unsigned long)state.stats.dropped_count);
        return false; // Don't process, just drop
    }

    const int64_t now_us = esp_timer_get_time();
    const int64_t interval_us = (int64_t)state.config.interval_ms * 1000;

    if (interval_us <= 0) {
        return GetNextQueuedEvent(event); // Process immediately if possible
    }

    if ((now_us - state.last_process_time) >= interval_us) {
        return GetNextQueuedEvent(event);
    }

    return false;
}

bool EventProcessor::ProcessMerge(Event& event, EventState& state) {
    const int64_t now_us = esp_timer_get_time();
    const int64_t win_us = (int64_t)state.config.merge_window_ms * 1000;

    if (win_us <= 0) {
        state.pending_event.reset();
        state.has_pending = false;
        state.pending_count = 0;
        return true;
    }

    const bool in_window = state.has_pending && (now_us - state.last_trigger_time) < win_us;

    if (in_window) {
        MergeEvents(*state.pending_event, event);
        state.pending_count++;
        state.stats.merged_count++;
        state.last_trigger_time = now_us;
        ESP_LOGD(TAG, "[MERGE] merged, count=%lu", (unsigned long)state.pending_count);
        return false;
    }

    if (state.has_pending) {
        event = *state.pending_event;
        state.pending_event.reset();
        state.has_pending = false;
        state.pending_count = 0;
        state.last_process_time = now_us;
        ESP_LOGD(TAG, "[MERGE] window closed -> flush");
        return true;
    }

    state.pending_event = std::make_unique<Event>(event);
    state.has_pending = true;
    state.pending_count = 1;
    state.last_trigger_time = now_us;
    ESP_LOGD(TAG, "[MERGE] window started");
    return false;
}

bool EventProcessor::ProcessCooldown(Event& event, EventState& state) {
    int64_t current_time = esp_timer_get_time();
    int64_t time_since_last = (current_time - state.last_process_time) / 1000;
    
    if (time_since_last < state.config.interval_ms) {
        return false;
    }
    
    return true;
}

void EventProcessor::MergeEvents(Event& existing, const Event& new_event) {
    if (existing.type == new_event.type) {
        if (existing.type == EventType::TOUCH_TAP) {
            existing.data.touch_data.tap_count++;
        } else if (existing.type == EventType::TOUCH_LONG_PRESS) {
            if (new_event.data.touch_data.duration_ms > existing.data.touch_data.duration_ms) {
                existing.data.touch_data.duration_ms = new_event.data.touch_data.duration_ms;
            }
        }
    }
}

bool EventProcessor::GetNextQueuedEvent(Event& out) {
    if (event_queue_.empty()) return false;
    
    std::unique_ptr<Event> queued_event = std::move(event_queue_.front());
    event_queue_.pop();
    
    out = *queued_event;
    ESP_LOGD(TAG, "[QUEUE] manual pop, size=%zu", event_queue_.size());
    return true;
}

void EventProcessor::ClearEventQueue(EventType type) {
    if (event_queue_.empty()) return;

    std::queue<std::unique_ptr<Event>> kept;
    size_t removed = 0;

    while (!event_queue_.empty()) {
        std::unique_ptr<Event> front = std::move(event_queue_.front());
        event_queue_.pop();
        if (front->type != type) {
            kept.push(std::move(front));
        } else {
            removed++;
        }
    }

    event_queue_.swap(kept);
    ESP_LOGI(TAG, "[QUEUE] cleared type=%d, removed=%zu, remain=%zu",
             static_cast<int>(type), removed, event_queue_.size());
}

void EventProcessor::ClearEventQueueAll() {
    std::queue<std::unique_ptr<Event>> empty_queue;
    event_queue_.swap(empty_queue);
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