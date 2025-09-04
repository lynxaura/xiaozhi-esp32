#ifndef I2C_BUS_MANAGER_H
#define I2C_BUS_MANAGER_H

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <driver/i2c_master.h>
#include <esp_log.h>

/**
 * I2C总线管理器 - 提供线程安全的I2C访问
 * 防止多个设备同时访问I2C总线导致冲突
 */
class I2cBusManager {
private:
    static I2cBusManager* instance_;
    static SemaphoreHandle_t mutex_;
    
    i2c_master_bus_handle_t bus_handle_;
    
    I2cBusManager();
    
public:
    static I2cBusManager* GetInstance();
    
    void SetBusHandle(i2c_master_bus_handle_t bus_handle);
    
    /**
     * 获取I2C总线访问锁
     * @param timeout_ms 超时时间（毫秒）
     * @return true if lock acquired successfully
     */
    bool AcquireLock(uint32_t timeout_ms = 100);
    
    /**
     * 释放I2C总线访问锁
     */
    void ReleaseLock();
    
    /**
     * RAII锁定包装器
     */
    class Lock {
    private:
        I2cBusManager* manager_;
        bool locked_;
        
    public:
        Lock(I2cBusManager* manager, uint32_t timeout_ms = 100);
        ~Lock();
        bool IsLocked() const;
    };
};

#endif // I2C_BUS_MANAGER_H