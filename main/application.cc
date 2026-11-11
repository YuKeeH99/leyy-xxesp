#include "application.h"
#include "board.h"
#include "wifi_board.h"
#include "dual_network_board.h"
#include "display.h"
#include "system_info.h"
#include "settings.h"  // 添加这行
#include "ml307_ssl_transport.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "font_awesome_symbols.h"
#include "iot/thing_manager.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "audio_debugger.h"

#include "clock_display.h"

#include "oled_emotion_task.h"

#include <esp_ota_ops.h>

#if CONFIG_USE_AUDIO_PROCESSOR
#include "afe_audio_processor.h"
#else
#include "no_audio_processor.h"
#endif

#if CONFIG_USE_AFE_WAKE_WORD
#include "afe_wake_word.h"
#elif CONFIG_USE_ESP_WAKE_WORD
#include "esp_wake_word.h"
#else
#include "no_wake_word.h"
#endif

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <esp_http_client.h>
#include <vector>
#include <esp_jpeg_dec.h>
#include <regex>
#include <cmath>  // 添加这行来解决 sqrt 函数未声明的问题

#define TAG "Application"

#include "esp_system.h"
#include "esp_heap_caps.h"


#include "freertos/queue.h"

#include <cJSON.h> // 添加这行

#define GPIO_INPUT_PIN  GPIO_NUM_4  // 需要监测的 GPIO 引脚
#define GPIO_INTERRUPT_QUEUE_SIZE  10
void IRAM_ATTR gpio_isr_handler(void* arg); 
void gpio_task_execute(void* arg);


//#endif

// 全局变量定义
static QueueHandle_t lvgl_event_queue = NULL;
static lv_image_dsc_t* display_image_dsc = NULL;
static lv_obj_t* current_image_obj = NULL;
static lv_image_dsc_t* current_image_dsc = NULL;
static uint8_t* dynamic_image_data = NULL;
static lv_image_dsc_t dynamic_image;

static TimerHandle_t image_timeout_timer = NULL;
static TaskHandle_t main_task_handle = NULL;

static const char* const STATE_STRINGS[] = {
    "unknown",
    "starting",
    "configuring",
    "idle",
    "connecting",
    "listening",
    "speaking",
    "upgrading",
    "activating",
    "audio_testing",
    "factory_testing",
    "keep_connected",
    "sleeping",
    "fatal_error",
  //  "demo_model",
    "invalid_state"
};
//static uint8_t* g_image_data = nullptr;
//static size_t g_image_data_size = 0;

lv_image_dsc_t* createImageDescriptorFromJson(const cJSON *root);
lv_image_dsc_t* createImageDescriptorFromJpeg(const uint8_t *jpeg_data, size_t data_len, bool using_rgb888);

void displayImage(lv_image_dsc_t* img_dsc);

void InitLvglEventSystem(void* param);
void RequestDisplayImage(lv_image_dsc_t* img_dsc) ;
// void displayImageOnMainThread(lv_image_dsc_t* img_dsc) ;
void cleanupPreviousImage();

Application::Application() {
    event_group_ = xEventGroupCreate();
    background_task_ = new BackgroundTask(4096 * 7);
    radar_initialized_ = false;

    // 初始化滑窗相关变量
    is_speech_active_ = false;
    last_speech_time_ = 0;
    speech_start_time_ = 0;
    

#if CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

#if CONFIG_USE_AUDIO_PROCESSOR
    audio_processor_ = std::make_unique<AfeAudioProcessor>();
#else
    audio_processor_ = std::make_unique<NoAudioProcessor>();
#endif

#if CONFIG_USE_AFE_WAKE_WORD
    wake_word_ = std::make_unique<AfeWakeWord>();
#elif CONFIG_USE_ESP_WAKE_WORD
    wake_word_ = std::make_unique<EspWakeWord>();
#else
    wake_word_ = std::make_unique<NoWakeWord>();
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            app->OnClockTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);

    this->mac_addr = SystemInfo::GetMacAddress();
}

Application::~Application() {
    if (unread_check_timer_ != nullptr) {
        esp_timer_stop(unread_check_timer_);
        esp_timer_delete(unread_check_timer_);
    }
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    if (heartbeat_timer_ != nullptr) {
        esp_timer_stop(heartbeat_timer_);
        esp_timer_delete(heartbeat_timer_);
        heartbeat_timer_ = nullptr;
    }
    if (listening_state_timer_ != nullptr) {
        esp_timer_stop(listening_state_timer_);
        esp_timer_delete(listening_state_timer_);
        listening_state_timer_ = nullptr;
    }
    if (background_task_ != nullptr) {
        delete background_task_;
    }
    if (radar_initialized_) {
        RadarModule::GetInstance().Stop();
    }
    vEventGroupDelete(event_group_);
}

void Application::CheckNewVersion() {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // 初始重试延迟为10秒

    while (true) {
        SetDeviceState(kDeviceStateActivating);
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        if (!ota_.CheckVersion()) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char buffer[128];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, ota_.GetCheckVersionUrl().c_str());
            Alert(Lang::Strings::ERROR, buffer, "sad", Lang::Sounds::P3_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (device_state_ == kDeviceStateIdle) {
                    break;
                }
            }
            retry_delay *= 2; // 每次重试后延迟时间翻倍
            continue;
        }
        retry_count = 0;
        retry_delay = 10; // 重置重试延迟时间

        if (ota_.HasNewVersion()) {
            Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "happy", Lang::Sounds::P3_UPGRADE);

            // 设置音量为 -1，重启后会自动设置为最大音量
            Settings settings("audio", true);
            settings.SetInt("output_volume", -1);
            
            vTaskDelay(pdMS_TO_TICKS(3000));

            SetDeviceState(kDeviceStateUpgrading);
            
            display->SetIcon(FONT_AWESOME_DOWNLOAD);
            std::string message = std::string(Lang::Strings::NEW_VERSION) + ota_.GetFirmwareVersion();
            display->SetChatMessage("system", message.c_str());

            auto& board = Board::GetInstance();
            board.SetPowerSaveMode(false);
            wake_word_->StopDetection();
            // 预先关闭音频输出，避免升级过程有音频操作
            auto codec = board.GetAudioCodec();
            codec->EnableInput(false);
            codec->EnableOutput(false);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                audio_decode_queue_.clear();
            }
            background_task_->WaitForCompletion();
            delete background_task_;
            background_task_ = nullptr;
            vTaskDelay(pdMS_TO_TICKS(1000));

            ota_.StartUpgrade([display](int progress, size_t speed) {
                char buffer[64];
                snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
                display->SetChatMessage("system", buffer);
            });

            // If upgrade success, the device will reboot and never reach here
            display->SetStatus(Lang::Strings::UPGRADE_FAILED);
            ESP_LOGI(TAG, "Firmware upgrade failed...");
            vTaskDelay(pdMS_TO_TICKS(3000));
            Reboot();
            return;
        }

        // No new version, mark the current version as valid
        ota_.MarkCurrentVersionValid();
        if (!ota_.HasActivationCode() && !ota_.HasActivationChallenge()) {
            xEventGroupSetBits(event_group_, CHECK_NEW_VERSION_DONE_EVENT);
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota_.HasActivationCode()) {
            ShowActivationCode();
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i) {
    
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota_.Activate();
            if (err == ESP_OK) {
                xEventGroupSetBits(event_group_, CHECK_NEW_VERSION_DONE_EVENT);
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (device_state_ == kDeviceStateIdle) {
                break;
            }
        }
    }
}

void Application::ShowActivationCode() {
    auto& message = ota_.GetActivationMessage();
    auto& code = ota_.GetActivationCode();

    
    // 检查是否进入了工厂测试模式
    if (IsInFactoryTestingMode()) {
        ESP_LOGI(TAG, "Factory testing mode detected, exiting activation");
        return;
    }


    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::P3_0},
        digit_sound{'1', Lang::Sounds::P3_1}, 
        digit_sound{'2', Lang::Sounds::P3_2},
        digit_sound{'3', Lang::Sounds::P3_3},
        digit_sound{'4', Lang::Sounds::P3_4},
        digit_sound{'5', Lang::Sounds::P3_5},
        digit_sound{'6', Lang::Sounds::P3_6},
        digit_sound{'7', Lang::Sounds::P3_7},
        digit_sound{'8', Lang::Sounds::P3_8},
        digit_sound{'9', Lang::Sounds::P3_9}
    }};


    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "happy", Lang::Sounds::P3_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            PlaySound(it->sound);
        }
    }
}

void Application::ShowActivationCode(std::string message,std::string code)
{
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::P3_0},
        digit_sound{'1', Lang::Sounds::P3_1}, 
        digit_sound{'2', Lang::Sounds::P3_2},
        digit_sound{'3', Lang::Sounds::P3_3},
        digit_sound{'4', Lang::Sounds::P3_4},
        digit_sound{'5', Lang::Sounds::P3_5},
        digit_sound{'6', Lang::Sounds::P3_6},
        digit_sound{'7', Lang::Sounds::P3_7},
        digit_sound{'8', Lang::Sounds::P3_8},
        digit_sound{'9', Lang::Sounds::P3_9}
    }};
    std::string show_msg = message + code;
    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, show_msg.c_str(), "happy", Lang::Sounds::P3_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert %s: %s [%s]", status, message, emotion);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        ResetDecoder();
        PlaySound(sound);
    }

    if(strcmp(status,Lang::Strings::ERROR) == 0 && strcmp(message,Lang::Strings::REG_ERROR) == 0)
    {
        ESP_LOGW(TAG, "切换网络模式***********");
        HandleNetworkSwitch();
    }
}

void Application::DismissAlert() {
    if (device_state_ == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::PlaySound(const std::string_view& sound) {
    /*ESP_LOGI(TAG, "PlaySound called: %.*s, device_state: %s, aborted: %s", 
             (int)sound.size(), sound.data(), 
             STATE_STRINGS[device_state_], 
             aborted_ ? "true" : "false");
    
    auto codec = Board::GetInstance().GetAudioCodec();
    codec->EnableOutput(true);
*/

    aborted_ = false;  // 确保播放不受之前的中断状态影响
    // Wait for the previous sound to finish
    {
        std::unique_lock<std::mutex> lock(mutex_);
        audio_decode_cv_.wait(lock, [this]() {
            ESP_LOGI(TAG, "audio_decode_queue_.empty()");
            return audio_decode_queue_.empty();
        });
    }
    background_task_->WaitForCompletion();

    

    const char* data = sound.data();
    size_t size = sound.size();
    for (const char* p = data; p < data + size; ) {
        auto p3 = (BinaryProtocol3*)p;
        p += sizeof(BinaryProtocol3);

        auto payload_size = ntohs(p3->payload_size);
        AudioStreamPacket packet;
        packet.sample_rate = 16000;
        packet.frame_duration = 60;
        packet.payload.resize(payload_size);
        memcpy(packet.payload.data(), p3->payload, payload_size);
        p += payload_size;

        std::lock_guard<std::mutex> lock(mutex_);
        audio_decode_queue_.emplace_back(std::move(packet));
    }
}

void Application::EnterAudioTestingMode() {
    ESP_LOGI(TAG, "Entering audio testing mode");
    ResetDecoder();
    SetDeviceState(kDeviceStateAudioTesting);
}

void Application::ExitAudioTestingMode() {
    ESP_LOGI(TAG, "Exiting audio testing mode");
    SetDeviceState(kDeviceStateWifiConfiguring);
    // Copy audio_testing_queue_ to audio_decode_queue_
    std::lock_guard<std::mutex> lock(mutex_);
    audio_decode_queue_ = std::move(audio_testing_queue_);
    audio_decode_cv_.notify_all();
}

void Application::ToggleChatState() {
#ifdef DEBUG_VERSON
    ESP_LOGI(TAG, "ToggleChatState");
#endif
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        EnterAudioTestingMode();
        return;
    } else if (device_state_ == kDeviceStateAudioTesting) {
        ExitAudioTestingMode();
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateSleeping) {

        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        });

    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
        
    } else if (device_state_ == kDeviceStateListening) {
        Schedule([this]() {
            protocol_->CloseAudioChannel();
        });

    }
}

void Application::StartListening() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        EnterAudioTestingMode();
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            SetListeningMode(kListeningModeManualStop);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
            SetListeningMode(kListeningModeManualStop);
        });
    }
}

void Application::StopListening() {
    if (device_state_ == kDeviceStateAudioTesting) {
        ExitAudioTestingMode();
        return;
    }

    const std::array<int, 3> valid_states = {
        kDeviceStateListening,
        kDeviceStateSpeaking,
        kDeviceStateIdle,
    };
    // If not valid, do nothing
    if (std::find(valid_states.begin(), valid_states.end(), device_state_) == valid_states.end()) {
        return;
    }

    Schedule([this]() {
        if (device_state_ == kDeviceStateListening) {
            protocol_->SendStopListening();
            SetDeviceState(kDeviceStateIdle);
        }
    });
}

void Application::ExitChat() {
    ESP_LOGI(TAG, "Exiting chat via MCP command");
    
    Schedule([this]() {
        // 根据当前设备状态执行相应的退出操作
        switch (device_state_) {
            case kDeviceStateListening:
                // 如果正在监听，停止监听并关闭音频通道
                if (protocol_) {
                    protocol_->SendStopListening();
                    protocol_->CloseAudioChannel();
                }
                SetDeviceState(kDeviceStateIdle);
                break;
                
            case kDeviceStateSpeaking:
                // 如果正在说话，中断说话
                AbortSpeaking(kAbortReasonNone);
                if (protocol_) {
                    protocol_->CloseAudioChannel();
                }
                SetDeviceState(kDeviceStateIdle);
                break;
                
            case kDeviceStateConnecting:
                // 如果正在连接，关闭音频通道
                if (protocol_) {
                    protocol_->CloseAudioChannel();
                }
                SetDeviceState(kDeviceStateIdle);
                break;
                
            case kDeviceStateIdle:
                // 如果已经是idle状态，确保音频通道已关闭
                if (protocol_ && protocol_->IsAudioChannelOpened()) {
                    protocol_->CloseAudioChannel();
                }
                break;
                
            default:
                // 其他状态不需要特殊处理
                ESP_LOGW(TAG, "ExitChat called in state: %s", STATE_STRINGS[device_state_]);
                break;
        }
        
        // 显示退出聊天的提示
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    });
}

void Application::Start() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    /* Setup the display */
    auto display = board.GetDisplay();
    

    /* Setup the audio codec */
    auto codec = board.GetAudioCodec();
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(codec->output_sample_rate(), 1, OPUS_FRAME_DURATION_MS);
    opus_encoder_ = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);

    network_type = "WIFI";
    if (aec_mode_ != kAecOff) {
        ESP_LOGI(TAG, "AEC mode: %d, setting opus encoder complexity to 0", aec_mode_);
        opus_encoder_->SetComplexity(0);
    } else if (board.GetBoardType() == "ml307") {
        ESP_LOGI(TAG, "ML307 board detected, setting opus encoder complexity to 3");
        // 降低编码复杂度以减少CPU占用，缓解任务饥饿
        opus_encoder_->SetComplexity(3);
        network_type = "4G";
    } else {
        ESP_LOGI(TAG, "WiFi board detected, setting opus encoder complexity to 0");
        opus_encoder_->SetComplexity(0);
    }

    if (codec->input_sample_rate() != 16000) {
        input_resampler_.Configure(codec->input_sample_rate(), 16000);
        reference_resampler_.Configure(codec->input_sample_rate(), 16000);
    }
    codec->Start();

#if CONFIG_USE_AUDIO_PROCESSOR
    // 音频主循环固定到CPU1；背景解码在CPU0。通过并行分工降低单核饥饿风险
    xTaskCreatePinnedToCore([](void* arg) {
        Application* app = (Application*)arg;
        app->AudioLoop();
        vTaskDelete(NULL);
    }, "audio_loop", 4096 * 2, this, 5, &audio_loop_task_handle_, 1);
#else
    xTaskCreatePinnedToCore([](void* arg) {
        Application* app = (Application*)arg;
        app->AudioLoop();
        vTaskDelete(NULL);
    }, "audio_loop", 4096 * 2, this, 5, &audio_loop_task_handle_, 1);
#endif

   

    /* Wait for the network to be ready */
    board.StartNetwork();

    //DownloadAndDisplayQRCode("https://static.contextai.pro/s-xiaoxin/other/20251118-175730.jpeg",10);
    // ... existing code ...

    if (!HasDeviceNumber())
    {
        ESP_LOGI(TAG, "No device number found, registering device...");
        std::string ota_url = ota_.GetCheckVersionUrl();
        std::string base_url = ExtractHostFromUrl(ota_url);
        //bool registered = SendDeviceRegisterRequest(base_url.c_str());
        //ESP_LOGI(TAG, "Device registered: %d,base_url = %s", registered,base_url);
        //StartBindQrCodePolling(base_url);
    } else {
        int device_num = GetDeviceNumber();
        ESP_LOGI(TAG, "Device already registered with number: %d", device_num);
    }


    // Update the status bar immediately to show the network state
    

// 运行HTTPS测试
   /* ESP_LOGI(TAG, "Starting HTTPS tests...");
    run_https_tests();
    ESP_LOGI(TAG, "HTTPS tests completed");
*/


#if OPEN_RADARR_MODULE
    // 初始化雷达模块
    InitRadarModule();
#endif
#if ENABLE_GPIO_INTERRUPT
    // 配置GPIO10为输入模式
    gpio_monitoring_init();
#endif

    // Check for new firmware version or get the MQTT broker address
#if !USING_MAR_SERVER
    CheckNewVersion();
#endif

    // Initialize the protocol
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    // Add MCP common tools before initializing the protocol
#if CONFIG_IOT_PROTOCOL_MCP
    McpServer::GetInstance().AddCommonTools();
