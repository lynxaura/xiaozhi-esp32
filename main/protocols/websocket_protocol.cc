#include "websocket_protocol.h"
#include "board.h"
#include "system_info.h"
#include "application.h"
#include "settings.h"
#include "wifi_station.h"

#include <cstring>
#include <cJSON.h>
#include <esp_log.h>
#include <arpa/inet.h>
#include <esp_http_client.h>
#include "assets/lang_config.h"

#define TAG "WS"

WebsocketProtocol::WebsocketProtocol() {
    event_group_handle_ = xEventGroupCreate();
}

WebsocketProtocol::~WebsocketProtocol() {
    StopHeartbeat();
    vEventGroupDelete(event_group_handle_);
}

bool WebsocketProtocol::Start() {
    // Only connect to server when audio channel is needed
    return true;
}

bool WebsocketProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet) {
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        return false;
    }

    if (version_ == 2) {
        std::string serialized;
        serialized.resize(sizeof(BinaryProtocol2) + packet->payload.size());
        auto bp2 = (BinaryProtocol2*)serialized.data();
        bp2->version = htons(version_);
        bp2->type = 0;
        bp2->reserved = 0;
        bp2->timestamp = htonl(packet->timestamp);
        bp2->payload_size = htonl(packet->payload.size());
        memcpy(bp2->payload, packet->payload.data(), packet->payload.size());

        return websocket_->Send(serialized.data(), serialized.size(), true);
    } else if (version_ == 3) {
        std::string serialized;
        serialized.resize(sizeof(BinaryProtocol3) + packet->payload.size());
        auto bp3 = (BinaryProtocol3*)serialized.data();
        bp3->type = 0;
        bp3->reserved = 0;
        bp3->payload_size = htons(packet->payload.size());
        memcpy(bp3->payload, packet->payload.data(), packet->payload.size());

        return websocket_->Send(serialized.data(), serialized.size(), true);
    } else {
        return websocket_->Send(packet->payload.data(), packet->payload.size(), true);
    }
}

bool WebsocketProtocol::SendText(const std::string& text) {
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        return false;
    }

    if (!websocket_->Send(text)) {
        ESP_LOGE(TAG, "Failed to send text: %s", text.c_str());
        SetError(Lang::Strings::SERVER_ERROR);
        return false;
    }

    return true;
}

bool WebsocketProtocol::IsAudioChannelOpened() const {
    return websocket_ != nullptr && websocket_->IsConnected() && !error_occurred_ && !IsTimeout();
}

void WebsocketProtocol::CloseAudioChannel() {
    StopHeartbeat();
    websocket_.reset();
}

