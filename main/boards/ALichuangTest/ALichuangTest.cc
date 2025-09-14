#include "wifi_board.h"
#include "codecs/box_audio_codec.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "i2c_device.h"
#include "esp32_camera.h"
#include "skills/vibration.h"
#include "skills/motion.h"
#include "qmi8658.h"
#include "interaction/core/event_engine.h"
#include "interaction/upload/event_uploader.h"
#include "interaction/controller/mcp_response_controller.h"
#include "interaction/controller/local_response_controller.h"
#include "pca9685.h"
#include "i2c_bus_manager.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <wifi_station.h>
#include <esp_lcd_touch_ft5x06.h>
#include <esp_lvgl_port.h>
#include <lvgl.h>
#include <esp_timer.h>
#include <mutex>

/* SD Card */
#include "sddata_pro.h"
/* SD Card End */

#if CONFIG_LINGXI_ANIMA_UI
#include "skills/animation.h"
#include "skills/animation_player.h"
//#include "images/emotions/neutral/1.h"
//#include "images/emotions/angry/1.h"
//#include "images/emotions/angry/2.h"
//#include "images/emotions/angry/3.h"
//#include "images/emotions/angry/4.h"
//#include "images/emotions/happy/1.h"
//#include "images/emotions/happy/2.h"
//#include "images/emotions/happy/3.h"
//#include "images/emotions/laughting/1.h"
//#include "images/emotions/sad/1.h"
//#include "images/emotions/sad/2.h"
//#include "images/emotions/sad/3.h"
//#include "images/emotions/surprised/2.h"
//#include "images/emotions/surprised/4.h"
//#include "images/emotions/surprised/6.h"
#elif CONFIG_XIAOZHI_DEFAULT_UI
#include "display/lcd_display.h"
#endif


#define TAG "ALichuangTest"

LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);

class Pca9557 : public I2cDevice {
public:
    Pca9557(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        WriteReg(0x01, 0x03);
        WriteReg(0x03, 0xf8);
    }

    void SetOutputState(uint8_t bit, uint8_t level) {
        uint8_t data = ReadReg(0x01);
        data = (data & ~(1 << bit)) | (level << bit);
        WriteReg(0x01, data);
    }
};

class CustomAudioCodec : public BoxAudioCodec {
private:
    Pca9557* pca9557_;

public:
    CustomAudioCodec(i2c_master_bus_handle_t i2c_bus, Pca9557* pca9557) 
        : BoxAudioCodec(i2c_bus, 
                       AUDIO_INPUT_SAMPLE_RATE, 
                       AUDIO_OUTPUT_SAMPLE_RATE,
                       AUDIO_I2S_GPIO_MCLK, 
                       AUDIO_I2S_GPIO_BCLK, 
                       AUDIO_I2S_GPIO_WS, 
                       AUDIO_I2S_GPIO_DOUT, 
                       AUDIO_I2S_GPIO_DIN,
                       GPIO_NUM_NC, 
                       AUDIO_CODEC_ES8311_ADDR, 
                       AUDIO_CODEC_ES7210_ADDR, 
                       AUDIO_INPUT_REFERENCE),
          pca9557_(pca9557) {
    }

    virtual void EnableOutput(bool enable) override {
        BoxAudioCodec::EnableOutput(enable);
        if (enable) {
            pca9557_->SetOutputState(1, 1);
        } else {
            pca9557_->SetOutputState(1, 0);
        }
    }
};

class ALichuangTest : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_dev_handle_t pca9557_handle_;
    Button boot_button_;
    Pca9557* pca9557_;
    Esp32Camera* camera_;
    Qmi8658* imu_ = nullptr;
    EventEngine* event_engine_ = nullptr;
    EventUploader* event_uploader_ = nullptr;
    esp_timer_handle_t event_timer_ = nullptr;
    Pca9685* pca9685_ = nullptr;           // PCA9685 PWM控制器
    Vibration* vibration_skill_ = nullptr; // 振动技能管理器
    Motion* motion_skill_ = nullptr;       // 直流马达动作控制技能
    McpResponseController* mcp_response_controller_ = nullptr; // MCP响应控制器
    LocalResponseController* local_response_controller_ = nullptr; // 本地响应控制器
    TaskHandle_t delay_task_handle = nullptr;
    SDdata_Pro* sdhccard = nullptr;   
