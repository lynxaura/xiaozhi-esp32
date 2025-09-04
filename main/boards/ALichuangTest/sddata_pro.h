#ifndef _SDDATA_PRO_
#define _SDDATA_PRO_

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/sdmmc_host.h"

#define IMAGE_FULL_SIZE (153600)
#define IMAGE_MAX_FRAME_C 6
#define ANGRY_PATH    "/sdcard/image/emotions/angry/"
#define HAPPY_PATH    "/sdcard/image/emotions/happy/"
#define LAUGH_PATH    "/sdcard/image/emotions/laughting/"
#define NEUTRAL_PATH    "/sdcard/image/emotions/neutral/"
#define SAD_PATH    "/sdcard/image/emotions/sad/"
#define SURPRISE_PATH    "/sdcard/image/emotions/surprised/"

class SDdata_Pro {
public:
    SDdata_Pro();
    ~SDdata_Pro();

    sdmmc_card_t *m_card;
    uint8_t m_image[IMAGE_MAX_FRAME_C][IMAGE_FULL_SIZE];

    void TestFile(); // 测试读写
    void SetAngryFlash(); // 设置画面缓存为ANGRY动画
    void SetHappyFlash(); // 设置画面缓存为HAPPY动画
    void SetLaughFlash(); // 设置画面缓存为laughting动画
    void SetNeutralFlash(); // 设置画面缓存为Neutral动画
    void SetSadFlash(); // 设置画面缓存为SAD动画
    void SetSurpriseFlash(); // 设置画面缓存为surprised动画
private:
    esp_err_t TWriteFile(const char *path, char *data);
    esp_err_t TReadFile(const char *path);
    void ReadImageBin(const char *path, uint8_t *databuf);
};

#endif /* _SDDATA_PRO_ */