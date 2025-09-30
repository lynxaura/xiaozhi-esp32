#include "animation.h"

#include <esp_log.h>
#include <esp_err.h>
#include <esp_lvgl_port.h>
#include <esp_heap_caps.h>
#include <cstring>

#include "board.h"

#define TAG "AnimaDisplay"

AnimaDisplay::AnimaDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                           int width, int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy)
    : LcdDisplay(panel_io, panel, width, height) {

    // draw white
    std::vector<uint16_t> buffer(width_, 0xFFFF);
    for (int y = 0; y < height_; y++) {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    // Set the display to on
    ESP_LOGI(TAG, "Turning display on");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 2;  // 降低LVGL任务优先级，避免与蓝牙任务(优先级5)冲突
    port_cfg.timer_period_ms = 100;  // 增加定时器周期，减少与蓝牙任务的冲突
    port_cfg.task_stack = 4096;  // 确保足够的栈大小
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * 20),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        .rotation = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = 1,
            .buff_spiram = 0,
            .sw_rotate = 0,
            .swap_bytes = 1,
            .full_refresh = 0,
            .direct_mode = 0,
        },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    // 调用简化的SetupUI
    SetupUI();
}

void AnimaDisplay::SetupUI() {
    DisplayLockGuard lock(this);
    
    // 创建最基本的LVGL对象以避免空指针，但不设置复杂UI
    auto screen = lv_screen_active();
    
    // 创建透明容器
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, width_, height_);
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);
    
    // 创建基本的状态标签（设为隐藏，避免系统调用时崩溃）
    emoji_label_ = lv_label_create(container_);
    lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    
    // 其他UI组件设为nullptr，避免系统调用时出错
    status_label_ = nullptr;
    notification_label_ = nullptr;
    network_label_ = nullptr;
    mute_label_ = nullptr;
    battery_label_ = nullptr;
    chat_message_label_ = nullptr;
    low_battery_popup_ = nullptr;
    low_battery_label_ = nullptr;
    status_bar_ = nullptr;
    content_ = nullptr;
    
    ESP_LOGI(TAG, "Simplified UI setup completed");
}

void AnimaDisplay::SetEmotion(const char* emotion) {
    // 检查动画是否已暂停
    if (animation_suspended_) {
        ESP_LOGD(TAG, "Animation suspended, deferring emotion change to: %s", emotion);
        return;
    }

    // 触发情感变化回调
    if (emotion_callback_) {
        emotion_callback_(std::string(emotion));
    }
}