bool WebsocketProtocol::OpenAudioChannel() {
    Settings settings("websocket", false);
    std::string url = settings.GetString("url");
    std::string token = settings.GetString("token");
    int version = settings.GetInt("version");
    if (version != 0) {
        version_ = version;
    }

    // Load heartbeat configuration
    int heartbeat_interval = settings.GetInt("heartbeat_interval", 60);
    heartbeat_interval_ms_ = heartbeat_interval * 1000; // Convert to milliseconds

    error_occurred_ = false;

    auto network = Board::GetInstance().GetNetwork();
    websocket_ = network->CreateWebSocket(1);
    if (websocket_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create websocket");
        return false;
    }

    if (!token.empty()) {
        // If token not has a space, add "Bearer " prefix
        if (token.find(" ") == std::string::npos) {
            token = "Bearer " + token;
        }
        websocket_->SetHeader("Authorization", token.c_str());
    }
    websocket_->SetHeader("Protocol-Version", std::to_string(version_).c_str());
    websocket_->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    websocket_->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());

    websocket_->OnData([this](const char* data, size_t len, bool binary) {
        if (binary) {
            if (on_incoming_audio_ != nullptr) {
                if (version_ == 2) {
                    BinaryProtocol2* bp2 = (BinaryProtocol2*)data;
                    bp2->version = ntohs(bp2->version);
                    bp2->type = ntohs(bp2->type);
                    bp2->timestamp = ntohl(bp2->timestamp);
                    bp2->payload_size = ntohl(bp2->payload_size);
                    auto payload = (uint8_t*)bp2->payload;
                    on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                        .sample_rate = server_sample_rate_,
                        .frame_duration = server_frame_duration_,
                        .timestamp = bp2->timestamp,
                        .payload = std::vector<uint8_t>(payload, payload + bp2->payload_size)
                    }));
                } else if (version_ == 3) {
                    BinaryProtocol3* bp3 = (BinaryProtocol3*)data;
                    bp3->type = bp3->type;
                    bp3->payload_size = ntohs(bp3->payload_size);
                    auto payload = (uint8_t*)bp3->payload;
                    on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                        .sample_rate = server_sample_rate_,
                        .frame_duration = server_frame_duration_,
                        .timestamp = 0,
                        .payload = std::vector<uint8_t>(payload, payload + bp3->payload_size)
                    }));
                } else {
                    on_incoming_audio_(std::make_unique<AudioStreamPacket>(AudioStreamPacket{
                        .sample_rate = server_sample_rate_,
                        .frame_duration = server_frame_duration_,
                        .timestamp = 0,
                        .payload = std::vector<uint8_t>((uint8_t*)data, (uint8_t*)data + len)
                    }));
                }
            }
        } else {
            // Parse JSON data
            auto root = cJSON_Parse(data);
            auto type = cJSON_GetObjectItem(root, "type");
            if (cJSON_IsString(type)) {
                if (strcmp(type->valuestring, "hello") == 0) {
                    ParseServerHello(root);
                } else {
                    if (on_incoming_json_ != nullptr) {
                        on_incoming_json_(root);
                    }
                }
            } else {
                ESP_LOGE(TAG, "Missing message type, data: %s", data);
            }
            cJSON_Delete(root);
        }
        last_incoming_time_ = std::chrono::steady_clock::now();
    });

    websocket_->OnDisconnected([this]() {
        ESP_LOGI(TAG, "Websocket disconnected");
        if (on_audio_channel_closed_ != nullptr) {
            on_audio_channel_closed_();
        }
    });

    ESP_LOGI(TAG, "Connecting to websocket server: %s with version: %d", url.c_str(), version_);
    if (!websocket_->Connect(url.c_str())) {
        ESP_LOGE(TAG, "Failed to connect to websocket server");
        SetError(Lang::Strings::SERVER_NOT_CONNECTED);
        return false;
    }

    // Send hello message to describe the client
    auto message = GetHelloMessage();
    if (!SendText(message)) {
        return false;
    }

    // Wait for server hello
    EventBits_t bits = xEventGroupWaitBits(event_group_handle_, WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT, pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT)) {
        ESP_LOGE(TAG, "Failed to receive server hello");
        SetError(Lang::Strings::SERVER_TIMEOUT);
        return false;
    }

    if (on_audio_channel_opened_ != nullptr) {
        on_audio_channel_opened_();
    }

    // Start heartbeat after successful connection
    StartHeartbeat();

    return true;
}