#if CONFIG_LINGXI_ANIMA_UI
    // 情感相关成员变量
    std::string current_emotion_ = "neutral";
    mutable std::mutex emotion_mutex_;
    Display* display_;
    TaskHandle_t image_task_handle_ = nullptr; // 图片显示任务句柄

    void StartImageSlideshow() {
        ESP_LOGI(TAG, "=== StartImageSlideshow called ===");
        ESP_LOGI(TAG, "CONFIG_LINGXI_ANIMA_UI is enabled");

        // 检查AnimationPlayer是否已初始化
        auto& player = AnimationPlayer::GetInstance();
        if (!player.HasCanvas()) {
            ESP_LOGW(TAG, "AnimationPlayer canvas not found, creating...");
            player.CreateCanvas();
        }

        ESP_LOGI(TAG, "Starting idle animation with AnimationPlayer");
        ESP_LOGI(TAG, "Expected animation path: /sdcard/state_expression/idle/idle_q1/animation/");

        // 直接播放idle状态动画
        // AnimationPlayer会自动：
        // 1. 获取当前情感象限（开机默认V=0.2 A=0.2 = Q1）
        // 2. 构建路径：/sdcard/state_expression/idle/idle_q1/animation/
        // 3. 加载并播放动画
        player.PlayStateExpression(kDeviceStateIdle);

        ESP_LOGI(TAG, "=== Idle animation playback requested ===");

        // 注释掉原来的ImageSlideshowTask创建
        // xTaskCreate(ImageSlideshowTask, "img_slideshow", 4096, this, 3, &image_task_handle_);
        // ESP_LOGI(TAG, "图片循环显示任务已启动");
    }
    
    // 根据情感获取对应的图片数组
    std::pair<const uint8_t**, int> GetEmotionImageArray(const std::string& emotion) {
        // 默认图片数组（neutral或未知情感时使用）
        sdhccard->SetNeutralFlash();
        static const uint8_t* neutral_images[] = {
            // gImage_1  // neutral时只显示第一张静态图片
            sdhccard->m_image[0]
        };
        
        // 根据情感返回对应的图片数组
        if (emotion == "happy" || emotion == "funny") {
            sdhccard->SetHappyFlash();
            // 开心相关情感 - 使用快节奏动画
            static const uint8_t* happy_images[] = {
                // gImage_9, gImage_10, gImage_11
                sdhccard->m_image[0], sdhccard->m_image[1], sdhccard->m_image[2]

            };
            return {happy_images, 3};
        }
        else if (emotion == "laughting") {
            sdhccard->SetLaughFlash();
            // 大笑情感
            static const uint8_t* angry_images[] = {
                // gImage_12
                sdhccard->m_image[0]
            };
            return {angry_images, 1};
        }
        else if (emotion == "angry") {
            sdhccard->SetAngryFlash();
            // 愤怒情感 - 使用较强烈的图片
            static const uint8_t* angry_images[] = {
                // gImage_2, gImage_3, gImage_4, gImage_5
                sdhccard->m_image[0], sdhccard->m_image[1], sdhccard->m_image[2], sdhccard->m_image[3]
            };
            return {angry_images, 4};
        }
        else if (emotion == "sad" || emotion == "crying") {
            sdhccard->SetSadFlash();
            // 悲伤相关情感 - 使用较慢的动画
            static const uint8_t* sad_images[] = {
                //gImage_23, gImage_24, gImage_25
                sdhccard->m_image[0], sdhccard->m_image[1], sdhccard->m_image[2]
            };
            return {sad_images, 3};
        }
        else if (emotion == "surprised" || emotion == "shocked") {
            sdhccard->SetSurpriseFlash();
            // 惊讶相关情感 - 使用跳跃式动画
            static const uint8_t* surprised_images[] = {
                // gImage_27, gImage_29, gImage_31
                sdhccard->m_image[1], sdhccard->m_image[3], sdhccard->m_image[5]
            };
            return {surprised_images, 3};
        }
        else {
            // neutral或其他情感 - 只显示静态图片
            return {neutral_images, 1};
        }
    }
    
    // 获取当前情感状态
    std::string GetCurrentEmotion() {
        std::lock_guard<std::mutex> lock(emotion_mutex_);
        return current_emotion_;
    }
    
    // 设置当前情感状态
    void SetCurrentEmotion(const std::string& emotion) {
        std::lock_guard<std::mutex> lock(emotion_mutex_);
        current_emotion_ = emotion;
        ESP_LOGI(TAG, "情感状态变更为: %s", emotion.c_str());
    }
    
    // 根据情感获取播放间隔（毫秒）
    int GetEmotionPlayInterval(const std::string& emotion) {
        if (emotion == "happy" || emotion == "laughing" || emotion == "funny") {
            return 50;  // 开心情感 - 快速播放
        }
        else if (emotion == "angry") {
            return 40;  // 愤怒情感 - 很快播放，表达强烈情绪
        }
        else if (emotion == "sad" || emotion == "crying") {
            return 120; // 悲伤情感 - 慢速播放
        }
        else if (emotion == "surprised" || emotion == "shocked") {
            return 80;  // 惊讶情感 - 中等速度
        }
        else if (emotion == "thinking") {
            return 150; // 思考情感 - 最慢播放
        }
        else {
            return 60;  // 默认间隔
        }
    }
    
    // 图片循环显示任务函数 (DEPRECATED - now using AnimationPlayer)
    static void ImageSlideshowTask(void* arg) {
#if CONFIG_LINGXI_ANIMA_UI
        // 使用AnimationPlayer时，此函数不再需要
        ESP_LOGI(TAG, "ImageSlideshowTask disabled - using AnimationPlayer instead");
        vTaskDelete(NULL);
        return;
#else
        ALichuangTest* board = static_cast<ALichuangTest*>(arg);
        // AnimaDisplay* display = board->GetDisplay(); // Commented out - using AnimationPlayer now
        Display* display = board->GetDisplay();

        if (!display) {
            ESP_LOGE(TAG, "无法获取显示设备");
            vTaskDelete(NULL);
            return;
        }
        
        // 获取AudioProcessor实例的事件组 - 从application.h中直接获取
        auto& app = Application::GetInstance();
        // 这里使用Application中可用的方法来判断音频状态
        // 根据编译错误修改为可用的方法
        
        // 创建画布（如果不存在）
        if (!display->HasCanvas()) {
            display->CreateCanvas();
        }
        
        // 设置图片显示参数
        int imgWidth = 320;
        int imgHeight = 240;
        int x = 0;
        int y = 0;
        
        // 根据当前情感动态获取图片数组
        std::string current_emotion = board->GetCurrentEmotion();
        auto [imageArray, totalImages] = board->GetEmotionImageArray(current_emotion);
        
        ESP_LOGI(TAG, "当前情感: %s, 图片数量: %d", current_emotion.c_str(), totalImages);
        
        // 创建临时缓冲区用于字节序转换
        uint16_t* convertedData = new uint16_t[imgWidth * imgHeight];
        if (!convertedData) {
            ESP_LOGE(TAG, "无法分配内存进行图像转换");
            vTaskDelete(NULL);
            return;
        }
        
        // 先显示第一张图片
        int currentIndex = 0;
        const uint8_t* currentImage = imageArray[currentIndex];
        
        // 转换并显示第一张图片
        for (int i = 0; i < imgWidth * imgHeight; i++) {
            uint16_t pixel = ((uint16_t*)currentImage)[i];
            convertedData[i] = ((pixel & 0xFF) << 8) | ((pixel & 0xFF00) >> 8);
        }
        display->DrawImageOnCanvas(x, y, imgWidth, imgHeight, (const uint8_t*)convertedData);
        ESP_LOGI(TAG, "初始显示图片");
        
        // 持续监控和处理图片显示
        TickType_t lastUpdateTime = xTaskGetTickCount();
        TickType_t cycleInterval = pdMS_TO_TICKS(60); // 图片切换间隔，会根据情感动态调整
        
        // 定义用于判断是否正在播放音频的变量
        bool isAudioPlaying = false;
        
        // 定义用于检测情感变化的变量
        std::string lastEmotion = current_emotion;
        
        // 定义用于判断是否应该播放情感动画的变量
        bool shouldPlayAnimation = false;
        bool wasPlayingAnimation = false;
        
        // 定义自动回归neutral的超时机制（10秒无音频播放后自动回到neutral）
        TickType_t lastAudioTime = xTaskGetTickCount();
        const TickType_t neutralTimeout = pdMS_TO_TICKS(10000); // 10秒超时
        
        while (true) {
            // 检查情感是否发生变化
            std::string currentEmotion = board->GetCurrentEmotion();
            if (currentEmotion != lastEmotion) {
                ESP_LOGI(TAG, "情感变化检测: %s -> %s", lastEmotion.c_str(), currentEmotion.c_str());
                // 重新获取图片数组
                auto [newImageArray, newTotalImages] = board->GetEmotionImageArray(currentEmotion);
                imageArray = newImageArray;
                totalImages = newTotalImages;
                lastEmotion = currentEmotion;
                currentIndex = 0; // 重置到第一张图片
                
                // 根据新情感调整播放间隔
                int intervalMs = board->GetEmotionPlayInterval(currentEmotion);
                cycleInterval = pdMS_TO_TICKS(intervalMs);
                ESP_LOGI(TAG, "调整播放间隔为: %d毫秒", intervalMs);
                
                // 立即显示新情感的第一张图片
                currentImage = imageArray[currentIndex];
                for (int i = 0; i < imgWidth * imgHeight; i++) {
                    uint16_t pixel = ((uint16_t*)currentImage)[i];
                    convertedData[i] = ((pixel & 0xFF) << 8) | ((pixel & 0xFF00) >> 8);
                }
                display->DrawImageOnCanvas(x, y, imgWidth, imgHeight, (const uint8_t*)convertedData);
                ESP_LOGI(TAG, "切换到新情感图片组: %s，图片数: %d", currentEmotion.c_str(), totalImages);
            }
            
            // 检查是否正在播放音频 - 使用应用程序状态判断
            isAudioPlaying = (app.GetDeviceState() == kDeviceStateSpeaking);
            
            // 更新最后一次音频播放时间
            if (isAudioPlaying) {
                lastAudioTime = xTaskGetTickCount();
            }
            
            // 检查是否需要自动回归neutral状态
            TickType_t timeSinceLastAudio = xTaskGetTickCount() - lastAudioTime;
            if (!isAudioPlaying && currentEmotion != "neutral" && timeSinceLastAudio > neutralTimeout) {
                ESP_LOGI(TAG, "长时间无音频播放，自动回归neutral状态");
                board->SetCurrentEmotion("neutral");
                // 注意：这里不直接修改currentEmotion，让下次循环检测情感变化时处理
            }
            
            // 判断是否应该播放情感动画：情绪不为neutral且正在说话
            bool isEmotionalState = (currentEmotion != "neutral") && (currentEmotion != "sleepy") && (currentEmotion != "");
            shouldPlayAnimation = isEmotionalState && isAudioPlaying;
            
            // 输出调试信息（每10次循环输出一次，避免日志过多）
            static int debugCount = 0;
            if (++debugCount >= 10) {
                ESP_LOGD(TAG, "状态检查 - 情绪: %s, 说话: %s, 播放动画: %s", 
                    currentEmotion.c_str(), 
                    isAudioPlaying ? "是" : "否",
                    shouldPlayAnimation ? "是" : "否");
                debugCount = 0;
            }
            
            TickType_t currentTime = xTaskGetTickCount();
            
            // 如果应该播放情感动画且时间到了切换间隔
            if (shouldPlayAnimation && (currentTime - lastUpdateTime >= cycleInterval)) {
                // 更新索引到下一张图片
                currentIndex = (currentIndex + 1) % totalImages;
                currentImage = imageArray[currentIndex];
                
                // 转换并显示新图片
                for (int i = 0; i < imgWidth * imgHeight; i++) {
                    uint16_t pixel = ((uint16_t*)currentImage)[i];
                    convertedData[i] = ((pixel & 0xFF) << 8) | ((pixel & 0xFF00) >> 8);
                }
                display->DrawImageOnCanvas(x, y, imgWidth, imgHeight, (const uint8_t*)convertedData);
                
                // 更新上次更新时间
                lastUpdateTime = currentTime;
            }
            // 如果不应该播放情感动画但之前在播放，或者当前不在第一张图片
            else if ((!shouldPlayAnimation && wasPlayingAnimation) || (!shouldPlayAnimation && currentIndex != 0)) {
                // 切换回第一张图片
                currentIndex = 0;
                currentImage = imageArray[currentIndex];
                
                // 转换并显示第一张图片
                for (int i = 0; i < imgWidth * imgHeight; i++) {
                    uint16_t pixel = ((uint16_t*)currentImage)[i];
                    convertedData[i] = ((pixel & 0xFF) << 8) | ((pixel & 0xFF00) >> 8);
                }
                display->DrawImageOnCanvas(x, y, imgWidth, imgHeight, (const uint8_t*)convertedData);
                ESP_LOGI(TAG, "停止情感动画，显示初始图片 - 情绪: %s, 说话: %s", 
                    currentEmotion.c_str(), isAudioPlaying ? "是" : "否");
            }
            
            // 更新上一次动画播放状态
            wasPlayingAnimation = shouldPlayAnimation;
            
            // 短暂延时，避免CPU占用过高
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        
        // 释放资源（实际上不会执行到这里，除非任务被外部终止）
        delete[] convertedData;
        vTaskDelete(NULL);
#endif
    }
#elif CONFIG_XIAOZHI_DEFAULT_UI
    LcdDisplay* display_;
#endif

    void InitializeI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));

        // 设置I2C总线管理器
        I2cBusManager::GetInstance()->SetBusHandle(i2c_bus_);

        // Initialize PCA9557
        pca9557_ = new Pca9557(i2c_bus_, 0x19);
    }

    bool IsDevicePresent(uint8_t addr) {
        // 创建设备句柄
        i2c_master_dev_handle_t dev_handle;
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = 100000,
        };
        
        esp_err_t ret = i2c_master_bus_add_device(i2c_bus_, &dev_cfg, &dev_handle);
        if (ret != ESP_OK) {
            return false;
        }
        
        // 发送一个字节的数据来检测设备
        uint8_t test_data = 0x00;
        ret = i2c_master_transmit(dev_handle, &test_data, 1, 100);
        
        // 删除设备句柄
        i2c_master_bus_rm_device(dev_handle);
        
        return (ret == ESP_OK);
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = GPIO_NUM_40;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = GPIO_NUM_41;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });

