#ifndef _APPLICATION_H_
#define _APPLICATION_H_

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <lvgl.h>  // 添加这行来声明 lv_image_dsc_t

#include <string>
#include <mutex>
#include <list>
#include <vector>
#include <condition_variable>
#include <memory>
#include <regex>  // 用于正则表达式解析

#include <opus_encoder.h>
#include <opus_decoder.h>
#include <opus_resampler.h>

#include "protocol.h"
#include "ota.h"
#include "background_task.h"
#include "audio_processor.h"
#include "wake_word.h"
#include "audio_debugger.h"
#include "radar_common.h"
#include "radar/radar.h"

#include "factory_test_manager.h"
#include "demo_mode.h"


#define SCHEDULE_EVENT (1 << 0)
#define SEND_AUDIO_EVENT (1 << 1)
#define CHECK_NEW_VERSION_DONE_EVENT (1 << 2)
#define DEVICE_BOUND_EVENT (1 << 3)

enum AecMode {
    kAecOff,
    kAecOnDeviceSide,
    kAecOnServerSide,
};

enum DeviceState {
    kDeviceStateUnknown,
    kDeviceStateStarting,
    kDeviceStateWifiConfiguring,
    kDeviceStateIdle,
    kDeviceStateConnecting,
    kDeviceStateListening,
    kDeviceStateSpeaking,
    kDeviceStateUpgrading,
    kDeviceStateActivating,
    kDeviceStateAudioTesting,
    kDeviceStateFactoryTesting, // 添加工厂测试模式状态
    kDeviceStateKeepConnected, // 保持连接状态，但不上传音频采集（保留唤醒词上传数据）,除非tts有数据-》listening状态
    kDeviceStateSleeping, // 睡眠状态
   // kDeviceStateDemo,  // 新增demo模式状态
    kDeviceStateFatalError
};

#define OPUS_FRAME_DURATION_MS 60
#define MAX_AUDIO_PACKETS_IN_QUEUE (7200 / OPUS_FRAME_DURATION_MS)
#define AUDIO_TESTING_MAX_DURATION_MS 10000




#define OPEN_RADARR_MODULE 1// 定义宏以启用雷达模块

// 定义GPIO中断宏开关
#define ENABLE_GPIO_INTERRUPT 1  // 设置为0可禁用GPIO10中断

#define USING_MAR_SERVER 0
//#define DEBUG_VERSON
//#define MEMORY_MONITOR
// 设备数据结构定义
struct DeviceData {
    int id;
    char mac_addr[18];  // MAC地址格式: XX:XX:XX:XX:XX:XX
    char random_num[10];
    char kz_token[100];
    char status[20];    // 设备状态: UNBOUND, BOUND, DISABLED
    char bind_code[5];  // 4位绑定码 + 结束符
    int user_id;        // 绑定的用户ID
};

class Application {
    friend class FactoryTestManager;
    friend class RadarTest;
    
public:
    static Application& GetInstance() {
        static Application instance;
        return instance;
    }
    // 删除拷贝构造函数和赋值运算符
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void Start();
    DeviceState GetDeviceState() const { return device_state_; }
    bool IsVoiceDetected() const { return voice_detected_; }
    void Schedule(std::function<void()> callback);
    void SetDeviceState(DeviceState state);
    void Alert(const char* status, const char* message, const char* emotion = "", const std::string_view& sound = "");
    void DismissAlert();
    void AbortSpeaking(AbortReason reason);
    void ToggleChatState();
    void StartListening();
    void StopListening();
    void ExitChat();  // 新增退出聊天方法
    void UpdateIotStates();
    void Reboot();
    void WakeWordInvoke(const std::string& wake_word);
    void PlaySound(const std::string_view& sound);
    bool CanEnterSleepMode();
    void SendMcpMessage(const std::string& payload);
    void SetAecMode(AecMode mode);
    AecMode GetAecMode() const { return aec_mode_; }
    BackgroundTask* GetBackgroundTask() const { return background_task_; }
    void ClearAudioQueues();  // 新增：清空音频队列的方法
private:
    Application();
    ~Application();

    std::unique_ptr<WakeWord> wake_word_;
    std::unique_ptr<AudioProcessor> audio_processor_;
    std::unique_ptr<AudioDebugger> audio_debugger_;
    Ota ota_;
    std::mutex mutex_;
    std::list<std::function<void()>> main_tasks_;
    std::unique_ptr<Protocol> protocol_;
    EventGroupHandle_t event_group_ = nullptr;
    esp_timer_handle_t clock_timer_handle_ = nullptr;
    volatile DeviceState device_state_ = kDeviceStateUnknown;
    ListeningMode listening_mode_ = kListeningModeAutoStop;
    AecMode aec_mode_ = kAecOff;