#endif
/*
Settings settings("websocket", true);
    //正式 7768 dev:7269
    //settings.SetString("url", "ws://47.112.188.49:7269");
    settings.SetString("url", "ws://iot.contextai.pro/xiaozhi/v1/");
    
    settings.SetString("token", "test-token"); //默认就是1
    settings.SetString("version", "1"); //默认就是1
*/
#if USING_MAR_SERVER
    //强制设置websocket 连接
    Settings settings("websocket", true);
    //正式 7768 dev:7269
    //settings.SetString("url", "ws://47.112.188.49:7269");
    settings.SetString("url", "ws://47.112.188.49:7269");
    
    settings.SetString("token", "test-token"); //默认就是1
    settings.SetString("version", "1"); //默认就是1

    //start_device_bind_process();
    ESP_LOGW(TAG, "***********bind query ***************");
    this->device_bind_task();

    ESP_LOGW(TAG, "***********waiting bind result***************");
    protocol_ = std::make_unique<WebsocketProtocol>();
#else
    if (ota_.HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota_.HasWebsocketConfig()) {

        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }
#endif	
	 /* Start the clock timer to update the status bar */
    esp_timer_start_periodic(clock_timer_handle_, 1000000);
    // 初始化GPIO10中断


    /* Initialize unread message check timer */
   /* esp_timer_create_args_t unread_check_timer_args = {
        .callback = UnreadCheckTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "unread_check_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&unread_check_timer_args, &unread_check_timer_);
    esp_timer_start_periodic(unread_check_timer_, CHECK_UNREAD_INTERVAL_MS * 1000);
*/

   
	
    protocol_->OnNetworkError([this](const std::string& message) {
        ESP_LOGW(TAG, "Network error: %s", message.c_str());
        SetDeviceState(kDeviceStateIdle);
        Alert(Lang::Strings::ERROR, message.c_str(), "sad", Lang::Sounds::P3_EXCLAMATION);

 #if OPEN_RADARR_MODULE 
            PersonState current_person_state_ = PersonState::NOBODY;
            #ifdef USE_RADAR_2410D
                current_person_state_ = Radar2410D::GetInstance().GetPersonState();
            #else
                current_person_state_ = RadarModule::GetInstance().GetPersonState();
            #endif
            if(current_person_state_ != PersonState::NOBODY &&
               current_person_state_ != PersonState::PERSON_LEAVING)
            {
                #ifdef DEBUG_VERSON
                ESP_LOGI(TAG, "keep connected current_person_state_ =%d", (int)current_person_state_);
                #endif
                ConnectToServer();
            }
#endif 

    });
    protocol_->OnIncomingAudio([this](AudioStreamPacket&& packet) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (device_state_ == kDeviceStateSpeaking) {
            if (audio_decode_queue_.size() >= MAX_AUDIO_PACKETS_IN_QUEUE) {
                audio_decode_queue_.pop_front();
                ESP_LOGW(TAG, "audio_decode_queue_ full, dropping oldest packet. size=%d", (int)audio_decode_queue_.size());
            }
            audio_decode_queue_.emplace_back(std::move(packet));
        }
    });
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
#if USING_MAR_SERVER
        //Marshall add
        this->UpdateDeviceOnlineStatus(true, "normal open");
#endif
        board.SetPowerSaveMode(false);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }

        // 启动心跳包定时器
        //StartHeartbeatTimer();

#if CONFIG_IOT_PROTOCOL_XIAOZHI
        auto& thing_manager = iot::ThingManager::GetInstance();
        protocol_->SendIotDescriptors(thing_manager.GetDescriptorsJson());
        std::string states;
        if (thing_manager.GetStatesJson(states, false)) {
            protocol_->SendIotStates(states);
        }
#endif
    });
    protocol_->OnAudioChannelClosed([this, &board]() {

 #if USING_MAR_SERVER
        //Marshall add
        this->UpdateDeviceOnlineStatus(false, "normal close");
#endif
        board.SetPowerSaveMode(true);
        Schedule([this]() {
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "");
            SetDeviceState(kDeviceStateIdle);
        });
    });
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        // Parse JSON data
        char* json_string = cJSON_Print(root);
#ifdef DEBUG_VERSON
        if (json_string) {
            ESP_LOGI(TAG, "Received JSON: %s", json_string);
            free(json_string);  // 释放内存
        }
#endif
        auto type = cJSON_GetObjectItem(root, "type");

        // 处理消息推送
        /*if (strcmp(type->valuestring, "push_message") == 0) {
            auto data = cJSON_GetObjectItem(root, "data");
            if (cJSON_IsObject(data)) {
                auto id = cJSON_GetObjectItem(data, "id");
                auto title = cJSON_GetObjectItem(data, "title");
                auto content = cJSON_GetObjectItem(data, "content");
                auto short_url = cJSON_GetObjectItem(data, "short_url");
                auto qr_code_url = cJSON_GetObjectItem(data, "qr_code_url");

                if (cJSON_IsNumber(id) && cJSON_IsString(title) && 
                    cJSON_IsString(content) && cJSON_IsString(short_url) && 
                    cJSON_IsString(qr_code_url)) {
                    DisplayMessage(id->valueint, title->valuestring, content->valuestring,
                                 short_url->valuestring, qr_code_url->valuestring);
                }
            }
            return;
        }
*/
        ESP_LOGW(TAG, "OnIncomingJson type=%s ", type->valuestring);
        if (strcmp(type->valuestring, "demo_mode") == 0) {
            // 处理演示模式协议消息
            ESP_LOGI(TAG, "Received demo_mode protocol message");
            ESP_LOGI(TAG, "About to call HandleProtocolMessage");
            
            //display->ShowNotification("演示模式");
            
            auto& demo_mode = DemoMode::GetInstance();
            
            bool result = demo_mode.HandleProtocolMessage(root, this);
            ESP_LOGI(TAG, "HandleProtocolMessage returned: %s", result ? "true" : "false");
            
            if (!result) {
                ESP_LOGW(TAG, "Failed to handle demo_mode protocol message");
            }
            
        } else if (strcmp(type->valuestring, "tts") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (strcmp(state->valuestring, "start") == 0) {
                tts_image_displayed_ = false;
                tts_session_active_ = true;
                Schedule([this]() {
                    aborted_ = false;
                    /*
                    if(device_state_ == kDeviceStateKeepConnected)
                    {
                        //PlaySound(Lang::Sounds::P3_HUANXING);
                        PlaySound(Lang::Sounds::P3_POPUP);
                        

                    }*/
                   if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening || device_state_ == kDeviceStateKeepConnected) {
                        SetDeviceState(kDeviceStateSpeaking);
                    }
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {
                if (!tts_session_active_) {
                    return;
                }
                tts_session_active_ = false;
                tts_image_displayed_ = false;
                b_playing_msg = false;
               /* if(IsInDemoMode())
                {
                    StopDemoMode();
                }
*/
                Schedule([this]() {
                    //marshall modify for 实时打断
                    //audio_decode_queue_.clear();
                    //background_task_->WaitForCompletion();
                    ESP_LOGI(TAG, "tts stop listening_mode_=%d,device_state_=%d",(int)listening_mode_,(int)device_state_);
                    if (device_state_ == kDeviceStateSpeaking) {
                        if (listening_mode_ == kListeningModeManualStop) {
                            ESP_LOGI(TAG, "#########listening_mode_ == kListeningModeManualStop");
                            SetDeviceState(kDeviceStateIdle);
                        } else {
                        #ifdef DEBUG_VERSON  
                            ESP_LOGI(TAG, "bGreetingTTS=%d", bGreetingTTS);
                        #endif   
                            if(bGreetingTTS == true)
                            {
                                bGreetingTTS = false;
                                //SetDeviceState(kDeviceStateListening);
                                //vTaskDelay(pdMS_TO_TICKS(100));
                                SetDeviceState(kDeviceStateKeepConnected);
                            }
                            else
                            {
                                SetDeviceState(kDeviceStateListening);
                            }

                        }
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
            // 检查是否在演示模式中，如果是则跳过TTS显示处理
                if (IsInDemoMode()) {
                    ESP_LOGI(TAG, "In demo mode, skipping TTS sentence_start display processing");
                    display->SetEmotion("演示模式");
                    //return;
                }
                
                // 检查是否有 data 字段
                auto data = cJSON_GetObjectItem(root, "data");
                auto text = cJSON_GetObjectItem(root, "text");
                
                // 提取需要的字符串值，避免指针悬空
                bool has_data = cJSON_IsObject(data);
                std::string notification_type_str;
                std::string text_str;
                std::string qr_code_url_str;
                int emoji_value = 0; // 添加emoji值
                
                if (has_data) {
                    auto notification_type_item = cJSON_GetObjectItem(data, "notification_type");
                    if (cJSON_IsString(notification_type_item)) {
                        notification_type_str = notification_type_item->valuestring;
                    }
                    auto qr_code_url_item = cJSON_GetObjectItem(data, "qr_code_url");
                    if (cJSON_IsString(qr_code_url_item)) {
                        qr_code_url_str = qr_code_url_item->valuestring;
                    }
                    // 添加emoji字段处理
                    auto emoji_item = cJSON_GetObjectItem(data, "emoji");
                    if (cJSON_IsNumber(emoji_item)) {
                        emoji_value = emoji_item->valueint;
                    }
/*
                    auto demo_model = cJSON_GetObjectItem(data, "demo_model");
                    if (demo_model && cJSON_IsArray(demo_model)) {
                        // 启动demo模式
                        ESP_LOGI(TAG, "#############Starting demo mode");

                        StartDemoMode(demo_model);

                    }*/
                    
                }
                if (cJSON_IsString(text)) {
                    text_str = text->valuestring;
                }
                
                // 统一的Schedule调用，使用复制的字符串值
                Schedule([this, display, has_data, notification_type_str, text_str, qr_code_url_str, emoji_value]() {
                    // 处理emoji表情显示
                    if (emoji_value > 0) {
                        auto& oled = OledEmotionTask::GetInstance();
                        std::string emotion_name;
                        
                        // 根据emoji值映射到对应的表情名称
                        switch (emoji_value) {
                            case 1:
                                emotion_name = "dongyidong";
                                PlaySound(Lang::Sounds::P3_DONGYIDONG);
                                break;
                            case 2:
                                emotion_name = "duoheshui";
                                PlaySound(Lang::Sounds::P3_DUOHESHUI);
                                break;
                            case 3:
                                emotion_name = "happy";
                                break;
                            case 4:
                                emotion_name = "idle";
                                break;
                            case 5:
                                emotion_name = "listening";
                                break;
                            case 6:
                                emotion_name = "newmsg";
                                break;
                            case 7:
                                emotion_name = "ok";
                                break;
                            case 8:
                                emotion_name = "sad";
                                break;
                            case 9:
                                emotion_name = "sleepy";
                                break;
                            case 10:
                                emotion_name = "speaking";
                                break;
                            case 11:
                                emotion_name = "speaking2";
                                break;
                            case 12:
                                emotion_name = "superhappy";
                                break;
                            default:
                                emotion_name = "idle"; // 默认表情
                                break;
                        }
                        
                        ESP_LOGI(TAG, "Setting emotion: %s (emoji: %d)", emotion_name.c_str(), emoji_value);
                        oled.SendEmotion(emotion_name.c_str(),10);
                    }
                    
                    if (has_data) {
                        if (!notification_type_str.empty()) {
                            ESP_LOGI(TAG, "notification_type=%s", notification_type_str.c_str());
                            
                            if (notification_type_str == "greeting_request") {
                                // 开机问候语：显示文本内容
                                bGreetingTTS = true;
                                if (!text_str.empty()) {
                                    display->SetChatMessage("system", text_str.c_str());
                                    display->SetEmotion("happy");
                                }
                            } else if (notification_type_str == "messages") {
                                // 新消息通知：显示消息图标
                                if (!text_str.empty()) {
                                    display->SetChatMessage("system", text_str.c_str());
                                    display->SetEmotion("happy");
                                    // 如果没有指定emoji，则使用默认的newmsg表情
                                    if (emoji_value == 0) {
                                        auto& oled = OledEmotionTask::GetInstance();
                                        oled.SendEmotion("newmsg",10);
                                    }
                                    has_new_msg = true;
                                }
                                
                                // 处理二维码：本次 TTS 会话仅展示一次
                                if (!qr_code_url_str.empty()) {
                                    if (!tts_image_displayed_) {
                                        ESP_LOGI(TAG, "qr_url=%s", qr_code_url_str.c_str());
                                        DownloadAndDisplayQRCode(qr_code_url_str);
                                        tts_image_displayed_ = true;
                                    } else {
                                        ESP_LOGI(TAG, "[TTS] 已显示过二维码，本次跳过: %s", qr_code_url_str.c_str());
                                    }
                                }
                            } else if (notification_type_str == "reading_message") {
                                // 重读消息：下载并显示二维码
                                b_playing_msg = true;
                                if (!qr_code_url_str.empty()) {
                                    if (!tts_image_displayed_) {
                                        ESP_LOGI(TAG, "qr_url=%s", qr_code_url_str.c_str());
                                        DownloadAndDisplayQRCode(qr_code_url_str);
                                        tts_image_displayed_ = true;
                                        display->SetChatMessage("system", text_str.c_str());
                                    } else {
                                        ESP_LOGI(TAG, "[TTS] 已显示过二维码，本次跳过: %s", qr_code_url_str.c_str());
                                    }
                                }
                                has_new_msg = false;
                            } else {
                                // 其他notification_type
                                if (!text_str.empty()) {
                                    display->SetChatMessage("system", text_str.c_str());
                                    display->SetEmotion("happy");
                                }
                                
                            }
                        } else {
                            // data存在但没有notification_type，按默认处理
                            if (!text_str.empty()) {
                                display->SetChatMessage("system", text_str.c_str());
                                display->SetEmotion("happy");
                            }

                            if (!qr_code_url_str.empty()) {
                                if (!tts_image_displayed_) {
                                    ESP_LOGI(TAG, "qr_url=%s", qr_code_url_str.c_str());
                                    DownloadAndDisplayQRCode(qr_code_url_str);
                                    tts_image_displayed_ = true;
                                    display->SetChatMessage("system", text_str.c_str());
                                } else {
                                    ESP_LOGI(TAG, "[TTS] 已显示过二维码，本次跳过: %s", qr_code_url_str.c_str());
                                }
                            }
                        }
                    } else {
                        // 没有data字段，直接显示text
                        if (!text_str.empty()) {
                            display->SetChatMessage("assistant", text_str.c_str());
                        }
                    }
                });
                
                return;
            }
        }
        
        else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                Schedule([this, display, message = std::string(text->valuestring)]() {
                    display->SetChatMessage("user", message.c_str());
                });

                std::string reg_text = text->valuestring;
                    
                if (reg_text.find("切换网络") != std::string::npos) {
                    Schedule([this]() {
                    HandleNetworkSwitch();
                    });

                    return;
                }

                 if (reg_text.find("工厂模式") != std::string::npos) {
                    Schedule([this]() {
                        SetDeviceState(kDeviceStateIdle);
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        EnterFactoryTestingMode();
                    });

                    return;
                }

                if (reg_text.find("设备号") != std::string::npos || reg_text.find("设备码") != std::string::npos) {
                    Schedule([this]() {
                        PlayDeviceNumber();
                    });
                    return;
                }

                if (reg_text.find("系统版本") != std::string::npos || reg_text.find("版本号") != std::string::npos|| reg_text.find("版本信息") != std::string::npos) {
                    Schedule([this]() {
                        ShowSystemVersion();
                    });
                    return;
                }

                if (reg_text.find("wifi配网") != std::string::npos || 
                    reg_text.find("配网模式") != std::string::npos || 
                    reg_text.find("无线配网") != std::string::npos) {
                    
                    Schedule([this]() {
                        HandleWifiConfig();
                    });
                    
                    return;
                }

            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());
                });
            }
#if CONFIG_IOT_PROTOCOL_MCP
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
#endif
#if CONFIG_IOT_PROTOCOL_XIAOZHI
        } else if (strcmp(type->valuestring, "iot") == 0) {
            auto commands = cJSON_GetObjectItem(root, "commands");
            if (cJSON_IsArray(commands)) {
                auto& thing_manager = iot::ThingManager::GetInstance();
                for (int i = 0; i < cJSON_GetArraySize(commands); ++i) {
                    auto command = cJSON_GetArrayItem(commands, i);
                    thing_manager.Invoke(command);
                }
            }
#endif
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // Do a reboot if user requests a OTA update
                    Schedule([this]() {
                        Reboot();
                    });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::P3_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });
    bool protocol_started = protocol_->Start();

    audio_debugger_ = std::make_unique<AudioDebugger>();
    audio_processor_->Initialize(codec);
    audio_processor_->OnOutput([this](std::vector<int16_t>&& data) {
        {
            // Mar todo
            
            std::lock_guard<std::mutex> lock(mutex_);

            if (audio_send_queue_.size() >= MAX_AUDIO_PACKETS_IN_QUEUE) {
                ESP_LOGW(TAG, "Too many audio packets in queue, drop the newest packet");
                //Mar modify:pop old packet and no return
                audio_send_queue_.pop_front();  // 移除最旧的包
                //return;
            }
        }
        background_task_->Schedule([this, data = std::move(data)]() mutable {
            opus_encoder_->Encode(std::move(data), [this](std::vector<uint8_t>&& opus) {
                AudioStreamPacket packet;
                packet.payload = std::move(opus);
#ifdef CONFIG_USE_SERVER_AEC
                {
                    std::lock_guard<std::mutex> lock(timestamp_mutex_);
                    if (!timestamp_queue_.empty()) {
                        packet.timestamp = timestamp_queue_.front();
                        timestamp_queue_.pop_front();
                    } else {
                        packet.timestamp = 0;
                    }

                    if (timestamp_queue_.size() > 3) { // 限制队列长度3
                        timestamp_queue_.pop_front(); // 该包发送前先出队保持队列长度
                        return;
                    }
                }
#endif
                std::lock_guard<std::mutex> lock(mutex_);
                //ESP_LOGI(TAG, "Send audio packet, size: %d", audio_send_queue_.size());
                if (audio_send_queue_.size() >= MAX_AUDIO_PACKETS_IN_QUEUE) {
                    ESP_LOGW(TAG, "Too many audio packets in queue, drop the oldest packet");
                    audio_send_queue_.pop_front();
                }
                audio_send_queue_.emplace_back(std::move(packet));

                
                xEventGroupSetBits(event_group_, SEND_AUDIO_EVENT);
            });
        });
    });
    audio_processor_->OnVadStateChange([this](bool speaking) {
        if (device_state_ == kDeviceStateListening) {
            Schedule([this, speaking]() {
                voice_detected_ = speaking;
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            });
        }
    });
