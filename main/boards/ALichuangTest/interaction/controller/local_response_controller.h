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
    EMOTION
};

// 轻量级响应组件（避免虚拟继承）
struct ResponseComponent {
    ComponentType type;
    union {
        vibration_id_t vibration_pattern;
        motion_id_t motion_id;
        struct {
            const char* emotion_name;  // 使用字符串常量而非std::string
            uint32_t duration_ms;
        } emotion;
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
    
    static ResponseComponent CreateEmotion(const char* emotion, uint32_t duration = 2000) {
        ResponseComponent comp;
        comp.type = ComponentType::EMOTION;
        comp.data.emotion.emotion_name = emotion;
        comp.data.emotion.duration_ms = duration;
        return comp;
    }
    
    // 执行函数
    void Execute(const ExecutionContext& context) const;
    bool CanExecute(DeviceState state) const;
    uint32_t GetDurationMs() const;
    const char* GetTypeName() const;
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
    
    // 基础组件（固定大小数组，避免动态分配）
    ResponseComponent base_components[MAX_BASE_COMPONENTS];
    size_t base_component_count = 0;
    
    // 象限特定组件（固定大小数组）
    struct QuadrantComponents {
        ResponseComponent components[MAX_QUADRANT_COMPONENTS];
        size_t count = 0;
    };
    QuadrantComponents quadrant_variants[4]; // 4个情感象限
    
    ResponseTemplate(): name(nullptr), trigger_event(static_cast<EventType>(0)), priority(0) {}
    ResponseTemplate(const char* name, EventType event, int priority);
    ~ResponseTemplate() = default;
    
    // 获取要执行的所有组件
    void GetComponents(EmotionQuadrant quadrant, ResponseComponent** components, size_t* count) const;
    
    // 添加基础组件
    void AddBaseComponent(const ResponseComponent& component);
    
    // 添加象限特定组件
    void AddQuadrantComponent(EmotionQuadrant quadrant, const ResponseComponent& component);
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
    static constexpr size_t MAX_TEMPLATES = 15;
    ResponseTemplate templates_[MAX_TEMPLATES];
    size_t template_count_ = 0;
    
    // 初始化状态
    bool initialized_;
    
    // 内部方法
    ResponseTemplate* FindTemplate(EventType event_type);
    ExecutionContext CreateContext(const Event& event) const;
    void ExecuteComponents(ResponseComponent** components, size_t count, const ExecutionContext& context);
    
    // 模板创建
    void CreateDefaultTemplates();
    void AddEmergencyTemplates();
    void AddQuadrantTemplates();
    vibration_id_t ParseVibrationPattern(const std::string& pattern_str);
    motion_id_t ParseMotionAction(const std::string& action_str);
    EmotionQuadrant ParseQuadrant(const std::string& quadrant_str);
};

#endif // ALICHUANGTEST_LOCAL_RESPONSE_CONTROLLER_H