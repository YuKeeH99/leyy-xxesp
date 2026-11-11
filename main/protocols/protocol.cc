#include "protocol.h"
#include "system_info.h"
#include "board.h"

#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <iomanip>
#include <sstream>
#include <chrono>

#define TAG "Protocol"

void Protocol::OnIncomingJson(std::function<void(const cJSON* root)> callback) {
    on_incoming_json_ = callback;
}

void Protocol::OnIncomingAudio(std::function<void(AudioStreamPacket&& packet)> callback) {
    on_incoming_audio_ = callback;
}

void Protocol::OnAudioChannelOpened(std::function<void()> callback) {
    on_audio_channel_opened_ = callback;
}

void Protocol::OnAudioChannelClosed(std::function<void()> callback) {
    on_audio_channel_closed_ = callback;
}

void Protocol::OnNetworkError(std::function<void(const std::string& message)> callback) {
    on_network_error_ = callback;
}

void Protocol::SetError(const std::string& message) {
    error_occurred_ = true;
    if (on_network_error_ != nullptr) {
        on_network_error_(message);
    }
}

void Protocol::SendAbortSpeaking(AbortReason reason) {
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"abort\"";
    if (reason == kAbortReasonWakeWordDetected) {
        message += ",\"reason\":\"wake_word_detected\"";
    }
    message += "}";
    SendText(message);
}

void Protocol::SendWakeWordDetected(const std::string& wake_word) {
    std::string json = "{\"session_id\":\"" + session_id_ + 
                      "\",\"type\":\"listen\",\"state\":\"detect\",\"text\":\"" + wake_word + "\"}";
    SendText(json);
}

void Protocol::SendStartListening(ListeningMode mode) {
    std::string message = "{\"session_id\":\"" + session_id_ + "\"";
    message += ",\"type\":\"listen\",\"state\":\"start\"";
    if (mode == kListeningModeRealtime) {
        message += ",\"mode\":\"realtime\"";
    } else if (mode == kListeningModeAutoStop) {
        message += ",\"mode\":\"auto\"";
    } else {
        message += ",\"mode\":\"manual\"";
    }
    message += "}";
    SendText(message);
}

void Protocol::SendStopListening() {
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"listen\",\"state\":\"stop\"}";
    SendText(message);
}

void Protocol::SendIotDescriptors(const std::string& descriptors) {
    cJSON* root = cJSON_Parse(descriptors.c_str());
    if (root == nullptr) {
        ESP_LOGE(TAG, "Failed to parse IoT descriptors: %s", descriptors.c_str());
        return;
    }

    if (!cJSON_IsArray(root)) {
        ESP_LOGE(TAG, "IoT descriptors should be an array");
        cJSON_Delete(root);
        return;
    }

    int arraySize = cJSON_GetArraySize(root);
    for (int i = 0; i < arraySize; ++i) {
        cJSON* descriptor = cJSON_GetArrayItem(root, i);
        if (descriptor == nullptr) {
            ESP_LOGE(TAG, "Failed to get IoT descriptor at index %d", i);
            continue;
        }

        cJSON* messageRoot = cJSON_CreateObject();
        cJSON_AddStringToObject(messageRoot, "session_id", session_id_.c_str());
        cJSON_AddStringToObject(messageRoot, "type", "iot");
        cJSON_AddBoolToObject(messageRoot, "update", true);

        cJSON* descriptorArray = cJSON_CreateArray();
        cJSON_AddItemToArray(descriptorArray, cJSON_Duplicate(descriptor, 1));
        cJSON_AddItemToObject(messageRoot, "descriptors", descriptorArray);

        char* message = cJSON_PrintUnformatted(messageRoot);
        if (message == nullptr) {
            ESP_LOGE(TAG, "Failed to print JSON message for IoT descriptor at index %d", i);
            cJSON_Delete(messageRoot);
            continue;
        }

        SendText(std::string(message));
        cJSON_free(message);
        cJSON_Delete(messageRoot);
    }

    cJSON_Delete(root);
}

