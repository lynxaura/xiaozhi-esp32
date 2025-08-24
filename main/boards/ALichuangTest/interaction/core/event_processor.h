#ifndef ALICHUANGTEST_EVENT_PROCESSOR_H
#define ALICHUANGTEST_EVENT_PROCESSOR_H

#include <queue>
#include <map>
#include <esp_timer.h>
#include <optional>
#include <memory> // For std::unique_ptr

// Forward declarations
struct Event;
enum class EventType;

// Event processing strategies
enum class EventProcessingStrategy {
    IMMEDIATE,      // Immediately process each event
    DEBOUNCE,       // Debounce: only process the last event
    THROTTLE,       // Throttle: only process the first event in a time window
    QUEUE,          // Queue: process events sequentially with a minimum interval
    MERGE,          // Merge: combine similar events into one
    COOLDOWN        // Cooldown: wait for a period after processing before accepting a new event
};

// Event processing configuration
struct EventProcessingConfig {
    EventProcessingStrategy strategy;
    uint32_t interval_ms;      // Debounce/throttle/cooldown time (ms)
    uint32_t merge_window_ms;  // Merging window time (ms)
    uint32_t max_queue_size;   // Maximum queue length for QUEUE strategy
    bool allow_interrupt;      // Whether this event can be interrupted by a high-priority one
    
    EventProcessingConfig() 
        : strategy(EventProcessingStrategy::IMMEDIATE)
        , interval_ms(500)
        , merge_window_ms(1000)
        , max_queue_size(10)
        , allow_interrupt(false) {}
};

// Event processor class
class EventProcessor {
public:
    EventProcessor();
    ~EventProcessor();
    
    void ConfigureEventType(EventType type, const EventProcessingConfig& config);
    void SetDefaultStrategy(const EventProcessingConfig& config);
    bool ProcessEvent(const Event& event, Event& processed_event);
    bool GetNextQueuedEvent(Event& event);
    void ClearEventQueue(EventType type);
    void ClearEventQueueAll();
    bool IsInCooldown(EventType type) const;
    
    struct EventStats {
        uint32_t received_count;
        uint32_t processed_count;
        uint32_t dropped_count;
        uint32_t merged_count;
        int64_t last_process_time;
    };
    
    EventStats GetStats(EventType type) const;
    
private:
    struct EventState {
        int64_t last_trigger_time;
        int64_t last_process_time;
        uint32_t pending_count;
        std::unique_ptr<Event> pending_event; // Use smart pointer for safety
        bool has_pending;
        EventProcessingConfig config;
        EventStats stats;
        
        EventState() 
            : last_trigger_time(0)
            , last_process_time(0)
            , pending_count(0)
            , pending_event(nullptr)
            , has_pending(false)
            , stats{0, 0, 0, 0, 0} {}
        
        // Rule of Five: Ensure proper handling of unique_ptr
        EventState(EventState&&) = default;
        EventState& operator=(EventState&&) = default;
        EventState(const EventState&) = delete;
        EventState& operator=(const EventState&) = delete;
    };
    
    std::map<int, EventState> event_states_;
    std::queue<std::unique_ptr<Event>> event_queue_; // Use smart pointer for safety
    EventProcessingConfig default_config_;
    
    bool ProcessImmediate(Event& event, EventState& state);
    bool ProcessDebounce(Event& event, EventState& state);
    bool ProcessThrottle(Event& event, EventState& state);
    bool ProcessQueue(Event& event, EventState& state);
    bool ProcessMerge(Event& event, EventState& state);
    bool ProcessCooldown(Event& event, EventState& state);
    
    void MergeEvents(Event& existing, const Event& new_event);
};

// Predefined processing strategy configurations
namespace EventProcessingPresets {
    inline EventProcessingConfig TouchTapConfig() {
        EventProcessingConfig config;
        config.strategy = EventProcessingStrategy::COOLDOWN;
        config.interval_ms = 300;
        return config;
    }
    
    inline EventProcessingConfig MultiTapConfig() {
        EventProcessingConfig config;
        config.strategy = EventProcessingStrategy::MERGE;
        config.merge_window_ms = 2000;
        config.interval_ms = 500;
        return config;
    }
    
    inline EventProcessingConfig MotionEventConfig() {
        EventProcessingConfig config;
        config.strategy = EventProcessingStrategy::THROTTLE;
        config.interval_ms = 1000;
        return config;
    }
    
    inline EventProcessingConfig EmergencyEventConfig() {
        EventProcessingConfig config;
        config.strategy = EventProcessingStrategy::IMMEDIATE;
        config.allow_interrupt = true;
        return config;
    }
    
    inline EventProcessingConfig QueuedEventConfig() {
        EventProcessingConfig config;
        config.strategy = EventProcessingStrategy::QUEUE;
        config.interval_ms = 800;
        config.max_queue_size = 5;
        return config;
    }
}

#endif // ALICHUANGTEST_EVENT_PROCESSOR_H