std::string WebsocketProtocol::GetHelloMessage() {
    // keys: message type, version, audio_params (format, sample_rate, channels)
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", version_);
    cJSON* features = cJSON_CreateObject();
#if CONFIG_USE_SERVER_AEC
    cJSON_AddBoolToObject(features, "aec", true);
#endif
    cJSON_AddBoolToObject(features, "mcp", true);
    cJSON_AddItemToObject(root, "features", features);
    cJSON_AddStringToObject(root, "transport", "websocket");
    cJSON* audio_params = cJSON_CreateObject();
    cJSON_AddStringToObject(audio_params, "format", "opus");
    cJSON_AddNumberToObject(audio_params, "sample_rate", 16000);
    cJSON_AddNumberToObject(audio_params, "channels", 1);
    cJSON_AddNumberToObject(audio_params, "frame_duration", OPUS_FRAME_DURATION_MS);
    cJSON_AddItemToObject(root, "audio_params", audio_params);
    auto json_str = cJSON_PrintUnformatted(root);
    std::string message(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    return message;
}

void WebsocketProtocol::ParseServerHello(const cJSON* root) {
    auto transport = cJSON_GetObjectItem(root, "transport");
    if (transport == nullptr || strcmp(transport->valuestring, "websocket") != 0) {
        ESP_LOGE(TAG, "Unsupported transport: %s", transport->valuestring);
        return;
    }

    auto session_id = cJSON_GetObjectItem(root, "session_id");
    if (cJSON_IsString(session_id)) {
        session_id_ = session_id->valuestring;
        ESP_LOGI(TAG, "Session ID: %s", session_id_.c_str());
    }

    auto audio_params = cJSON_GetObjectItem(root, "audio_params");
    if (cJSON_IsObject(audio_params)) {
        auto sample_rate = cJSON_GetObjectItem(audio_params, "sample_rate");
        if (cJSON_IsNumber(sample_rate)) {
            server_sample_rate_ = sample_rate->valueint;
        }
        auto frame_duration = cJSON_GetObjectItem(audio_params, "frame_duration");
        if (cJSON_IsNumber(frame_duration)) {
            server_frame_duration_ = frame_duration->valueint;
        }
    }

    xEventGroupSetBits(event_group_handle_, WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT);
}

// ============================================================================
// Heartbeat Implementation
// ============================================================================

void WebsocketProtocol::StartHeartbeat() {
    ESP_LOGI(TAG, "Starting heartbeat with interval: %d seconds", heartbeat_interval_ms_ / 1000);

    // Create WebSocket PING timer (every 30 seconds by default)
    esp_timer_create_args_t ping_timer_args = {
        .callback = &WebsocketProtocol::PingTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "ws_ping",
        .skip_unhandled_events = false
    };

    if (ping_timer_ == nullptr) {
        ESP_ERROR_CHECK(esp_timer_create(&ping_timer_args, &ping_timer_));
    }

    last_pong_time_ = esp_timer_get_time();
    missed_pong_count_ = 0;
    ESP_ERROR_CHECK(esp_timer_start_periodic(ping_timer_, ping_interval_ms_ * 1000)); // Convert to microseconds

    // Create HTTP heartbeat timer (configurable, default 60 seconds)
    esp_timer_create_args_t http_heartbeat_timer_args = {
        .callback = &WebsocketProtocol::HttpHeartbeatTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "http_heartbeat",
        .skip_unhandled_events = false
    };

    if (http_heartbeat_timer_ == nullptr) {
        ESP_ERROR_CHECK(esp_timer_create(&http_heartbeat_timer_args, &http_heartbeat_timer_));
    }

    ESP_ERROR_CHECK(esp_timer_start_periodic(http_heartbeat_timer_, heartbeat_interval_ms_ * 1000)); // Convert to microseconds

    // Send first heartbeat immediately
    SendHttpHeartbeat();
}

void WebsocketProtocol::StopHeartbeat() {
    if (ping_timer_ != nullptr) {
        esp_timer_stop(ping_timer_);
        esp_timer_delete(ping_timer_);
        ping_timer_ = nullptr;
    }

    if (http_heartbeat_timer_ != nullptr) {
        esp_timer_stop(http_heartbeat_timer_);
        esp_timer_delete(http_heartbeat_timer_);
        http_heartbeat_timer_ = nullptr;
    }

    ESP_LOGI(TAG, "Heartbeat stopped");
}

void WebsocketProtocol::PingTimerCallback(void* arg) {
    auto* protocol = static_cast<WebsocketProtocol*>(arg);
    protocol->SendWebSocketPing();
}

void WebsocketProtocol::HttpHeartbeatTimerCallback(void* arg) {
    auto* protocol = static_cast<WebsocketProtocol*>(arg);
    protocol->SendHttpHeartbeat();
}

void WebsocketProtocol::SendWebSocketPing() {
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        ESP_LOGW(TAG, "WebSocket not connected, skipping PING");
        return;
    }

    // Check if last PONG was received in time
    int64_t now = esp_timer_get_time();
    int64_t time_since_pong = (now - last_pong_time_) / 1000; // Convert to milliseconds

    if (time_since_pong > ping_timeout_ms_ + ping_interval_ms_) {
        missed_pong_count_++;
        ESP_LOGW(TAG, "PONG timeout! Missed count: %d/%d", missed_pong_count_, MAX_MISSED_PONG);

        if (missed_pong_count_ >= MAX_MISSED_PONG) {
            ESP_LOGE(TAG, "Too many missed PONGs, triggering reconnection");
            SetError(Lang::Strings::SERVER_TIMEOUT);
            if (on_audio_channel_closed_ != nullptr) {
                on_audio_channel_closed_();
            }
            return;
        }
    } else {
        missed_pong_count_ = 0; // Reset on successful PONG
    }

    // Send PING frame
    websocket_->Ping();
    ESP_LOGD(TAG, "WebSocket PING sent");

    // Note: We assume PONG is received immediately since WebSocket class
    // handles PONG internally. For now, we update last_pong_time_ here.
    // In a more robust implementation, we would have a PONG callback.
    last_pong_time_ = now;
}

