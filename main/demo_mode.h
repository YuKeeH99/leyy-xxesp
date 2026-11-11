#ifndef _DEMO_MODE_H_
#define _DEMO_MODE_H_

#include <string>
#include <vector>
#include <cJSON.h>
#include <esp_log.h>  // 添加这行来解决ESP_LOGI未声明的问题

class Application;

// Demo协议消息结构
struct DemoProtocolMessage {
    std::string type;           // "demo_mode"
    std::string state;          // "start", "text", "qr_code", "end"
    std::string content;           // 文本内容
    std::string session_id;     // 会话ID
    std::string img_url;        // 图片URL
    std::string emoji;          // 表情序号（逗号分隔）
    int duration;       // 执行时长
};

class DemoMode {
public:
    static DemoMode& GetInstance() {
        static DemoMode instance;
        //ESP_LOGI("DemoMode", "GetInstance called");
        return instance;
    }
    
    // 删除拷贝构造函数和赋值运算符
    DemoMode(const DemoMode&) = delete;
    DemoMode& operator=(const DemoMode&) = delete;
    
    // 核心接口：处理协议消息
    bool HandleProtocolMessage(const cJSON* message_json, Application* app);
    
    // 检查是否在demo模式中
    bool IsInDemoMode() const { return is_demo_active_; }
    
    // 获取当前会话ID
    const std::string& GetCurrentSessionId() const { return current_session_id_; }
    
    // 停止demo模式
    void StopDemo();
    
private:
    DemoMode() = default;
    ~DemoMode(); // 移除 = default，因为需要自定义实现
    
    // 解析协议消息
    bool ParseProtocolMessage(const cJSON* message_json, DemoProtocolMessage& msg);
    
    // 处理不同状态的消息
    void HandleStartMessage(const DemoProtocolMessage& msg);
    void HandleTextMessage(const DemoProtocolMessage& msg);
    void HandleQRCodeMessage(const DemoProtocolMessage& msg);
    void HandleEndMessage(const DemoProtocolMessage& msg);
    void HandleEmojiMessage(const DemoProtocolMessage& msg);
    
    // 工具函数
    std::vector<int> ParseEmojiString(const std::string& emoji_str);
    void DisplayTextWithDuration(const std::string& text, int duration_sec);
    void DisplayEmojisWithDuration(const std::vector<int>& emojis, int duration_sec);
    void DisplayImageWithDuration(const std::string& img_url, int duration_sec);
    std::string GetEmotionName(int emoji_id); // 添加这个声明
    
private:
    Application* app_ = nullptr;
    bool is_demo_active_ = false;         // demo模式是否激活
    std::string current_session_id_;      // 当前会话ID
};

#endif // _DEMO_MODE_H_