void Protocol::SendIotStates(const std::string& states) {
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"iot\",\"update\":true,\"states\":" + states + "}";
    SendText(message);
}

void Protocol::SendMcpMessage(const std::string& payload) {
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"mcp\",\"payload\":" + payload + "}";
    SendText(message);
#ifdef DEBUG_VERSON
    ESP_LOGE(TAG, "MCP: %s", message.c_str());
#endif
}

bool Protocol::IsTimeout() const {
    const int kTimeoutSeconds = 120;
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_incoming_time_);
    bool timeout = duration.count() > kTimeoutSeconds;
    if (timeout) {
        ESP_LOGE(TAG, "Channel timeout %ld seconds", (long)duration.count());
    }
    return timeout;
}


void Protocol::SendGreetingRequest(const std::string& method, const std::string& greeting_type) {
    ESP_LOGI(TAG, "SendGreetingRequest method: %s, greeting_type: %s", method.c_str(), greeting_type.c_str());

    // 生成随机的trace_id和request_id
    auto generate_random_id = [](const std::string& prefix) -> std::string {
        uint32_t random1 = esp_random();
        uint32_t random2 = esp_random();
        std::stringstream ss;
        ss << prefix << "_" << std::hex << random1 << random2;
        return ss.str();
    };
    
    std::string trace_id = generate_random_id("trace_id");
    std::string request_id = generate_random_id("request_id");
    
    // 获取设备信息
    std::string device_id = SystemInfo::GetMacAddress();
    
    // 获取固件版本
    const esp_app_desc_t* app_desc = esp_app_get_description();
    std::string version = app_desc->version;
    
    // 获取设备名称
    std::string device_name = BOARD_NAME;
    
    // 获取当前时间戳（毫秒）
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // 构建JSON消息
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "session_id", session_id_.c_str());
    cJSON_AddStringToObject(root, "type", "cml_message_bridge");
    cJSON_AddStringToObject(root, "api_version", "v1.0");
    
    // 构建payload对象
    cJSON* payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(payload, "id", 1);
    cJSON_AddStringToObject(payload, "destination", "cml_server");
    cJSON_AddStringToObject(payload, "method", method.c_str());
    cJSON_AddStringToObject(payload, "trace_id", trace_id.c_str());
    cJSON_AddStringToObject(payload, "request_id", request_id.c_str());
    
    // 构建params对象
    cJSON* params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "type", greeting_type.c_str());
    cJSON_AddItemToObject(payload, "params", params);
    
    // 构建device_info对象
    cJSON* device_info = cJSON_CreateObject();
    cJSON_AddStringToObject(device_info, "device_id", device_id.c_str());
    cJSON_AddStringToObject(device_info, "version", version.c_str());
    cJSON_AddStringToObject(device_info, "name", device_name.c_str());
    cJSON_AddItemToObject(payload, "device_info", device_info);
    
    cJSON_AddNumberToObject(payload, "timestamp", now);
    
    cJSON_AddItemToObject(root, "payload", payload);
    
    // 转换为字符串并发送
    char* json_string = cJSON_PrintUnformatted(root);
    if (json_string != nullptr) {
        SendText(std::string(json_string));
#ifdef DEBUG_VERSON
        ESP_LOGI(TAG, "Greeting request sent: %s", json_string);
#endif
        cJSON_free(json_string);
    } else {
        ESP_LOGE(TAG, "Failed to create greeting request JSON");
    }
    
    cJSON_Delete(root);
}

void Protocol::SendHeartbeat() {
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"heartbeat\",\"timestamp\":" + 
                         std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch()).count()) + "}";
    SendText(message);
    ESP_LOGI(TAG, "Heartbeat sent");

    last_incoming_time_ = std::chrono::steady_clock::now(); // 添加这行
}
