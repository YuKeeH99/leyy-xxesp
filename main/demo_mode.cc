#include "demo_mode.h"
#include "application.h"
#include "board.h"
#include "boards/common/dual_network_board.h"  // 添加这行
#include "display/lcd_display.h"
#include "oled_emotion_task.h"
#include "assets/lang_config.h"
#include <sstream>
#include <esp_log.h>

static const char* TAG = "DemoMode";

// 实现析构函数（不能是 = default）
DemoMode::~DemoMode() {
    // 清理资源
}

bool DemoMode::HandleProtocolMessage(const cJSON* message_json, Application* app) {
    if (!message_json || !app) {
        ESP_LOGE(TAG, "Invalid parameters for HandleProtocolMessage");
        return false;
    }
    ESP_LOGI(TAG, "HandleProtocolMessage");
    
    DemoProtocolMessage msg;
    if (!ParseProtocolMessage(message_json, msg)) {
        ESP_LOGE(TAG, "Failed to parse protocol message");
        return false;
    }
    ESP_LOGI(TAG, "msg.type: %s msg.state=%s", msg.type.c_str(),msg.state.c_str());
    // 验证消息类型
    if (msg.type != "demo_mode") {
        ESP_LOGE(TAG, "Invalid message type: %s", msg.type.c_str());
        return false;
    }
    
    app_ = app;
    
    // 根据状态处理消息
    if (msg.state == "start") {
        HandleStartMessage(msg);
    } else if (msg.state == "text") {
        HandleTextMessage(msg);
    } else if (msg.state == "emoji") {
        HandleEmojiMessage(msg);
    } else if (msg.state == "image") {  // 新增image处理
        HandleQRCodeMessage(msg);  // 复用二维码处理逻辑
    }else if (msg.state == "stop") {
        HandleEndMessage(msg);
    } else {
        ESP_LOGW(TAG, "Unknown demo state: %s", msg.state.c_str());
        return false;
    }
    
    return true;
}

bool DemoMode::ParseProtocolMessage(const cJSON* message_json, DemoProtocolMessage& msg) {
    // 解析type字段
    cJSON* type_item = cJSON_GetObjectItem(message_json, "type");
    if (cJSON_IsString(type_item)) {
        msg.type = type_item->valuestring;
    }
    
    // 解析state字段（兼容旧格式）
    cJSON* state_item = cJSON_GetObjectItem(message_json, "state");
    if (cJSON_IsString(state_item)) {
        msg.state = state_item->valuestring;
    }
    
    // 解析action字段（新格式）
    cJSON* action_item = cJSON_GetObjectItem(message_json, "action");
    if (cJSON_IsString(action_item)) {
        msg.state = action_item->valuestring;  // 将action映射到state字段
    }
    
    // 解析content字段（新格式的文本内容）
    cJSON* content_item = cJSON_GetObjectItem(message_json, "content");
    if (cJSON_IsString(content_item)) {
        msg.content = content_item->valuestring;
    }
    
    // 解析session_id字段
    cJSON* session_id_item = cJSON_GetObjectItem(message_json, "session_id");
    if (cJSON_IsString(session_id_item)) {
        msg.session_id = session_id_item->valuestring;
    }
    
    // 解析img_url字段
    cJSON* img_url_item = cJSON_GetObjectItem(message_json, "img_url");
    if (cJSON_IsString(img_url_item)) {
        msg.img_url = img_url_item->valuestring;
    }
    
    // 解析emoji字段
    cJSON* emoji_item = cJSON_GetObjectItem(message_json, "emoji");
    if (cJSON_IsString(emoji_item)) {
        msg.emoji = emoji_item->valuestring;
    }
    
    // 解析duration字段 - 修复：支持数字和字符串类型
    cJSON* duration_item = cJSON_GetObjectItem(message_json, "duration");
    if (duration_item != nullptr) {
        if (cJSON_IsNumber(duration_item)) {
            msg.duration = duration_item->valueint;
        } else if (cJSON_IsString(duration_item)) {
            msg.duration = std::stoi(duration_item->valuestring);
        }
    } else {
        msg.duration = 0;  // 默认值
    }
    
    return true;
}

void DemoMode::HandleStartMessage(const DemoProtocolMessage& msg) {
    ESP_LOGI(TAG, "Starting demo mode with session_id: %s", msg.session_id.c_str());
    
    // 如果已经在demo模式中，先停止
    if (is_demo_active_) {
        StopDemo();
    }
    
    // 设置demo模式
    is_demo_active_ = true;
    current_session_id_ = msg.session_id;
    
    // 设置设备为demo模式
    //app_->SetDeviceState(kDeviceStateDemo);
    
    // 关闭录音
    auto audio_processor = app_->GetAudioProcessor();
    if (audio_processor) {
        audio_processor->Stop();
    }
    
    // 停止唤醒词检测
    auto wake_word = app_->GetWakeWord();
    if (wake_word) {
        wake_word->StopDetection();
    }
    
    ESP_LOGI(TAG, "Demo mode started in protocol mode");
}