    bool aborted_ = false;
    bool voice_detected_ = false;
    bool busy_decoding_audio_ = false;
    int clock_ticks_ = 0;
    TaskHandle_t check_new_version_task_handle_ = nullptr;
    TaskHandle_t qr_test_task_handle_ = nullptr;

    // Audio encode / decode
    TaskHandle_t audio_loop_task_handle_ = nullptr;
    BackgroundTask* background_task_ = nullptr;
    std::chrono::steady_clock::time_point last_output_time_;
    std::list<AudioStreamPacket> audio_send_queue_;
    std::list<AudioStreamPacket> audio_decode_queue_;
    std::condition_variable audio_decode_cv_;
    std::list<AudioStreamPacket> audio_testing_queue_;

    // 新增：用于维护音频包的timestamp队列
    std::list<uint32_t> timestamp_queue_;
    std::mutex timestamp_mutex_;

    std::unique_ptr<OpusEncoderWrapper> opus_encoder_;
    std::unique_ptr<OpusDecoderWrapper> opus_decoder_;

    OpusResampler input_resampler_;
    OpusResampler reference_resampler_;
    OpusResampler output_resampler_;

    void MainEventLoop();
    void OnAudioInput();
    void OnAudioOutput();
    
    
    void ResetDecoder();
    void SetDecodeSampleRate(int sample_rate, int frame_duration);
    void CheckNewVersion();
    void ShowActivationCode();
    void OnClockTimer();
    void SetListeningMode(ListeningMode mode);
    void AudioLoop();
    void EnterAudioTestingMode();
    
    std::string mac_addr;  // 修改为 std::string 类型
    void show_device_msg(const char * message);
    void device_bind_task();
    void start_device_bind_process();
    int parse_device_status(const char *json_str, DeviceData *data);
    int query_device_status(const char *mac_addr, DeviceData *data);

    void UpdateDeviceOnlineStatus(bool online,std::string reason);

    std::string network_type;
    // API URLs
    static constexpr const char* base_url = "http://47.112.188.49:7167";
    static constexpr const char* api_device_init = "/api/device/init";
    static constexpr const char* api_device_online = "/api/device/online";
    static constexpr const char* api_device_offline = "/api/device/offline";
    static constexpr const char* api_messages_unread = "/api/messages/unread";
    static constexpr const char* api_messages_read = "/api/messages/read";

    int CheckBindQrCodeStatus(const std::string& baseUrl, std::string& qr_code_url);
    void StartBindQrCodePolling(const std::string& baseUrl);

    void ExitAudioTestingMode();
    DeviceData deviceData;
    void ShowActivationCode(std::string message,std::string code);

public:    
    // 移除 is_show_qrcode 变量，因为我们不再使用二维码显示功能
    // bool is_show_qrcode = false;
    bool ReadAudio(std::vector<int16_t>& data, int sample_rate, int samples);
    // 消息推送相关
    void HandlePushMessage(const std::string& message);
    void HandleMessageRead(int message_id);
    void CheckUnreadMessages();
    void SendMessageReadStatus(int message_id, const std::string& mac_address);
    void DisplayMessage(int message_id, const std::string& title, const std::string& content, 
                       const std::string& short_url, const std::string& qr_code_url);
    void DownloadAndDisplayQRCode(const std::string& url,int duration=15);
    void StartQrTest();
    void StopQrTest();
    
    static constexpr int CHECK_UNREAD_INTERVAL_MS = 150000; // 15秒检查一次未读消息
    esp_timer_handle_t unread_check_timer_ = nullptr;
    
    // 添加无人状态计时器
    esp_timer_handle_t nobody_state_timer_ = nullptr;
    static void NobodyStateTimerCallback(void* arg);
    
    // 添加新的 Listening 状态计时器
    esp_timer_handle_t listening_state_timer_ = nullptr;
    static void ListeningStateTimerCallback(void* arg);
    
    // 添加心跳包定时器
    esp_timer_handle_t heartbeat_timer_ = nullptr;
    static void HeartbeatTimerCallback(void* arg);
    

    void SwitchDisplayMode(bool clock_mode); 

private:
    static void UnreadCheckTimerCallback(void* arg);
    static void QrTestTask(void* arg);

// VAD 相关参数
    static const int VAD_WINDOW_SIZE_MS = 30;           // VAD检测窗口30ms
    static const int VAD_THRESHOLD =100;               // VAD阈值
    static const int SPEECH_TIMEOUT_MS = 500;           // 语音结束超时时间
    static const int SPEECH_PADDING_MS = 200;           // 语音前后填充时间
    