#if CONFIG_USE_DEVICE_AEC
        boot_button_.OnDoubleClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateIdle) {
                app.SetAecMode(app.GetAecMode() == kAecOff ? kAecOnDeviceSide : kAecOff);
            }
        });
#endif
    }

    void InitializeSt7789Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = GPIO_NUM_NC;
        io_config.dc_gpio_num = GPIO_NUM_39;
        io_config.spi_mode = 2;
        io_config.pclk_hz = 80 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片ST7789
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
        
        esp_lcd_panel_reset(panel);
        pca9557_->SetOutputState(0, 0);

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, true);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
#if CONFIG_LINGXI_ANIMA_UI
        // 先创建常规SpiLcdDisplay来初始化LVGL系统
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                    {
                                        .text_font = &font_puhui_20_4,
                                        .icon_font = &font_awesome_20_4,
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
                                        .emoji_font = font_emoji_32_init(),
#else
                                        .emoji_font = font_emoji_64_init(),
#endif
                                    });

        // 然后初始化AnimationPlayer（使用已经初始化的LVGL系统）
        bool init_success = AnimationPlayer::GetInstance().Initialize(
            panel_io, panel,
            DISPLAY_WIDTH, DISPLAY_HEIGHT,
            DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
            DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);

        if (!init_success) {
            ESP_LOGE(TAG, "Failed to initialize AnimationPlayer");
        } else {
            ESP_LOGI(TAG, "AnimationPlayer initialized successfully");
        }
