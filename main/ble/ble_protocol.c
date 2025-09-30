#include "ble_protocol.h"
#include "esp_ble.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>

static const char* TAG = "BLE_PROTOCOL";

// 数据队列结构 - 渐进式优化：保证功能同时减少内存
typedef struct {
    uint16_t conn_id;
    uint16_t handle;
    uint16_t len;
    uint8_t data[128];  // 适中的缓冲区大小
} ble_protocol_data_msg_t;

// 全局变量
static ble_protocol_cmd_handler_t g_handlers[BLE_PROTOCOL_MAX_HANDLERS];
static SemaphoreHandle_t g_handlers_mutex = NULL;
static QueueHandle_t g_data_queue = NULL;
static TaskHandle_t g_process_task = NULL;
static bool g_task_running = false;

// 任务配置 - 渐进式优化：保证功能同时减少内存占用
#define BLE_PROTOCOL_TASK_STACK_SIZE    3072   // 适中的栈大小(3KB)
#define BLE_PROTOCOL_TASK_PRIORITY      5      // 提高优先级避免看门狗超时
#define BLE_PROTOCOL_QUEUE_SIZE         10     // 增加队列大小减少丢包

// 内部函数声明
static void ble_protocol_event_handler(ble_evt_t *evt);
static void ble_protocol_process_task(void *arg);
static esp_err_t ble_protocol_process_data(uint16_t conn_id, uint8_t *data, uint16_t len);