#if !USING_MAR_SERVER
    wake_word_->Initialize(codec);
    wake_word_->OnWakeWordDetected([this](const std::string& wake_word) {
        Schedule([this, &wake_word]() {
            if (!protocol_) {
                return;
            }

            if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateKeepConnected || device_state_ == kDeviceStateSleeping) {

                if (!protocol_->IsAudioChannelOpened()) {
                    SetDeviceState(kDeviceStateConnecting);
                    if (!protocol_->OpenAudioChannel()) {
                        wake_word_->StartDetection();
                        return;
                    }
                }

                ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
                // 本地“滴”一声回应即可，唤醒词的音频和文本都不上送云端，
                // 否则云端会把它当成用户说的话，回复“我不叫小新”。
                ResetDecoder();
                PlaySound(Lang::Sounds::P3_POPUP);
                vTaskDelay(pdMS_TO_TICKS(60));
                //listening_mode_ = aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime;
                SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
            } else if (device_state_ == kDeviceStateSpeaking) {
                AbortSpeaking(kAbortReasonWakeWordDetected);
            } else if (device_state_ == kDeviceStateActivating) {
                SetDeviceState(kDeviceStateIdle);
            }
        });
    });
    wake_word_->StartDetection();
#endif
    // Wait for the new version check to finish
    //xEventGroupWaitBits(event_group_, CHECK_NEW_VERSION_DONE_EVENT, pdTRUE, pdFALSE, portMAX_DELAY);
    

    if (protocol_started) {
        std::string message = std::string(Lang::Strings::VERSION) + ota_.GetCurrentVersion();
        display->ShowNotification(message.c_str());
        display->SetChatMessage("system", "");
        // Play the success sound to indicate the device is ready
        //ResetDecoder();
        //PlaySound(Lang::Sounds::P3_SUCCESS);
    }

    // Print heap stats
    SystemInfo::PrintHeapStats();

#if USING_MAR_SERVER
    // Mar add
    if (!protocol_->IsAudioChannelOpened()) {
        SetDeviceState(kDeviceStateConnecting);
        if (!protocol_->OpenAudioChannel()) {
            //wake_word_->StartDetection();
            return;
        }
    }

    SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
     InitLvglEventSystem((void*)this);
#endif    
   
/*
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
  
    LV_IMG_DECLARE(qrcode);
    lv_obj_t *img1 = lv_img_create(scr);
    lv_img_set_src(img1, &qrcode);
*/

    

    // 初始化OLED表情显示任务
    auto& oled_task = OledEmotionTask::GetInstance();
    if (!oled_task.Initialize()) {
        ESP_LOGE(TAG, "Failed to initialize OLED emotion task");
    }
    //让任务先运行起来
    //vTaskDelay(pdMS_TO_TICKS(500));
    //SetDeviceState(kDeviceStateIdle);
    //display->SetEmotion("happy");


#ifdef MEMORY_MONITOR
    // 启动内存监控任务
    StartMemoryMonitorTask();
#endif    

    ConnectToServer();
    // SetDeviceState(kDeviceStateIdle);
    // Enter the main event loop
/*    
#if OPEN_RADARR_MODULE
    #ifdef USE_RADAR_2410D
        Radar2410D::GetInstance().ResetState();
    #else
        RadarModule::GetInstance().ResetState();
    #endif
#endif
*/
    MainEventLoop();
}

void Application::OnClockTimer() {
    clock_ticks_++;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    display->UpdateStatusBar();

    // 获取当前时间
    time_t now = time(NULL);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", localtime(&now));
    
    // 只在idle状态下更新时钟显示
    if (/*device_state_ == kDeviceStateIdle ||*/ device_state_ == kDeviceStateSleeping || device_state_ == kDeviceStateKeepConnected) {


        // 使用IsClockDisplay方法检查是否为时钟显示
        if (display->IsClockDisplay()) {
            display->UpdateClock(time_str);
        }
        
        // 如果已同步服务器时间，设置状态栏为当前时间
        if (ota_.HasServerTime() && (clock_ticks_ % 10 == 0)) {
            Schedule([this]() {
                time_t now = time(NULL);
                char time_str[64];
                strftime(time_str, sizeof(time_str), "%H:%M  ", localtime(&now));
                Board::GetInstance().GetDisplay()->SetStatus(time_str);
            });
        }
    }

    // 每10秒打印调试信息
    if (clock_ticks_ % 10 == 0) {
        SystemInfo::PrintHeapStats();
    }
}

// Add a async task to MainLoop
void Application::Schedule(std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, SCHEDULE_EVENT);
}

// The Main Event Loop controls the chat state and websocket connection
// If other tasks need to access the websocket or chat state,
// they should use Schedule to call this function
void Application::UnreadCheckTimerCallback(void* arg) {
    ESP_LOGI(TAG,"UnreadCheckTimerCallback");
    Application* app = (Application*)arg;
    app->CheckUnreadMessages();
}

void Application::CheckUnreadMessages() {
    ESP_LOGI(TAG, "Checking unread messages...");
    //if (!protocol_ || !protocol_->IsAudioChannelOpened()) 
    {
        auto http = Board::GetInstance().CreateHttp();
        
        std::string method = "POST";
        
        // 设置Content-Type为application/json
        http->SetHeader("Content-Type", "application/json");
        http->SetHeader("Accept", "application/json");
        
        // 设置请求内容
        std::string post_data = "{\"mac_address\":\"" + mac_addr + "\"}";
        http->SetContent(std::move(post_data));
        
        char url[256];
        snprintf(url, sizeof(url), "%s%s", base_url, api_messages_unread);
        ESP_LOGI(TAG, "url = %s", url);
        
        if (!http->Open(method, url)) {
            ESP_LOGE(TAG, "Failed to open HTTP connection");
            delete http;
            return;
        }
        
        ESP_LOGI(TAG, "Sending HTTP request to check unread messages...");
        auto response = http->ReadAll();
        ESP_LOGI(TAG, "HTTP request completed");
        
        if (!response.empty()) {
            ESP_LOGI(TAG, "Received message response: %s", response.c_str());
            cJSON* root = cJSON_Parse(response.c_str());
            if (!root) {
                ESP_LOGE(TAG, "Failed to parse JSON response: %s", cJSON_GetErrorPtr());
            } else if (!cJSON_IsArray(root)) {
                ESP_LOGE(TAG, "JSON response is not an array");
                cJSON_Delete(root);
            } else {
                int array_size = cJSON_GetArraySize(root);
                ESP_LOGI(TAG, "Found %d unread messages", array_size);
                
                if (array_size > 0) {
                    // 只处理最新的一条消息（数组中的第一条）
                    cJSON* message = cJSON_GetArrayItem(root, 0);
                    if (message) {
                        auto id = cJSON_GetObjectItem(message, "id");
                        auto title = cJSON_GetObjectItem(message, "title");
                        auto content = cJSON_GetObjectItem(message, "content");
                        auto short_url = cJSON_GetObjectItem(message, "short_url");
                        auto qr_code_url = cJSON_GetObjectItem(message, "qr_code_url");

                        if (cJSON_IsNumber(id) && 
                            cJSON_IsString(content) && cJSON_IsString(short_url) && 
                            cJSON_IsString(qr_code_url)) {
                            // title 可能为空，使用空字符串代替
                            const char* title_str = (title && cJSON_IsString(title)) ? title->valuestring : "";
                            ESP_LOGI(TAG, "Displaying latest message: id=%d, title=%s", id->valueint, title_str);
                            DisplayMessage(id->valueint, title_str, content->valuestring,
                                         short_url->valuestring, qr_code_url->valuestring);
                        }
                    }
                    if (array_size > 1) {
                        ESP_LOGI(TAG, "There are %d more unread messages, will display in next check", array_size - 1);
                    }
                }
                cJSON_Delete(root);
            }
        }
        else
        {
            ESP_LOGI(TAG, "No unread messages");
        }
        
        http->Close();
        delete http;
    }
}

void Application::DisplayMessage(int message_id, const std::string& title, const std::string& content,
                               const std::string& short_url, const std::string& qr_code_url) {
    ESP_LOGI(TAG, "Displaying message: id=%d, title=%s, content=%s, url=%s", 
             message_id, title.c_str(), content.c_str(), short_url.c_str());
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus("新消息");
    display->SetChatMessage("system", (title + "\n" + content).c_str());

    // 显示二维码
    if (!qr_code_url.empty()) {
        // 下载并显示二维码图片
        std::string full_url = std::string(base_url) + qr_code_url;
        ESP_LOGI(TAG, "Downloading QR code from: %s", full_url.c_str());
        DownloadAndDisplayQRCode(full_url);
    }

    // 播放提示音
    PlaySound(Lang::Sounds::P3_POPUP);

    // 发送已读状态
    SendMessageReadStatus(message_id, mac_addr);
}

void Application::SendMessageReadStatus(int message_id, const std::string& mac_address) {
    ESP_LOGI(TAG, "Sending message read status: id=%d, mac=%s", message_id, mac_address.c_str());
   /* if (protocol_ && protocol_->IsAudioChannelOpened()) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "type", "message_read");
        cJSON_AddNumberToObject(root, "message_id", message_id);
        cJSON_AddStringToObject(root, "mac_address", mac_address.c_str());
        char* json_str = cJSON_PrintUnformatted(root);
        SendMcpMessage(json_str);
        ESP_LOGI(TAG, "Sent message read status via WebSocket");
        free(json_str);
        cJSON_Delete(root);
    } else {
     */{
        char url[256];
        // 1. URL 中移除 message_id 路径参数，改为基础路径
        snprintf(url, sizeof(url), "%s%s", base_url, api_messages_read);

        auto http = Board::GetInstance().CreateHttp();
        if (!http) {
            ESP_LOGE(TAG, "HTTP客户端创建失败");
            return;
        }
        
        // 设置 HTTP 头部
        http->SetHeader("Content-Type", "application/json");
        http->SetHeader("Accept", "application/json");
        
        // 设置请求方法
        std::string method = "POST";
        
        // 2. POST 数据中添加 message_id 字段
        std::string post_data;
        // 拼接 JSON 时包含 message_id 和 mac_address
        post_data = "{\"message_id\":" + std::to_string(message_id) + ",\"mac_address\":\"" + mac_address + "\"}";
        http->SetContent(std::move(post_data));
      ESP_LOGI(TAG, "url=%s", url);  
        // 打开连接
        if (!http->Open(method, url)) {
            ESP_LOGE(TAG, "打开 HTTP 连接失败");
            delete http;
            return;
        }
        
        // 读取响应内容
        auto response = http->ReadAll();
        http->Close();
        
        if (response.empty()) {
            ESP_LOGE(TAG, "发送消息已读状态失败，HTTP 响应为空");
        } else {
            ESP_LOGI(TAG, "通过 HTTP 发送消息已读状态成功");
        }
        
        delete http;
    }
}

void Application::MainEventLoop() {
    // Raise the priority of the main event loop to avoid being interrupted by background tasks (which has priority 2)
    vTaskPrioritySet(NULL, 3);

    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, SCHEDULE_EVENT | SEND_AUDIO_EVENT, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & SEND_AUDIO_EVENT) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto packets = std::move(audio_send_queue_);
            lock.unlock();
#ifdef DEBUG_VERSON
            //ESP_LOGI(TAG, "Send audio packets: %d", packets.size());
#endif

            for (auto& packet : packets) {
                if (!protocol_->SendAudio(packet)) {
                    break;
                }
            }
        }

        if (bits & SCHEDULE_EVENT) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }
        
        // 添加工厂测试超时处理
        if (factory_test_manager_ && IsInFactoryTestingMode()) {
            factory_test_manager_->ProcessTimeout();
        }

    }
}