void AnimaDisplay::CreateCanvas() {
    DisplayLockGuard lock(this);

    // 如果已经有画布，先销毁
    if (canvas_ != nullptr) {
        // 在当前锁内直接销毁，不再调用DestroyCanvas()
        lv_obj_del(canvas_);
        canvas_ = nullptr;

        if (canvas_buffer_ != nullptr) {
            heap_caps_free(canvas_buffer_);
            canvas_buffer_ = nullptr;
        }
    }
    
    // 创建画布所需的缓冲区
    // 每个像素2字节(RGB565)
    size_t buf_size = width_ * height_ * 2;  // RGB565: 2 bytes per pixel
    
    // 分配内存，优先使用PSRAM
    canvas_buffer_ = heap_caps_malloc(buf_size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (canvas_buffer_ == nullptr) {
        ESP_LOGE("Display", "Failed to allocate canvas buffer");
        return;
    }
    
    // 获取活动屏幕
    lv_obj_t* screen = lv_screen_active();
    
    // 创建画布对象
    canvas_ = lv_canvas_create(screen);
    if (canvas_ == nullptr) {
        ESP_LOGE("Display", "Failed to create canvas");
        heap_caps_free(canvas_buffer_);
        canvas_buffer_ = nullptr;
        return;
    }
    
    // 初始化画布
    lv_canvas_set_buffer(canvas_, canvas_buffer_, width_, height_, LV_COLOR_FORMAT_RGB565);
    
    // 设置画布位置为全屏
    // lv_obj_set_pos(canvas_, 0, 25);
    // lv_obj_set_size(canvas_, width_, height_ - 25);
    lv_obj_set_pos(canvas_, 0, 0);
    lv_obj_set_size(canvas_, width_, height_);
    
    // 设置画布为透明
    lv_canvas_fill_bg(canvas_, lv_color_make(0, 0, 0), LV_OPA_TRANSP);
    
    // 设置画布为顶层
    lv_obj_move_foreground(canvas_);
    
    ESP_LOGI("Display", "Canvas created successfully");
}

void AnimaDisplay::DestroyCanvas() {
    // 注意：此方法假设调用者已经获取了LVGL锁

    if (canvas_ != nullptr) {
        lv_obj_del(canvas_);
        canvas_ = nullptr;
    }

    if (canvas_buffer_ != nullptr) {
        heap_caps_free(canvas_buffer_);
        canvas_buffer_ = nullptr;
    }

    ESP_LOGI("Display", "Canvas destroyed");
}

void AnimaDisplay::DrawImageOnCanvas(int x, int y, int width, int height, const uint8_t* img_data) {
    // 首先检查动画是否已暂停，避免不必要的锁获取
    if (animation_suspended_) {
        ESP_LOGD("Display", "Animation suspended, skipping image draw");
        return;
    }

    // 确保有画布，避免不必要的锁获取
    if (canvas_ == nullptr) {
        ESP_LOGD("Display", "Canvas not created, skipping image draw");
        return;
    }

    DisplayLockGuard lock(this);

    // 再次检查状态，因为可能在获取锁的过程中状态发生了变化
    if (animation_suspended_ || canvas_ == nullptr) {
        ESP_LOGD("Display", "Animation suspended or canvas destroyed during lock acquisition");
        return;
    }
    
    // 创建一个描述器来映射图像数据
    const lv_image_dsc_t img_dsc = {
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RGB565,
            .flags = 0,
            .w = (uint32_t)width,
            .h = (uint32_t)height,
            .stride = (uint32_t)(width * 2),  // RGB565: 2 bytes per pixel
            .reserved_2 = 0,
        },
        .data_size = (uint32_t)(width * height * 2),  // RGB565: 2 bytes per pixel
        .data = img_data,
        .reserved = NULL
    };
    
    // 使用图层绘制图像到画布上
    lv_layer_t layer;
    lv_canvas_init_layer(canvas_, &layer);
    
    lv_draw_image_dsc_t draw_dsc;
    lv_draw_image_dsc_init(&draw_dsc);
    draw_dsc.src = &img_dsc;
    
    lv_area_t area;
    area.x1 = x;
    area.y1 = y;
    area.x2 = x + width - 1;
    area.y2 = y + height - 1;

    lv_draw_image(&layer, &draw_dsc, &area);
    lv_canvas_finish_layer(canvas_, &layer);
    
    // 确保画布在最上层
    lv_obj_move_foreground(canvas_);
    
    // ESP_LOGI("Display", "Image drawn on canvas at x=%d, y=%d, w=%d, h=%d", x, y, width, height);
}

void AnimaDisplay::SetTheme(Theme* theme) {
    // AnimaDisplay doesn't use traditional themes since it uses canvas-based rendering
    // Store the theme but don't apply it to UI elements
    current_theme_ = theme;
}

