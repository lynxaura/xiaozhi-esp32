#include "device_activation.h"
#include "system_info.h"
#include "settings.h"

#include <esp_log.h>
#include <esp_http_client.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "DeviceActivation"

// 静态成员初始化
const char* DeviceActivation::ACTIVATE_URL =
    "http://115.190.7.5:9500/loomhart-server/api/v1/device_self/activate";
const char* DeviceActivation::NVS_NAMESPACE = "device";
const char* DeviceActivation::NVS_KEY_ACTIVATED = "activated";

bool DeviceActivation::CheckAndActivate() {
    // 检查是否已激活
    if (IsActivated()) {
        ESP_LOGI(TAG, "Device already activated, skipping activation");
        return true;
    }

    ESP_LOGI(TAG, "Device not activated, starting activation process...");

    // 执行激活（带重试）
    bool success = PerformActivation();

    if (success) {
        ESP_LOGI(TAG, "Device activation completed successfully");
    } else {
        ESP_LOGE(TAG, "Device activation failed after all retry attempts");
    }

    return success;
}

bool DeviceActivation::IsActivated() const {
    Settings settings(NVS_NAMESPACE, false);
    return settings.GetBool(NVS_KEY_ACTIVATED, false);
}

void DeviceActivation::ResetActivationStatus() {
    Settings settings(NVS_NAMESPACE, true);
    settings.EraseKey(NVS_KEY_ACTIVATED);
    ESP_LOGI(TAG, "Device activation status has been reset");
}

bool DeviceActivation::PerformActivation() {
    // 获取设备 MAC 地址作为序列号
    std::string serial_number = SystemInfo::GetMacAddress();

    if (serial_number.empty()) {
        ESP_LOGE(TAG, "Failed to get MAC address for activation");
        return false;
    }

    ESP_LOGI(TAG, "Activating device with serial_number: %s", serial_number.c_str());

    // 重试机制
    for (int attempt = 1; attempt <= MAX_RETRY_COUNT; attempt++) {
        ESP_LOGI(TAG, "Device activation attempt %d/%d", attempt, MAX_RETRY_COUNT);

        if (ActivateOnce(serial_number)) {
            // 激活成功，保存标志到 NVS
            Settings settings(NVS_NAMESPACE, true);
            settings.SetBool(NVS_KEY_ACTIVATED, true);
            ESP_LOGI(TAG, "Device activated successfully on attempt %d", attempt);
            return true;
        }

        // 如果不是最后一次尝试，等待后重试
        if (attempt < MAX_RETRY_COUNT) {
            ESP_LOGW(TAG, "Activation failed, retrying in %d ms...", RETRY_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
        }
    }

    ESP_LOGE(TAG, "Device activation failed after %d attempts", MAX_RETRY_COUNT);
    return false;
}

bool DeviceActivation::ActivateOnce(const std::string& serial_number) {
    // 构造 JSON payload
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return false;
    }

    cJSON_AddStringToObject(root, "serial_number", serial_number.c_str());

    char* json_str = cJSON_PrintUnformatted(root);
    if (json_str == nullptr) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        cJSON_Delete(root);
        return false;
    }

    std::string payload(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);

    ESP_LOGD(TAG, "Activation request payload: %s", payload.c_str());

    // 配置 HTTP 客户端（使用与心跳相同的方式）
    esp_http_client_config_t config = {};
    config.url = ACTIVATE_URL;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 5000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return false;
    }

    // 设置请求头
    esp_http_client_set_header(client, "Content-Type", "application/json");

    bool success = false;

    // 使用 open/write/fetch/read 流程，避免 perform 之后无法读取响应体的问题
    esp_err_t err = esp_http_client_open(client, payload.length());
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int to_write = payload.length();
    int written = esp_http_client_write(client, payload.c_str(), to_write);
    if (written < 0 || written != to_write) {
        ESP_LOGE(TAG, "Failed to write POST data: written=%d expected=%d", written, to_write);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    int64_t content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP activation response: status=%d, content_length=%lld", status_code, (long long)content_length);

    if (status_code == 200) {
        std::string response;
        response.reserve(512);
        char buffer[256];
        int total_read = 0;

        while (true) {
            int r = esp_http_client_read(client, buffer, sizeof(buffer) - 1);
            if (r <= 0) break;
            buffer[r] = '\0';
            response.append(buffer, r);
            total_read += r;
            if (content_length > 0 && total_read >= content_length) break;
        }

        ESP_LOGI(TAG, "Read %d bytes from response (content_length=%lld)", total_read, (long long)content_length);

        if (!response.empty()) {
            ESP_LOGI(TAG, "Activation response: %s", response.c_str());
            cJSON* response_json = cJSON_Parse(response.c_str());
            if (response_json != nullptr) {
                cJSON* code = cJSON_GetObjectItem(response_json, "code");
                if (cJSON_IsNumber(code)) {
                    ESP_LOGI(TAG, "Server response code: %d", code->valueint);
                    if (code->valueint == 0) {
                        ESP_LOGI(TAG, "Activation request successful");
                        success = true;
                        cJSON* data = cJSON_GetObjectItem(response_json, "data");
                        if (cJSON_IsObject(data)) {
                            cJSON* device = cJSON_GetObjectItem(data, "device");
                            if (cJSON_IsObject(device)) {
                                cJSON* device_id = cJSON_GetObjectItem(device, "id");
                                if (cJSON_IsString(device_id)) {
                                    ESP_LOGI(TAG, "Server assigned device_id: %s", device_id->valuestring);
                                }
                            }
                        }
                    } else {
                        cJSON* message = cJSON_GetObjectItem(response_json, "message");
                        if (cJSON_IsString(message)) {
                            ESP_LOGW(TAG, "Activation failed: %s", message->valuestring);
                        } else {
                            ESP_LOGW(TAG, "Activation failed with code: %d", code->valueint);
                        }
                    }
                } else {
                    ESP_LOGE(TAG, "Response JSON does not contain valid 'code' field");
                }
                cJSON_Delete(response_json);
            } else {
                ESP_LOGE(TAG, "Failed to parse activation response JSON: %s", response.c_str());
            }
        } else {
            ESP_LOGE(TAG, "Read 0 bytes from response (empty response body)");
        }
    } else {
        ESP_LOGW(TAG, "Activation HTTP request failed with status: %d", status_code);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return success;
}