    // VAD状态管理
    bool is_speech_active_;                             // 当前是否在语音状态
    uint32_t last_speech_time_;                         // 最后检测到语音的时间
    uint32_t speech_start_time_;                        // 语音开始时间
    
    // 辅助方法
    bool DetectVoiceActivity(const int16_t* data);
    bool ShouldSendAudioWithVAD(const int16_t* data);
    void UpdateSpeechState(bool voice_detected);

    //end 滑窗

    void InitRadarModule();
    void OnRadarStatusUpdate(uint8_t target_state, uint16_t moving_distance, uint8_t moving_energy, 
                            uint16_t static_distance, uint8_t static_energy, uint16_t detection_distance);
    void OnPersonStateChanged(PersonState state);
    bool radar_initialized_ = false;
    bool radar_status_changed_ = false;
    // for demo only
    void HandleRadarWakeUp();
    // end for demo
    bool bFirstBoot = true; //冷启动第一次开机

    void ConnectToServer();
    std::string notification_type;

public:
    //处理触摸中断
    QueueHandle_t gpio_evt_queue;
    void gpio_monitoring_init();
    
    // Add this member variable declaration in the private section
    std::unique_ptr<FactoryTestManager> factory_test_manager_;
    
    void EnterFactoryTestingMode() {
        if (!factory_test_manager_) {
            factory_test_manager_ = std::make_unique<FactoryTestManager>(this);
        }
        factory_test_manager_->EnterFactoryTestingMode();
    }
    
    void ExitFactoryTestingMode() {
        if (factory_test_manager_) {
            factory_test_manager_->ExitFactoryTestingMode();
        }
    }
    
    void RunFactoryTest(int test_index) {
        if (factory_test_manager_) {
            factory_test_manager_->RunTest(test_index);
        }
    }
    
    void HandleVolumeUpPress() {
        if (factory_test_manager_) {
            factory_test_manager_->HandleVolumeUpPress();
        }
    }

    
    bool IsInFactoryTestingMode() const {
        return factory_test_manager_ && factory_test_manager_->IsInFactoryTestingMode();
    }
    void HandleAudioTestInterrupt();
#ifdef MEMORY_MONITOR    
public:
    void StartMemoryMonitorTask();  // 添加内存监控任务声明
    
private:
    static void MemoryMonitorTask(void* arg);  // 内存监控任务函数
    TaskHandle_t memory_monitor_task_handle_ = nullptr;  // 任务句柄
#endif
    
       // 处理切换网络指令
    void HandleNetworkSwitch();
    
    // 处理WiFi配网指令
    void HandleWifiConfig();

    void StartHeartbeatTimer();
    void StopHeartbeatTimer();

    bool SendDeviceRegisterRequest(const std::string& baseUrl);
    int GetDeviceNumber(); // 获取设备编号
    bool HasDeviceNumber(); // 检查是否已有设备编号

    std::string ExtractHostFromUrl(const std::string& url);

    bool bGreetingTTS = false;

    void PlayDeviceNumber();  // 添加播放设备号的方法声明
    void ShowSystemVersion(); // 添加显示系统版本的方法声明
    
    // 添加记录 kDeviceStateKeepConnected 开始时间的成员变量
    uint32_t keep_connected_start_time_ = 0;
    bool bWakeupPassData = true;
     
private:
    // 添加图片保护状态
    bool is_image_protected_ = false;
    esp_timer_handle_t image_protection_timer_ = nullptr;
    std::string last_bind_qr_code_url_;
    bool qr_code_shown_ = false;
    
public:
    // 添加保护图片显示方法
    void SetProtectedImage(lv_image_dsc_t* img_dsc, int protection_time_ms = 5000);
    static void ImageProtectionTimerCallback(void* arg);

    bool has_new_msg = false;
    bool b_playing_msg = false;
    // 在一次 TTS 会话中仅显示一次二维码图片
    bool tts_image_displayed_ = false;
    bool tts_session_active_ = false;
private:
    bool pending_long_sitting_reminder_ = false;  // 待播放的久坐提醒标记
    
    void PlayLongSittingReminder();  // 播放久坐提醒的方法
    void CheckPendingLongSittingReminder();  // 检查并播放待处理的久坐提醒

// 在Application类的public部分添加
public:
    // Demo模式相关方法
    bool IsInDemoMode() const;
    
    // 获取内部组件的访问器（供DemoMode使用）
    AudioProcessor* GetAudioProcessor() const { return audio_processor_.get(); }
    WakeWord* GetWakeWord() const { return wake_word_.get(); }

};

#endif // _APPLICATION_H_




