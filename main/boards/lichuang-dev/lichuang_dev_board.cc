#include "wifi_board.h"
#include "audio_codecs/box_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "i2c_device.h"
#include "iot/thing_manager.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <wifi_station.h>

#include "esp_lcd_touch_ft5x06.h"   
#include "esp_lvgl_port.h"      
#include "esp_check.h" 

#define TAG "LichuangDevBoard"

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


class LichuangDevBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_dev_handle_t pca9557_handle_;
    Button boot_button_;
    LcdDisplay* display_;
    Pca9557* pca9557_;

    esp_lcd_touch_handle_t tp_;  
    


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

        // Initialize PCA9557
        pca9557_ = new Pca9557(i2c_bus_, 0x19);
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
    }

    esp_err_t InitTouchDriver(esp_lcd_touch_handle_t *ret_touch)
{
    ESP_LOGI(TAG, ">>> InitTouchDriver() enter");
    /* ① 触摸控制器能力描述 */
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = 320,               // <-- TODO 改成你的 X 分辨率
        .y_max = 240,               // <-- TODO 改成你的 Y 分辨率
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset      = 0,
            .interrupt  = 0,
        },
        .flags = {
            .swap_xy    = 1,        // 屏幕是竖的就开；横屏关
            .mirror_x   = 1,
            .mirror_y   = 0,
        },
    };

    /* ② 用现有 i2c_bus_ 挂载 FT5x06 面板 IO */
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = {};   // 全部 0

    io_cfg.dev_addr            = 0x38;
    io_cfg.on_color_trans_done = nullptr;
    io_cfg.user_ctx            = nullptr;
    io_cfg.control_phase_bytes = 1;
    io_cfg.dc_bit_offset       = 0;
    io_cfg.lcd_cmd_bits        = 8;
    io_cfg.lcd_param_bits      = 8;
    io_cfg.scl_speed_hz        = 400 * 1000;
    io_cfg.flags.disable_control_phase = 1;
    
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_i2c_v2(i2c_bus_, &io_cfg, &tp_io),
        TAG, "new_panel_io_i2c_v2 failed");

    /* ③ 创建驱动 */
   /* 创建 FT5x06 触摸驱动 */
esp_err_t err = esp_lcd_touch_new_i2c_ft5x06(tp_io, &tp_cfg, ret_touch);
if (err != ESP_OK) {
    ESP_LOGE(TAG, "touch_new_i2c_ft5x06 failed: %s", esp_err_to_name(err));
    return err;          // 如果想让上层知道失败，保留这行
}
ESP_LOGI(TAG, "FT5x06 driver init OK"); 
/* 成功则继续往下走 */


    /* ④ （可选）把这个设备额外挂到 I²C，总线调试更方便 */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length   = I2C_ADDR_BIT_LEN_7,
        .device_address    = 0x38,
        .scl_speed_hz      = 400*1000,
    };
    i2c_master_dev_handle_t tmp;
    i2c_master_bus_add_device(i2c_bus_, &dev_cfg, &tmp);

    return ESP_OK;
}

/* 2. 把触摸注册到 LVGL，并返回 indev 指针（如果你将来想自己用）*/
    lv_indev_t *InitLvglIndev(lv_disp_t *disp)
    {
        ESP_ERROR_CHECK(InitTouchDriver(&tp_));
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp   = disp,
            .handle = tp_,
        };
        return lvgl_port_add_touch(&touch_cfg);
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Screen"));
    }

    /* --- 触摸调试任务：每 50 ms 打印一次坐标 ------------------ */
    static void TouchLogTask(void *arg)
{
    auto *board = static_cast<LichuangDevBoard *>(arg);

    uint16_t x, y;
    uint8_t  points;

    while (true) {
        /* ① 采样：和触摸控制器走一次 I²C，把数据写入驱动缓存 */
        esp_lcd_touch_read_data(board->GetTouchHandle());

        /* ② 读取：从缓存里取第 1 个触点坐标 */
        bool touched = esp_lcd_touch_get_coordinates(
                           board->GetTouchHandle(),
                           &x, &y,
                           nullptr,     // strength 可忽略
                           &points,     // 返回有效触点数
                           1);          // 只取 1 个触点

        if (touched && points) {
            ESP_LOGI("TOUCH", "x=%u  y=%u", x, y);
        }
        vTaskDelay(pdMS_TO_TICKS(200));   // 5 Hz
    }
}


public:
    esp_lcd_touch_handle_t GetTouchHandle() const { return tp_; }

    LichuangDevBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSpi();
        InitializeSt7789Display();
        InitializeButtons();
        InitLvglIndev(display_->GetLvDisp());
        InitializeIot();
        GetBacklight()->RestoreBrightness();
        
        #if CONFIG_DEBUG_TOUCH_LOG
        xTaskCreate(TouchLogTask, "touch_log", 4096, this, 5, nullptr);
        #endif
    }

    virtual AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec audio_codec(
            i2c_bus_, 
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
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
    
    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }
};

DECLARE_BOARD(LichuangDevBoard);