int WebsocketProtocol::GetNetworkLevel() const {
    auto& board = Board::GetInstance();
    auto network = board.GetNetwork();

    // For WiFi boards, get RSSI and convert to level (1-5)
    // RSSI typically ranges from -100 (worst) to 0 (best)
    int8_t rssi = -100; // Default worst signal

    // Try to get RSSI from WiFi station
    auto& wifi = WifiStation::GetInstance();
    if (wifi.IsConnected()) {
        rssi = wifi.GetRssi();
    }

    // Convert RSSI to level (1-5):
    // Level 5 (excellent): RSSI >= -50 dBm
    // Level 4 (good):      RSSI >= -60 dBm
    // Level 3 (fair):      RSSI >= -70 dBm
    // Level 2 (weak):      RSSI >= -80 dBm
    // Level 1 (poor):      RSSI <  -80 dBm

    int level = 1;
    if (rssi >= -50) {
        level = 5;
    } else if (rssi >= -60) {
        level = 4;
    } else if (rssi >= -70) {
        level = 3;
    } else if (rssi >= -80) {
        level = 2;
    }

    return level;
}

void WebsocketProtocol::SendHttpHeartbeat() {
    Settings settings("websocket", false);
    std::string heartbeat_url = settings.GetString("heartbeat_url");

    if (heartbeat_url.empty()) {
        heartbeat_url = "http://115.190.7.5:9500/loomhart-server/api/v1/device_self/heartbeat";
    }

    // Get device status
    auto& board = Board::GetInstance();
    int battery_level = 0;
    bool charging = false;
    bool discharging = false;

    if (!board.GetBatteryLevel(battery_level, charging, discharging)) {
        battery_level = 100; // Default to 100% if battery info not available
    }

    int network_level = GetNetworkLevel();

    // Get MAC address as serial number
    std::string serial_number = SystemInfo::GetMacAddress();

    // Build JSON payload
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "serial_number", serial_number.c_str());
    cJSON_AddNumberToObject(root, "battery", battery_level);
    cJSON_AddNumberToObject(root, "network_level", network_level);

    char* json_str = cJSON_PrintUnformatted(root);
    std::string payload(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Sending HTTP heartbeat to %s: %s", heartbeat_url.c_str(), payload.c_str());

    // Configure HTTP client
    esp_http_client_config_t config = {};
    config.url = heartbeat_url.c_str();
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 5000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client for heartbeat");
        return;
    }

    // Set headers
    esp_http_client_set_header(client, "Content-Type", "application/json");

    // Set POST data
    esp_http_client_set_post_field(client, payload.c_str(), payload.length());

    // Perform HTTP request
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);

        ESP_LOGI(TAG, "HTTP heartbeat response: status=%d, content_length=%d", status_code, content_length);

        if (status_code == 200) {
            // Read response to check for configuration updates
            char response_buffer[512];
            int read_len = esp_http_client_read(client, response_buffer, sizeof(response_buffer) - 1);
            if (read_len > 0) {
                response_buffer[read_len] = '\0';
                ESP_LOGD(TAG, "Heartbeat response: %s", response_buffer);

                // Parse response to check for heartbeat_interval updates
                cJSON* response_json = cJSON_Parse(response_buffer);
                if (response_json != nullptr) {
                    cJSON* config_item = cJSON_GetObjectItem(response_json, "heartbeat_interval");
                    if (cJSON_IsNumber(config_item) && config_item->valueint > 0) {
                        int new_interval_ms = config_item->valueint * 1000;
                        if (new_interval_ms != heartbeat_interval_ms_) {
                            ESP_LOGI(TAG, "Updating heartbeat interval from %d to %d seconds",
                                     heartbeat_interval_ms_ / 1000, new_interval_ms / 1000);
                            heartbeat_interval_ms_ = new_interval_ms;

                            // Restart HTTP heartbeat timer with new interval
                            if (http_heartbeat_timer_ != nullptr) {
                                esp_timer_stop(http_heartbeat_timer_);
                                esp_timer_start_periodic(http_heartbeat_timer_, heartbeat_interval_ms_ * 1000);
                            }
                        }
                    }
                    cJSON_Delete(response_json);
                }
            }
        } else {
            ESP_LOGW(TAG, "HTTP heartbeat failed with status: %d", status_code);
        }
    } else {
        ESP_LOGE(TAG, "HTTP heartbeat request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}