#elif CONFIG_XIAOZHI_DEFAULT_UI
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                    {
                                        .text_font = &font_puhui_20_4,
                                        .icon_font = &font_awesome_20_4,
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
                                        .emoji_font = font_emoji_32_init(),
#else
                                        .emoji_font = font_emoji_64_init(),
#endif
                                    });
#endif
    }

    void InitializeTouch()
    {
        esp_lcd_touch_handle_t tp;
        esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_WIDTH,
            .y_max = DISPLAY_HEIGHT,
            .rst_gpio_num = GPIO_NUM_NC, // Shared with LCD reset
            .int_gpio_num = GPIO_NUM_NC, 
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = 1,
                .mirror_x = 1,
                .mirror_y = 0,
            },
        };
        esp_lcd_panel_io_handle_t tp_io_handle = NULL;
        esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
        tp_io_config.scl_speed_hz = 400000;

        esp_lcd_new_panel_io_i2c(i2c_bus_, &tp_io_config, &tp_io_handle);
        esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp);
        assert(tp);

        /* Add touch input (for selected screen) */
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lv_display_get_default(), 
            .handle = tp,
        };

        lvgl_port_add_touch(&touch_cfg);
    }

    void InitializeCamera() {
        // Open camera power
        pca9557_->SetOutputState(2, 0);

        camera_config_t config = {};
        config.ledc_channel = LEDC_CHANNEL_2;  // LEDC通道选择  用于生成XCLK时钟 但是S3不用
        config.ledc_timer = LEDC_TIMER_2; // LEDC timer选择  用于生成XCLK时钟 但是S3不用
        config.pin_d0 = CAMERA_PIN_D0;
        config.pin_d1 = CAMERA_PIN_D1;
        config.pin_d2 = CAMERA_PIN_D2;
        config.pin_d3 = CAMERA_PIN_D3;
        config.pin_d4 = CAMERA_PIN_D4;
        config.pin_d5 = CAMERA_PIN_D5;
        config.pin_d6 = CAMERA_PIN_D6;
        config.pin_d7 = CAMERA_PIN_D7;
        config.pin_xclk = CAMERA_PIN_XCLK;
        config.pin_pclk = CAMERA_PIN_PCLK;
        config.pin_vsync = CAMERA_PIN_VSYNC;
        config.pin_href = CAMERA_PIN_HREF;
        config.pin_sccb_sda = -1;   // 这里写-1 表示使用已经初始化的I2C接口
        config.pin_sccb_scl = CAMERA_PIN_SIOC;
        config.sccb_i2c_port = 1;
        config.pin_pwdn = CAMERA_PIN_PWDN;
        config.pin_reset = CAMERA_PIN_RESET;
        config.xclk_freq_hz = XCLK_FREQ_HZ;
        config.pixel_format = PIXFORMAT_RGB565;
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

        camera_ = new Esp32Camera(config);
    }
    
    void InitializeImu() {
        // 仅初始化IMU硬件
        imu_ = new Qmi8658(i2c_bus_);
        
        if (imu_->Initialize() == ESP_OK) {
            ESP_LOGI(TAG, "IMU initialized successfully");
        } else {
            ESP_LOGW(TAG, "Failed to initialize IMU");
            delete imu_;
            imu_ = nullptr;
        }
    }

    void InitializePca9685() {
        IsDevicePresent(PCA9685_DEFAULT_ADDR);
        ESP_LOGI(TAG, "Initializing PCA9685 at address 0x40...");
        pca9685_ = new Pca9685(i2c_bus_, PCA9685_DEFAULT_ADDR);
        
        ESP_LOGI(TAG, "🔧 设置PCA9685 PWM频率为200Hz (适配DRV8837直流马达驱动)");
        esp_err_t ret = pca9685_->Initialize(200);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize PCA9685: %s", esp_err_to_name(ret));
            delete pca9685_;
            pca9685_ = nullptr;
        }
    }
    
    void InitializeVibration() {
        if (pca9685_ == nullptr) {
            ESP_LOGW(TAG, "PCA9685 not available, skipping vibration initialization");
            return;
        }
        
        vibration_skill_ = new Vibration(pca9685_, 0);
        
        esp_err_t ret = vibration_skill_->Initialize();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Vibration skill initialized successfully");
        } else {
            ESP_LOGE(TAG, "Failed to initialize vibration skill: %s", esp_err_to_name(ret));
            delete vibration_skill_;
            vibration_skill_ = nullptr;
        }
    }
    
    void InitializeMotion() {
        if (pca9685_ == nullptr) {
            ESP_LOGW(TAG, "PCA9685 not available, skipping motion initialization");
            return;
        }
        
        // 使用PCA9685的通道1和通道2连接DRV883X的IN1和IN2
        motion_skill_ = new Motion(pca9685_, 1, 2);
        
        esp_err_t ret = motion_skill_->Initialize();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Motion skill initialized successfully");
        } else {
            ESP_LOGE(TAG, "Failed to initialize motion skill: %s", esp_err_to_name(ret));
            delete motion_skill_;
            motion_skill_ = nullptr;
        }
    }
    
    void StartVibrationTask() {
        if (vibration_skill_) {
            esp_err_t ret = vibration_skill_->StartTask();
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Vibration task started successfully");
            } else {
                ESP_LOGE(TAG, "Failed to start vibration task: %s", esp_err_to_name(ret));
            }
            // 如有振动效果测试需要再启用
            // vibration_skill_->EnableButtonTest(VIBRATION_SHORT_BUZZ, true); 
            ESP_LOGI(TAG, "Button cycle test enabled - press GPIO11 to test all patterns");
        }
    }
    
    void StartMotionTask() {
        if (motion_skill_) {
            esp_err_t ret = motion_skill_->StartTask();
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Motion task started successfully");
            } else {
                ESP_LOGE(TAG, "Failed to start motion task: %s", esp_err_to_name(ret));
            }
        }
    }
    
    void InitializeInteractionSystem() {
        // 创建事件引擎
        event_engine_ = new EventEngine();
        event_engine_->Initialize();
        
        // 初始化情感引擎
        event_engine_->InitializeEmotionEngine();
        
        // 初始化运动引擎（如果IMU可用）
        if (imu_) {
            event_engine_->InitializeMotionEngine(imu_, true);  // 启用调试输出
            // 重新加载配置，因为motion engine在Initialize()时还不存在
            event_engine_->ReloadMotionConfig();
        }
        
        // 初始化多点触摸引擎
        event_engine_->InitializeMultitouchEngine(i2c_bus_);
        
        // 创建事件上传器
        event_uploader_ = new EventUploader();
        event_uploader_->Enable(true);
        ESP_LOGI(TAG, "EventUploader created and enabled");
        
        // 设置情感状态上报回调
        event_engine_->SetEmotionReportCallback([this](const Event& event, float valence, float arousal) {
            ESP_LOGI(TAG, "🎭 Emotion state changed: V=%.2f, A=%.2f for event type=%d", 
                     valence, arousal, (int)event.type);
            
            // 将情感状态设置到事件上传器中，用于云端上报
            if (event_uploader_) {
                event_uploader_->SetCurrentEmotionState(valence, arousal);
            }
        });
        
        // 事件处理策略已通过配置文件自动加载
        // 如需覆盖特定策略，可在此处调用：
        // event_engine_->ConfigureEventProcessing(EventType::TOUCH_TAP, custom_config);
        
        // 设置单事件回调（用于本地响应和状态更新）
        event_engine_->RegisterCallback([this](const Event& event) {
            // 1. 先执行本地响应（最高优先级，即时反应）
            if (local_response_controller_) {
                local_response_controller_->ProcessEvent(event);
            }
            
            // 2. 处理事件日志和情感状态更新
            HandleEvent(event);
        });
        
        // 设置批量事件回调（用于云端上传）
        event_engine_->RegisterBatchCallback([this](const std::vector<Event>& events) {
            ESP_LOGI(TAG, "Batch upload: processing %u events", events.size());
            
            // 批量上传事件到云端（真正的批量，一个JSON payload）
            if (event_uploader_) {
                event_uploader_->HandleBatchEvents(events);
            }
        });
        
        // 创建定时器，每50ms处理一次事件
        esp_timer_create_args_t event_timer_args = {};
        event_timer_args.callback = [](void* arg) {
            auto* engine = static_cast<EventEngine*>(arg);
            engine->Process();
        };
        event_timer_args.arg = event_engine_;
        event_timer_args.dispatch_method = ESP_TIMER_TASK;
        event_timer_args.name = "event_timer";
        event_timer_args.skip_unhandled_events = true;
        
        esp_timer_create(&event_timer_args, &event_timer_);
        esp_timer_start_periodic(event_timer_, 50000);  // 50ms
        
        ESP_LOGI(TAG, "Interaction system initialized and started");
    }
    
    void InitialSDCard() {
        sdhccard = SDmoduleInit();
        SDLoadImageTest();
        //sdhccard->TestFile();
    }

    void InitializeMcpTools() {
        ESP_LOGI(TAG, "Initializing MCP local response tools...");
        
        try {
            // 创建McpResponseController实例
            mcp_response_controller_ = new McpResponseController(
                motion_skill_,
                vibration_skill_,
                event_engine_,
                [this]() -> Display* { return GetDisplay(); },
                [this]() -> std::string { return GetCurrentEmotion(); },
                [this](const std::string& emotion) { SetCurrentEmotion(emotion); }
            );
            
            // 初始化MCP工具
            if (mcp_response_controller_->Initialize()) {
                ESP_LOGI(TAG, "✅ MCP local response system initialized successfully");
            } else {
                ESP_LOGE(TAG, "❌ Failed to initialize MCP local response system");
                delete mcp_response_controller_;
                mcp_response_controller_ = nullptr;
            }
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "Exception during MCP tools initialization: %s", e.what());
            if (mcp_response_controller_) {
                delete mcp_response_controller_;
                mcp_response_controller_ = nullptr;
            }
        }
    }
    
    void InitializeLocalResponseSystem() {
        ESP_LOGI(TAG, "Initializing Local Response System...");
        
        try {
            // 创建本地响应控制器
            local_response_controller_ = new LocalResponseController(
                motion_skill_,
                vibration_skill_, 
                GetDisplay()
            );
            
            // 初始化本地响应系统
            if (local_response_controller_->Initialize()) {
                ESP_LOGI(TAG, "✅ Local Response System initialized successfully");
                local_response_controller_->ListTemplates();
            } else {
                ESP_LOGE(TAG, "❌ Failed to initialize Local Response System");
                delete local_response_controller_;
                local_response_controller_ = nullptr;
            }
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "Exception during Local Response System initialization: %s", e.what());
            if (local_response_controller_) {
                delete local_response_controller_;
                local_response_controller_ = nullptr;
            }
        }
    }
    
    void HandleEvent(const Event& event) {
        // 在终端输出事件信息
        const char* event_name = "";
        const ImuData& data = event.data.imu_data;
        
        switch (event.type) {
            case EventType::MOTION_FREE_FALL:
                event_name = "FREE_FALL";
                ESP_LOGW(TAG, "⚠️ FREE FALL DETECTED! Accel magnitude: %.3f g", 
                        std::sqrt(data.accel_x * data.accel_x + 
                                data.accel_y * data.accel_y + 
                                data.accel_z * data.accel_z));
                break;
            case EventType::MOTION_SHAKE_VIOLENTLY:
                event_name = "SHAKE_VIOLENTLY";
                ESP_LOGW(TAG, "⚡ VIOLENT SHAKE! Accel: X=%.2f Y=%.2f Z=%.2f g", 
                        data.accel_x, data.accel_y, data.accel_z);
                break;
            case EventType::MOTION_FLIP: 
                event_name = "FLIP";
                ESP_LOGI(TAG, "🔄 Device flipped! (gyro: x=%.1f y=%.1f z=%.1f deg/s)", 
                        data.gyro_x, data.gyro_y, data.gyro_z);
                break;
            case EventType::MOTION_SHAKE: 
                event_name = "SHAKE";
                ESP_LOGI(TAG, "🔔 Device shaken!");
                break;
            case EventType::MOTION_PICKUP: 
                event_name = "PICKUP";
                ESP_LOGI(TAG, "📱 Device picked up!");
                break;
            case EventType::MOTION_UPSIDE_DOWN:
                event_name = "UPSIDE_DOWN";
                ESP_LOGI(TAG, "🙃 Device is upside down! (Z-axis: %.2f g)", data.accel_z);
                break;
            // 处理触摸事件
            case EventType::TOUCH_TAP: {
                event_name = "TOUCH_TAP";
                // 使用新的TouchEventData结构
                const char* side_str = "UNKNOWN";
                switch (event.data.touch_data.position) {
                    case TouchPosition::LEFT: side_str = "LEFT"; break;
                    case TouchPosition::RIGHT: side_str = "RIGHT"; break;
                    case TouchPosition::BOTH: side_str = "BOTH"; break;
                    case TouchPosition::ANY: side_str = "ANY"; break;
                }
                ESP_LOGI(TAG, "👆 Touch TAP on %s side! (duration: %lu ms, count: %lu)", 
                        side_str,
                        (unsigned long)event.data.touch_data.duration_ms,
                        (unsigned long)event.data.touch_data.tap_count);
                break;
            }
            case EventType::TOUCH_DOUBLE_TAP:
                event_name = "TOUCH_DOUBLE_TAP";
                ESP_LOGI(TAG, "👆👆 Touch DOUBLE TAP! (duration: %lu ms)", 
                        (unsigned long)event.data.touch_data.duration_ms);
                break;
            case EventType::TOUCH_LONG_PRESS:
                event_name = "TOUCH_LONG_PRESS";
                {
                    const char* press_side = "UNKNOWN";
                    switch (event.data.touch_data.position) {
                        case TouchPosition::LEFT: press_side = "LEFT"; break;
                        case TouchPosition::RIGHT: press_side = "RIGHT"; break;
                        case TouchPosition::BOTH: press_side = "BOTH"; break;
                        case TouchPosition::ANY: press_side = "ANY"; break;
                    }
                    ESP_LOGI(TAG, "👇 Touch LONG PRESS on %s side! (duration: %lu ms)", 
                            press_side,
                            (unsigned long)event.data.touch_data.duration_ms);
                }
                break;
            
            // 其他触摸事件
            case EventType::TOUCH_CRADLED:
                event_name = "TOUCH_CRADLED";
                ESP_LOGI(TAG, "🤗 Device is being cradled!");
                break;
            case EventType::TOUCH_TICKLED:
                event_name = "TOUCH_TICKLED";
                ESP_LOGI(TAG, "😄 Device is being tickled!");
                break;
            case EventType::TOUCH_HOLD:
            case EventType::TOUCH_RELEASE:
                // 暂时不处理
                return;
                
            // 音频事件（预留）
            case EventType::AUDIO_WAKE_WORD:
            case EventType::AUDIO_SPEAKING:
            case EventType::AUDIO_LISTENING:
                // 暂时不处理
                return;
                
            // 系统事件（预留）
            case EventType::SYSTEM_BOOT:
            case EventType::SYSTEM_SHUTDOWN:
            case EventType::SYSTEM_ERROR:
                // 暂时不处理
                return;
                
            // 默认情况
            case EventType::MOTION_NONE:
            default: 
                return;
        }
        
        // 显示详细的IMU数据
        ESP_LOGD(TAG, "IMU Event [%s] - Accel(g): X=%.2f Y=%.2f Z=%.2f | Angles(°): X=%.1f Y=%.1f Z=%.1f",
                event_name,
                data.accel_x, data.accel_y, data.accel_z,
                data.angle_x, data.angle_y, data.angle_z);
    }

