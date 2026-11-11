#ifndef OLED_EMOTION_TASK_H
#define OLED_EMOTION_TASK_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>
#include <string.h>

// 消息类型枚举
enum class OledMessageType {
    SET_EMOTION,
    SET_CHAT_MESSAGE,
    CLEAR_DISPLAY,
    SHOW_NOTIFICATION
};

// OLED显示消息结构
// 在 OledMessage 结构体中添加新字段
struct OledMessage {
    OledMessageType type;
    char data1[64];  // 用于存储emotion名称或role
    char data2[256]; // 用于存储content或notification
    int duration_ms; // 用于notification持续时间
    int emotion_duration_sec; // 新增：表情持续时间（秒）
};

class OledEmotionTask {
public:
    static OledEmotionTask& GetInstance();
    
    // 初始化任务
    bool Initialize();
    
    // 发送表情显示消息
    // 修改 SendEmotion 方法签名，添加默认参数
    bool SendEmotion(const char* emotion, int duration_seconds = 0);
    
    // 发送聊天消息
    bool SendChatMessage(const char* role, const char* content);
    
    // 发送通知消息
    bool SendNotification(const char* notification, int duration_ms = 3000);
    
    // 清空显示
    bool ClearDisplay();
    
    // 停止任务
    void Stop();
    
private:
    OledEmotionTask() = default;
    ~OledEmotionTask() = default;
    
    // 任务函数
    static void TaskFunction(void* parameter);
    
    // 处理消息
    void ProcessMessage(const OledMessage& message);
    
    TaskHandle_t task_handle_ = nullptr;
    QueueHandle_t message_queue_ = nullptr;
    bool is_running_ = false;
    
    static const int QUEUE_SIZE = 10;
    static const int TASK_STACK_SIZE = 4096;
    static const int TASK_PRIORITY = 5;
    
    // 新增私有成员变量
    TimerHandle_t emotion_timer_ = nullptr;
    bool has_timed_emotion_playing_ = false;  // 新增：跟踪是否有定时表情正在播放
    
    // 新增静态回调函数
    static void EmotionTimerCallback(TimerHandle_t timer);
};

#endif // OLED_EMOTION_TASK_H