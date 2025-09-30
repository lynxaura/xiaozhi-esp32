#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "MemCheck";

void print_memory_info() {
    // 获取各种内存类型的信息
    size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    size_t internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    size_t spiram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t spiram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t spiram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    size_t dma_total = heap_caps_get_total_size(MALLOC_CAP_DMA);
    size_t dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA);

    // FreeRTOS任务栈信息
    UBaseType_t free_stack = uxTaskGetStackHighWaterMark(NULL);

    ESP_LOGI(TAG, "=== ESP32-S3 Memory Analysis ===");
    ESP_LOGI(TAG, "📊 Total Heap Memory:");
    ESP_LOGI(TAG, "  Total: %zu KB, Free: %zu KB, Used: %zu KB (%.1f%%)",
             total_heap / 1024, free_heap / 1024, (total_heap - free_heap) / 1024,
             (float)(total_heap - free_heap) * 100.0f / total_heap);
    ESP_LOGI(TAG, "  Largest free block: %zu KB", largest_free_block / 1024);

    ESP_LOGI(TAG, "🏠 Internal SRAM (MALLOC_CAP_INTERNAL):");
    ESP_LOGI(TAG, "  Total: %zu KB, Free: %zu KB, Used: %zu KB (%.1f%%)",
             internal_total / 1024, internal_free / 1024, (internal_total - internal_free) / 1024,
             (float)(internal_total - internal_free) * 100.0f / internal_total);
    ESP_LOGI(TAG, "  Largest free block: %zu KB", internal_largest / 1024);

    ESP_LOGI(TAG, "💾 PSRAM (MALLOC_CAP_SPIRAM):");
    ESP_LOGI(TAG, "  Total: %zu KB, Free: %zu KB, Used: %zu KB (%.1f%%)",
             spiram_total / 1024, spiram_free / 1024, (spiram_total - spiram_free) / 1024,
             spiram_total > 0 ? (float)(spiram_total - spiram_free) * 100.0f / spiram_total : 0.0f);
    ESP_LOGI(TAG, "  Largest free block: %zu KB", spiram_largest / 1024);

    ESP_LOGI(TAG, "🚀 DMA Memory (MALLOC_CAP_DMA):");
    ESP_LOGI(TAG, "  Total: %zu KB, Free: %zu KB, Used: %zu KB (%.1f%%)",
             dma_total / 1024, dma_free / 1024, (dma_total - dma_free) / 1024,
             (float)(dma_total - dma_free) * 100.0f / dma_total);

    ESP_LOGI(TAG, "📚 Current Task Stack:");
    ESP_LOGI(TAG, "  Free stack words: %u (≈%u bytes)", free_stack, free_stack * 4);

    ESP_LOGI(TAG, "⚙️ System Configuration:");
    ESP_LOGI(TAG, "  Main task stack: %d bytes", CONFIG_ESP_MAIN_TASK_STACK_SIZE);
    ESP_LOGI(TAG, "  Internal reserved: %d KB", CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL / 1024);
    ESP_LOGI(TAG, "  Always internal: %d bytes", CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);
}

// 测试栈使用情况
void test_stack_usage() {
    ESP_LOGI(TAG, "=== Stack Usage Test ===");

    // 测试递归调用的栈使用
    UBaseType_t initial_stack = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "Initial free stack: %u words (%u bytes)", initial_stack, initial_stack * 4);

    // 创建一些栈变量来测试使用情况
    char large_buffer[1024];
    memset(large_buffer, 0, sizeof(large_buffer));

    UBaseType_t after_allocation = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "After 1KB local allocation: %u words (%u bytes)",
             after_allocation, after_allocation * 4);
    ESP_LOGI(TAG, "Stack used for local vars: %d bytes",
             (initial_stack - after_allocation) * 4);
}

// 测试堆分配
void test_heap_allocation() {
    ESP_LOGI(TAG, "=== Heap Allocation Test ===");

    print_memory_info();

    ESP_LOGI(TAG, "Testing large allocations...");

    // 测试PSRAM分配
    void* psram_ptr = heap_caps_malloc(150 * 1024, MALLOC_CAP_SPIRAM);
    if (psram_ptr) {
        ESP_LOGI(TAG, "✅ Successfully allocated 150KB in PSRAM");
        ESP_LOGI(TAG, "PSRAM free after allocation: %zu KB",
                 heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
        heap_caps_free(psram_ptr);
        ESP_LOGI(TAG, "✅ Freed PSRAM allocation");
    } else {
        ESP_LOGE(TAG, "❌ Failed to allocate 150KB in PSRAM");
    }

    // 测试内部RAM分配
    void* internal_ptr = heap_caps_malloc(32 * 1024, MALLOC_CAP_INTERNAL);
    if (internal_ptr) {
        ESP_LOGI(TAG, "✅ Successfully allocated 32KB in internal RAM");
        ESP_LOGI(TAG, "Internal free after allocation: %zu KB",
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
        heap_caps_free(internal_ptr);
        ESP_LOGI(TAG, "✅ Freed internal RAM allocation");
    } else {
        ESP_LOGE(TAG, "❌ Failed to allocate 32KB in internal RAM");
    }
}

extern "C" void app_main() {
    ESP_LOGI(TAG, "Starting ESP32-S3 Memory Analysis...");

    // 基本内存信息
    print_memory_info();

    ESP_LOGI(TAG, "");

    // 栈使用测试
    test_stack_usage();

    ESP_LOGI(TAG, "");

    // 堆分配测试
    test_heap_allocation();

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Memory analysis complete!");
}