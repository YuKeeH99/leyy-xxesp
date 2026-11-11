#include "oled_emotion_task.h"
#include "boards/common/board.h"
#include "display/custom_gif_emoji_display.h"
#include <freertos/timers.h>  // 新增：包含定时器头文件

static const char* TAG = "OledEmotionTask***********";

OledEmotionTask& OledEmotionTask::GetInstance() {
    static OledEmotionTask instance;
    return instance;
}

bool OledEmotionTask::Initialize() {
    if (is_running_) {
        ESP_LOGW(TAG, "Task already running");
        return true;
    }
    
    // 创建消息队列
    message_queue_ = xQueueCreate(QUEUE_SIZE, sizeof(OledMessage));
    if (!message_queue_) {
        ESP_LOGE(TAG, "Failed to create message queue");
        return false;
    }
    is_running_ = true;
    // 创建任务
    BaseType_t result = xTaskCreate(
        TaskFunction,
        "OledEmotionTask",
        TASK_STACK_SIZE,
        this,
        TASK_PRIORITY,
        &task_handle_
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        vQueueDelete(message_queue_);
        message_queue_ = nullptr;
        return false;
    }
    
    
    ESP_LOGI(TAG, "OLED emotion task initialized successfully");
    return true;
}

// 修改 SendEmotion 方法实现
bool OledEmotionTask::SendEmotion(const char* emotion, int duration_seconds) {
    ESP_LOGW(TAG, "SendEmotion: %s, duration: %d seconds", emotion, duration_seconds);
    if (!is_running_ || !message_queue_ || !emotion) {
        return false;
    }

    ESP_LOGW(TAG, "SendEmotion: %s", emotion);
    
    OledMessage message;
    message.type = OledMessageType::SET_EMOTION;
    strncpy(message.data1, emotion, sizeof(message.data1) - 1);
    message.data1[sizeof(message.data1) - 1] = '\0';
    message.emotion_duration_sec = duration_seconds;  // 新增：设置持续时间
    
    BaseType_t result = xQueueSend(message_queue_, &message, pdMS_TO_TICKS(100));
    if (result != pdPASS) {
        ESP_LOGW(TAG, "Failed to send emotion message: %s", emotion);
        return false;
    }
    
    return true;
}

bool OledEmotionTask::SendChatMessage(const char* role, const char* content) {
    if (!is_running_ || !message_queue_ || !role || !content) {
        return false;
    }
    
    OledMessage message;
    message.type = OledMessageType::SET_CHAT_MESSAGE;
    strncpy(message.data1, role, sizeof(message.data1) - 1);
    message.data1[sizeof(message.data1) - 1] = '\0';
    strncpy(message.data2, content, sizeof(message.data2) - 1);
    message.data2[sizeof(message.data2) - 1] = '\0';
    
    BaseType_t result = xQueueSend(message_queue_, &message, pdMS_TO_TICKS(100));
    if (result != pdPASS) {
        ESP_LOGW(TAG, "Failed to send chat message");
        return false;
    }
    
    return true;
}

bool OledEmotionTask::SendNotification(const char* notification, int duration_ms) {
    if (!is_running_ || !message_queue_ || !notification) {
        return false;
    }
    
    OledMessage message;
    message.type = OledMessageType::SHOW_NOTIFICATION;
    strncpy(message.data1, notification, sizeof(message.data1) - 1);
    message.data1[sizeof(message.data1) - 1] = '\0';
    message.duration_ms = duration_ms;
    
    BaseType_t result = xQueueSend(message_queue_, &message, pdMS_TO_TICKS(100));
    if (result != pdPASS) {
        ESP_LOGW(TAG, "Failed to send notification message");
        return false;
    }
    
    return true;
}

bool OledEmotionTask::ClearDisplay() {
    if (!is_running_ || !message_queue_) {
        return false;
    }
    
    OledMessage message;
    message.type = OledMessageType::CLEAR_DISPLAY;
    
    BaseType_t result = xQueueSend(message_queue_, &message, pdMS_TO_TICKS(100));
    return result == pdPASS;
}