esp_err_t ble_protocol_init(void)
{
    ESP_LOGI(TAG, "Initializing BLE protocol module");

    // 内存诊断 - 检查可用内存
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free_heap = esp_get_minimum_free_heap_size();
    ESP_LOGI(TAG, "Memory before BLE init - Free: %d bytes, Min free: %d bytes", free_heap, min_free_heap);

    // 检查是否有足够内存进行BLE初始化
    size_t required_memory = sizeof(ble_protocol_data_msg_t) * BLE_PROTOCOL_QUEUE_SIZE +
                           BLE_PROTOCOL_TASK_STACK_SIZE +
                           sizeof(g_handlers) + 1024; // 额外的1KB缓冲
    ESP_LOGI(TAG, "Required memory: %d bytes", required_memory);

    if (free_heap < required_memory) {
        ESP_LOGE(TAG, "Insufficient memory for BLE protocol init. Need %d bytes, have %d bytes",
                 required_memory, free_heap);
        return ESP_ERR_NO_MEM;
    }

    // 初始化处理器数组
    memset(g_handlers, 0, sizeof(g_handlers));
    
    // 创建互斥锁
    g_handlers_mutex = xSemaphoreCreateMutex();
    if (g_handlers_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create handlers mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // 创建数据队列 - 带详细的错误诊断
    ESP_LOGI(TAG, "Attempting to create queue: size=%d, item_size=%d, total_memory=%d bytes",
             BLE_PROTOCOL_QUEUE_SIZE, sizeof(ble_protocol_data_msg_t),
             BLE_PROTOCOL_QUEUE_SIZE * sizeof(ble_protocol_data_msg_t));

    g_data_queue = xQueueCreate(BLE_PROTOCOL_QUEUE_SIZE, sizeof(ble_protocol_data_msg_t));
    if (g_data_queue == NULL) {
        size_t free_after_mutex = esp_get_free_heap_size();
        ESP_LOGW(TAG, "Failed to create standard queue, trying smaller configuration...");
        ESP_LOGI(TAG, "Queue parameters: size=%d, item_size=%d bytes, total_needed=%d bytes",
                 BLE_PROTOCOL_QUEUE_SIZE, sizeof(ble_protocol_data_msg_t),
                 BLE_PROTOCOL_QUEUE_SIZE * sizeof(ble_protocol_data_msg_t));
        ESP_LOGI(TAG, "Free memory after mutex creation: %d bytes", free_after_mutex);

        // 尝试最小配置：队列大小为2
        ESP_LOGW(TAG, "Attempting fallback configuration: queue size = 2");
        g_data_queue = xQueueCreate(2, sizeof(ble_protocol_data_msg_t));
        if (g_data_queue == NULL) {
            ESP_LOGE(TAG, "Even minimal queue creation failed - memory severely limited");
            vSemaphoreDelete(g_handlers_mutex);
            g_handlers_mutex = NULL;
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGW(TAG, "Fallback queue created successfully with size=2");
    }

    ESP_LOGI(TAG, "Data queue created successfully");

    // 再次检查内存状态
    size_t free_after_queue = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Free memory after queue creation: %d bytes", free_after_queue);
    
    
    
    // 注册BLE事件回调
    esp_err_t esp_ret = esp_ble_register_evt_callback(ble_protocol_event_handler);
    if (esp_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register BLE callback: %s", esp_err_to_name(esp_ret));
        vQueueDelete(g_data_queue);
        vSemaphoreDelete(g_handlers_mutex);
        return esp_ret;
    }

    // 任务创建前的系统诊断
    size_t free_before_task = esp_get_free_heap_size();
    min_free_heap = esp_get_minimum_free_heap_size();  // 重用之前的变量
    UBaseType_t task_count = uxTaskGetNumberOfTasks();

    // 增强内存诊断 - 检查碎片化
    multi_heap_info_t heap_info;
    heap_caps_get_info(&heap_info, MALLOC_CAP_DEFAULT);
    size_t largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

    ESP_LOGI(TAG, "=== Enhanced Memory Diagnostics ===");
    ESP_LOGI(TAG, "  Total free: %d bytes, Min free ever: %d bytes", free_before_task, min_free_heap);
    ESP_LOGI(TAG, "  Largest free block: %d bytes", largest_free_block);
    ESP_LOGI(TAG, "  Internal SRAM free: %d bytes, largest: %d bytes", internal_free, internal_largest);
    ESP_LOGI(TAG, "  Current task count: %d", task_count);
    ESP_LOGI(TAG, "  Requested stack size: %d bytes", BLE_PROTOCOL_TASK_STACK_SIZE);

    // 检查是否存在严重碎片化
    float fragmentation_ratio = (float)(free_before_task - largest_free_block) / free_before_task * 100;
    ESP_LOGI(TAG, "  Fragmentation ratio: %.1f%%", fragmentation_ratio);

    if (largest_free_block < 8192) {  // 如果最大连续块小于8KB
        ESP_LOGW(TAG, "  WARNING: Severe fragmentation detected!");
    }

    g_task_running = true;

    // 如果碎片化严重，尝试强制垃圾回收
    if (largest_free_block < 4096) {
        ESP_LOGW(TAG, "Attempting heap defragmentation...");
        // 强制释放一些内存并触发垃圾回收
        heap_caps_malloc_extmem_enable(32 * 1024);  // 扩展内存阈值
    }

    // 尝试不同的任务创建策略
    uint32_t stack_sizes[] = {BLE_PROTOCOL_TASK_STACK_SIZE, 4096, 2048}; // 优先使用配置的栈大小
    uint32_t priorities[] = {BLE_PROTOCOL_TASK_PRIORITY, 3, 1}; // 优先使用配置的优先级
    const char* task_names[] = {"ble_proto", "ble_prot", "ble_task"};
    BaseType_t ret = pdFAIL;
    bool task_created = false;

    // 策略1: 标准任务创建
    for (int i = 0; i < 3 && !task_created; i++) {
        for (int j = 0; j < 3 && !task_created; j++) {
            ESP_LOGI(TAG, "Strategy 1 - xTaskCreate: stack=%d, priority=%d, name=%s",
                     stack_sizes[i], priorities[j], task_names[i]);

            ret = xTaskCreate(
                ble_protocol_process_task,
                task_names[i],
                stack_sizes[i],
                NULL,
                priorities[j],
                &g_process_task
            );

            if (ret == pdPASS) {
                ESP_LOGI(TAG, "Standard task creation succeeded");
                task_created = true;
                break;
            } else {
                ESP_LOGW(TAG, "Standard creation failed: %d", ret);
            }
        }
    }

    // 策略2: 固定到CPU核心0的任务创建
    if (!task_created) {
        ESP_LOGW(TAG, "Trying pinned task creation on core 0...");
        for (int i = 0; i < 3 && !task_created; i++) {
            ESP_LOGI(TAG, "Strategy 2 - xTaskCreatePinnedToCore: stack=%d, core=0",
                     stack_sizes[i]);

            ret = xTaskCreatePinnedToCore(
                ble_protocol_process_task,
                task_names[i],
                stack_sizes[i],
                NULL,
                priorities[i],  // 使用对应的优先级
                &g_process_task,
                0  // 固定到核心0
            );

            if (ret == pdPASS) {
                ESP_LOGI(TAG, "Pinned task creation succeeded - stack: %d", stack_sizes[i]);
                task_created = true;
                break;
            } else {
                ESP_LOGW(TAG, "Pinned creation failed - stack: %d, error: %d", stack_sizes[i], ret);
            }
        }
    }

    // 策略3: 使用静态任务创建（最后手段）
    if (!task_created) {
        ESP_LOGW(TAG, "Trying static task creation as last resort...");

        // 为静态任务分配内存
        static StackType_t task_stack[BLE_PROTOCOL_TASK_STACK_SIZE];  // 使用配置的栈大小
        static StaticTask_t task_buffer;

        g_process_task = xTaskCreateStatic(
            ble_protocol_process_task,
            "ble_static",
            BLE_PROTOCOL_TASK_STACK_SIZE,
            NULL,
            BLE_PROTOCOL_TASK_PRIORITY,  // 使用配置的优先级
            task_stack,
            &task_buffer
        );

        if (g_process_task != NULL) {
            ESP_LOGI(TAG, "Static task creation succeeded");
            task_created = true;
        } else {
            ESP_LOGE(TAG, "Even static task creation failed");
        }
    }

    if (!task_created) {
        g_task_running = false;
        ESP_LOGE(TAG, "All task creation attempts failed!");
        esp_ble_unregister_evt_callback(ble_protocol_event_handler);
        vQueueDelete(g_data_queue);
        g_data_queue = NULL;
        vSemaphoreDelete(g_handlers_mutex);
        g_handlers_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "BLE protocol module initialized successfully");
    return ESP_OK;
}

esp_err_t ble_protocol_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing BLE protocol module");
    
    // 取消注册BLE事件回调
    esp_ble_unregister_evt_callback(ble_protocol_event_handler);
    
    // 停止任务
    if (g_task_running) {
        g_task_running = false;
        
        // 等待任务退出
        vTaskDelay(pdMS_TO_TICKS(100));
        
        if (g_process_task) {
            vTaskDelete(g_process_task);
            g_process_task = NULL;
        }
    }
    
    // 清理队列
    if (g_data_queue) {
        vQueueDelete(g_data_queue);
        g_data_queue = NULL;
    }
    
    // 清理互斥锁
    if (g_handlers_mutex) {
        vSemaphoreDelete(g_handlers_mutex);
        g_handlers_mutex = NULL;
    }
    
    // 清理处理器数组
    memset(g_handlers, 0, sizeof(g_handlers));
    
    ESP_LOGI(TAG, "BLE protocol module deinitialized");
    return ESP_OK;
}

esp_err_t ble_protocol_register_handler(uint8_t cmd, ble_protocol_handler_t handler, const char* name)
{
    if (handler == NULL || name == NULL) {
        ESP_LOGE(TAG, "Invalid parameters for handler registration");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_handlers_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take handlers mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    esp_err_t ret = ESP_ERR_NO_MEM;
    
    // 查找空闲槽位
    for (int i = 0; i < BLE_PROTOCOL_MAX_HANDLERS; i++) {
        if (g_handlers[i].handler == NULL) {
            g_handlers[i].cmd = cmd;
            g_handlers[i].handler = handler;
            g_handlers[i].name = name;
            ESP_LOGI(TAG, "Registered handler for cmd 0x%02X: %s", cmd, name);
            ret = ESP_OK;
            break;
        }
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No more handler slots available");
    }
    
    xSemaphoreGive(g_handlers_mutex);
    return ret;
}

esp_err_t ble_protocol_unregister_handler(uint8_t cmd)
{
    if (xSemaphoreTake(g_handlers_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take handlers mutex");
        return ESP_ERR_TIMEOUT;
    }
    
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    
    // 查找并移除处理器
    for (int i = 0; i < BLE_PROTOCOL_MAX_HANDLERS; i++) {
        if (g_handlers[i].cmd == cmd && g_handlers[i].handler != NULL) {
            ESP_LOGI(TAG, "Unregistered handler for cmd 0x%02X: %s", cmd, g_handlers[i].name);
            memset(&g_handlers[i], 0, sizeof(ble_protocol_cmd_handler_t));
            ret = ESP_OK;
            break;
        }
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Handler for cmd 0x%02X not found", cmd);
    }
    
    xSemaphoreGive(g_handlers_mutex);
    return ret;
}

static void ble_protocol_event_handler(ble_evt_t *evt)
{
    if (evt == NULL) {
        return;
    }

    // 在中断上下文中，减少日志输出以避免阻塞
    switch (evt->evt_id) {
        case BLE_EVT_CONNECTED:
            // 简化日志，避免在中断上下文中长时间处理
            ESP_LOGD(TAG, "BLE connected: %d", evt->params.connected.conn_id);
            break;

        case BLE_EVT_DISCONNECTED:
            ESP_LOGD(TAG, "BLE disconnected: %d", evt->params.disconnected.conn_id);
            break;

        case BLE_EVT_DATA_RECEIVED:
            {
                // 快速检查数据有效性
                if (evt->params.data_received.len > BLE_PROTOCOL_MAX_PAYLOAD_LEN) {
                    return; // 直接返回，不输出日志避免阻塞
                }

                // 将数据放入队列中异步处理
                ble_protocol_data_msg_t msg;
                msg.conn_id = evt->params.data_received.conn_id;
                msg.handle = evt->params.data_received.handle;
                msg.len = evt->params.data_received.len;

                // 快速内存拷贝
                memcpy(msg.data, evt->params.data_received.p_data, msg.len);

                // 使用FromISR版本的队列发送，确保中断安全
                BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                if (xQueueSendFromISR(g_data_queue, &msg, &xHigherPriorityTaskWoken) != pdTRUE) {
                    // 队列满时静默丢弃，避免日志阻塞
                    // 可以在这里增加计数器统计丢失的包
                }

                // 如果有更高优先级任务被唤醒，进行任务切换
                portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
            }
            break;

        default:
            break;
    }
}

static void ble_protocol_process_task(void *arg)
{
    ble_protocol_data_msg_t msg;

    ESP_LOGI(TAG, "BLE protocol process task started (priority %d)", uxTaskPriorityGet(NULL));

    while (g_task_running) {
        // 减少超时等待时间，提高响应性
        if (xQueueReceive(g_data_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            // 处理数据
            esp_err_t ret = ble_protocol_process_data(msg.conn_id, msg.data, msg.len);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to process data: %s", esp_err_to_name(ret));
            }
        }
        // 短暂让出CPU，避免独占处理器
        taskYIELD();
    }

    ESP_LOGI(TAG, "BLE protocol process task exited");
    vTaskDelete(NULL);
}

static esp_err_t ble_protocol_process_data(uint16_t conn_id, uint8_t *data, uint16_t len)
{
    // 检查最小包长度
    if (len < BLE_PROTOCOL_MIN_PACKET_LEN) {
        ESP_LOGE(TAG, "Received data too short: %d", len);
        return ESP_ERR_INVALID_ARG;
    }
    
    // 解析协议包
    uint8_t cmd;
    const uint8_t *payload;
    size_t payload_len;
    
    if (!ble_protocol_parse_packet(data, len, &cmd, &payload, &payload_len)) {
        ESP_LOGD(TAG, "Not a valid protocol packet, ignoring");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Processing protocol command: 0x%02X, payload_len: %d", cmd, payload_len);
    
    // 查找并调用对应的处理器
    if (xSemaphoreTake(g_handlers_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        esp_err_t ret = ESP_ERR_NOT_SUPPORTED;
        
        for (int i = 0; i < BLE_PROTOCOL_MAX_HANDLERS; i++) {
            if (g_handlers[i].cmd == cmd && g_handlers[i].handler != NULL) {
                ESP_LOGI(TAG, "Calling handler: %s", g_handlers[i].name);
                ret = g_handlers[i].handler(conn_id, payload, payload_len);
                break;
            }
        }
        
        if (ret == ESP_ERR_NOT_SUPPORTED) {
            ESP_LOGE(TAG, "No handler found for command: 0x%02X", cmd);
        }
        
        xSemaphoreGive(g_handlers_mutex);
        return ret;
    } else {
        ESP_LOGE(TAG, "Failed to take handlers mutex");
        return ESP_ERR_TIMEOUT;
    }
}

bool ble_protocol_parse_packet(const uint8_t *data, size_t len, uint8_t *cmd, const uint8_t **payload, size_t *payload_len)
{
    if (data == NULL || cmd == NULL || payload == NULL || payload_len == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }
    
    if (len < BLE_PROTOCOL_MIN_PACKET_LEN) {
        ESP_LOGE(TAG, "Packet too short: %d bytes", len);
        return false;
    }
    
    if (data[0] != BLE_PROTOCOL_HEADER_0 || data[1] != BLE_PROTOCOL_HEADER_1) {
        ESP_LOGD(TAG, "Invalid header: 0x%02X 0x%02X", data[0], data[1]);
        return false;
    }
    
    *cmd = data[2];
    *payload = (len > 3) ? &data[3] : NULL;
    *payload_len = (len > 3) ? len - 3 : 0;
    
    ESP_LOGD(TAG, "Parsed packet: cmd=0x%02X, payload_len=%d", *cmd, *payload_len);
    return true;
}

size_t ble_protocol_build_packet(uint8_t cmd, const uint8_t *payload, size_t payload_len, uint8_t *packet, size_t max_len)
{
    if (packet == NULL) {
        ESP_LOGE(TAG, "Packet buffer is NULL");
        return 0;
    }
    
    size_t total_len = BLE_PROTOCOL_MIN_PACKET_LEN + payload_len;
    if (total_len > max_len) {
        ESP_LOGE(TAG, "Packet buffer too small: need %d, have %d", total_len, max_len);
        return 0;
    }
    
    if (payload_len > BLE_PROTOCOL_MAX_PAYLOAD_LEN) {
        ESP_LOGE(TAG, "Payload too large: %d bytes", payload_len);
        return 0;
    }
    
    packet[0] = BLE_PROTOCOL_HEADER_0;
    packet[1] = BLE_PROTOCOL_HEADER_1;
    packet[2] = cmd;
    
    if (payload && payload_len > 0) {
        memcpy(&packet[3], payload, payload_len);
    }
    
    ESP_LOGD(TAG, "Built packet: cmd=0x%02X, total_len=%d", cmd, total_len);
    return total_len;
}

esp_err_t ble_protocol_send_response(uint16_t conn_id, uint8_t cmd, const uint8_t *payload, uint16_t payload_len)
{
    uint8_t packet_buffer[BLE_PROTOCOL_MIN_PACKET_LEN + BLE_PROTOCOL_MAX_PAYLOAD_LEN];
    
    size_t packet_len = ble_protocol_build_packet(cmd, payload, payload_len, packet_buffer, sizeof(packet_buffer));
    if (packet_len == 0) {
        ESP_LOGE(TAG, "Failed to build response packet");
        return ESP_ERR_INVALID_ARG;
    }
    
    uint16_t notify_handle = esp_ble_get_notify_handle();
    if (notify_handle == 0) {
        ESP_LOGE(TAG, "Invalid notify handle");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = esp_ble_notify_data(conn_id, notify_handle, packet_buffer, packet_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send response: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGD(TAG, "Response sent: cmd=0x%02X, len=%d", cmd, packet_len);
    }
    
    return ret;
}
