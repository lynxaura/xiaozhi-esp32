#ifndef QMI8658_H
#define QMI8658_H

#include "i2c_device.h"

// QMI8658 Register Map
#define QMI8658_WHO_AM_I            0x00
#define QMI8658_REVISION_ID         0x01
#define QMI8658_CTRL1               0x02
#define QMI8658_CTRL2               0x03
#define QMI8658_CTRL3               0x04
#define QMI8658_CTRL4               0x05
#define QMI8658_CTRL5               0x06
#define QMI8658_CTRL6               0x07
#define QMI8658_CTRL7               0x08
#define QMI8658_CTRL8               0x09
#define QMI8658_CTRL9               0x0A
#define QMI8658_CAL1_L              0x0B
#define QMI8658_CAL1_H              0x0C
#define QMI8658_CAL2_L              0x0D
#define QMI8658_CAL2_H              0x0E
#define QMI8658_CAL3_L              0x0F
#define QMI8658_CAL3_H              0x10
#define QMI8658_CAL4_L              0x11
#define QMI8658_CAL4_H              0x12
#define QMI8658_FIFO_WTM_TH         0x13
#define QMI8658_FIFO_CTRL           0x14
#define QMI8658_FIFO_SMPL_CNT       0x15
#define QMI8658_FIFO_STATUS         0x16
#define QMI8658_FIFO_DATA           0x17
#define QMI8658_I2CM_STATUS         0x2C
#define QMI8658_STATUSINT           0x2D
#define QMI8658_STATUS0             0x2E
#define QMI8658_STATUS1             0x2F
#define QMI8658_TIMESTAMP_LOW       0x30
#define QMI8658_TIMESTAMP_MID       0x31
#define QMI8658_TIMESTAMP_HIGH      0x32
#define QMI8658_TEMP_L              0x33
#define QMI8658_TEMP_H              0x34
#define QMI8658_AX_L                0x35
#define QMI8658_AX_H                0x36
#define QMI8658_AY_L                0x37
#define QMI8658_AY_H                0x38
#define QMI8658_AZ_L                0x39
#define QMI8658_AZ_H                0x3A
#define QMI8658_GX_L                0x3B
#define QMI8658_GX_H                0x3C
#define QMI8658_GY_L                0x3D
#define QMI8658_GY_H                0x3E
#define QMI8658_GZ_L                0x3F
#define QMI8658_GZ_H                0x40

// Expected WHO_AM_I value
#define QMI8658_CHIP_ID             0x05

// Data structure for IMU data
struct ImuData {
    float accel_x;
    float accel_y;
    float accel_z;
    float gyro_x;
    float gyro_y;
    float gyro_z;
    float temperature;
};

class QMI8658 : public I2cDevice {
public:
    QMI8658(i2c_master_bus_handle_t i2c_bus, uint8_t addr = 0x6A);
    
    bool Initialize();
    bool ReadImuData(ImuData* data);
    bool IsDataReady();
    
    // Configuration methods
    void SetAccelRange(uint8_t range);  // 0=2g, 1=4g, 2=8g, 3=16g
    void SetGyroRange(uint8_t range);   // 0=16dps, 1=32dps, 2=64dps, ..., 7=2048dps
    void SetOutputDataRate(uint8_t odr); // Output data rate
    
private:
    bool CheckDeviceId();
    void ConfigureDevice();
    int16_t CombineBytes(uint8_t low, uint8_t high);
    
    float accel_scale_;
    float gyro_scale_;
};

#endif // QMI8658_H