// 修改 ProcessMessage 方法
void OledEmotionTask::ProcessMessage(const OledMessage& message) {
    ESP_LOGW(TAG, "ProcessMessage");
    auto& board = Board::GetInstance();
    auto oled_display = board.GetOledDisplay();
    
    if (!oled_display) {
        ESP_LOGW(TAG, "OLED display not available");
        return;
    }

    try {
        switch (message.type) {
            case OledMessageType::SET_EMOTION:
                // 新增：检查是否应该阻止播放
                if (message.emotion_duration_sec == 0 && has_timed_emotion_playing_) {
                    ESP_LOGI(TAG, "Blocking emotion '%s' because timed emotion is playing", message.data1);
                    return;  // 阻止播放无时长的表情
                }
                
                ESP_LOGI(TAG, "Setting emotion: %s", message.data1);
                oled_display->SetEmotion(message.data1);
                
                // 处理定时器逻辑
                if (message.emotion_duration_sec > 0) {
                    // 如果已有定时器在运行，先停止它
                    if (emotion_timer_ != nullptr) {
                        xTimerStop(emotion_timer_, 0);
                        xTimerDelete(emotion_timer_, 0);
                        emotion_timer_ = nullptr;
                    }
                    
                    // 创建新的定时器
                    emotion_timer_ = xTimerCreate(
                        "EmotionTimer",
                        pdMS_TO_TICKS(message.emotion_duration_sec * 1000),
                        pdFALSE,  // 不重复
                        this,     // 传递this指针作为定时器ID
                        EmotionTimerCallback
                    );
                    
                    if (emotion_timer_ != nullptr) {
                        xTimerStart(emotion_timer_, 0);
                        has_timed_emotion_playing_ = true;  // 新增：标记有定时表情正在播放
                        ESP_LOGI(TAG, "Started emotion timer for %d seconds", message.emotion_duration_sec);
                    } else {
                        ESP_LOGE(TAG, "Failed to create emotion timer");
                    }
                } else {
                    // 无时长的表情，清除定时状态
                    has_timed_emotion_playing_ = false;
                }
                break;
                
            case OledMessageType::SET_CHAT_MESSAGE:
                ESP_LOGI(TAG, "Setting chat message: %s - %s", message.data1, message.data2);
                oled_display->SetChatMessage(message.data1, message.data2);
                break;
                
            case OledMessageType::SHOW_NOTIFICATION:
                ESP_LOGI(TAG, "Showing notification: %s", message.data1);
                oled_display->ShowNotification(message.data1, message.duration_ms);
                break;
                
            case OledMessageType::CLEAR_DISPLAY:
                ESP_LOGI(TAG, "Clearing display");
                oled_display->SetChatMessage("", "");
                oled_display->SetEmotion("neutral");
                break;
                
            default:
                ESP_LOGW(TAG, "Unknown message type: %d", static_cast<int>(message.type));
                break;
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception in ProcessMessage: %s", e.what());
    } catch (...) {
        ESP_LOGE(TAG, "Unknown exception in ProcessMessage");
    }
}

// 新增：定时器回调函数实现
void OledEmotionTask::EmotionTimerCallback(TimerHandle_t timer) {
    // 从定时器ID获取OledEmotionTask实例
    OledEmotionTask* task = static_cast<OledEmotionTask*>(pvTimerGetTimerID(timer));
    
    if (task != nullptr) {
        ESP_LOGI(TAG, "Emotion timer expired, switching to idle");
        
        // 清除定时状态
        task->has_timed_emotion_playing_ = false;
        
        // 发送idle表情消息
        task->SendEmotion("idle");
        
        // 清理定时器
        task->emotion_timer_ = nullptr;
    }
}

// 修改 Stop 方法，确保清理定时器
void OledEmotionTask::Stop() {
    if (!is_running_) {
        return;
    }
    
    is_running_ = false;
    
    // 清理定时器和状态
    if (emotion_timer_ != nullptr) {
        xTimerStop(emotion_timer_, 0);
        xTimerDelete(emotion_timer_, 0);
        emotion_timer_ = nullptr;
    }
    has_timed_emotion_playing_ = false;  // 新增：重置状态
    
    // 等待任务结束
    if (task_handle_) {
        // 发送停止信号或等待任务自然结束
        vTaskDelay(pdMS_TO_TICKS(100));
        task_handle_ = nullptr;
    }
    
    // 清理消息队列
    if (message_queue_) {
        vQueueDelete(message_queue_);
        message_queue_ = nullptr;
    }
    
    ESP_LOGI(TAG, "OLED emotion task stopped");
}

void OledEmotionTask::TaskFunction(void* parameter) {
    OledEmotionTask* task = static_cast<OledEmotionTask*>(parameter);
    OledMessage message;
    
    ESP_LOGI(TAG, "OLED emotion task started");
    
    while (task->is_running_) {
        // 等待消息
        if (xQueueReceive(task->message_queue_, &message, pdMS_TO_TICKS(1000)) == pdPASS) {
            task->ProcessMessage(message);
        }
        
        // 让出CPU时间
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    
    ESP_LOGI(TAG, "OLED emotion task ended");
    vTaskDelete(nullptr);
}