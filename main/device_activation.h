#ifndef DEVICE_ACTIVATION_H
#define DEVICE_ACTIVATION_H

#include <string>

/**
 * DeviceActivation - 设备激活管理类
 *
 * 负责设备首次激活时向服务器注册，获取服务器分配的设备标识。
 * 激活成功后会在 NVS 中持久化存储激活状态，避免重复激活。
 *
 * 特性：
 * - 单例模式
 * - 自动重试机制（最多3次）
 * - 持久化激活状态
 * - 独立于其他组件运行
 */
class DeviceActivation {
public:
    /**
     * 获取 DeviceActivation 单例实例
     */
    static DeviceActivation& GetInstance() {
        static DeviceActivation instance;
        return instance;
    }

    // 禁用拷贝构造和赋值
    DeviceActivation(const DeviceActivation&) = delete;
    DeviceActivation& operator=(const DeviceActivation&) = delete;

    /**
     * 检查并执行设备激活
     *
     * 如果设备已激活，直接返回 true；
     * 如果未激活，则向服务器发起激活请求（带重试）。
     *
     * @return true - 设备已激活或激活成功
     *         false - 激活失败（已重试3次）
     */
    bool CheckAndActivate();

    /**
     * 检查设备是否已激活
     *
     * @return true - 已激活
     *         false - 未激活
     */
    bool IsActivated() const;

    /**
     * 重置激活状态
     *
     * 用于恢复出厂设置或测试场景，清除 NVS 中的激活标志。
     * 调用后设备下次启动时会重新执行激活流程。
     */
    void ResetActivationStatus();

private:
    DeviceActivation() = default;
    ~DeviceActivation() = default;

    /**
     * 执行设备激活（带重试）
     *
     * 向服务器发送激活请求，最多重试 MAX_RETRY_COUNT 次。
     * 每次失败后等待 RETRY_DELAY_MS 毫秒再重试。
     *
     * @return true - 激活成功
     *         false - 所有重试均失败
     */
    bool PerformActivation();

    /**
     * 单次激活请求
     *
     * 向服务器发送 HTTP POST 请求，携带设备的 MAC 地址作为 serial_number。
     *
     * @param serial_number 设备序列号（MAC 地址）
     * @return true - 请求成功且服务器返回 code == 0
     *         false - 请求失败或服务器返回错误
     */
    bool ActivateOnce(const std::string& serial_number);

    // 常量配置
    static const int MAX_RETRY_COUNT = 3;           // 最大重试次数
    static const int RETRY_DELAY_MS = 2000;         // 重试间隔（毫秒）
    static const char* ACTIVATE_URL;                // 激活接口 URL
    static const char* NVS_NAMESPACE;               // NVS 命名空间
    static const char* NVS_KEY_ACTIVATED;           // NVS 激活标志 key
};

#endif // DEVICE_ACTIVATION_H