void AnimaDisplay::SuspendAnimation() {
    if (animation_suspended_) {
        ESP_LOGW(TAG, "Animation already suspended");
        return;
    }

    // 记录内存状态
    size_t free_heap_before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t free_psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    ESP_LOGI(TAG, "🔍 Memory before suspension - Heap: %zu KB, PSRAM: %zu KB",
             free_heap_before / 1024, free_psram_before / 1024);

    // 记录当前是否有canvas
    canvas_was_created_ = (canvas_ != nullptr);

    // 先设置暂停标志，防止新的绘制操作
    animation_suspended_ = true;

    // 等待更长时间确保LVGL任务完成所有操作
    vTaskDelay(pdMS_TO_TICKS(200));

    // 现在安全地销毁canvas
    if (canvas_was_created_) {
        ESP_LOGI(TAG, "Suspending animation - destroying canvas to free memory");

        // 计算预期释放的内存大小
        size_t expected_freed = width_ * height_ * 2;  // RGB565: 2 bytes per pixel

        // 获取LVGL锁并销毁canvas
        if (lvgl_port_lock(500)) {
            DestroyCanvas();
            lvgl_port_unlock();

            // 强制垃圾回收
            vTaskDelay(pdMS_TO_TICKS(100));

            // 验证内存是否真正释放
            size_t free_heap_after = heap_caps_get_free_size(MALLOC_CAP_8BIT);
            size_t free_psram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

            size_t heap_freed = free_heap_after - free_heap_before;
            size_t psram_freed = free_psram_after - free_psram_before;

            ESP_LOGI(TAG, "📊 Memory after suspension - Heap: %zu KB (+%zu KB), PSRAM: %zu KB (+%zu KB)",
                     free_heap_after / 1024, heap_freed / 1024,
                     free_psram_after / 1024, psram_freed / 1024);

            if (heap_freed > 0 || psram_freed > 0) {
                ESP_LOGI(TAG, "✅ Animation suspended - actually freed %zu KB (expected %zu KB)",
                         (heap_freed + psram_freed) / 1024, expected_freed / 1024);
            } else {
                ESP_LOGW(TAG, "⚠️ Animation suspended but no memory freed! Expected %zu KB",
                         expected_freed / 1024);
            }
        } else {
            ESP_LOGE(TAG, "❌ Failed to get LVGL lock for canvas destruction");
        }
    } else {
        ESP_LOGI(TAG, "✅ Animation suspended - no canvas to free");
    }
}

void AnimaDisplay::ResumeAnimation() {
    if (!animation_suspended_) {
        ESP_LOGW(TAG, "Animation not suspended");
        return;
    }

    ESP_LOGI(TAG, "Resuming animation - current state: canvas_was_created=%s",
             canvas_was_created_ ? "true" : "false");

    // 如果之前有canvas，重新创建
    if (canvas_was_created_) {
        ESP_LOGI(TAG, "Resuming animation - recreating canvas");

        // 获取LVGL锁并创建canvas
        if (lvgl_port_lock(500)) {
            // 在锁内重新创建canvas
            size_t buf_size = width_ * height_ * 2;  // RGB565: 2 bytes per pixel

            // 分配内存，优先使用PSRAM
            canvas_buffer_ = heap_caps_malloc(buf_size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
            if (canvas_buffer_ != nullptr) {
                // 获取活动屏幕
                lv_obj_t* screen = lv_screen_active();

                // 创建画布对象
                canvas_ = lv_canvas_create(screen);
                if (canvas_ != nullptr) {
                    // 初始化画布
                    lv_canvas_set_buffer(canvas_, canvas_buffer_, width_, height_, LV_COLOR_FORMAT_RGB565);
                    lv_obj_set_pos(canvas_, 0, 0);
                    lv_obj_set_size(canvas_, width_, height_);
                    lv_canvas_fill_bg(canvas_, lv_color_make(0, 0, 0), LV_OPA_TRANSP);
                    lv_obj_move_foreground(canvas_);

                    ESP_LOGI(TAG, "Animation resumed - canvas recreated successfully");
                } else {
                    ESP_LOGE(TAG, "Failed to create canvas object during resume");
                    heap_caps_free(canvas_buffer_);
                    canvas_buffer_ = nullptr;
                }
            } else {
                ESP_LOGE(TAG, "Failed to allocate canvas buffer during resume");
            }

            lvgl_port_unlock();
        } else {
            ESP_LOGE(TAG, "Failed to get LVGL lock for canvas recreation");
        }
    }

    animation_suspended_ = false;
    canvas_was_created_ = false; // 重置状态
}