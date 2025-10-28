#ifndef ALICHUANGTEST_LOCAL_RESPONSE_CONTROLLER_H
#define ALICHUANGTEST_LOCAL_RESPONSE_CONTROLLER_H

#include "../core/event_engine.h"
#include "../core/emotion_engine.h"
#include "../../skills/motion.h"
#include "../../skills/vibration.h"
#include "display/display.h"
#include "application.h"
#include <cJSON.h>
#include <memory>
#include <vector>
#include <map>
#include <functional>

// 前向声明
class ResponseComponent;
class ResponseTemplate;

// 执行上下文 - 包含执行响应所需的所有信息
struct ExecutionContext {
    DeviceState device_state;
    Event event;
    EmotionQuadrant current_quadrant;
    float current_valence;
    float current_arousal;
    
    // 硬件控制接口
    Motion* motion_skill;
    Vibration* vibration_skill;
    Display* display;
    
    ExecutionContext() 
        : device_state(DeviceState::kDeviceStateIdle)
        , current_quadrant(EmotionQuadrant::POSITIVE_LOW_AROUSAL)
        , current_valence(0.0f)
        , current_arousal(0.0f)
        , motion_skill(nullptr)
        , vibration_skill(nullptr)
        , display(nullptr) {}
};

// 响应组件类型枚举
enum class ComponentType {
    VIBRATION,
    MOTION,
    ANIMATION,
    AUDIO
};

// 轻量级响应组件（避免虚拟继承）
struct ResponseComponent {
    ComponentType type;
    union {
        vibration_id_t vibration_pattern;
        motion_id_t motion_id;
        struct {
            const char* animation_name;  // 动画名称（对应SD卡路径映射）
            int loop_count;              // 循环次数：-1=无限循环, 1=播放一次, >1=指定次数
        } animation;
        struct {
            const char* audio_name;      // 音频名称（对应SD卡路径映射）
            int volume;                  // 音量 0-100
        } audio;
    } data;
    
    // 构造函数
    static ResponseComponent CreateVibration(vibration_id_t pattern) {
        ResponseComponent comp;
        comp.type = ComponentType::VIBRATION;
        comp.data.vibration_pattern = pattern;
        return comp;
    }
    
    static ResponseComponent CreateMotion(motion_id_t motion) {
        ResponseComponent comp;
        comp.type = ComponentType::MOTION;
        comp.data.motion_id = motion;
        return comp;
    }
    
    static ResponseComponent CreateAnimation(const char* animation, int loop_count = 1) {
        ResponseComponent comp;
        comp.type = ComponentType::ANIMATION;
        comp.data.animation.animation_name = animation;
        comp.data.animation.loop_count = loop_count;
        return comp;
    }

    static ResponseComponent CreateAudio(const char* audio, int volume = 100) {
        ResponseComponent comp;
        comp.type = ComponentType::AUDIO;
        comp.data.audio.audio_name = audio;
        comp.data.audio.volume = volume;
        return comp;
    }
    
    // 执行函数
    void Execute(const ExecutionContext& context) const;
    bool CanExecute(DeviceState state) const;
    const char* GetTypeName() const;
};

// 状态响应模板 - 映射设备状态到四个象限的响应
struct StateResponseTemplate {
    DeviceState state;
    ResponseComponent quadrant_responses[4];  // 四个象限对应的响应
    bool has_response[4];  // 每个象限是否有配置响应

    StateResponseTemplate() : state(kDeviceStateUnknown) {
        for (int i = 0; i < 4; i++) {
            has_response[i] = false;
        }
    }
};

// 响应模板 - 定义特定事件的响应组合（内存优化版本）
class ResponseTemplate {
private:
    static constexpr size_t MAX_BASE_COMPONENTS = 5;
    static constexpr size_t MAX_QUADRANT_COMPONENTS = 3;
    
public:
    const char* name;  // 使用字符串常量
    EventType trigger_event;
    int priority;  // 1=紧急, 2=象限相关

    // 打断控制：指定哪些设备状态下可以执行此响应
    static constexpr size_t MAX_INTERRUPT_STATES = 5;
    DeviceState can_interrupt_states[MAX_INTERRUPT_STATES];
    size_t can_interrupt_count = 0;
    bool can_interrupt_all = false;  // true表示可以打断所有状态

    // 基础组件（固定大小数组，避免动态分配）
    ResponseComponent base_components[MAX_BASE_COMPONENTS];
    size_t base_component_count = 0;

    // 象限特定组件（固定大小数组）
    struct QuadrantComponents {
        ResponseComponent components[MAX_QUADRANT_COMPONENTS];
        size_t count = 0;
    };
    QuadrantComponents quadrant_variants[4]; // 4个情感象限