public:
    ALichuangTest() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSpi();
        InitializeSt7789Display();
        InitializeTouch();
        InitializeButtons();
        InitializeCamera();
        InitializeImu();  // 初始化IMU硬件
        InitializePca9685();  // 初始化PCA9685 PWM控制器
        InitializeVibration();  // 初始化振动技能（使用PCA9685）
        InitializeMotion();  // 初始化直流马达动作控制技能
        InitialSDCard();
        InitializeInteractionSystem();  // 初始化交互系统

        GetBacklight()->RestoreBrightness();
#if CONFIG_LINGXI_ANIMA_UI       
        // 启动图片循环显示任务
        StartImageSlideshow();
#endif
        // 启动振动任务
        StartVibrationTask();
        // 启动直流马达动作控制任务
        StartMotionTask();
                
        // 初始化本地响应系统（在交互系统和技能初始化后）
        InitializeLocalResponseSystem();

        // 所有skills初始化完成后，初始化MCP工具
        InitializeMcpTools();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static CustomAudioCodec audio_codec(
            i2c_bus_, 
            pca9557_);
        return &audio_codec;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }

#if CONFIG_LINGXI_ANIMA_UI
    virtual Display* GetDisplay() override {
        // 返回SpiLcdDisplay实例，保持兼容性
        return display_;
    }
#elif CONFIG_XIAOZHI_DEFAULT_UI
    virtual Display* GetDisplay() override {
        return display_;
    }
#endif
    
    virtual std::string GetBoardType() override {
        return "lingxi";
    }
    
    // 获取运动检测器（可选，用于外部访问）
    EventEngine* GetEventEngine() {
        return event_engine_;
    }
    
    // 获取事件上传器（供Application使用）
    EventUploader* GetEventUploader() { 
        return event_uploader_; 
    }
    
    // 获取IMU（可选，用于外部访问）
    Qmi8658* GetImu() {
        return imu_;
    }
    
    // 获取振动技能管理器（可选，用于外部访问和测试）
    Vibration* GetVibration() {
        return vibration_skill_;
    }
    
    // 获取直流马达动作控制技能（可选，用于外部访问和测试）
    Motion* GetMotion() {
        return motion_skill_;
    }
    
    // 获取本地响应控制器（用于调试和测试）
    LocalResponseController* GetLocalResponseController() {
        return local_response_controller_;
    }
};

DECLARE_BOARD(ALichuangTest);
