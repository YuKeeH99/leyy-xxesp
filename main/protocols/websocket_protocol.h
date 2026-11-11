#ifndef _WEBSOCKET_PROTOCOL_H_
#define _WEBSOCKET_PROTOCOL_H_

#include "protocol.h"

#include <web_socket.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/timers.h>  // 添加定时器头文件

#define WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT (1 << 0)
#define WEBSOCKET_PROTOCOL_CONNECT_TIMEOUT_EVENT (1 << 1)  // 添加超时事件

class WebsocketProtocol : public Protocol {
public:
    WebsocketProtocol();
    ~WebsocketProtocol();

    bool Start() override;
    bool SendAudio(const AudioStreamPacket& packet) override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel() override;
    bool IsAudioChannelOpened() const override;

private:
    EventGroupHandle_t event_group_handle_;
    WebSocket* websocket_ = nullptr;
    int version_ = 1;
    TimerHandle_t connect_timer_ = nullptr;  // 添加连接超时定时器

    void ParseServerHello(const cJSON* root);
    bool SendText(const std::string& text) override;
    std::string GetHelloMessage();
    
    // 添加定时器回调函数
    static void ConnectTimeoutCallback(TimerHandle_t timer);
};

#endif
