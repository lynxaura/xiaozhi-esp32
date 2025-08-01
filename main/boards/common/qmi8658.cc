#include "qmi8658.h"
#include <esp_log.h>
#include <cmath>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "QMI8658"

QMI8658::QMI8658(i2c_master_bus_handle_t i2c_bus, uint8_t addr) 
    : I2cDevice(i2c_bus, addr), accel_scale_(1.0f), gyro_scale_(1.0f) {
}

bool QMI8658::Initialize() {
    ESP_LOGI(TAG, "Initializing QMI8658 IMU sensor");
    
    if (!CheckDeviceId()) {
        ESP_LOGE(TAG, "Failed to verify device ID");
        return false;
    }
    
    ConfigureDevice();
    ESP_LOGI(TAG, "QMI8658 initialized successfully");
    return true;
}

bool QMI8658::CheckDeviceId() {
    uint8_t chip_id = ReadReg(QMI8658_WHO_AM_I);
    ESP_LOGI(TAG, "Chip ID: 0x%02X (expected: 0x%02X)", chip_id, QMI8658_CHIP_ID);
    return chip_id == QMI8658_CHIP_ID;
}

void QMI8658::ConfigureDevice() {
    // Reset device
    WriteReg(QMI8658_CTRL1, 0x60);
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Enable accelerometer and gyroscope
    WriteReg(QMI8658_CTRL7, 0x03);  // Enable accel and gyro
    
    // Set default ranges and ODR
    SetAccelRange(0);  // ±2g
    SetGyroRange(3);   // ±128dps
    SetOutputDataRate(6); // 100Hz
    
    // Enable data ready interrupt (optional)
    WriteReg(QMI8658_CTRL1, 0x40);
    
    vTaskDelay(pdMS_TO_TICKS(50)); // Allow sensor to stabilize
}

void QMI8658::SetAccelRange(uint8_t range) {
    uint8_t ctrl2 = ReadReg(QMI8658_CTRL2);
    ctrl2 = (ctrl2 & 0xF0) | (range & 0x0F);
    WriteReg(QMI8658_CTRL2, ctrl2);
    
    // Set scale factor based on range
    switch (range) {
        case 0: accel_scale_ = 2.0f / 32768.0f; break;   // ±2g
        case 1: accel_scale_ = 4.0f / 32768.0f; break;   // ±4g
        case 2: accel_scale_ = 8.0f / 32768.0f; break;   // ±8g
        case 3: accel_scale_ = 16.0f / 32768.0f; break;  // ±16g
        default: accel_scale_ = 2.0f / 32768.0f; break;
    }
}

void QMI8658::SetGyroRange(uint8_t range) {
    uint8_t ctrl3 = ReadReg(QMI8658_CTRL3);
    ctrl3 = (ctrl3 & 0xF0) | (range & 0x0F);
    WriteReg(QMI8658_CTRL3, ctrl3);
    
    // Set scale factor based on range (in dps)
    float ranges[] = {16.0f, 32.0f, 64.0f, 128.0f, 256.0f, 512.0f, 1024.0f, 2048.0f};
    if (range < 8) {
        gyro_scale_ = ranges[range] / 32768.0f;
    } else {
        gyro_scale_ = 16.0f / 32768.0f; // Default
    }
}

void QMI8658::SetOutputDataRate(uint8_t odr) {
    uint8_t ctrl2 = ReadReg(QMI8658_CTRL2);
    ctrl2 = (ctrl2 & 0x0F) | ((odr & 0x0F) << 4);
    WriteReg(QMI8658_CTRL2, ctrl2);
    
    uint8_t ctrl3 = ReadReg(QMI8658_CTRL3);
    ctrl3 = (ctrl3 & 0x0F) | ((odr & 0x0F) << 4);
    WriteReg(QMI8658_CTRL3, ctrl3);
}

bool QMI8658::IsDataReady() {
    uint8_t status = ReadReg(QMI8658_STATUS0);
    return (status & 0x03) == 0x03; // Both accel and gyro data ready
}

bool QMI8658::ReadImuData(ImuData* data) {
    if (!data) {
        return false;
    }
    
    // Read all sensor data in one transaction (more efficient)
    uint8_t raw_data[14];
    ReadRegs(QMI8658_TEMP_L, raw_data, 14);
    
    // Convert temperature (bytes 0-1)
    int16_t temp_raw = CombineBytes(raw_data[0], raw_data[1]);
    data->temperature = temp_raw / 256.0f;
    
    // Convert accelerometer data (bytes 2-7)
    int16_t accel_x = CombineBytes(raw_data[2], raw_data[3]);
    int16_t accel_y = CombineBytes(raw_data[4], raw_data[5]);
    int16_t accel_z = CombineBytes(raw_data[6], raw_data[7]);
    
    data->accel_x = accel_x * accel_scale_;
    data->accel_y = accel_y * accel_scale_;
    data->accel_z = accel_z * accel_scale_;
    
    // Convert gyroscope data (bytes 8-13)
    int16_t gyro_x = CombineBytes(raw_data[8], raw_data[9]);
    int16_t gyro_y = CombineBytes(raw_data[10], raw_data[11]);
    int16_t gyro_z = CombineBytes(raw_data[12], raw_data[13]);
    
    data->gyro_x = gyro_x * gyro_scale_;
    data->gyro_y = gyro_y * gyro_scale_;
    data->gyro_z = gyro_z * gyro_scale_;
    
    return true;
}

int16_t QMI8658::CombineBytes(uint8_t low, uint8_t high) {
    return (int16_t)((high << 8) | low);
}