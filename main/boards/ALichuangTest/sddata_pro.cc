#include "sddata_pro.h"
#include <esp_log.h>

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define BSP_SD_CLK          ((gpio_num_t)(47))
#define BSP_SD_CMD          ((gpio_num_t)(48))
#define BSP_SD_D0           ((gpio_num_t)(21))

#define MOUNT_POINT              "/sdcard"
#define EXAMPLE_MAX_CHAR_SIZE    64
const char mount_point[] = MOUNT_POINT;

#define TAG "SDCardPro"

SDdata_Pro::SDdata_Pro() {
    esp_err_t ret;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,   // 如果挂载不成功是否需要格式化SD卡
        .max_files = 5, // 允许打开的最大文件数
        .allocation_unit_size = 4 * 1024  // 分配单元大小
    };

    ESP_LOGI(TAG, "Initializing SD card");
    ESP_LOGI(TAG, "Using SDMMC peripheral");

    sdmmc_host_t host = SDMMC_HOST_DEFAULT(); // SDMMC主机接口配置
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT(); // SDMMC插槽配置
    slot_config.width = 1;  // 设置为1线SD模式
    slot_config.clk = BSP_SD_CLK; 
    slot_config.cmd = BSP_SD_CMD;
    slot_config.d0 = BSP_SD_D0;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP; // 打开内部上拉电阻

    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &m_card); // 挂载SD卡

    if (ret != ESP_OK) {  // 如果没有挂载成功
        if (ret == ESP_FAIL) { // 如果挂载失败
            ESP_LOGE(TAG, "Failed to mount filesystem. ");
        } else { // 如果是其它错误 打印错误名称
            ESP_LOGE(TAG, "Failed to initialize the card (%s). ", esp_err_to_name(ret));
        }
        return;
    }
    ESP_LOGI(TAG, "Filesystem mounted"); // 提示挂载成功
    sdmmc_card_print_info(stdout, m_card); // 终端打印SD卡的一些信息
}

SDdata_Pro::~SDdata_Pro() {
    // 卸载SD卡
    esp_vfs_fat_sdcard_unmount(mount_point, m_card);
    ESP_LOGI(TAG, "Card unmounted");
}

esp_err_t SDdata_Pro::TWriteFile(const char *path, char *data) {
    ESP_LOGI(TAG, "Opening file %s", path);
    FILE *f = fopen(path, "w");   // 以只写方式打开路径中文件
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing"); 
        return ESP_FAIL;
    }
    fprintf(f, data); // 写入内容
    fclose(f);  // 关闭文件
    ESP_LOGI(TAG, "File written");

    return ESP_OK;
}

esp_err_t SDdata_Pro::TReadFile(const char *path) {
    ESP_LOGI(TAG, "Reading file %s", path);
    FILE *f = fopen(path, "r");  // 以只读方式打开文件
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return ESP_FAIL;
    }
    char line[EXAMPLE_MAX_CHAR_SIZE];  // 定义一个字符串数组
    fgets(line, sizeof(line), f); // 获取文件中的内容到字符串数组
    fclose(f); // 关闭文件

    // strip newline
    char *pos = strchr(line, '\n'); // 查找字符串中的“\n”并返回其位置
    if (pos) {
        *pos = '\0'; // 把\n替换成\0
    }
    ESP_LOGI(TAG, "Read from file: '%s'", line); // 把数组内容输出到终端

    return ESP_OK;
}

void SDdata_Pro::TestFile() {
    esp_err_t ret;

    // 新建一个txt文件 并且给文件中写入几个字符
    const char *file_hello = MOUNT_POINT"/hello.txt";
    char data[EXAMPLE_MAX_CHAR_SIZE];
    snprintf(data, EXAMPLE_MAX_CHAR_SIZE, "%s %s!\n", "hello sdcarduo", m_card->cid.name);
    ret = TWriteFile(file_hello, data);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "Card write file err");
    }

    // 打开txt文件，并读出文件中的内容
    ret = TReadFile(file_hello);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "Card read file err");
    }
}

void SDdata_Pro::ReadImageBin(const char *path, uint8_t *databuf) {
    
    FILE* f = fopen(path, "r");
    if (f) {
        rewind(f);
        fread(databuf, 1, 153600, f);
        fclose(f);
    }
}

void SDdata_Pro::SetAngryFlash() {
    const char *file_1 = ANGRY_PATH"1.bin";
    ReadImageBin(file_1, &m_image[0][0]);
    const char *file_2 = ANGRY_PATH"2.bin";
    ReadImageBin(file_2, &m_image[1][0]);
    const char *file_3 = ANGRY_PATH"3.bin";
    ReadImageBin(file_3, &m_image[2][0]);
    const char *file_4 = ANGRY_PATH"4.bin";
    ReadImageBin(file_4, &m_image[3][0]);
}

void SDdata_Pro::SetHappyFlash() {
    const char *file_1 = HAPPY_PATH"1.bin";
    ReadImageBin(file_1, &m_image[0][0]);
    const char *file_2 = HAPPY_PATH"2.bin";
    ReadImageBin(file_2, &m_image[1][0]);
    const char *file_3 = HAPPY_PATH"3.bin";
    ReadImageBin(file_3, &m_image[2][0]);
}

void SDdata_Pro::SetLaughFlash() {
    const char *file_1 = LAUGH_PATH"1.bin";
    ReadImageBin(file_1, &m_image[0][0]);
}

void SDdata_Pro::SetNeutralFlash() {
    const char *file_1 = NEUTRAL_PATH"1.bin";
    ReadImageBin(file_1, &m_image[0][0]);
    ESP_LOGI(TAG, "0x%02x-0x%02x-0x%02x-0x%02x-0x%02x-0x%02x-0x%02x-0x%02x", 
        m_image[0][0], m_image[0][1], m_image[0][2], m_image[0][3], m_image[0][4], m_image[0][5], m_image[0][6], m_image[0][7]);
}

void SDdata_Pro::SetSadFlash() {
    const char *file_1 = SAD_PATH"1.bin";
    ReadImageBin(file_1, &m_image[0][0]);
    const char *file_2 = SAD_PATH"2.bin";
    ReadImageBin(file_2, &m_image[1][0]);
    const char *file_3 = SAD_PATH"3.bin";
    ReadImageBin(file_3, &m_image[2][0]);
}

void SDdata_Pro::SetSurpriseFlash() {
    const char *file_1 = SURPRISE_PATH"1.bin";
    ReadImageBin(file_1, &m_image[0][0]);
    const char *file_2 = SURPRISE_PATH"2.bin";
    ReadImageBin(file_2, &m_image[1][0]);
    const char *file_3 = SURPRISE_PATH"3.bin";
    ReadImageBin(file_3, &m_image[2][0]);
    const char *file_4 = SURPRISE_PATH"4.bin";
    ReadImageBin(file_4, &m_image[3][0]);
    const char *file_5 = SURPRISE_PATH"5.bin";
    ReadImageBin(file_5, &m_image[4][0]);
    const char *file_6 = SURPRISE_PATH"6.bin";
    ReadImageBin(file_6, &m_image[5][0]);
}