#include "i2c_bus_manager.h"

// 静态成员定义
I2cBusManager* I2cBusManager::instance_ = nullptr;
SemaphoreHandle_t I2cBusManager::mutex_ = nullptr;

// I2cBusManager 实现
I2cBusManager::I2cBusManager() : bus_handle_(nullptr) {
}

I2cBusManager* I2cBusManager::GetInstance() {
    if (!instance_) {
        instance_ = new I2cBusManager();
        mutex_ = xSemaphoreCreateMutex();
    }
    return instance_;
}

void I2cBusManager::SetBusHandle(i2c_master_bus_handle_t bus_handle) {
    bus_handle_ = bus_handle;
}

bool I2cBusManager::AcquireLock(uint32_t timeout_ms) {
    if (!mutex_) return false;
    return xSemaphoreTake(mutex_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void I2cBusManager::ReleaseLock() {
    if (mutex_) {
        xSemaphoreGive(mutex_);
    }
}

// I2cBusManager::Lock 实现
I2cBusManager::Lock::Lock(I2cBusManager* manager, uint32_t timeout_ms) 
    : manager_(manager), locked_(false) {
    locked_ = manager_->AcquireLock(timeout_ms);
    if (!locked_) {
        ESP_LOGW("I2CBusMgr", "Failed to acquire I2C bus lock within %lums", timeout_ms);
    }
}

I2cBusManager::Lock::~Lock() {
    if (locked_) {
        manager_->ReleaseLock();
    }
}

bool I2cBusManager::Lock::IsLocked() const {
    return locked_;
}