void DemoMode::HandleTextMessage(const DemoProtocolMessage& msg) {
    if (!is_demo_active_) {
        ESP_LOGW(TAG, "Received text message but not in demo mode");
        return;
    }
    
    if (msg.session_id != current_session_id_) {
        ESP_LOGW(TAG, "Session ID mismatch: expected %s, got %s", 
                current_session_id_.c_str(), msg.session_id.c_str());
        return;
    }
    
    //ESP_LOGI(TAG, "Displaying text: %s for %s seconds", msg.content.c_str(), msg.duration.c_str());
    int duration = msg.duration;
    // 显示文本
    /*
    if (!msg.content.empty()) {
        int duration_sec = static_cast<int>(std::stof(msg.duration));
        ESP_LOGI(TAG, "Displaying text: %s for %d seconds", msg.content.c_str(), duration_sec);
        DisplayTextWithDuration(msg.content, duration_sec);
    }
    */
    if(msg.content == "正在联网...") 
    {
        auto display = Board::GetInstance().GetDisplay();
        auto& board = Board::GetInstance();
        
        // 显示联网状态界面
        display->SetStatus("正在联网...");
        
        // 检查网络类型并显示相应图标
        if (auto* dual_board = static_cast<DualNetworkBoard*>(&board)) {
            NetworkType network_type = dual_board->GetNetworkType();
            if (network_type == NetworkType::ML307) {
                display->SetIcon("📶");  // 4G信号图标
                //display->ShowNotification("正在连接4G网络", duration);
            } else {
                display->SetIcon("📶");  // WiFi信号图标
                //display->ShowNotification("正在连接WiFi", duration);
            }
        }
        
        // 显示网络连接动画表情
        display->SetEmotion("idle");
    } 
    else if(msg.content == "进入配网模式,请用手机连接热点MJ-xxx") 
    {
        auto display = Board::GetInstance().GetDisplay();
        
        // 显示配网模式界面
        display->SetStatus("配网模式");
        display->SetIcon("📱");  // 手机图标
        //display->ShowNotification("配网模式", duration);
        
        // 显示配网提示表情
        display->SetEmotion("happy");
        
        // 可以显示二维码或热点信息
        display->SetChatMessage("system", "请用手机连接WiFi热点MJ-xxx", duration);
    } 
    else if(msg.content == "正在升级中,下载50%") 
    {
        auto display = Board::GetInstance().GetDisplay();
        
        // 显示OTA升级界面
        display->SetStatus("OTA升级中...");
        display->SetIcon("⬇️");  // 下载图标
        
        // 显示升级进度
        display->SetChatMessage("system", "正在下载更新\n进度: 50%\n请勿断电",  duration);
        
        // 显示升级表情
        display->SetEmotion("ok");
    } 
    else if(msg.content == "喝水喝动一动") 
    {
        auto display = Board::GetInstance().GetDisplay();
        
        // 显示健康提醒界面
        //display->SetStatus("健康提醒");
        display->SetIcon("💧");  // 水滴图标
        //display->ShowNotification("提醒", duration);
        
        // 显示健康提醒消息
        display->SetChatMessage("assistant", "记得多喝水\n适当运动哦~", duration);
        
        // 显示提醒表情
        display->SetEmotion("happy");
        
        auto& oled_task = OledEmotionTask::GetInstance();
        oled_task.SendEmotion("duoheshui", duration);
        if (app_) {
            app_->PlaySound(Lang::Sounds::P3_DONGYIDONG);
        }
        
    } 
    else
    {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus("展示中...");
    }


}

void DemoMode::HandleEmojiMessage(const DemoProtocolMessage& msg) {
    // 处理表情
    if (!msg.emoji.empty()) {
        std::vector<int> emojis = ParseEmojiString(msg.emoji);
        if (!emojis.empty()) {
            int duration_sec = msg.duration;
            auto display = Board::GetInstance().GetDisplay();
             display->SetChatMessage("assistant", "表情轮播", msg.duration);
             display->SetEmotion("happy");
             display->SetStatus("演示中...");
            ESP_LOGI(TAG, "Displaying emojis: %s for %d seconds", msg.emoji.c_str(), msg.duration);
            DisplayEmojisWithDuration(emojis,msg.duration);
        }
    }

}


