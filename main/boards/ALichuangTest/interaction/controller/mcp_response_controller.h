#ifndef ALICHUANGTEST_MCP_RESPONSE_CONTROLLER_H
#define ALICHUANGTEST_MCP_RESPONSE_CONTROLLER_H

#include "mcp_server.h"
#include "../../skills/motion.h"
#include "../../skills/vibration.h"
#include "../core/event_engine.h"
#include <string>
#include <functional>

// 前向声明
class Display;

/**
 * @brief MCP响应控制器
 *        基于MCP协议实现云端大模型调用本地响应功能
 *        提供身体动作、振动反馈、屏幕动画等综合表达能力
 */
class McpResponseController {
public:
    /**
     * @brief 构造函数
     * @param motion_skill 直流马达动作控制技能指针
     * @param vibration_skill 振动技能指针
     * @param event_engine 事件引擎指针
     * @param get_display_func 获取显示器的函数
     * @param get_current_emotion_func 获取当前情绪的函数
     * @param set_current_emotion_func 设置当前情绪的函数
     */
    McpResponseController(
        Motion* motion_skill,
        Vibration* vibration_skill, 
        EventEngine* event_engine,
        std::function<Display*()> get_display_func,
        std::function<std::string()> get_current_emotion_func,
        std::function<void(const std::string&)> set_current_emotion_func
    );

    /**
     * @brief 析构函数
     */
    ~McpResponseController();

    /**
     * @brief 初始化并注册所有MCP工具
     * @return true如果成功，false如果失败
     */
    bool Initialize();

private:
    // 依赖的组件
    Motion* motion_skill_;
    Vibration* vibration_skill_;
    EventEngine* event_engine_;
    std::function<Display*()> get_display_func_;
    std::function<std::string()> get_current_emotion_func_;
    std::function<void(const std::string&)> set_current_emotion_func_;

    // MCP工具注册方法
    void RegisterMotionTools();
    void RegisterVibrationTools();
    void RegisterDisplayTools();
    void RegisterComplexExpressionTools();
    void RegisterStatusTools();

    // 工具实现方法 - 身体动作控制
    ReturnValue BasicMotionTool(const PropertyList& properties);
    ReturnValue ExploreMotionTool(const PropertyList& properties);
    ReturnValue ReactionMotionTool(const PropertyList& properties);
    ReturnValue PerformanceMotionTool(const PropertyList& properties);
    ReturnValue AngleControlTool(const PropertyList& properties);

    // 工具实现方法 - 振动反馈控制
    ReturnValue BasicVibrationTool(const PropertyList& properties);

    // 工具实现方法 - 屏幕动画控制
    ReturnValue ShowEmotionTool(const PropertyList& properties);
    ReturnValue AnimationControlTool(const PropertyList& properties);

    // 工具实现方法 - 8种情绪表达
    ReturnValue CalmExpressionTool(const PropertyList& properties);
    ReturnValue HappyExpressionTool(const PropertyList& properties);
    ReturnValue SadExpressionTool(const PropertyList& properties);
    ReturnValue AngryExpressionTool(const PropertyList& properties);
    ReturnValue ScaredExpressionTool(const PropertyList& properties);
    ReturnValue CuriousExpressionTool(const PropertyList& properties);
    ReturnValue ShyExpressionTool(const PropertyList& properties);
    ReturnValue ContentExpressionTool(const PropertyList& properties);

    // 工具实现方法 - 状态查询
    ReturnValue MotionStatusTool(const PropertyList& properties);
    ReturnValue EventsStatusTool(const PropertyList& properties);
    ReturnValue SystemStatusTool(const PropertyList& properties);

    // 辅助方法
    motion_id_t GetMotionIdForEmotion(const std::string& emotion);
    vibration_id_t GetVibrationIdForEmotion(const std::string& emotion);
    motion_speed_t ParseMotionSpeed(const std::string& speed_str);
    vibration_id_t ParseVibrationPattern(const std::string& pattern);
};

#endif // ALICHUANGTEST_MCP_RESPONSE_CONTROLLER_H