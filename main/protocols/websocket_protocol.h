#ifndef _WEBSOCKET_PROTOCOL_H_
#define _WEBSOCKET_PROTOCOL_H_


#include "protocol.h"

#include <web_socket.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_timer.h>

#define WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT (1 << 0)

class WebsocketProtocol : public Protocol {
public:
    WebsocketProtocol();
    ~WebsocketProtocol();

    bool Start() override;
    bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel() override;
    bool IsAudioChannelOpened() const override;

private:
    EventGroupHandle_t event_group_handle_;
    std::unique_ptr<WebSocket> websocket_;
    int version_ = 1;

    // Heartbeat related members
    esp_timer_handle_t ping_timer_ = nullptr;
    esp_timer_handle_t http_heartbeat_timer_ = nullptr;
    int64_t last_pong_time_ = 0;
    int heartbeat_interval_ms_ = 60000; // Default 60 seconds
    int ping_interval_ms_ = 30000;      // PING every 30 seconds
    int ping_timeout_ms_ = 10000;       // PONG timeout 10 seconds
    int missed_pong_count_ = 0;
    static const int MAX_MISSED_PONG = 3;

    void ParseServerHello(const cJSON* root);
    bool SendText(const std::string& text) override;
    std::string GetHelloMessage();

    // Heartbeat methods
    void StartHeartbeat();
    void StopHeartbeat();
    void SendWebSocketPing();
    void SendHttpHeartbeat();
    int GetNetworkLevel() const;

    static void PingTimerCallback(void* arg);
    static void HttpHeartbeatTimerCallback(void* arg);
};

#endif