// The Audio Loop is used to input and output audio data
void Application::AudioLoop() {
    auto codec = Board::GetInstance().GetAudioCodec();
    while (!aborted_) {
        // 即使在显示二维码时也要保持音频输入输出功能
        OnAudioInput();
        if (codec->output_enabled()) {
            OnAudioOutput();
        }

        // 新增：统一兜底延时，确保让出CPU，避免紧凑自旋触发WDT
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void Application::OnAudioOutput() {
    if (busy_decoding_audio_) {
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
    auto now = std::chrono::steady_clock::now();
    auto codec = Board::GetInstance().GetAudioCodec();
    const int max_silence_seconds = 10;

    std::unique_lock<std::mutex> lock(mutex_);
    if (audio_decode_queue_.empty()) {
        // Disable the output if there is no audio data for a long time
        if (device_state_ == kDeviceStateIdle) {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_output_time_).count();
            if (duration > max_silence_seconds) {
                codec->EnableOutput(false);
            }
        }
        return;
    }

    auto packet = std::move(audio_decode_queue_.front());
    audio_decode_queue_.pop_front();

    lock.unlock();
    audio_decode_cv_.notify_all();

    // Synchronize the sample rate and frame duration
    SetDecodeSampleRate(packet.sample_rate, packet.frame_duration);

    busy_decoding_audio_ = true;
    background_task_->Schedule([this, codec, packet = std::move(packet)]() mutable {
        // 解码与输出结束后再清除忙标志，避免并发解码导致队列积压与CPU饥饿
        auto clear_busy = [&]() { busy_decoding_audio_ = false; };
        if (aborted_) { clear_busy(); return; }

        std::vector<int16_t> pcm;
        if (!opus_decoder_->Decode(std::move(packet.payload), pcm)) { clear_busy(); return; }
        // Resample if the sample rate is different
        if (opus_decoder_->sample_rate() != codec->output_sample_rate()) {
            int target_size = output_resampler_.GetOutputSamples(pcm.size());
            std::vector<int16_t> resampled(target_size);
            output_resampler_.Process(pcm.data(), pcm.size(), resampled.data());
            pcm = std::move(resampled);
        }
#ifdef DEBUG_VERSON        
        //ESP_LOGI(TAG, "pcm.size()=%d", pcm.size());
#endif        
        codec->OutputData(pcm);
#ifdef CONFIG_USE_SERVER_AEC
        std::lock_guard<std::mutex> lock(timestamp_mutex_);
        timestamp_queue_.push_back(packet.timestamp);
#endif
        last_output_time_ = std::chrono::steady_clock::now();
        clear_busy();
    });
}

void Application::OnAudioInput() {
    // 轻量速率限制：最多每2ms处理一次，避免短周期自旋导致CPU占用过高
    static TickType_t last_tick = 0;
    TickType_t now_tick = xTaskGetTickCount();
    if (now_tick - last_tick < pdMS_TO_TICKS(2)) {
        vTaskDelay(pdMS_TO_TICKS(2));
        return;
    }
    last_tick = now_tick;

    if (device_state_ == kDeviceStateAudioTesting) {
        if (audio_testing_queue_.size() >= AUDIO_TESTING_MAX_DURATION_MS / OPUS_FRAME_DURATION_MS) {
            ExitAudioTestingMode();
            // 早退路径加入短暂延时，避免紧凑循环占满CPU
            vTaskDelay(pdMS_TO_TICKS(2));
            return;
        }
        std::vector<int16_t> data;
        int samples = OPUS_FRAME_DURATION_MS * 16000 / 1000;
        if (ReadAudio(data, 16000, samples)) {
            background_task_->Schedule([this, data = std::move(data)]() mutable {
                opus_encoder_->Encode(std::move(data), [this](std::vector<uint8_t>&& opus) {
                    AudioStreamPacket packet;
                    packet.payload = std::move(opus);
                    packet.frame_duration = OPUS_FRAME_DURATION_MS;
                    packet.sample_rate = 16000;
                    std::lock_guard<std::mutex> lock(mutex_);
                    audio_testing_queue_.push_back(std::move(packet));
                });
            });
            // 早退路径加入短暂延时
            vTaskDelay(pdMS_TO_TICKS(2));
            return;
        }
    }

    if (wake_word_->IsDetectionRunning()) {
        std::vector<int16_t> data;
        int samples = wake_word_->GetFeedSize();
        if (samples > 0) {
            if (ReadAudio(data, 16000, samples)) {
                wake_word_->Feed(data);
                // 早退路径加入短暂延时
                vTaskDelay(pdMS_TO_TICKS(2));
                return;

                
            }
        }
    }

    if (audio_processor_->IsRunning()) {
        //在保持连接状态下，不处理音频输入
        if(b_playing_msg == true || IsInDemoMode() == true) {
            //ESP_LOGI(TAG, "b_playing_msg == true, return");
            vTaskDelay(pdMS_TO_TICKS(2));
            return;
        }
        if(device_state_ == kDeviceStateKeepConnected) {
            // 检查是否超过5秒
            uint32_t current_time = esp_timer_get_time() / 1000; // 转换为毫秒
            uint32_t time_diff = current_time - keep_connected_start_time_;

            if (current_time - keep_connected_start_time_ > 5000) { // 5秒 = 5000毫秒
#ifdef DEBUG_VERSON
                ESP_LOGI(TAG, "##########keep connected timeout after 5 seconds, returning");
#endif
                //xEventGroupSetBits(event_group_, SEND_AUDIO_EVENT);
                //vTaskDelay(pdMS_TO_TICKS(100));
                wake_word_->StartDetection();
                vTaskDelay(pdMS_TO_TICKS(2));
                return;
            }
            //return;
        }

        std::vector<int16_t> data;
        int samples = audio_processor_->GetFeedSize();
        if (samples > 0) {
            if (ReadAudio(data, 16000, samples)) {
                // 添加滑窗音量检测
                
               // if (!ShouldSendAudioWithVAD(data.data())) {
                    //ESP_LOGI(TAG, "********[VOLUME_CHECK] Audio volume below threshold, not sending");
               //     return;  // 音量低于阈值，不发送音频
               // }
               
                //ESP_LOGI(TAG, "[VOLUME_CHECK] Audio volume above threshold, sending audio");

                audio_processor_->Feed(data);
                vTaskDelay(pdMS_TO_TICKS(2));
                return;
            }
        }
    }

    vTaskDelay(pdMS_TO_TICKS(OPUS_FRAME_DURATION_MS / 2));
}

bool Application::ReadAudio(std::vector<int16_t>& data, int sample_rate, int samples) {
    auto codec = Board::GetInstance().GetAudioCodec();
    if (!codec->input_enabled()) {
        return false;
    }

    if (codec->input_sample_rate() != sample_rate) {
        data.resize(samples * codec->input_sample_rate() / sample_rate);
        if (!codec->InputData(data)) {
            return false;
        }
        if (codec->input_channels() == 2) {
            auto mic_channel = std::vector<int16_t>(data.size() / 2);
            auto reference_channel = std::vector<int16_t>(data.size() / 2);
            for (size_t i = 0, j = 0; i < mic_channel.size(); ++i, j += 2) {
                mic_channel[i] = data[j];
                reference_channel[i] = data[j + 1];
            }
            auto resampled_mic = std::vector<int16_t>(input_resampler_.GetOutputSamples(mic_channel.size()));
            auto resampled_reference = std::vector<int16_t>(reference_resampler_.GetOutputSamples(reference_channel.size()));
            input_resampler_.Process(mic_channel.data(), mic_channel.size(), resampled_mic.data());
            reference_resampler_.Process(reference_channel.data(), reference_channel.size(), resampled_reference.data());
            data.resize(resampled_mic.size() + resampled_reference.size());
            for (size_t i = 0, j = 0; i < resampled_mic.size(); ++i, j += 2) {
                data[j] = resampled_mic[i];
                data[j + 1] = resampled_reference[i];
            }
        } else {
            auto resampled = std::vector<int16_t>(input_resampler_.GetOutputSamples(data.size()));
            input_resampler_.Process(data.data(), data.size(), resampled.data());
            data = std::move(resampled);
        }
    } else {
        data.resize(samples);
        if (!codec->InputData(data)) {
            return false;
        }
    }
    
    // 音频调试：发送原始音频数据
    if (audio_debugger_) {
        audio_debugger_->Feed(data);
    }
    
    return true;
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    protocol_->SendAbortSpeaking(reason);
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

void Application::SetDeviceState(DeviceState state) {
    if (device_state_ == state /*|| is_show_qrcode == true*/) {
        return;
    }
    
    clock_ticks_ = 0;
    auto previous_state = device_state_;

    if(state == kDeviceStateKeepConnected) {
        keep_connected_start_time_ = esp_timer_get_time() / 1000; // 转换为毫秒
#ifdef DEBUG_VERSON        
        ESP_LOGI(TAG, "SetDeviceState kDeviceStateKeepConnected: new_time=%d", 
                      (int)keep_connected_start_time_);
#endif                      
    }

    device_state_ = state;
    ESP_LOGI(TAG, "##### SetDeviceState STATE: %s", STATE_STRINGS[device_state_]);
    // The state is changed, wait for all background tasks to finish
    background_task_->WaitForCompletion();

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto lcd_display = static_cast<LcdDisplay*>(display);
    
    //CustomOledGifEmojiDisplay* custom_oled_display = (CustomOledGifEmojiDisplay*)oled_display;

    auto& oled_task = OledEmotionTask::GetInstance();
    //auto oleddisplay = board.GetOledDisplay();

    auto led = board.GetLed();
    led->OnStateChanged();

    char* network_icon = (char*)board.GetNetworkStateIcon();
    int over_time = 0;

    if (listening_state_timer_ != nullptr) {
        esp_timer_stop(listening_state_timer_);
        esp_timer_delete(listening_state_timer_);
        listening_state_timer_ = nullptr;
    }
    esp_timer_create_args_t listening_timer_args = {
                .callback = ListeningStateTimerCallback,
                .arg = this,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "listening_state_timer"
            };
#if OPEN_RADARR_MODULE 
    PersonState current_person_state_ = PersonState::NOBODY;
#endif            
    switch (state) {
        case kDeviceStateSleeping:
            if(has_new_msg){
                oled_task.SendEmotion("newmsg");
            }else{
                oled_task.SendEmotion("sleepy");
            }

            oled_task.SendChatMessage("system", "");
            break;
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);
            lcd_display->SetEmotionWithScale("neutral",300);
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
            // 使用任务安全地设置OLED表情
            if(has_new_msg){
                oled_task.SendEmotion("newmsg");
            }else{
                oled_task.SendEmotion("idle");
            }
            oled_task.SendChatMessage("system", "");
            audio_processor_->Stop();
            wake_word_->StartDetection();
            SwitchDisplayMode(false); // 切换时钟模式
            
            StopHeartbeatTimer();

            // 检查是否有待播放的久坐提醒
            CheckPendingLongSittingReminder();
 #if OPEN_RADARR_MODULE 
            current_person_state_ = PersonState::NOBODY;
            #ifdef USE_RADAR_2410D
                current_person_state_ = Radar2410D::GetInstance().GetPersonState();
            #else
                current_person_state_ = RadarModule::GetInstance().GetPersonState();
            #endif
            if(current_person_state_ != PersonState::NOBODY &&
               current_person_state_ != PersonState::PERSON_LEAVING)
            {
                #ifdef DEBUG_VERSON
                ESP_LOGI(TAG, "keep connected current_person_state_ =%d", (int)current_person_state_);
                #endif
                ConnectToServer();
            }
#endif
            break;
        case kDeviceStateKeepConnected:
            display->SetStatus("待命中");
            lcd_display->SetChatMessage("system", "");
            lcd_display->SetEmotionWithScale("happy",300);

            if(has_new_msg){
                oled_task.SendEmotion("newmsg");
            }else{
                oled_task.SendEmotion("idle");
            }
            
            oled_task.SendChatMessage("system", "");

            if(previous_state != kDeviceStateListening)
            {
                wake_word_->StartDetection();
            }
            
            SwitchDisplayMode(false); // 切换时钟模式

            ESP_LOGI(TAG, "SetDeviceState kDeviceStateListening bFirstBoot %d", bFirstBoot);

            if(bFirstBoot == true)
            {
                bFirstBoot = false;
                //注册设备
                //protocol_->SendGreetingRequest("device_register", "physical_device");

                //发送问候语请求
                Schedule([this]() {
                    protocol_->SendGreetingRequest("greeting_request", "regular_boot");
                });
                
            }

            ESP_LOGI(TAG, "Audio channel opened: %s", protocol_->IsAudioChannelOpened() ? "true" : "false");
            // 启动心跳包定时器
            StartHeartbeatTimer();

            // 检查是否有待播放的久坐提醒
            CheckPendingLongSittingReminder();
            break;

        case kDeviceStateConnecting:
            
            if (strcmp(network_icon, FONT_AWESOME_SIGNAL_OFF) == 0)
            {
                ESP_LOGI(TAG, "Network not ready on wake word, scheduling delayed connection");
                board.SetPowerSaveMode(false);
                while(strcmp(network_icon, FONT_AWESOME_SIGNAL_OFF) == 0 && over_time < 4000)
                {
                    network_icon = (char*)board.GetNetworkStateIcon();
                    vTaskDelay(pdMS_TO_TICKS(100));
                    over_time = over_time + 100;
                }
                ESP_LOGI(TAG, "network recovery %d", over_time);
            }
           

            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            oled_task.SendEmotion("listening");
            timestamp_queue_.clear();
            break;
        
        case kDeviceStateListening:
            if (esp_timer_create(&listening_timer_args, &listening_state_timer_) == ESP_OK) {
                esp_timer_start_once(listening_state_timer_, 25 * 1000 * 1000); // 15秒，单位微秒
                ESP_LOGI(TAG, "Started listening state timer for 10 seconds");
            }
            
            // Update the IoT states before sending the start listening command
#if CONFIG_IOT_PROTOCOL_XIAOZHI
            UpdateIotStates();
#endif
            wake_word_->StopDetection();
            // Make sure the audio processor is running
            if (!audio_processor_->IsRunning()) {
                // Send the start listening command
#ifdef DEBUG_VERSON
            ESP_LOGI(TAG, "SetDeviceState kDeviceStateListening RESET audio");
#endif
                protocol_->SendStartListening(listening_mode_);
                if (previous_state == kDeviceStateSpeaking) {
                    audio_decode_queue_.clear();
                    audio_decode_cv_.notify_all();
                    // 短暂等待扬声器缓冲切换，减少唤醒打断延迟
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                opus_encoder_->ResetState();
                audio_processor_->Start();
                //wake_word_->StopDetection();
            }
            //ESP_LOGI(TAG, "SetDeviceState kDeviceStateListening");
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");
            oled_task.SendEmotion("listening");
            //custom_oled_display->SetEmotion("anger");
            // 不再切换到时钟模式，保持当前显示状态
            // 删除或注释掉下面这行
            SwitchDisplayMode(false); // 切换时钟模式

            ESP_LOGI(TAG, "Audio channel opened: %s", protocol_->IsAudioChannelOpened() ? "true" : "false");
            break;
        case kDeviceStateSpeaking:            
            display->SetStatus(Lang::Strings::SPEAKING);
            display->SetEmotion("neutral");
            oled_task.SendEmotion("speaking");
            //custom_oled_display->SetEmotion("happy");
            SwitchDisplayMode(false); // 切换到聊天模式

            // 播放时停止通信音频上传，仅保留唤醒词打断，避免残回声误触发
            audio_processor_->Stop();
#if CONFIG_USE_AFE_WAKE_WORD
            wake_word_->StartDetection();
#else
            wake_word_->StopDetection();
#endif
            ResetDecoder();
            break;
        case kDeviceStateFactoryTesting:
            display->SetStatus("工厂测试模式");
            display->SetEmotion("neutral");
            //custom_oled_display->SetEmotion("idle");
            // 停止可能冲突的服务
            audio_processor_->Stop();
            wake_word_->StopDetection();
            // 确保音频输出被启用，以便工厂测试中的声音提示能正常播放
            {
                auto codec = Board::GetInstance().GetAudioCodec();
                if (codec && !codec->output_enabled()) {
                    codec->EnableOutput(true);
                    ESP_LOGI(TAG, "Audio output enabled for factory testing mode");
                }
            }
            // 不切换显示模式，保持当前状态
            break;
/*
        case kDeviceStateDemo:
            display->SetStatus("演示模式");
            // 停止录音和唤醒词检测
            audio_processor_->Stop();
            wake_word_->StopDetection();
            // 停止心跳定时器
            StopHeartbeatTimer();
            ESP_LOGI(TAG, "Entered demo mode");
            break;
*/
        default:
            // Do nothing
            break;
    }
}

void Application::ResetDecoder() {
    std::lock_guard<std::mutex> lock(mutex_);
    opus_decoder_->ResetState();
    audio_decode_queue_.clear();
    audio_decode_cv_.notify_all();
    last_output_time_ = std::chrono::steady_clock::now();
    auto codec = Board::GetInstance().GetAudioCodec();
    codec->EnableOutput(true);
}

void Application::SetDecodeSampleRate(int sample_rate, int frame_duration) {
    if (opus_decoder_->sample_rate() == sample_rate && opus_decoder_->duration_ms() == frame_duration) {
        return;
    }

    opus_decoder_.reset();
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(sample_rate, 1, frame_duration);

    auto codec = Board::GetInstance().GetAudioCodec();
    if (opus_decoder_->sample_rate() != codec->output_sample_rate()) {
        ESP_LOGI(TAG, "Resampling audio from %d to %d", opus_decoder_->sample_rate(), codec->output_sample_rate());
        output_resampler_.Configure(opus_decoder_->sample_rate(), codec->output_sample_rate());
    }
}

void Application::UpdateIotStates() {
#if CONFIG_IOT_PROTOCOL_XIAOZHI
    auto& thing_manager = iot::ThingManager::GetInstance();
    std::string states;
    if (thing_manager.GetStatesJson(states, true)) {
        protocol_->SendIotStates(states);
    }
#endif
}

void Application::HandleNetworkSwitch() {
    ESP_LOGI(TAG, "检测到切换网络指令，执行网络切换");
    
    // 获取板卡实例并检查是否支持网络切换
    auto& board = Board::GetInstance();
    std::string board_type = board.GetBoardType();
    ESP_LOGI(TAG, "board_type=%s", board_type.c_str());
    
    // 检查是否为支持双网络的板卡
    if (board_type.find("DualNetwork") != std::string::npos) {
        // 转换为 DualNetworkBoard 指针
        DualNetworkBoard* dual_board = static_cast<DualNetworkBoard*>(&board);
        if (dual_board) {
            // 创建延迟任务来执行网络切换，确保响应能够发送
            auto switch_task = [](void* param) {
                vTaskDelay(pdMS_TO_TICKS(2000));  // 等待2秒确保响应发送完成
                DualNetworkBoard* board_ptr = static_cast<DualNetworkBoard*>(param);
                ESP_LOGI(TAG, "开始执行网络切换");
                board_ptr->SwitchNetworkType();
                vTaskDelete(NULL);
            };
            
            xTaskCreate(switch_task, "network_switch_task", 2048, dual_board, 5, NULL);
        } else {
            ESP_LOGW(TAG, "板卡类型转换失败，无法执行网络切换");
        }
    } else {
        ESP_LOGW(TAG, "当前板卡不支持网络切换功能: %s", board_type.c_str());
    }
}

void Application::HandleWifiConfig() {
    ESP_LOGI(TAG, "检测到wifi配网指令");
    
    // 获取板卡实例
    auto& board = Board::GetInstance();
    std::string board_type = board.GetBoardType();
    
    // 检查当前网络是否为WiFi模式
    bool is_wifi_mode = false;
    
    if (board_type.find("DualNetwork") != std::string::npos) {
        // 双网络板卡，检查当前网络类型
        DualNetworkBoard* dual_board = static_cast<DualNetworkBoard*>(&board);
        if (dual_board && dual_board->GetNetworkType() == NetworkType::WIFI) {
            is_wifi_mode = true;
        }
    } else if (board_type == "wifi") {
        // 纯WiFi板卡
        is_wifi_mode = true;
    }
    
    if (is_wifi_mode) {
        ESP_LOGI(TAG, "当前处于WiFi模式，进入配网模式");
        
        // 创建延迟任务来执行WiFi配网，确保响应能够发送
        auto config_task = [](void* param) {
            vTaskDelay(pdMS_TO_TICKS(2000));  // 等待2秒确保响应发送完成
            
            auto& board = Board::GetInstance();
            std::string board_type = board.GetBoardType();
            
            if (board_type.find("DualNetwork") != std::string::npos) {
                // 双网络板卡，获取WiFi板卡实例
                DualNetworkBoard* dual_board = static_cast<DualNetworkBoard*>(&board);
                if (dual_board) {
                    WifiBoard* wifi_board = static_cast<WifiBoard*>(&dual_board->GetCurrentBoard());
                    if (wifi_board) {
                        ESP_LOGI(TAG, "开始执行WiFi配网");
                        wifi_board->ResetWifiConfiguration();
                    }
                }
            } else {
                // 纯WiFi板卡
                WifiBoard* wifi_board = static_cast<WifiBoard*>(&board);
                if (wifi_board) {
                    ESP_LOGI(TAG, "开始执行WiFi配网");
                    wifi_board->ResetWifiConfiguration();
                }
            }
            
            vTaskDelete(NULL);
        };
        
        xTaskCreate(config_task, "wifi_config_task", 2048, nullptr, 5, NULL);
    } else {
        ESP_LOGW(TAG, "当前不处于WiFi模式，无法进行WiFi配网。当前板卡类型: %s", board_type.c_str());
        Settings settings("wifi", true);
        settings.SetInt("force_ap", 1);     
        HandleNetworkSwitch();
    }
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    esp_restart();
}

void Application::SwitchDisplayMode(bool clock_mode) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    
    if (clock_mode) {
        // 切换到时钟模式
        if (display->IsClockDisplay()) {
            // 如果是ClockDisplay，调用ShowClock方法
            ClockDisplay* clock_display = static_cast<ClockDisplay*>(display);
            if (clock_display) {
                clock_display->ShowClock(true);
            }
            
            time_t now = time(NULL);  // 获取本地时间
            char time_str[64];
            strftime(time_str, sizeof(time_str), "%H:%M:%S", localtime(&now));  // 格式化本地时间
            display->UpdateClock(time_str);
        }
    } else {
        // 切换到聊天模式
        if (display->IsClockDisplay()) {
            // 如果是ClockDisplay，隐藏时钟
            ClockDisplay* clock_display = static_cast<ClockDisplay*>(display);
            if (clock_display) {
                clock_display->ShowClock(false);
            }
        }
    }
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    if (device_state_ == kDeviceStateIdle) {
        ToggleChatState();
        Schedule([this, wake_word]() {
            if (protocol_) {
                protocol_->SendWakeWordDetected(wake_word); 
            }
        }); 
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {   
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

bool Application::CanEnterSleepMode() {
   if (device_state_ != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

void Application::SendMcpMessage(const std::string& payload) {
    Schedule([this, payload]() {
        if (protocol_) {
            protocol_->SendMcpMessage(payload);
        }
    });
}

void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_processor_->EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_processor_->EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_processor_->EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }

        // If the AEC mode is changed, close the audio channel
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}
void Application::show_device_msg(const char * message)
{
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();

    if(message != NULL)
    {
        display->SetStatus(message);
    }
    
}

int Application::query_device_status(const char *mac_addr, DeviceData *data) {
    auto http = Board::GetInstance().CreateHttp();
    
    //http->SetHeader("Content-Type", "application/x-www-form-urlencoded");
    //http->SetHeader("Accept", "application/json");

    std::string method = "POST";

    // 修改Content-Type为application/json
    http->SetHeader("Content-Type", "application/json");
    http->SetHeader("Accept", "application/json");
    
    // 设置请求内容
    std::string post_data = "{\"mac_address\":\"" + std::string(mac_addr) + "\"}";
    http->SetContent(std::move(post_data));
    char url[256];
    snprintf(url, sizeof(url), "%s%s", base_url, api_device_init);
    if (!http->Open(method, url)) {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        delete http;
        return false;
    }
    
    auto response = http->ReadAll();
    ESP_LOGI("Query", "response: %s", response.c_str());

    if (parse_device_status(response.c_str(), data) == 0) {
        http->Close();
        return 0;
    } else {
        ESP_LOGE("Query", "JSON 解析失败");
    }

    return -1;
}
int Application::parse_device_status(const char *json_str, DeviceData *data) {
    cJSON *root = cJSON_Parse(json_str);
    
    ESP_LOGI("Query", "json_str = %s",json_str);
    if (root == NULL) {
        ESP_LOGE("JSON", "JSON 解析失败: %s", cJSON_GetErrorPtr());
        return -1;
    }
    
    cJSON *status_json = cJSON_GetObjectItem(root, "status");
    if (cJSON_IsString(status_json)) {
        strncpy(data->status, status_json->valuestring, sizeof(data->status) - 1);

        data->status[strlen(data->status)] = '\0';
    }

    
    if (strcmp(data->status, "UNBOUND") == 0) {

        cJSON *bind_code_json = cJSON_GetObjectItem(root, "bind_code");
        if (cJSON_IsString(bind_code_json)) {
            strncpy(data->bind_code, bind_code_json->valuestring, sizeof(data->bind_code) - 1);
            data->bind_code[strlen(data->bind_code)] = '\0';
        }

    } else if (strcmp(data->status, "BOUND") == 0) {
        cJSON *id_json = cJSON_GetObjectItem(root, "user_id");
        if (cJSON_IsNumber(id_json)) {
            data->user_id = id_json->valueint;
        }
    }
    
    return 0;
}


void Application::UpdateDeviceOnlineStatus(bool online,std::string reason)
{
    auto http = Board::GetInstance().CreateHttp();
    
    //http->SetHeader("Content-Type", "application/x-www-form-urlencoded");
    //http->SetHeader("Accept", "application/json");

    std::string method = "POST";

    //char* url;
    char url[1024];
    if(online) {
        snprintf(url, sizeof(url), "%s%s", base_url, api_device_online);
    } else {
        snprintf(url, sizeof(url), "%s%s", base_url, api_device_offline);
    }
    
    // 修改Content-Type为application/json
    http->SetHeader("Content-Type", "application/json");
    http->SetHeader("Accept", "application/json");

    // 设置请求内容
    std::string post_data;
    if(online) {
        post_data = "{\"mac_address\":\"" + mac_addr + "\",\"network_type\":\"" + network_type + "\"}";
    } else {
        post_data = "{\"mac_address\":\"" + mac_addr + "\",\"reason\":\"" + reason + "\"}";
    }
    http->SetContent(std::move(post_data));
    if (!http->Open(method, url)) {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        delete http;
        return;
    }
    
    auto response = http->ReadAll();
    ESP_LOGI("Query", "response: %s", response.c_str());

    return;
}


void Application::device_bind_task() {
    //DeviceData data;
    bool device_bound = false;
    //const int MAX_RETRIES = 10; // 最大重试次数
    
    while (!device_bound) {
        //this->mac_addr = SystemInfo::GetMacAddress().c_str();
        ESP_LOGI("Bind", "MAC 地址: %s", this->mac_addr.c_str());
        
        if (query_device_status(this->mac_addr.c_str(), &this->deviceData) == 0) {
            if (strcmp(this->deviceData.status, "BOUND") == 0) {
                ESP_LOGI("Bind", "设备已绑定，用户ID: %d,mac=%s", this->deviceData.user_id,this->mac_addr.c_str());
                device_bound = true;

                show_device_msg("设备已绑定");
                break;
            } else if (strcmp(this->deviceData.status, "UNBOUND") == 0) {
                ESP_LOGI("Bind", "设备未绑定，绑定码: %s", this->deviceData.bind_code);
                //show_device_msg(this->deviceData.bind_code);
                this->ShowActivationCode("绑定码是:", this->deviceData.bind_code);
                
                // 如果设备仍未绑定，重新获取绑定码
                ESP_LOGI("Bind", "重新获取绑定码");
            } else {
                ESP_LOGW("Bind", "设备状态未知: %s", this->deviceData.status);
            }
        } else {
            ESP_LOGE("Bind", "查询设备状态失败");
        }

        
        vTaskDelay(pdMS_TO_TICKS(10000)); // 等待5秒后重试
    }
}

// 在Application类的初始化或适当位置添加
void Application::start_device_bind_process() {
    xTaskCreate([](void* arg) {
        Application* app = (Application*)arg;
        app->device_bind_task();
        vTaskDelete(NULL);
    }, "device_bind_task", 4096 * 2, this, 1, nullptr);
}

#include <sstream>

int Application::CheckBindQrCodeStatus(const std::string& baseUrl, std::string& qr_code_url) {
    auto& board = Board::GetInstance();
    auto http = std::unique_ptr<Http>(board.CreateHttp());
    http->SetHeader("Content-Type", "application/json");
    http->SetHeader("Accept", "application/json");

    std::string url = baseUrl + "/api/v1/ai-device/get-bind-qrcode";
    std::string post_data = std::string("{") + "\"mac\":\"" + mac_addr + "\"}";
    http->SetContent(std::move(post_data));
    if (!http->Open("POST", url)) {
        return -1;
    }
    int status_code = http->GetStatusCode();
    if (status_code != 200) {
        http->Close();
        return -1;
    }
    std::string response = http->ReadAll();
    ESP_LOGI("Query", "####response: %s", response.c_str());

    http->Close();

    cJSON* root = cJSON_Parse(response.c_str());
    if (!root) {
        return -1;
    }
    int status = -1;
    auto status_item = cJSON_GetObjectItem(root, "status");
    if (status_item && cJSON_IsNumber(status_item)) {
        status = status_item->valueint;
    }
    auto data_item = cJSON_GetObjectItem(root, "data");
    if (data_item && cJSON_IsObject(data_item)) {
        auto qr_item = cJSON_GetObjectItem(data_item, "bind_qr_code");
        if (qr_item && cJSON_IsString(qr_item)) {
            qr_code_url = qr_item->valuestring;
        }
    }
    cJSON_Delete(root);
    return status;
}

void Application::StartBindQrCodePolling(const std::string& baseUrl) {
    struct BindQrPollParams { Application* app; std::string baseUrl; };
    auto* params = new BindQrPollParams{ this, baseUrl };
    xTaskCreate([](void* arg) {
        auto* p = static_cast<BindQrPollParams*>(arg);
        Application* app = p->app;
        const std::string& host = p->baseUrl;
        while (true) {
            std::string qr_url;
            int status = app->CheckBindQrCodeStatus(host, qr_url);
            if (status == 2) {
                app->show_device_msg("设备已绑定");
                app->Schedule([]() {
                    auto& board = Board::GetInstance();
                    auto display = board.GetDisplay();
                    display->ClearImageProtection();
                    cleanupPreviousImage();
                    display->SetEmotion("happy");
                });
                xEventGroupSetBits(app->event_group_, DEVICE_BOUND_EVENT);
                break;
            }
            if (status == 0) {
                if (!app->qr_code_shown_ && !qr_url.empty()) {
                    app->last_bind_qr_code_url_ = qr_url;
                    app->DownloadAndDisplayQRCode(qr_url, 3600);
                    app->qr_code_shown_ = true;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10000));
        }
        delete p;
        vTaskDelete(NULL);
    }, "bind_qrcode_poll", 4096 * 2, params, 3, nullptr);
}


#include "mbedtls/base64.h"  // 包含Mbed TLS的Base64头文件

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

#include "lvgl.h"


Application * pGlobal_app;

// 在主线程中执行LVGL操作的函数
void RunOnMainThread(std::function<void()> task) {
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        // 如果当前在主线程，直接执行
        if (xTaskGetCurrentTaskHandle() == main_task_handle) {
            task();
            return;
        }
        
        // 否则，使用队列发送到主线程
        xQueueSendToBack(lvgl_event_queue, &task, portMAX_DELAY);
    }
}


void ImageTimeoutCallback(TimerHandle_t timer) {
    ESP_LOGI(TAG,"ImageTimeoutCallback");
    // 只发送事件到队列，不在定时器回调中执行复杂操作
    if (lvgl_event_queue) {
        uint32_t event_id = 2; // 使用不同的事件ID表示清理图像
        // 使用xQueueSendFromISR确保在中断上下文中安全
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(lvgl_event_queue, &event_id, &xHigherPriorityTaskWoken);
        // 如果发送事件导致更高优先级的任务就绪，则请求上下文切换
        if (xHigherPriorityTaskWoken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}

// 设置图像超时
void SetupImageTimeout() {
    // 如果已有定时器，先删除
    if (image_timeout_timer) {
        xTimerDelete(image_timeout_timer, 0);
    }
    
    // 创建新的定时器，3秒后触发
    image_timeout_timer = xTimerCreate(
        "image_timeout",
        pdMS_TO_TICKS(5000),
        pdFALSE,
        NULL,
        ImageTimeoutCallback
    );
    
    if (image_timeout_timer) {
        xTimerStart(image_timeout_timer, 0);
    }
}

/**
 * 从Base64编码数据创建图像描述符（使用mbedtls_base64_decode）
 * @param base64_data Base64编码的RGB565图像数据
 * @param data_len 数据长度
 * @param width 图像宽度
 * @param height 图像高度
 * @return 成功返回lv_image_dsc_t指针，失败返回NULL
 */
lv_image_dsc_t* createImageDescriptorFromBase64(const char *base64_data, size_t data_len, uint16_t width, uint16_t height) {
    // 计算解码后数据的长度（RGB565每个像素2字节）
    size_t expected_decoded_len = width * height * 2;
    ESP_LOGE(TAG, "createImageDescriptorFromBase64 expected_decoded_len = %d",expected_decoded_len);
    // 分配内存存储解码后的数据
    dynamic_image_data = (uint8_t *)heap_caps_malloc(expected_decoded_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (dynamic_image_data == NULL) {
        ESP_LOGE(TAG, "内存分配失败");
        return NULL;
    }
    
    
    // 使用mbedtls_base64_decode进行解码
    size_t actual_decoded_len = 0;
    int ret = mbedtls_base64_decode(
        dynamic_image_data,         // 输出缓冲区
        expected_decoded_len,       // 输出缓冲区大小
        &actual_decoded_len,        // 实际解码长度
        (const unsigned char*)base64_data, // 输入Base64数据（转换为unsigned char*）
        data_len                    // 输入数据长度
    );
    
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_base64_decode失败，错误码: %d", ret);
        heap_caps_free(dynamic_image_data);
        return NULL;
    }
    
    // 验证解码后的数据长度是否符合预期
    if (actual_decoded_len != expected_decoded_len) {
        ESP_LOGE(TAG, "Base64解码长度不匹配: 期望 %zu 字节，实际 %zu 字节", 
                 expected_decoded_len, actual_decoded_len);
        heap_caps_free(dynamic_image_data);
        return NULL;
    }
    
    // 初始化图像描述符
    dynamic_image.header.cf = LV_COLOR_FORMAT_RGB565;
    dynamic_image.header.magic = LV_IMAGE_HEADER_MAGIC;
    dynamic_image.header.w = width;
    dynamic_image.header.h = height;
    dynamic_image.data_size = actual_decoded_len;
    dynamic_image.data = dynamic_image_data;
    
    return &dynamic_image;
}

/**
 * 从JSON解析图像数据并创建图像描述符
 * @param root JSON根对象
 * @return 成功返回lv_image_dsc_t指针，失败返回NULL
 */
lv_image_dsc_t* createImageDescriptorFromJson(const cJSON *root) {
    if (!root) {
        ESP_LOGE(TAG, "无效的JSON根对象");
        return NULL;
    }
    
    // 提取宽度和高度
    cJSON *width_obj = cJSON_GetObjectItem(root, "width");
    cJSON *height_obj = cJSON_GetObjectItem(root, "height");
    cJSON *data_obj = cJSON_GetObjectItem(root, "data");
    
    if (!width_obj || !height_obj || !data_obj) {
        ESP_LOGE(TAG, "缺少必要的JSON字段");
        return NULL;
    }
    
    if (!cJSON_IsNumber(width_obj) || !cJSON_IsNumber(height_obj) || !cJSON_IsString(data_obj)) {
        ESP_LOGE(TAG, "JSON字段类型错误");
        return NULL;
    }
    
    uint16_t width = width_obj->valueint;
    uint16_t height = height_obj->valueint;
    const char *base64_data = data_obj->valuestring;
    size_t data_len = strlen(base64_data);
    
    ESP_LOGI(TAG, "收到图像数据: %dx%d, Base64大小: %d 字节", width, height, data_len);
    
    // 创建图像描述符
    return createImageDescriptorFromBase64(base64_data, data_len, width, height);
}
/**
 * 清理动态分配的图像内存

 */

// 清理之前的图像对象和内存
void cleanupPreviousImage() {
    ESP_LOGI(TAG, "开始清理之前的图像");
    
    // 清理当前显示的图像对象
    if (current_image_obj != NULL) {
        ESP_LOGI(TAG, "删除当前图像对象");
        lv_obj_del(current_image_obj);
        current_image_obj = NULL;
    }
    
    // 清理当前图像描述符
    if (current_image_dsc != NULL) {
        ESP_LOGI(TAG, "清理当前图像描述符");
        current_image_dsc = NULL;
    }
    
    // 不再手动管理dynamic_image_data
    // 如果dynamic_image.data存在，释放它
    if (dynamic_image.data != NULL) {
        ESP_LOGI(TAG, "释放动态图像数据: %p", dynamic_image.data);
        heap_caps_free((void*)dynamic_image.data);
        memset(&dynamic_image, 0, sizeof(dynamic_image));
    }
    
    ESP_LOGI(TAG, "图像清理完成");
}

/**
 * 显示图像
 * @param img_dsc 图像描述符
 */
void displayImage(lv_image_dsc_t* img_dsc) {
    ESP_LOGE(TAG, "displayImage");
    if (!img_dsc) {
        ESP_LOGE(TAG, "无效的图像描述符");
        return;
    }

    // 清理之前的图像对象和内存
    //cleanupPreviousImage();

    /**/
    // 获取当前活动屏幕并清空
    lv_obj_t *scr = lv_scr_act();
    //lv_obj_clean(scr);
    
    // 创建图像对象并设置源
    lv_obj_t *img1 = lv_img_create(scr);
    lv_img_set_src(img1, img_dsc);
    lv_obj_center(img1);
    
    // 设置图像样式（可选）
    static lv_style_t style_img;
    lv_style_init(&style_img);
    lv_style_set_border_width(&style_img, 2);
    lv_style_set_border_color(&style_img, lv_palette_main(LV_PALETTE_BLUE));
    lv_obj_add_style(img1, &style_img, 0);
    
    ESP_LOGI(TAG, "图像显示成功: %dx%d", img_dsc->header.w, img_dsc->header.h);

    // 保存当前图像对象，以便后续清理
    current_image_obj = img1;
    current_image_dsc = img_dsc;

     // 显示完成后释放内存（如果不再需要）
   // vTaskDelay(pdMS_TO_TICKS(3000));  // 显示5秒后释放


   // cleanupPreviousImage();
// 设置定时器清理图像
    SetupImageTimeout();


}



// 处理LVGL事件的任务
void LvglEventTask(void* arg) {
    uint32_t event = 0;
    Application * pApp = (Application*)arg;

    pGlobal_app = pApp;
    while (1) {
        if (xQueueReceive(lvgl_event_queue, &event, portMAX_DELAY) == pdPASS) {
            switch (event) {
                case 1: // 显示图像事件
                    if (display_image_dsc) {
                        pGlobal_app->Schedule([]() {
                            // 不捕获pGlobal_app，直接调用全局函数
                            displayImage(display_image_dsc);
                        });
                    }
                    break;
                    
                case 2: // 清理图像事件
                    ESP_LOGI(TAG, "处理清理图像事件");
                    pGlobal_app->Schedule([]() {
                        // 在主线程中安全地清理图像资源
                        cleanupPreviousImage();
                    });
                    break;
                    
                // 可以添加其他事件处理
                default:
                    //ESP_LOGW(TAG, "未知的LVGL事件ID: %d", event);
                    break;
            }
        }
    }
}

// 初始化LVGL事件系统
void InitLvglEventSystem(void* param) {
    // 创建事件队列
    lvgl_event_queue = xQueueCreate(10, sizeof(uint32_t));
    
    // 创建处理LVGL事件的任务
    xTaskCreate(LvglEventTask, "lvgl_event_task", 4096, param, 5, NULL);
}

// 请求显示图像
void RequestDisplayImage(lv_image_dsc_t* img_dsc) {
    // 保存图像描述符
    display_image_dsc = img_dsc;
    // 发送事件
    uint32_t event_id = 1;
    xQueueSend(lvgl_event_queue, &event_id, 0);
}

/**
 * 从JPEG数据创建图像描述符
 * @param jpeg_data JPEG图像数据
 * @param data_len 数据长度
 * @param using_rgb888 是否使用RGB888格式（否则使用RGB565）
 * @return 成功返回lv_image_dsc_t指针，失败返回NULL
 */
lv_image_dsc_t* createImageDescriptorFromJpeg(const uint8_t *jpeg_data, size_t data_len, bool using_rgb888) {
    ESP_LOGI(TAG, "解码JPEG图像，数据大小: %d 字节", data_len);
    
    // 完全移除全局dynamic_image_data的管理
    // 不再尝试释放之前的内存，让LVGL和定时器负责清理
    ESP_LOGI(TAG, "开始JPEG解码，不清理全局内存");
    
    // 重置动态图像描述符
    memset(&dynamic_image, 0, sizeof(dynamic_image));
    
    // 使用ESP-IDF自带的JPEG解码器
    uint8_t *output_buf = NULL;
    int width = 0, height = 0;
    jpeg_raw_type_t output_type = using_rgb888 ? JPEG_RAW_TYPE_RGB888 : JPEG_RAW_TYPE_RGB565_LE;
    
    // 实现类似于esp_jpeg_decoder_one_picture的功能
    // 创建默认配置
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = output_type;
    config.rotate = JPEG_ROTATE_0D;
    
    // 创建解码器句柄
    jpeg_dec_handle_t *jpeg_dec = NULL;
    jpeg_dec = jpeg_dec_open(&config);
    if (!jpeg_dec) {
        ESP_LOGE(TAG, "无法初始化JPEG解码器");
        return NULL;
    }
    
    // 创建IO回调句柄
    jpeg_dec_io_t *jpeg_io = (jpeg_dec_io_t *)calloc(1, sizeof(jpeg_dec_io_t));
    if (!jpeg_io) {
        ESP_LOGE(TAG, "无法分配内存用于JPEG IO结构");
        jpeg_dec_close(jpeg_dec);
        return NULL;
    }
    
    // 创建头信息结构
    jpeg_dec_header_info_t *out_info = (jpeg_dec_header_info_t *)calloc(1, sizeof(jpeg_dec_header_info_t));
    if (!out_info) {
        ESP_LOGE(TAG, "无法分配内存用于JPEG头信息");
        free(jpeg_io);
        jpeg_dec_close(jpeg_dec);
        return NULL;
    }
    
    // 设置输入缓冲区
    jpeg_io->inbuf = (uint8_t*)jpeg_data;
    jpeg_io->inbuf_len = data_len;
    
    // 解析JPEG头信息
    jpeg_error_t ret = jpeg_dec_parse_header(jpeg_dec, jpeg_io, out_info);
    if (ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "解析JPEG头信息失败: %d (0x%X)", ret, ret);
        free(out_info);
        free(jpeg_io);
        jpeg_dec_close(jpeg_dec);
        return NULL;
    }
    
    width = out_info->width;
    height = out_info->height;
    ESP_LOGI(TAG, "JPEG图像尺寸: %dx%d", width, height);
    
    // 按输出大小限制，允许较大二维码，避免不必要的拒绝
    // 例如RGB565下最大约256KB，足够容纳约360x360图像
    const int MAX_OUTPUT_BYTES = 256 * 1024;
    
    // 计算输出缓冲区大小
    int outbuf_len = 0;
    if (output_type == JPEG_RAW_TYPE_RGB565_LE || output_type == JPEG_RAW_TYPE_RGB565_BE) {
        outbuf_len = width * height * 2;
    } else if (output_type == JPEG_RAW_TYPE_RGB888) {
        outbuf_len = width * height * 3;
    } else {
        ESP_LOGE(TAG, "不支持的输出格式");
        free(out_info);
        free(jpeg_io);
        jpeg_dec_close(jpeg_dec);
        return NULL;
    }
    if (outbuf_len > MAX_OUTPUT_BYTES) {
        ESP_LOGE(TAG, "图像输出大小过大: %d bytes (limit %d)", outbuf_len, MAX_OUTPUT_BYTES);
        free(out_info);
        free(jpeg_io);
        jpeg_dec_close(jpeg_dec);
        return NULL;
    }
    
    // 分配内存存储解码后的数据：使用PSRAM，避免消耗片内SRAM导致任务饥饿
    output_buf = (uint8_t *)heap_caps_malloc(outbuf_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!output_buf) {
        ESP_LOGE(TAG, "无法分配内存用于解码JPEG");
        free(out_info);
        free(jpeg_io);
        jpeg_dec_close(jpeg_dec);
        return NULL;
    }
    
    // 设置输出缓冲区（ESP-IDF JPEG 解码器不需要 outbuf_len 字段）
    jpeg_io->outbuf = output_buf;

    // 更新输入缓冲区指针和长度，使解码器从剩余数据开始处理
    int inbuf_consumed = jpeg_io->inbuf_len - jpeg_io->inbuf_remain;
    jpeg_io->inbuf = (uint8_t*)jpeg_data + inbuf_consumed;
    jpeg_io->inbuf_len = jpeg_io->inbuf_remain;
    
    // 解码JPEG
    ret = jpeg_dec_process(jpeg_dec, jpeg_io);
    if (ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "JPEG解码失败: %d (0x%X)", ret, ret);
        // 失败路径及时释放已分配输出缓冲，避免内存泄漏
        if (output_buf) {
            heap_caps_free(output_buf);
            output_buf = nullptr;
        }
        free(out_info);
        free(jpeg_io);
        jpeg_dec_close(jpeg_dec);
        return NULL;
    }
    
    // 释放不再需要的资源
    jpeg_dec_close(jpeg_dec);
    free(jpeg_io);
    free(out_info);
    
    // 如果是RGB888格式，需要转换为RGB565
    if (using_rgb888) {
        // 分配RGB565内存
        size_t rgb565_size = width * height * 2;
        // 转换输出也使用PSRAM，继续避免片内SRAM占用
        uint8_t *rgb565_buf = (uint8_t *)heap_caps_malloc(rgb565_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!rgb565_buf) {
            ESP_LOGE(TAG, "无法分配内存用于RGB888到RGB565转换");
            if (output_buf) {
                heap_caps_free(output_buf);
                output_buf = nullptr;
            }
            return NULL;
        }
        
        // 使用批量处理减少循环开销，每次处理多个像素
        const size_t batch_size = 64; // 每批处理的像素数
        size_t total_pixels = width * height;
        size_t batches = total_pixels / batch_size;
        size_t remaining = total_pixels % batch_size;
        
        // 批量处理像素
        for (size_t b = 0; b < batches; b++) {
            size_t base_idx = b * batch_size;
            for (size_t i = 0; i < batch_size; i++) {
                size_t idx = base_idx + i;
                uint8_t r = output_buf[idx * 3];
                uint8_t g = output_buf[idx * 3 + 1];
                uint8_t b = output_buf[idx * 3 + 2];
                
                // RGB888 to RGB565 conversion
                uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
                rgb565_buf[idx * 2] = rgb565 & 0xFF;
                rgb565_buf[idx * 2 + 1] = (rgb565 >> 8) & 0xFF;
            }
        }
        
        // 处理剩余像素
        size_t base_idx = batches * batch_size;
        for (size_t i = 0; i < remaining; i++) {
            size_t idx = base_idx + i;
            uint8_t r = output_buf[idx * 3];
            uint8_t g = output_buf[idx * 3 + 1];
            uint8_t b = output_buf[idx * 3 + 2];
            
            // RGB888 to RGB565 conversion
            uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
            rgb565_buf[idx * 2] = rgb565 & 0xFF;
            rgb565_buf[idx * 2 + 1] = (rgb565 >> 8) & 0xFF;
        }
        
        // 释放RGB888内存
        heap_caps_free(output_buf);
        output_buf = rgb565_buf;
    }
    
    // 初始化图像描述符
    dynamic_image.header.cf = LV_COLOR_FORMAT_RGB565;
    dynamic_image.header.magic = LV_IMAGE_HEADER_MAGIC;
    dynamic_image.header.w = width;
    dynamic_image.header.h = height;
    dynamic_image.data_size = width * height * 2;
    dynamic_image.data = output_buf;
    
    // 不再设置dynamic_image_data全局变量
    // 让LVGL自己管理内存生命周期
    ESP_LOGI(TAG, "JPEG解码完成，图像数据: %p, 大小: %d", output_buf, (int)dynamic_image.data_size);
    
    return &dynamic_image;
}


void Application::InitRadarModule() {
    ESP_LOGI(TAG, "Initializing radar module");

#ifdef USE_RADAR_2410D

    // 使用2410D雷达芯片
    if (Radar2410D::GetInstance().Init(UART_NUM_2, 15, 16, 115200)) {
        // 设置2410D雷达回调函数
        
          // 设置人员状态检测参数 (来人15秒, 人走15秒, 久坐1小时)
        Radar2410D::GetInstance().SetPersonStateParams(3000, 3000, 1800 * 1000);
        // 设置人员状态回调函数
        Radar2410D::GetInstance().SetPersonStateCallback(
            std::bind(&Application::OnPersonStateChanged, this, std::placeholders::_1));
        
/*
        Radar2410D::GetInstance().SetRadarStatusCallback(
            std::bind(&Application::OnRadarStatusUpdate, this, std::placeholders::_1, 
                      std::placeholders::_2, std::placeholders::_3, std::placeholders::_4,
                      std::placeholders::_5, std::placeholders::_6));    
        // 设置久坐检测回调函数
*/
        // 启动2410D雷达模块
        Radar2410D::GetInstance().Start();
        radar_initialized_ = true;
        
        ESP_LOGI(TAG, "2410D radar module initialized successfully");
    } else {
        ESP_LOGE(TAG, "Failed to initialize 2410D radar module");
    }
#else
    // 使用原有雷达模块
    if (RadarModule::GetInstance().Init(UART_NUM_2, 15, 16, 256000)) {
        // 设置雷达状态回调函数
        ESP_LOGI(TAG, "Setting radar callbacks");
        /*
        RadarModule::GetInstance().SetRadarStatusCallback(
            std::bind(&Application::OnRadarStatusUpdate, this, std::placeholders::_1, std::placeholders::_2, 
                      std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
        */
                   
        // 设置人员状态回调函数
        RadarModule::GetInstance().SetPersonStateCallback(
            std::bind(&Application::OnPersonStateChanged, this, std::placeholders::_1));

        // 设置人员状态检测参数 (来人30秒, 人走40秒)
        RadarModule::GetInstance().SetPersonStateParams(15000, 15000, 2 * 60 * 1000);
        
        // 启动雷达模块
        RadarModule::GetInstance().Start();
        radar_initialized_ = true;
        
        ESP_LOGI(TAG, "Original radar module initialized successfully");
    } else {
        ESP_LOGE(TAG, "Failed to initialize original radar module");
    }
#endif
}

// 雷达状态更新回调
void Application::OnRadarStatusUpdate(uint8_t target_state, uint16_t moving_distance, uint8_t moving_energy, 
                                     uint16_t static_distance, uint8_t static_energy, uint16_t detection_distance) {
    // 标记雷达状态已发生变化（用于工厂测试）
    radar_status_changed_ = true;
    
    // 添加时间控制，每2秒输出一次日志
    static uint32_t last_log_time = 0;
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // 解析目标状态
    const char* target_status_str = "Unknown";
    bool has_moving_target = false;
    bool has_static_target = false;
    
    switch (target_state) {
        case 0x00:
            target_status_str = "无目标";
            break;
        case 0x01:
            target_status_str = "运动目标";
            has_moving_target = true;
            break;
        case 0x02:
            target_status_str = "静止目标";
            has_static_target = true;
            break;
        case 0x03:
            target_status_str = "运动&静止目标";
            has_moving_target = true;
            has_static_target = true;
            break;
        default:
            target_status_str = "其他状态";
            break;
    }
    
    // 在工厂测试模式下，将雷达信息显示到屏幕上
    if (device_state_ == kDeviceStateFactoryTesting) {
        char radar_info[256];
        snprintf(radar_info, sizeof(radar_info), 
                //"雷达检测结果\n"
                "状态: %s\n"
                "探测距离: %d cm", 
                target_status_str, detection_distance);
        /*
        if (has_moving_target) {
            char moving_info[64];
            snprintf(moving_info, sizeof(moving_info), "\n运动距离: %d cm", moving_distance);
            strncat(radar_info, moving_info, sizeof(radar_info) - strlen(radar_info) - 1);
        }
        
        if (has_static_target) {
            char static_info[64];
            snprintf(static_info, sizeof(static_info), "\n静止距离: %d cm", static_distance);
            strncat(radar_info, static_info, sizeof(radar_info) - strlen(radar_info) - 1);
        }
        */
        // 显示到屏幕上
        auto display = Board::GetInstance().GetDisplay();
        if (display) {
            display->SetStatus("雷达检测");
            display->SetChatMessage("user", radar_info);
        }
    }
    
    // 只有当距离上次日志输出超过2秒时才输出日志
    if (current_time - last_log_time >= 2000) {
        ESP_LOGI(TAG, "\n" 
            "╔══════════════════════════════════════╗\n"
            "║            雷达检测结果              ║\n"
            "╠══════════════════════════════════════╣\n"
            "║ 目标状态: %-20s ║\n"
            "║ 状态码:   0x%02X                     ║\n"
            "║ 探测距离: %-4d cm                 ║\n"
            "╚══════════════════════════════════════╝",
            target_status_str, target_state, detection_distance);
        
        if (has_moving_target) {
            ESP_LOGI(TAG, "🏃 运动目标检测到:");
            ESP_LOGI(TAG, "   └─ 距离: %d cm", moving_distance);
            ESP_LOGI(TAG, "   └─ 能量: %d", moving_energy);
        }
        
        if (has_static_target) {
            ESP_LOGI(TAG, "🧍 静止目标检测到:");
            ESP_LOGI(TAG, "   └─ 距离: %d cm", static_distance);
            ESP_LOGI(TAG, "   └─ 能量: %d", static_energy);
        }
        
        if (!has_moving_target && !has_static_target) {
            ESP_LOGI(TAG, "❌ 当前区域内无目标检测");
        }
        
        // 更新最后日志时间
        last_log_time = current_time;
    }

}

// 人员状态变化回调处理函数
void Application::OnPersonStateChanged(PersonState state) {
    const char* state_str = "Unknown";


    // 将表情数组定义为static，这样它们在函数调用之间保持存在
    static const char* lairen_emotions[] = {"happy",  "superhappy"};
    static const char* jiuzuo_emotions[] = {"dongyidong", "duoheshui"};
    auto& oled_task = OledEmotionTask::GetInstance();
    //current_person_state_ = state;
#ifdef DEBUG_VERSON
    ESP_LOGI(TAG, "OnPersonStateChanged state: %d device_state_:%d", (int)state,(int)device_state_);
#endif   
    switch (state) {
        case PersonState::NOBODY: {
            pending_long_sitting_reminder_ = false;
            
            state_str = "无人";
            ESP_LOGD(TAG, "#########无人状态");
            if(has_new_msg){
                oled_task.SendEmotion("newmsg");
            }else{
                oled_task.SendEmotion("sad",5);
            }
            // 无人状态时，启动计时器
            if (nobody_state_timer_ != nullptr) {
                esp_timer_stop(nobody_state_timer_);
                esp_timer_delete(nobody_state_timer_);
                nobody_state_timer_ = nullptr;
            }
            
            esp_timer_create_args_t timer_args = {
                .callback = NobodyStateTimerCallback,
                .arg = this,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "nobody_state_timer"
            };
            
            if (esp_timer_create(&timer_args, &nobody_state_timer_) == ESP_OK) {
                esp_timer_start_once(nobody_state_timer_, 60 * 1000 * 1000); // 60秒，单位微秒
                ESP_LOGI(TAG, "Started nobody state timer for 60 seconds");
            }
            break;
        }
            
        case PersonState::PERSON_COMING: {
            state_str = "来人";
            // 有人状态时，停止并清理计时器
            if (nobody_state_timer_ != nullptr) {
                esp_timer_stop(nobody_state_timer_);
                esp_timer_delete(nobody_state_timer_);
                nobody_state_timer_ = nullptr;
                ESP_LOGI(TAG, "Stopped nobody state timer due to person presence");
            }
            
            int random_index = esp_random() % (sizeof(lairen_emotions) / sizeof(lairen_emotions[0]));
            const char* selected_emotion = lairen_emotions[random_index];

            if(has_new_msg){
                oled_task.SendEmotion("newmsg");
            }else{
                oled_task.SendEmotion(selected_emotion,5);
            }
            ESP_LOGI(TAG, "来人状态，显示表情: %s", selected_emotion);
            /*
            Schedule([this]() 
            {
                if(protocol_){
                     protocol_->SendGreetingRequest("user_status", "person_coming");
                }
            });*/
            break;
        }
            
        case PersonState::PERSON_PRESENT: {
            state_str = "有人";
            // 有人状态时，停止并清理计时器
            ESP_LOGI(TAG, "有人状态");
            if (nobody_state_timer_ != nullptr) {
                esp_timer_stop(nobody_state_timer_);
                esp_timer_delete(nobody_state_timer_);
                nobody_state_timer_ = nullptr;
                ESP_LOGI(TAG, "Stopped nobody state timer due to person presence");
            }

            if(has_new_msg){
                oled_task.SendEmotion("newmsg");
            }else{
            //    oled_task.SendEmotion("ok");
            }
            
            Schedule([this]() 
            {

                if(device_state_ == kDeviceStateSleeping || device_state_ == kDeviceStateIdle){
                    ConnectToServer();
                }

                if(protocol_){
                    protocol_->SendGreetingRequest("user_status", "person_exist");
                }
            });
            break;
        }
            
        case PersonState::PERSON_LEAVING: {
            state_str = "人走";
            Schedule([this]() 
            { 
                if(protocol_){
                    protocol_->SendGreetingRequest("user_status", "person_leaving");
                }
            });

            if(has_new_msg){
                oled_task.SendEmotion("newmsg");
            }else{
                oled_task.SendEmotion("happy");
            }
            break;
        }
            
        case PersonState::SHORT_HOLD:
            {
                if(has_new_msg){
                    oled_task.SendEmotion("newmsg");
                }else{
                    oled_task.SendEmotion("superhappy");
                }

                state_str = "短暂保持";
                // 有人状态时，停止并清理计时器
                if (nobody_state_timer_ != nullptr) {
                    esp_timer_stop(nobody_state_timer_);
                    esp_timer_delete(nobody_state_timer_);
                    nobody_state_timer_ = nullptr;
                    ESP_LOGI(TAG, "Stopped nobody state timer due to person presence");
                }
                //PlaySound(Lang::Sounds::P3_HUANXING);
               // HandleRadarWakeUp();
                
            }
            break;
            
        case PersonState::LONG_SITTING:
        {
            state_str = "久坐";
            /*
            Schedule([this]() 
            {
               
                if(protocol_){
                    protocol_->SendGreetingRequest("user_status", "person_long_sitting");
               }
            });
*/
            // 有人状态时，停止并清理计时器
            if (nobody_state_timer_ != nullptr) {
                esp_timer_stop(nobody_state_timer_);
                esp_timer_delete(nobody_state_timer_);
                nobody_state_timer_ = nullptr;
                ESP_LOGI(TAG, "Stopped nobody state timer due to person presence");
            }
            
            // 检查当前设备状态，如果是listening或speaking，则延迟播放
            DeviceState current_state = GetDeviceState();
            if (current_state == kDeviceStateListening || current_state == kDeviceStateSpeaking) {
                // 设置标记，表示有待播放的久坐提醒
                pending_long_sitting_reminder_ = true;
                ESP_LOGI(TAG, "久坐提醒延迟播放，当前状态: %s", STATE_STRINGS[current_state]);
            } else {
                // 立即播放久坐提醒
                PlayLongSittingReminder();
            }
            break;
        }
    }
}



void Application::DownloadAndDisplayQRCode(const std::string& url,int duration) {
    ESP_LOGI(TAG, "开始下载二维码图片: %s", url.c_str());
    
    // 添加重试机制
    int max_retries = 3;
    for (int retry = 0; retry < max_retries; retry++) {
        if (retry > 0) {
            ESP_LOGW(TAG, "重试下载，第 %d 次", retry + 1);
            vTaskDelay(pdMS_TO_TICKS(1000)); // 等待1秒后重试
        }
        
        auto http = Board::GetInstance().CreateHttp();
        if (!http) {
            ESP_LOGE(TAG, "HTTP客户端创建失败");
            continue;
        }
        
        // 添加防止缓存的头部，避免连接复用问题
        http->SetHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        http->SetHeader("Pragma", "no-cache");
        http->SetHeader("Expires", "0");
        http->SetHeader("Connection", "close"); // 强制关闭连接，避免复用
        
        std::string method = "GET";
        
        if (!http->Open(method, url)) {
            ESP_LOGE(TAG, "打开 HTTP 连接失败");
            delete http;
            continue;
        }
        // 检查状态码
        int status_code = http->GetStatusCode();
        ESP_LOGI(TAG, "HTTP 状态码: %d", status_code);
        if (status_code != 200) {
            std::string body = http->ReadAll();
            ESP_LOGE(TAG, "下载失败，状态码: %d, 响应: %s", status_code, body.c_str());
            http->Close();
            delete http;
            continue;
        }

        // 分块读取响应体
        size_t content_length = http->GetBodyLength();
        ESP_LOGI(TAG, "Content-Length: %u", (unsigned)content_length);

        std::vector<uint8_t> response_buf;
        if (content_length > 0) {
            response_buf.reserve(content_length);
        } else {
            response_buf.reserve(64 * 1024);
        }

        char tmp[1024];
        size_t total_read = 0;
        size_t recent_read = 0;
        size_t yield_bytes_accum = 0; // 累积字节用于周期性让出CPU
        auto last_log_time = esp_timer_get_time();
        while (true) {
            size_t to_read = sizeof(tmp);
            if (content_length > 0) {
                size_t remaining = content_length - total_read;
                if (remaining == 0) {
                    ESP_LOGI(TAG, "按Content-Length读取完成，总字节: %u", (unsigned)total_read);
                    break;
                }
                if (to_read > remaining) to_read = remaining;
            }
            int ret = http->Read(tmp, (int)to_read);
            if (ret < 0) {
                ESP_LOGE(TAG, "读取HTTP数据失败: %s", esp_err_to_name(ret));
                http->Close();
                delete http;
                break;
            }
            if (ret == 0) {
                ESP_LOGI(TAG, "HTTP 读取完成（连接关闭），总字节: %u", (unsigned)total_read);
                break;
            }
            response_buf.insert(response_buf.end(), tmp, tmp + ret);
            total_read += ret;
            recent_read += ret;
            yield_bytes_accum += ret;

            // 每累计约4KB，主动让出一次CPU，避免饥饿
            if (yield_bytes_accum >= 4096) {
                vTaskDelay(pdMS_TO_TICKS(1));
                yield_bytes_accum = 0;
            }

            // 每秒打印一次进度
            if (esp_timer_get_time() - last_log_time >= 1000000) {
                if (content_length > 0) {
                    size_t progress = total_read * 100 / content_length;
                    ESP_LOGI(TAG, "下载进度: %u%% (%u/%u), 速度: %uB/s",
                             (unsigned)progress, (unsigned)total_read, (unsigned)content_length, (unsigned)recent_read);
                } else {
                    ESP_LOGI(TAG, "已下载: %u 字节, 速度: %uB/s",
                             (unsigned)total_read, (unsigned)recent_read);
                }
                recent_read = 0;
                last_log_time = esp_timer_get_time();
            }
        }

        if (content_length > 0 && total_read != content_length) {
            ESP_LOGW(TAG, "读取结束但大小不匹配: %u != %u", (unsigned)total_read, (unsigned)content_length);
        }

        // 关闭连接
        http->Close();
        delete http;

        // 检查响应是否有效
        if (response_buf.empty()) {
            ESP_LOGE(TAG, "HTTP 响应为空，重试...");
            continue;
        }

        int content_length_int = (int)response_buf.size();
        ESP_LOGI(TAG, "图片大小: %d 字节", content_length_int);
        
        // 验证数据完整性
        if (content_length_int < 1000) { // JPEG文件通常至少几KB
            ESP_LOGW(TAG, "接收到的数据太小，可能不完整: %d 字节", content_length_int);
            continue;
        }
        
        uint8_t *buffer = (uint8_t *)heap_caps_malloc(content_length_int, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
        if (!buffer) {
            ESP_LOGE(TAG, "内存分配失败");
            return;
        }
        
        memcpy(buffer, response_buf.data(), content_length_int);
        
        // 验证JPEG格式
        if (content_length_int < 3 || !(buffer[0] == 0xFF && buffer[1] == 0xD8 && buffer[2] == 0xFF)) {
            ESP_LOGE(TAG, "图片格式验证失败 buffer[0]=0x%x buffer[1]=0x%x buffer[2]=0x%x", 
                     buffer[0], buffer[1], buffer[2]);
            free(buffer);
            continue; // 重试而不是直接返回
        }
        
        ESP_LOGI(TAG, "JPEG格式验证成功，继续处理");
        
        // 在解码前检查可用内存
        size_t free_heap = esp_get_free_heap_size();
        ESP_LOGI(TAG, "解码前可用内存: %d 字节", free_heap);
        
        if (free_heap < 100000) {
            ESP_LOGW(TAG, "内存不足，清理之前的图像");
            cleanupPreviousImage();
        }
        
        lv_image_dsc_t* img_dsc = createImageDescriptorFromJpeg(buffer, content_length_int, false);
        free(buffer);
        
        if (!img_dsc) {
            ESP_LOGE(TAG, "JPEG解码失败，重试...");
            continue;
        }
        
        ESP_LOGI(TAG, "图像解码成功，准备显示");
        auto display = Board::GetInstance().GetDisplay();
        //display->SetEmotion("happy");
        display->SetProtectedImage(img_dsc, duration*1000);
        return; // 成功完成
    }
    
    ESP_LOGE(TAG, "所有重试都失败，无法下载二维码图片");
}

void Application::StartQrTest() {
    if (qr_test_task_handle_ == nullptr) {
        xTaskCreate(QrTestTask, "qr_test_task", 8192, this, 5, &qr_test_task_handle_);
    }
}

void Application::StopQrTest() {
    if (qr_test_task_handle_ != nullptr) {
        vTaskDelete(qr_test_task_handle_);
        qr_test_task_handle_ = nullptr;
    }
}

void Application::QrTestTask(void* arg) {
    Application* app = static_cast<Application*>(arg);
    const char* url = "https://static.contextai.pro/cml_server/ai_device_qr/2025/12/04/qr_1764823020446546451.jpeg";
    for (;;) {
        app->DownloadAndDisplayQRCode(url, 5);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}



// 初始化 GPIO 监测
void Application::gpio_monitoring_init() {
    ESP_LOGI("MAR", "gpio_monitoring_init");
    
    // 配置 GPIO 引脚，增加上拉电阻提高抗干扰能力
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_INPUT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,  // 启用上拉电阻增强抗干扰
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE
    };
    gpio_config(&io_conf);
    
    // 设置GPIO中断阈值，增加抗噪声能力
    gpio_set_intr_type(GPIO_INPUT_PIN, GPIO_INTR_POSEDGE);
    
    // 创建中断事件队列
    gpio_evt_queue = xQueueCreate(GPIO_INTERRUPT_QUEUE_SIZE, sizeof(uint32_t));
    // 安装 GPIO 中断服务
    gpio_install_isr_service(0);
    // 添加中断处理函数
    gpio_isr_handler_add(GPIO_INPUT_PIN, gpio_isr_handler, (void*)this);
    // 创建线程来处理中断事件
    xTaskCreate(gpio_task_execute, "gpio_task_execute", 8192, this, 8, NULL);
}

// 中断处理函数
void IRAM_ATTR gpio_isr_handler(void* arg) {
    Application* app = static_cast<Application*>(arg);
    if (app == nullptr) {
        //ESP_LOGE("MAR", "gpio_isr_handler Received null pointer as argument!");
        return;
    }
    
    //uint32_t gpio_num = GPIO_INPUT_PIN;
    //xQueueSendFromISR(app->gpio_evt_queue, &gpio_num, NULL);

    uint32_t gpio_num = GPIO_INPUT_PIN;
     // 检查队列中的消息数量
     // 检查队列中的消息数量
    UBaseType_t queue_count = uxQueueMessagesWaitingFromISR(app->gpio_evt_queue);
    if (queue_count > 1) {
        // 队列中消息数量大于 1，直接返回
        //ESP_LOGE("MAR", "gpio_isr_handler exist interupt!");
        //return;
    }

    xQueueSendFromISR(app->gpio_evt_queue, &gpio_num, NULL);
}
// 线程处理函数
void gpio_task_execute(void* arg) {
    Application* app = static_cast<Application*>(arg);
    uint32_t gpio_num;
    
    static const char* lairen_emotions[] = {"happy",  "superhappy", "ok"};
    auto& oled_task = OledEmotionTask::GetInstance();

    for (;;) {
        if (xQueueReceive(app->gpio_evt_queue, &gpio_num, portMAX_DELAY)) {
            ESP_LOGI("GPIO", "GPIO[%" PRIu32 "] 检测到脉冲输入********", gpio_num);

            int random_index = esp_random() % (sizeof(lairen_emotions) / sizeof(lairen_emotions[0]));
            const char* selected_emotion = lairen_emotions[random_index];
            
            oled_task.SendEmotion(selected_emotion,3);
            app->Schedule([app]() {
                if(app->IsInDemoMode() == true)
                {
                    return;
                }

                else if(app->GetDeviceState() == kDeviceStateSpeaking)
                {
                     //app->AbortSpeaking(kAbortReasonNone);
                     app->SetDeviceState(kDeviceStateListening);
                }
                else if (app->GetDeviceState()  == kDeviceStateListening) {
                    // 从 Listening 转为 KeepConnected
                    app->SetDeviceState(kDeviceStateKeepConnected);
                }
                else
                {
                    app->PlaySound(Lang::Sounds::P3_POPUP);
                }

            });

            // 调用某个函数执行
           // app->abort_talk();
           // 播放触摸提示音
            if(app->IsInFactoryTestingMode())
            {
                app->PlaySound(Lang::Sounds::P3_POPUP);
            }
        }
    }
}

void Application::HandleRadarWakeUp()
{
    // 添加表情轮换相关变量
    static int speaking_emotion_index_ = 0;  // Speaking状态表情索引
    static int listening_emotion_index_ = 0; // Listening状态表情索引

    // 将表情数组定义为static，这样它们在函数调用之间保持存在
    static const char* speaking_emotions[] = {"angry", "sad", "annoyed", "frustrated"};
    static const char* listening_emotions[] = {"happy", "kissy", "surprised", "neutral"};
    static const int speaking_emotions_count = sizeof(speaking_emotions) / sizeof(speaking_emotions[0]);
    static const int listening_emotions_count = sizeof(listening_emotions) / sizeof(listening_emotions[0]);

    // 当雷达处于SHORT_HOLD状态时，判断设备是否处于idle状态
    if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateSleeping) {

        ESP_LOGI(TAG, "SHORT_HOLD detected in idle state, triggering wake up logic");
        
        // 执行与语音唤醒相同的逻辑
        ToggleChatState();
        Schedule([this]() {
            if (protocol_) {
                PlaySound(Lang::Sounds::P3_POPUP);
                auto& board = Board::GetInstance();
                board.GetBacklight()->RestoreBrightness();

                // 发送雷达唤醒事件到服务器
                protocol_->SendWakeWordDetected("你好"); 
                // 检查是否处于睡眠模式，如果是则进行唤醒
                
                board.SetPowerSaveMode(false);  // 这会触发唤醒操作
               
                ESP_LOGI(TAG, "Wake up from sleep mode triggered by radar");

                auto display = Board::GetInstance().GetDisplay();
                // 清空聊天消息
                display->SetChatMessage("system", "");
                // 直接设置表情和缩放，不依赖SetEmotion
                auto lcd_display = static_cast<LcdDisplay*>(display);
                lcd_display->SetEmotionWithScale("neutral", 300);
                
                vTaskDelay(pdMS_TO_TICKS(1500));
                lcd_display->SetEmotionWithScale("happy", 300);
            }
        });
        
        return;
    } 
    /*
    else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {       
            auto display = Board::GetInstance().GetDisplay();
            // 清空聊天消息
            display->SetChatMessage("system", "");
            //display->SetStatus("讨厌，打扰了");
            // 直接设置表情和缩放
            //PlaySound(Lang::Sounds::P3_HUANXING);
            auto lcd_display = static_cast<LcdDisplay*>(display);
            
            // 获取当前表情并轮换到下一个
            const char* current_emotion = speaking_emotions[speaking_emotion_index_];
            speaking_emotion_index_ = (speaking_emotion_index_ + 1) % speaking_emotions_count;
            
            lcd_display->SetEmotionWithScale(current_emotion, 300);
        });

    } else if (device_state_ == kDeviceStateListening || device_state_ == kDeviceStateKeepConnected)
     {   

        Schedule([this]() {       
            auto display = Board::GetInstance().GetDisplay();
            // 清空聊天消息
            display->SetChatMessage("system", "");

            //display->SetStatus("我看到你在挥手了");
            // 直接设置表情和缩放
            auto lcd_display = static_cast<LcdDisplay*>(display);
            
            // 获取当前表情并轮换到下一个
            const char* current_emotion = listening_emotions[listening_emotion_index_];
            listening_emotion_index_ = (listening_emotion_index_ + 1) % listening_emotions_count;
            
            lcd_display->SetEmotionWithScale(current_emotion, 300);
            //PlaySound(Lang::Sounds::P3_HUANXING);
        });
    }
    */
}

void Application::ConnectToServer()
{
    #ifdef DEBUG_VERSON
        ESP_LOGI(TAG, "ConnectToServer");
    #endif
    // Mar add
    if (!protocol_->IsAudioChannelOpened()) {
        SetDeviceState(kDeviceStateConnecting);
        if (!protocol_->OpenAudioChannel()) {
            //wake_word_->StartDetection();
            return;
        }
    }
#ifdef DEBUG_VERSON
    ESP_LOGI(TAG, "SetListeningMode listening_mode_ = %d", aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#endif
    
    SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
   //listening_mode_ = aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime;

   SetDeviceState(kDeviceStateKeepConnected);
    //移动到这才播放声音
   // ResetDecoder();
   // PlaySound(Lang::Sounds::P3_SUCCESS);
    //InitLvglEventSystem((void*)this);
}

// 添加计时器回调函数
void Application::NobodyStateTimerCallback(void* arg) {
    Application* app = static_cast<Application*>(arg);
    app->Schedule([app]() {
        DeviceState current_state = app->GetDeviceState();
        ESP_LOGI(TAG, "Nobody state timer expired, current state: %s", STATE_STRINGS[current_state]);
        
        if (current_state == kDeviceStateListening) {
            // 从 Speaking 转为 KeepConnected，并重启计时器
            app->SetDeviceState(kDeviceStateKeepConnected);
            ESP_LOGI(TAG, "Changed from Speaking to KeepConnected, restarting timer");
            
            // 重启计时器
            if (app->nobody_state_timer_ != nullptr) {
                esp_timer_start_once(app->nobody_state_timer_, 60 * 1000 * 1000); // 再次启动60秒计时器
            }
        } else if (current_state == kDeviceStateKeepConnected) {
            // 从 KeepConnected 转为 Idle
            app->SetDeviceState(kDeviceStateIdle);
            ESP_LOGI(TAG, "Changed from KeepConnected to Idle");
            
            // 清理计时器
            if (app->nobody_state_timer_ != nullptr) {
                esp_timer_delete(app->nobody_state_timer_);
                app->nobody_state_timer_ = nullptr;
            }
        }
    });
}

// Listening 状态计时器回调
void Application::ListeningStateTimerCallback(void* arg) {
    Application* app = static_cast<Application*>(arg);
    app->Schedule([app]() {
        DeviceState current_state = app->GetDeviceState();
        ESP_LOGI(TAG, "Listening state timer expired, current state: %s", STATE_STRINGS[current_state]);
        
        if (current_state == kDeviceStateListening) {
            // 从 Listening 转为 KeepConnected
            app->SetDeviceState(kDeviceStateKeepConnected);
            ESP_LOGI(TAG, "Changed from Listening to KeepConnected due to 10s timeout");
            
            // 清理计时器
            if (app->listening_state_timer_ != nullptr) {
                esp_timer_delete(app->listening_state_timer_);
                app->listening_state_timer_ = nullptr;
            }
        }
    });
}

// 心跳包定时器回调
void Application::HeartbeatTimerCallback(void* arg) {
    Application* app = (Application*)arg;
    app->Schedule([app]() {
        if (app->protocol_ && app->protocol_->IsAudioChannelOpened() && app->GetDeviceState() == kDeviceStateKeepConnected) {

            app->protocol_->SendHeartbeat();
        }
    });
}

// 在连接建立时启动心跳包定时器
void Application::StartHeartbeatTimer() {
    if (heartbeat_timer_ == nullptr) {
        esp_timer_create_args_t timer_args = {
            .callback = HeartbeatTimerCallback,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "heartbeat_timer",
            .skip_unhandled_events = true
        };
        esp_timer_create(&timer_args, &heartbeat_timer_);

        esp_timer_start_periodic(heartbeat_timer_, 30000000); // 10秒间隔
    }
    
}

void Application::StopHeartbeatTimer() {
    if (heartbeat_timer_ != nullptr) {
        esp_timer_stop(heartbeat_timer_);
        esp_timer_delete(heartbeat_timer_);  // 添加这行：删除定时器
        heartbeat_timer_ = nullptr;
        ESP_LOGI(TAG, "Heartbeat timer stopped");
    }
}

void Application::ClearAudioQueues() {
    ESP_LOGI(TAG, "Clearing audio queues");
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 清空录音发送队列
    if (!audio_send_queue_.empty()) {
        ESP_LOGI(TAG, "Clearing %zu packets from audio_send_queue_", audio_send_queue_.size());
        audio_send_queue_.clear();
    }
    
    // 清空音频解码队列
    if (!audio_decode_queue_.empty()) {
        ESP_LOGI(TAG, "Clearing %zu packets from audio_decode_queue_", audio_decode_queue_.size());
        audio_decode_queue_.clear();
    }
    
    // 清空音频测试队列
    if (!audio_testing_queue_.empty()) {
        ESP_LOGI(TAG, "Clearing %zu packets from audio_testing_queue_", audio_testing_queue_.size());
        audio_testing_queue_.clear();
    }
    
#ifdef CONFIG_USE_SERVER_AEC
    // 清空时间戳队列
    {
        std::lock_guard<std::mutex> timestamp_lock(timestamp_mutex_);
        if (!timestamp_queue_.empty()) {
            ESP_LOGI(TAG, "Clearing %zu timestamps from timestamp_queue_", timestamp_queue_.size());
            timestamp_queue_.clear();
        }
    }
#endif
    
    // 通知等待的线程
    audio_decode_cv_.notify_all();
    
    ESP_LOGI(TAG, "Audio queues cleared successfully");
}


//http post
bool Application::SendDeviceRegisterRequest(const std::string& baseUrl) {
    auto& board = Board::GetInstance();
    auto app_desc = esp_app_get_description();
    
    // 创建HTTP客户端
    auto http = std::unique_ptr<Http>(board.CreateHttp());
    
    // 设置HTTP头
    http->SetHeader("Content-Type", "application/json");
    http->SetHeader("User-Agent", std::string(BOARD_NAME "/") + app_desc->version);
    // 构建URL
    std::string url = baseUrl + "/cmlinfra/device-register";


    ESP_LOGI(TAG, "url: %s", url.c_str());
    // 获取当前时间戳
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    // 生成随机的trace_id和request_id
    auto generate_random_id = [](const std::string& prefix) -> std::string {
        uint32_t random1 = esp_random();
        uint32_t random2 = esp_random();
        std::stringstream ss;
        ss << prefix << "_" << std::hex << random1 << random2;
        return ss.str();
    };
    // 生成默认session_id（使用MAC地址+时间戳）
    std::string session_id = generate_random_id("session_id"); 
    std::string trace_id = generate_random_id("trace_id");
    std::string request_id = generate_random_id("request_id");

    // 构建JSON请求体
    cJSON* root = cJSON_CreateObject();
    cJSON* payload = cJSON_CreateObject();
    cJSON* params = cJSON_CreateObject();
    cJSON* device_info = cJSON_CreateObject();
    
    // 填充根级别字段
    cJSON_AddStringToObject(root, "session_id", session_id.c_str());
    cJSON_AddStringToObject(root, "type", "cml_message_bridge");
    cJSON_AddStringToObject(root, "api_version", "v1.0");
    
    // 填充payload字段
    cJSON_AddStringToObject(payload, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(payload, "id", 1);
    cJSON_AddStringToObject(payload, "destination", "cml_server");
    cJSON_AddStringToObject(payload, "method", "device_register");
    cJSON_AddStringToObject(payload, "trace_id", trace_id.c_str());
    cJSON_AddStringToObject(payload, "request_id", request_id.c_str());
    cJSON_AddNumberToObject(payload, "timestamp", now);
    
    // 填充params字段
    cJSON_AddStringToObject(params, "type", "physical_device");
    cJSON_AddItemToObject(payload, "params", params);
    
    // 填充device_info字段
    cJSON_AddStringToObject(device_info, "device_id", SystemInfo::GetMacAddress().c_str());
    cJSON_AddStringToObject(device_info, "version", app_desc->version);
    cJSON_AddStringToObject(device_info, "name", BOARD_NAME);

    Settings settings("device_info", false);  // 只读模式
    std::string iccid = settings.GetString("iccid", "");
 // 添加ICCID信息
    cJSON_AddStringToObject(device_info, "iccid", iccid.c_str());

    cJSON_AddItemToObject(payload, "device_info", device_info);
    
    // 将payload添加到根对象
    cJSON_AddItemToObject(root, "payload", payload);
    
    // 转换为字符串
    char* json_string = cJSON_Print(root);
    std::string json_data(json_string);
    ESP_LOGI(TAG, "Device register request: %s", json_string);
    // 清理内存
    free(json_string);
    cJSON_Delete(root);
    
    // 设置请求内容
    http->SetContent(std::move(json_data));
    
    // 发送POST请求
    if (!http->Open("POST", url)) {
        ESP_LOGE(TAG, "Failed to open HTTP connection for device register");
        return false;
    }
    
    auto status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGE(TAG, "Device register failed, status code: %d", status_code);
        http->Close();
        return false;
    }
    
    std::string response = http->ReadAll();
    http->Close();
    
    ESP_LOGI(TAG, "Device register response: %s", response.c_str());
    
    // 解析返回的JSON数据
    cJSON* response_json = cJSON_Parse(response.c_str());
    if (response_json == nullptr) {
        ESP_LOGE(TAG, "Failed to parse device register response JSON");
        return false;
    }
    
    // 提取设备编号
    cJSON* data = cJSON_GetObjectItem(response_json, "data");
    if (data != nullptr) {
        cJSON* payload_obj = cJSON_GetObjectItem(data, "payload");
        if (payload_obj != nullptr) {
            cJSON* result = cJSON_GetObjectItem(payload_obj, "result");
            if (result != nullptr) {
                cJSON* result_data = cJSON_GetObjectItem(result, "data");
                if (result_data != nullptr) {
                    cJSON* num = cJSON_GetObjectItem(result_data, "num");
                    if (num != nullptr && cJSON_IsNumber(num)) {
                        int device_num = (int)cJSON_GetNumberValue(num);
                        ESP_LOGI(TAG, "Device number received: %d", device_num);
                        
                        // 存储设备编号到flash
                        Settings settings("device", true);
                        settings.SetInt("device_num", device_num);
                        ESP_LOGI(TAG, "Device number %d saved to flash", device_num);
                        
                        cJSON_Delete(response_json);
                        return true;
                    }
                }
            }
        }
    }
    
    ESP_LOGW(TAG, "Failed to extract device number from response");
    cJSON_Delete(response_json);
    return false;
}
// 在文件开头添加辅助函数
std::string Application::ExtractHostFromUrl(const std::string& url) {
    // 查找协议部分
    size_t protocol_pos = url.find("://");
    if (protocol_pos == std::string::npos) {
        return url; // 如果没有协议，返回原URL
    }
    
    std::string protocol = url.substr(0, protocol_pos + 3); // 包含 "://"
    std::string remaining = url.substr(protocol_pos + 3);
    
    // 查找第一个斜杠，这标志着路径的开始
    size_t path_pos = remaining.find('/');
    if (path_pos != std::string::npos) {
        remaining = remaining.substr(0, path_pos);
    }
    
    return protocol + remaining;
}

int Application::GetDeviceNumber() {
    Settings settings("device", false);
    return settings.GetInt("device_num", -1); // 如果没有找到，返回-1
}

bool Application::HasDeviceNumber() {
    return GetDeviceNumber() != -1;
}

// ... existing code ...

void Application::PlayDeviceNumber() {
    int device_num = GetDeviceNumber();
    
    if (device_num <= 0) {
        ESP_LOGW(TAG, "Device not registered or invalid device number");
        auto display = Board::GetInstance().GetDisplay();
        display->SetChatMessage("system", "设备未注册");
        PlaySound(Lang::Sounds::P3_ERR_REG);
        return;
    }
    
    // 在屏幕上显示设备号
    auto display = Board::GetInstance().GetDisplay();
    std::string device_msg = "设备编码: " + std::to_string(device_num);
    display->SetChatMessage("system", device_msg.c_str(),5);
    display->SetEmotion("happy");
    
    ESP_LOGI(TAG, "Playing device number: %d", device_num);
    
    // 播放"设备编码是"提示音（使用OTA绑定码的音频）
    //PlaySound(Lang::Sounds::P3_ACTIVATION);  // 使用激活音频作为提示音
    
    // 等待提示音播放完成
    {
        std::unique_lock<std::mutex> lock(mutex_);
        audio_decode_cv_.wait(lock, [this]() {
            return audio_decode_queue_.empty();
        });
    }
    background_task_->WaitForCompletion();
    
    // 逐位播放设备号数字
    std::string device_str = std::to_string(device_num);
    for (char digit_char : device_str) {
        int digit = digit_char - '0';  // 转换字符到数字
        
        // 根据数字播放对应的音频
        std::string_view digit_sound;
        switch (digit) {
            case 0: digit_sound = Lang::Sounds::P3_0; break;
            case 1: digit_sound = Lang::Sounds::P3_1; break;
            case 2: digit_sound = Lang::Sounds::P3_2; break;
            case 3: digit_sound = Lang::Sounds::P3_3; break;
            case 4: digit_sound = Lang::Sounds::P3_4; break;
            case 5: digit_sound = Lang::Sounds::P3_5; break;
            case 6: digit_sound = Lang::Sounds::P3_6; break;
            case 7: digit_sound = Lang::Sounds::P3_7; break;
            case 8: digit_sound = Lang::Sounds::P3_8; break;
            case 9: digit_sound = Lang::Sounds::P3_9; break;
            default:
                ESP_LOGW(TAG, "Invalid digit: %d", digit);
                continue;
        }
        
        PlaySound(digit_sound);
        
        // 等待当前数字音频播放完成
        {
            std::unique_lock<std::mutex> lock(mutex_);
            audio_decode_cv_.wait(lock, [this]() {
                return audio_decode_queue_.empty();
            });
        }
        background_task_->WaitForCompletion();
        
        // 数字间添加短暂停顿
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    
    ESP_LOGI(TAG, "Device number playback completed");
}

void Application::ShowSystemVersion() {
    auto app_desc = esp_app_get_description();
    auto& board = Board::GetInstance();
    
    // 构建版本信息字符串
    std::string version_info = "系统版本信息：\n";
    version_info += "应用版本：" + std::string(app_desc->version) + "\n";
    version_info += "编译时间：" + std::string(app_desc->date) + " " + std::string(app_desc->time) + "\n";
    version_info += "MAC地址：" + SystemInfo::GetMacAddress();
    
    ESP_LOGI(TAG, "System Version Info: %s", version_info.c_str());
    
    // 在显示屏上显示版本信息
    Schedule([this, version_info]() {
        auto display = Board::GetInstance().GetDisplay();
        display->SetChatMessage("system", version_info.c_str(),5);
    });
    /*
    // 语音播报版本信息（简化版本）
    std::string voice_message = "当前系统版本是" + std::string(app_desc->version) + 
                               "，芯片型号" + SystemInfo::GetChipModelName();
    
    Schedule([this, voice_message]() {
        auto display = Board::GetInstance().GetDisplay();
        display->SetChatMessage("assistant", voice_message.c_str());
    });*/
}

// 在构造函数中初始化VAD相关变量
// VAD检测函数
bool Application::DetectVoiceActivity(const int16_t* data) {
    // 计算当前帧的能量
    float energy = 0.0f;
    for (int i = 0; i < 160; i++) {  // 10ms @ 16kHz = 160 samples
        energy += data[i] * data[i];
    }
    energy = sqrt(energy / 160);
    
    return energy > VAD_THRESHOLD;
}

// 更新语音状态
void Application::UpdateSpeechState(bool voice_detected) {
    uint32_t current_time = esp_timer_get_time() / 1000;  // 转换为毫秒
    
    if (voice_detected) {
        if (!is_speech_active_) {
            // 语音开始
            is_speech_active_ = true;
            speech_start_time_ = current_time;
            ESP_LOGI(TAG, "Speech started");
        }
        last_speech_time_ = current_time;
    } else {
        // 检查是否应该结束语音状态
        if (is_speech_active_ && 
            (current_time - last_speech_time_) > SPEECH_TIMEOUT_MS) {
            is_speech_active_ = false;
            ESP_LOGI(TAG, "Speech ended, duration: %d ms", 
                    (int)(current_time - speech_start_time_));
        }
    }
}

// 改进的音频发送判断
bool Application::ShouldSendAudioWithVAD(const int16_t* data) {
    bool voice_detected = DetectVoiceActivity(data);
    UpdateSpeechState(voice_detected);
    
    // 如果当前在语音状态，或者刚检测到语音，都发送
    if (is_speech_active_ || voice_detected) {
        ESP_LOGD(TAG, "Sending audio - Speech active: %s, Voice detected: %s",
                is_speech_active_ ? "true" : "false",
                voice_detected ? "true" : "false");
        return true;
    }
    
    return false;
}

void Application::PlayLongSittingReminder() {
    // 从久坐表情数组中随机选择一个表情
    static const char* jiuzuo_emotions[] = {"dongyidong", "duoheshui"};
    int random_index = esp_random() % (sizeof(jiuzuo_emotions) / sizeof(jiuzuo_emotions[0]));
    const char* selected_emotion = jiuzuo_emotions[random_index];

    auto& oled_task = OledEmotionTask::GetInstance();
    oled_task.SendEmotion(selected_emotion);
    if(random_index == 0)
    {
        PlaySound(Lang::Sounds::P3_DONGYIDONG);
    }
    else
    {
        PlaySound(Lang::Sounds::P3_DUOHESHUI);
    }
    ESP_LOGI(TAG, "久坐状态，显示表情: %s", selected_emotion);
    pending_long_sitting_reminder_ = false;  // 清除待播放标记
}

void Application::CheckPendingLongSittingReminder() {
    if (pending_long_sitting_reminder_) {
        PlayLongSittingReminder();
    }
}

bool Application::IsInDemoMode() const {
    auto& demo_mode = DemoMode::GetInstance();
    return demo_mode.IsInDemoMode();
}


#ifdef MEMORY_MONITOR

void Application::StartMemoryMonitorTask() {
    xTaskCreate(MemoryMonitorTask, "memory_monitor", 2048, this, 1, &memory_monitor_task_handle_);
}

void Application::MemoryMonitorTask(void* arg) {
    Application* app = (Application*)arg;
    
    while (true) {
        // 获取堆内存信息
        size_t free_heap = esp_get_free_heap_size();
        size_t min_free_heap = esp_get_minimum_free_heap_size();
        size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
        
        // 获取内部RAM信息
        size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t total_internal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
        
        // 获取PSRAM信息（如果有）
        size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        
        // 获取当前任务栈信息
        UBaseType_t stack_high_water_mark = uxTaskGetStackHighWaterMark(NULL);
        
        ESP_LOGI("MemoryMonitor", "=== 内存使用情况 ===");
        ESP_LOGI("MemoryMonitor", "总堆内存: %u bytes (%.2f KB)", total_heap, total_heap / 1024.0f);
        ESP_LOGI("MemoryMonitor", "可用堆内存: %u bytes (%.2f KB, %.1f%%)", 
                 free_heap, free_heap / 1024.0f, (float)free_heap * 100 / total_heap);
        ESP_LOGI("MemoryMonitor", "历史最小可用堆: %u bytes (%.2f KB)", min_free_heap, min_free_heap / 1024.0f);
        
        ESP_LOGI("MemoryMonitor", "内部RAM总量: %u bytes (%.2f KB)", total_internal, total_internal / 1024.0f);
        ESP_LOGI("MemoryMonitor", "内部RAM可用: %u bytes (%.2f KB, %.1f%%)", 
                 free_internal, free_internal / 1024.0f, (float)free_internal * 100 / total_internal);
        
        if (total_psram > 0) {
            ESP_LOGI("MemoryMonitor", "PSRAM总量: %u bytes (%.2f KB)", total_psram, total_psram / 1024.0f);
            ESP_LOGI("MemoryMonitor", "PSRAM可用: %u bytes (%.2f KB, %.1f%%)", 
                     free_psram, free_psram / 1024.0f, (float)free_psram * 100 / total_psram);
        }
        
        ESP_LOGI("MemoryMonitor", "当前任务栈剩余: %u bytes", stack_high_water_mark * sizeof(StackType_t));
        ESP_LOGI("MemoryMonitor", "========================");
        
        // 每30秒打印一次
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

#endif