void DemoMode::HandleQRCodeMessage(const DemoProtocolMessage& msg) {
    if (!is_demo_active_) {
        ESP_LOGW(TAG, "Received qr_code message but not in demo mode");
        return;
    }
    
    if (msg.session_id != current_session_id_) {
        ESP_LOGW(TAG, "Session ID mismatch: expected %s, got %s", 
                current_session_id_.c_str(), msg.session_id.c_str());
        return;
    }
    
    ESP_LOGI(TAG, "Displaying QR code: %s", msg.img_url.c_str());
    
    // 显示文本（如果有）
    if (!msg.content.empty() && msg.duration > 0) {
        int duration_sec = msg.duration;
        DisplayTextWithDuration(msg.content, duration_sec);
    }
    
    // 显示二维码图片
    if (!msg.img_url.empty()) {
        int duration_sec = msg.duration;
        ESP_LOGI(TAG, "Displaying QR code: %s for %d seconds", msg.img_url.c_str(), duration_sec);
        DisplayImageWithDuration(msg.img_url, duration_sec);
    }
}

void DemoMode::HandleEndMessage(const DemoProtocolMessage& msg) {
    
    ESP_LOGI(TAG, "Ending demo mode for session: %s", msg.session_id.c_str());
    StopDemo();
}

void DemoMode::StopDemo() {
    if (!is_demo_active_) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping demo mode");
    
    is_demo_active_ = false;
    current_session_id_.clear();
    
    // 返回到keep connected状态
    if (app_) {
        app_->SetDeviceState(kDeviceStateKeepConnected);
        app_ = nullptr;
    }
}

std::vector<int> DemoMode::ParseEmojiString(const std::string& emoji_str) {
    std::vector<int> emojis;
    std::stringstream ss(emoji_str);
    std::string item;
    
    while (std::getline(ss, item, ',')) {
        try {
            int emoji_id = std::stoi(item);
            emojis.push_back(emoji_id);
        } catch (const std::exception& e) {
            ESP_LOGW(TAG, "Invalid emoji ID: %s", item.c_str());
        }
    }
    
    return emojis;
}

void DemoMode::DisplayTextWithDuration(const std::string& text, int duration_sec) {
    auto display = Board::GetInstance().GetDisplay();
    if (display) {
        display->SetChatMessage("system", text.c_str(), duration_sec);
    }
}

void DemoMode::DisplayEmojisWithDuration(const std::vector<int>& emojis, int duration_sec) {
    if (emojis.empty()) return;
    
    // 创建任务来顺序显示表情
    xTaskCreate([](void* param) {
        auto* params = static_cast<std::pair<std::vector<int>, int>*>(param);
        const auto& emojis = params->first;
        int duration_sec = 2;
        
        auto& oled_task = OledEmotionTask::GetInstance();
        auto& demo_mode = DemoMode::GetInstance();
        for(size_t i = 0; i < emojis.size(); i++) {
            int emoji_id = emojis[i];  // 使用实际的emoji_id，而不是循环索引
            
            std::string emotion_name = demo_mode.GetEmotionName(emoji_id);
            
            ESP_LOGI("DemoMode", "Displaying emoji %d, emotion_name=%s", emoji_id, emotion_name.c_str());
            oled_task.SendEmotion(emotion_name.c_str(), duration_sec);
            
            // 等待当前表情显示完成再显示下一个
            vTaskDelay(pdMS_TO_TICKS(duration_sec * 1000));
        }
        
        // 清理参数内存
        delete params;
        vTaskDelete(NULL);
    }, "emoji_display_task", 4096, 
       new std::pair<std::vector<int>, int>(emojis, duration_sec), 
       5, NULL);
}

void DemoMode::DisplayImageWithDuration(const std::string& img_url, int duration) {
    ESP_LOGI(TAG, "Displaying image: `%s` for %d seconds", img_url.c_str(), duration);
    
    if (app_) {
        // Create a struct to hold both parameters
        struct TaskParams {
            Application* app;
            std::string img_url;
        };
        
        TaskParams* params = new TaskParams{app_, img_url};
        
        // Use a static function that can be converted to function pointer
        xTaskCreate([](void* param) {
            TaskParams* p = static_cast<TaskParams*>(param);
            p->app->DownloadAndDisplayQRCode(p->img_url, 10);
            delete p;  // Clean up allocated memory
            vTaskDelete(NULL);
        }, "image_download", 8192, params, 1, NULL);
    }
}

std::string DemoMode::GetEmotionName(int emoji_id) {
    switch (emoji_id) {
        case 1: return "dongyidong";
        case 2: return "newmsg";
        case 3: return "ok";
        case 4: return "sleepy";
        case 5: return "sad";
        case 6: return "superhappy";
    }
    return "idle";
}