    ResponseTemplate(): name(nullptr), trigger_event(static_cast<EventType>(0)), priority(0), can_interrupt_count(0), can_interrupt_all(false) {}
    ResponseTemplate(const char* name, EventType event, int priority);
    ~ResponseTemplate() = default;
    
    // 获取要执行的所有组件
    void GetComponents(EmotionQuadrant quadrant, ResponseComponent** components, size_t* count) const;

    // 添加基础组件
    void AddBaseComponent(const ResponseComponent& component);

    // 添加象限特定组件
    void AddQuadrantComponent(EmotionQuadrant quadrant, const ResponseComponent& component);

    // 检查是否可以在当前设备状态下执行
    bool CanInterrupt(DeviceState current_state) const;
};

// 本地响应控制器 - 主控制类
class LocalResponseController {
public:
    LocalResponseController(
        Motion* motion_skill,
        Vibration* vibration_skill,
        Display* display
    );
    ~LocalResponseController();
    
    // 初始化响应系统
    bool Initialize();
    
    // 处理事件并执行响应
    void ProcessEvent(const Event& event);

    // 处理设备状态变化并执行响应
    void ProcessStateChange(DeviceState new_state);

    // 加载响应配置
    bool LoadConfig(const std::string& config_json);
    bool LoadDefaultConfig();
    
    // 状态查询
    bool IsInitialized() const { return initialized_; }
    size_t GetTemplateCount() const { return template_count_; }
    
    // 调试接口
    void ListTemplates() const;
    void TestResponse(EventType event_type, EmotionQuadrant quadrant);
    
private:
    // 硬件接口
    Motion* motion_skill_;
    Vibration* vibration_skill_;
    Display* display_;

    // 响应模板（固定大小数组，避免动态分配）
    static constexpr size_t MAX_TEMPLATES = 50;  // 增加到50以容纳所有配置的事件
    ResponseTemplate templates_[MAX_TEMPLATES];
    size_t template_count_ = 0;

    // 状态响应模板数组
    static constexpr size_t MAX_STATE_TEMPLATES = 10;  // 最多支持10种设备状态的响应
    StateResponseTemplate state_templates_[MAX_STATE_TEMPLATES];
    size_t state_template_count_ = 0;

    // 字符串缓冲区池（用于存储从JSON读取的动画/音频名称）
    // 原因：cJSON_Delete()后指针会失效，需要持久化存储
    // Increase name buffer to reduce truncation of event/asset names
    static constexpr size_t MAX_NAME_LENGTH = 64;
    static constexpr size_t MAX_NAME_POOL_SIZE = 100;  // 50 templates * 2 (animation+audio per template)
    char name_pool_[MAX_NAME_POOL_SIZE][MAX_NAME_LENGTH];
    size_t name_pool_index_ = 0;

    // 初始化状态
    bool initialized_;

    struct ResponseTemplParaDef {
        vibration_id_t vibration_pattern;
        motion_id_t motion_id;
        struct {
            const char* animation_name;
            int loop_count;
        } animation;
        struct {
            const char* audio_name;
            int volume;
        } audio;
        bool vibration_enabled;
        bool motion_enabled;
        bool animation_enabled;
        bool audio_enabled;
    };
    
    // 内部方法
    ResponseTemplate* FindTemplate(EventType event_type);
    ExecutionContext CreateContext(const Event& event) const;
    void ExecuteComponents(ResponseComponent** components, size_t count, const ExecutionContext& context);
    
    // SD卡配置加载
    void CreateDefaultTemplatesFromSD();
    void LoadEventTemplate(const char* event_name, cJSON* event_config);
    void LoadStateTemplate(const char* event_name, cJSON* event_config);
    EventType ParseEventName(const char* event_name);
    void LoadResponseComponents(cJSON* event_config, const char* event_name, ResponseTemplate& tmpl);
    StateResponseTemplate* FindOrCreateStateTemplate(DeviceState state);
    StateResponseTemplate* FindStateTemplate(DeviceState state);
    DeviceState ParseStateName(const char* event_name);

    // 参数解析
    vibration_id_t ParseVibrationPattern(const char* pattern_str);
    motion_id_t ParseMotionAction(const char* action_str);
    DeviceState ParseDeviceState(const char* state_str);

    // can_interrupt 解析
    void LoadCanInterruptStates(cJSON* event_config, ResponseTemplate& tmpl);

    // 字符串池管理 - 分配持久化字符串存储
    const char* AllocateString(const char* str);
};

#endif // ALICHUANGTEST_LOCAL_RESPONSE_CONTROLLER_H
