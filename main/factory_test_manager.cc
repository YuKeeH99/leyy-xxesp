#include "factory_test_manager.h"
#include "application.h"
#include "board.h"
#include "audio_codec.h"
#include "display.h"
#include <esp_log.h>
#include <esp_system.h>  // 用于esp_restart()
#include <algorithm>
#include <cstdio>
#include "assets/lang_config.h"

static const char* TAG = "FactoryTest";

// TouchInterruptTest 实现
TouchInterruptTest::TouchInterruptTest(Application* app) : TestBase(app) {}

void TouchInterruptTest::Start() {
    //app_->PlaySound(Lang::Sounds::P3_POPUP);
    app_->Alert("触摸中断测试", "请触摸中断按钮");
}

void TouchInterruptTest::HandleTouchInterrupt() {
    if (!completed_) {
        completed_ = true;
        if (completion_callback_) {
            completion_callback_(true);
        }
    }
}

bool TouchInterruptTest::IsCompleted() const {
    return completed_;
}

// AudioTest 实现
AudioTest::AudioTest(Application* app) : TestBase(app) {}

void AudioTest::Start() {
    state_ = kWaitingToStart;
    app_->Alert("音频测试", "音频测试开始\n将自动录音5秒\n然后播放录音");
    
    // 延迟1秒后自动开始录音
    vTaskDelay(pdMS_TO_TICKS(1000));
    StartRecording();
}

void AudioTest::StartRecording() {
    if (state_ != kWaitingToStart) {
        return;
    }
    
    ESP_LOGI(TAG, "开始录音");
    recorded_audio_.clear();
    state_ = kRecording;
    recording_start_time_ = xTaskGetTickCount();
    
    // 启用音频输入
    auto codec = Board::GetInstance().GetAudioCodec();
    codec->EnableInput(true);
    
    app_->Alert("音频测试", "正在录音...\n请说话\n5秒后自动停止");
    
    // 创建录音任务
    xTaskCreate(RecordingTaskWrapper, "audio_recording", 4096, this, 5, &recording_task_handle_);
}

void AudioTest::PerformRecording() {
    auto codec = Board::GetInstance().GetAudioCodec();
    const int sample_rate = 16000;
    const int frame_duration_ms = 60;
    const int samples_per_frame = sample_rate * frame_duration_ms / 1000;
    const int max_recording_time_ms = 5000;
    const int max_frames = max_recording_time_ms / frame_duration_ms;
    
    ESP_LOGI(TAG, "录音任务开始，每帧%d样本，最大%d帧", samples_per_frame, max_frames);
    
    int frame_count = 0;
    while (state_ == kRecording && frame_count < max_frames) {
        std::vector<int16_t> frame_data;
        
        if (codec->input_enabled()) {
            frame_data.resize(samples_per_frame * codec->input_channels());
            if (codec->InputData(frame_data)) {
                // 如果是双声道，只取第一个声道
                if (codec->input_channels() == 2) {
                    std::vector<int16_t> mono_data(samples_per_frame);
                    for (int i = 0; i < samples_per_frame; i++) {
                        mono_data[i] = frame_data[i * 2];
                    }
                    recorded_audio_.insert(recorded_audio_.end(), mono_data.begin(), mono_data.end());
                } else {
                    recorded_audio_.insert(recorded_audio_.end(), frame_data.begin(), frame_data.end());
                }
                
                frame_count++;
                
                // 修正时间显示：从1秒开始显示，每秒显示一次
                int frames_per_second = 1000 / frame_duration_ms;
                if (frame_count % frames_per_second == 0) {
                    int seconds_recorded = frame_count / frames_per_second;
                    char progress_msg[100];
                    snprintf(progress_msg, sizeof(progress_msg), "正在录音...\n已录制: %d秒", seconds_recorded);
                    app_->Alert("音频测试", progress_msg);
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(frame_duration_ms));
    }
    
    if (state_ == kRecording) {
        StopRecording();
    }
}

void AudioTest::PlayRecordedAudio() {
    if (state_ != kWaitingToPlay || recorded_audio_.empty()) {
        ESP_LOGW(TAG, "无法播放：状态=%d，录音数据大小=%u", state_, (unsigned int)recorded_audio_.size());
        return;
    }
    
    ESP_LOGI(TAG, "开始播放录音，样本数: %u", (unsigned int)recorded_audio_.size());
    state_ = kPlaying;
    
    auto codec = Board::GetInstance().GetAudioCodec();
    codec->EnableOutput(true);
    
    // 优化播放参数以提高连续性
    const size_t chunk_size = 480;  // 减小chunk大小到30ms
    const TickType_t chunk_delay = pdMS_TO_TICKS(25);  // 减少延迟到25ms
    const size_t total_samples = recorded_audio_.size();
    size_t samples_played = 0;
    
    app_->Alert("音频测试", "开始播放录音...");
    
    // 分块播放音频，改进连续性
    for (size_t i = 0; i < total_samples && state_ == kPlaying; i += chunk_size) {
        size_t current_chunk_size = std::min(chunk_size, total_samples - i);
        std::vector<int16_t> chunk(recorded_audio_.begin() + i, 
                                 recorded_audio_.begin() + i + current_chunk_size);
        
        // 放大音频信号
        for (auto& sample : chunk) {
            int32_t amplified = static_cast<int32_t>(sample) * 3;
            sample = static_cast<int16_t>(std::max(static_cast<int32_t>(-32768), std::min(static_cast<int32_t>(32767), amplified)));
        }
        
        // 直接输出音频数据，移除错误的while循环
        codec->OutputData(chunk);
        
        samples_played += current_chunk_size;
        
        // 减少进度显示频率，避免影响播放连续性
        int progress_percent = (samples_played * 100) / total_samples;
        if (progress_percent % 20 == 0 && progress_percent > 0) {
            static int last_progress = 0;
            if (progress_percent != last_progress) {
                char progress_msg[100];
                snprintf(progress_msg, sizeof(progress_msg), "正在播放录音...\n进度: %d%%", progress_percent);
                app_->Alert("音频测试", progress_msg);
                last_progress = progress_percent;
            }
        }
        
        vTaskDelay(chunk_delay);
    }
    
    ESP_LOGI(TAG, "音频播放完成，播放了 %u 个样本", (unsigned int)samples_played);
    
    codec->EnableOutput(false);
    app_->Alert("音频测试", "播放完成！\n音频测试通过\n3秒后自动进入下一项测试");
    state_ = kCompleted;
    
    // 延迟3秒后自动完成测试
    vTaskDelay(pdMS_TO_TICKS(3000));
    if (completion_callback_) {
        completion_callback_(true);
    }
}

void AudioTest::HandleTouchInterrupt() {
    switch (state_) {
        case kWaitingToStart:
            StartRecording();
            break;
        case kRecording:
            // 录音中按键，停止录音并进入播放模式
            StopRecording();
            break;
        case kWaitingToPlay:
            PlayRecordedAudio();
            break;
        case kPlaying:
            // 播放中，忽略
            break;
        case kCompleted:
            if (completion_callback_) {
                completion_callback_(true);
            }
            break;
    }
}

bool AudioTest::IsCompleted() const {
    return state_ == kCompleted;
}

// RadarTest 实现
RadarTest::RadarTest(Application* app) : TestBase(app) {}

void RadarTest::Start() {
    app_->Alert("雷达测试", "雷达检测中...\n请在设备前挥手");
    EnableRadarForTesting();
    SetupRadarTestCallback();
}

void RadarTest::HandleTouchInterrupt() {
    if (!completed_) {
        completed_ = true;
       

    #ifdef USE_RADAR_2410D
        Radar2410D::GetInstance().SetPersonStateCallback(nullptr);
        Radar2410D::GetInstance().SetRadarStatusCallback(nullptr);
	#else
        // 清除雷达状态回调，避免继续显示雷达信息
        RadarModule::GetInstance().SetRadarStatusCallback(nullptr);
        RadarModule::GetInstance().SetPersonStateCallback(nullptr);

	#endif
        ESP_LOGI(TAG, "雷达测试完成，已清除雷达回调");
        
        if (completion_callback_) {
            completion_callback_(radar_detected_);
        }
    }
}

bool RadarTest::IsCompleted() const {
    return completed_;
}

void RadarTest::SetupRadarTestCallback() {
    // 设置雷达状态回调 - 这是用户要求的关键修改
	 #ifdef USE_RADAR_2410D
        Radar2410D::GetInstance().SetRadarStatusCallback(
        std::bind(&Application::OnRadarStatusUpdate, app_, std::placeholders::_1, std::placeholders::_2, 
                  std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
	#else			  
    RadarModule::GetInstance().SetRadarStatusCallback(
        std::bind(&Application::OnRadarStatusUpdate, app_, std::placeholders::_1, std::placeholders::_2, 
                  std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
	#endif
    
    ESP_LOGI(TAG, "雷达测试回调已设置");
}

void RadarTest::EnableRadarForTesting() {
    // 雷达模块在application.cc中已经初始化，这里只需要确保启用
    //RadarModule::GetInstance().SetEnabled(true);
    ESP_LOGI(TAG, "启用雷达进行测试");
}

std::string RadarTest::FormatRadarInfo(uint8_t target_state, uint16_t moving_distance, 
                                     uint16_t static_distance, uint16_t detection_distance) {
    return "雷达检测到目标";
}

// DisplayTest 实现
DisplayTest::DisplayTest(Application* app) : TestBase(app) {}

void DisplayTest::Start() {
    app_->Alert("显示测试", "屏幕将显示不同颜色\n触摸中断继续");
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 直接在当前任务中运行，不创建新任务
    RunColorTest();
}


void DisplayTest::RunColorTest() {
    ESP_LOGI("DisplayTest", "开始颜色测试");
    
    // 使用静态数组减少栈使用
    static const lv_color_t colors[] = {
        lv_color_make(255, 0, 0),    // 红色
        lv_color_make(0, 255, 0),    // 绿色
        lv_color_make(0, 0, 255)     // 蓝色
    };
    static const char* color_names[] = {"红色", "绿色", "蓝色"};
    
    lv_obj_t* overlay = nullptr;
    
    for (int i = 0; i < 3; i++) {
        ESP_LOGI("DisplayTest", "显示颜色: %s (%d/3)", color_names[i], i + 1);
        
        // 清理之前的覆盖层
        if (overlay != nullptr) {
            lv_obj_del(overlay);
            overlay = nullptr;
        }
        
        // 创建新的覆盖层
        overlay = lv_obj_create(lv_scr_act());
        if (overlay == nullptr) {
            ESP_LOGE("DisplayTest", "创建覆盖层失败");
            break;
        }
        
        // 设置覆盖层属性
        lv_obj_set_size(overlay, LV_HOR_RES, LV_VER_RES);
        lv_obj_set_pos(overlay, 0, 0);
        lv_obj_set_style_bg_color(overlay, colors[i], 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(overlay, 0, 0);
        lv_obj_set_style_pad_all(overlay, 0, 0);
        lv_obj_set_style_radius(overlay, 0, 0);  // 添加这行：设置圆角半径为0
        
        // 禁用所有事件处理
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_clear_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
        
        // 立即刷新显示
        lv_refr_now(nullptr);
        
        // 使用更短的延时，减少定时器负载
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    
    // 最终清理
    if (overlay != nullptr) {
        lv_obj_del(overlay);
        overlay = nullptr;
        lv_refr_now(nullptr);
    }
    
    // 额外延时确保清理完成
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI("DisplayTest", "颜色测试完成");
    completed_ = true;
    
    // 主动调用完成回调
    if (completion_callback_) {
        completion_callback_(true);
    }
}

void DisplayTest::HandleTouchInterrupt() {
    if (!completed_) {
        completed_ = true;
        if (completion_callback_) {
            completion_callback_(true);
        }
    }
}

bool DisplayTest::IsCompleted() const {
    return completed_;
}

// FactoryTestManager 实现
FactoryTestManager::FactoryTestManager(Application* app) : app_(app) {
    test_results_.resize(5, false);
    InitializeTests();
}

FactoryTestManager::~FactoryTestManager() {
    StopTestTimeout();  // 清理超时定时器
    tests_.clear();
}

void FactoryTestManager::InitializeTests() {
    tests_.clear();
    tests_.push_back(std::make_unique<RadarTest>(app_));     // 雷达测试调到第一步
    tests_.push_back(std::make_unique<TouchInterruptTest>(app_));
    tests_.push_back(std::make_unique<AudioTest>(app_));
    tests_.push_back(std::make_unique<DisplayTest>(app_));
    
    // 为每个测试设置完成回调
    for (size_t i = 0; i < tests_.size(); ++i) {
        tests_[i]->SetCompletionCallback([this, i](bool success) {
            OnTestCompleted(i, success);
        });
    }
}

void FactoryTestManager::EnterFactoryTestingMode() {
    ESP_LOGI(TAG, "进入工厂测试模式");
    
    // 清理可能存在的雷达回调
    #ifdef USE_RADAR_2410D
    Radar2410D::GetInstance().SetPersonStateCallback(nullptr);
    Radar2410D::GetInstance().SetRadarStatusCallback(nullptr);
    #else
    RadarModule::GetInstance().SetPersonStateCallback(nullptr);
    RadarModule::GetInstance().SetRadarStatusCallback(nullptr);
    #endif

    app_->SetDeviceState(kDeviceStateFactoryTesting);
    current_test_index_ = -1;  // 设置为-1，表示还未开始测试
    std::fill(test_results_.begin(), test_results_.end(), false);
    touch_interrupt_triggered_ = false;
    
    // 直接启动第一个测试（雷达测试），而不是显示欢迎信息等待按键
    ESP_LOGI(TAG, "自动启动第一个测试（雷达测试）");
    RunTest(0);  // 直接启动第一个测试
}

void FactoryTestManager::HandleVolumeUpPress() {
    // 首先处理可能的超时
    ProcessTimeout();
    
    ESP_LOGI(TAG, "HandleVolumeUpPress called");
    ESP_LOGI(TAG, "Factory test mode: %s", IsInFactoryTestingMode() ? "YES" : "NO");
    ESP_LOGI(TAG, "Current test index: %d, Total tests: %d", current_test_index_, tests_.size());
    
    if (!IsInFactoryTestingMode()) {
        ESP_LOGI(TAG, "Exiting HandleVolumeUpPress - not in factory test mode");
        return;
    }
    
    // 移除这部分代码，因为现在进入工厂模式后会直接启动第一个测试
    // if (current_test_index_ == -1) {
    //     ESP_LOGI(TAG, "Starting first test (Touch Test)");
    //     RunTest(0);  // 启动触摸测试
    //     return;
    // }
    
    // 如果已经完成所有测试，处理退出请求
    if (current_test_index_ >= tests_.size()) {
        ESP_LOGI(TAG, "All tests completed, exit requested");
        exit_requested_ = true;
        return;
    }
    
    // 检查当前测试是否已完成
    if (current_test_index_ >= 0 && current_test_index_ < tests_.size()) {
        if (tests_[current_test_index_]->IsCompleted()) {
            ESP_LOGI(TAG, "Current test already completed, proceeding to next test");
            // 停止当前的超时计时器
            StopTestTimeout();
            // 进入下一个测试
            RunTest(current_test_index_ + 1);
            return;
        }
    }
    
    // 添加：长时间按键或连续按键可以强制退出
    static int consecutive_presses = 0;
    consecutive_presses++;
    if (consecutive_presses >= 5) {
        ESP_LOGI(TAG, "Force exit requested after multiple presses");
        consecutive_presses = 0;  // 重置计数器
        ExitFactoryTestingMode();
        return;
    }
    
    // 按键操作会停止当前的超时计时器
    ESP_LOGI(TAG, "Key pressed, stopping current timeout timer");
    StopTestTimeout();
    
    // 对于雷达测试，音量键用于完成测试
    if (current_test_index_ == 0) {  // 雷达测试（现在是第一步）
        ESP_LOGI(TAG, "Radar test completed via volume key, proceeding to next test");
        consecutive_presses = 0;  // 成功完成测试时重置计数器
        tests_[current_test_index_]->HandleTouchInterrupt();  // 触发当前测试完成
    } 
    // 对于触摸测试，音量键用于完成测试
    else if (current_test_index_ == 1) {  // 触摸测试（现在是第二步）
        ESP_LOGI(TAG, "Touch test completed via volume key, proceeding to next test");
        consecutive_presses = 0;  // 成功完成测试时重置计数器
        tests_[current_test_index_]->HandleTouchInterrupt();  // 触发当前测试完成
    }
    // 对于音频测试，音量键用于控制录音和播放流程
    else if (current_test_index_ == 2) {  // 音频测试（现在是第三步）
        AudioTest* audio_test = static_cast<AudioTest*>(tests_[current_test_index_].get());
        ESP_LOGI(TAG, "Audio test volume key pressed, current state: %d", audio_test->GetState());
        
        switch (audio_test->GetState()) {
            case AudioTest::kWaitingToStart:
                // 开始录音
                audio_test->HandleTouchInterrupt();
                break;
            case AudioTest::kRecording:
                // 录音中，停止录音并进入播放
                audio_test->HandleTouchInterrupt();
                break;
            case AudioTest::kWaitingToPlay:
                // 开始播放
                audio_test->HandleTouchInterrupt();
                break;
            case AudioTest::kPlaying:
                // 播放中，跳过到完成
                audio_test->SetState(AudioTest::kCompleted);
                audio_test->HandleTouchInterrupt();
                break;
            case AudioTest::kCompleted:
                ESP_LOGI(TAG, "Audio test completed, proceeding to next test");
                consecutive_presses = 0;
                audio_test->HandleTouchInterrupt();
                break;
        }
    }
    else {
        ESP_LOGI(TAG, "Other test, volume key triggers completion");
        consecutive_presses = 0;
        tests_[current_test_index_]->HandleTouchInterrupt();
    }
}

bool FactoryTestManager::IsInFactoryTestingMode() const {
    return app_->GetDeviceState() == kDeviceStateFactoryTesting;
}

bool FactoryTestManager::IsTestPassed(int index) const {
    return index >= 0 && index < test_results_.size() && test_results_[index];
}

void FactoryTestManager::RunTest(int test_index) {
    ESP_LOGI(TAG, "Starting test %d", test_index);
    
    if (test_index >= tests_.size()) {
        ESP_LOGI(TAG, "Invalid test index %d, showing results", test_index);
        ShowTestResults();
        return;
    }
    
    // 停止之前的超时定时器
    StopTestTimeout();
    
    current_test_index_ = test_index;
    ESP_LOGI(TAG, "Running test: %s", tests_[test_index]->GetTestName());
    tests_[test_index]->Start();
    
    // 显示测试不需要超时定时器
    if (test_index != 3) {  // 3是显示测试的索引
        StartTestTimeout();
        
        // 为音频测试设置更长的超时时间
        if (test_index == 2) {  // 音频测试
            tests_[test_index]->SetTimeoutDuration(15000);  // 15秒
        }
    } else {
        ESP_LOGI(TAG, "Display test started without timeout timer");
    }
}

void FactoryTestManager::OnTestCompleted(int test_index, bool success) {
    // 添加状态检查，避免重复调用
    if (test_index != current_test_index_) {
        ESP_LOGI(TAG, "Test completion for index %d, but current index is %d, ignoring", 
                 test_index, current_test_index_);
        return;
    }
    
    // 添加静态变量防止重复调用
    static int last_completed_test = -1;
    if (test_index == last_completed_test) {
        ESP_LOGI(TAG, "Test %d already processed, ignoring duplicate", test_index);
        return;
    }
    last_completed_test = test_index;
    
    if (test_index >= 0 && test_index < test_results_.size()) {
        // 检查测试是否已经标记为完成
        if (test_results_[test_index]) {
            ESP_LOGI(TAG, "Test %d already marked as completed, ignoring duplicate completion", test_index);
            return;
        }
        
        test_results_[test_index] = success;
        ESP_LOGI(TAG, "测试 %d 完成，结果: %s", test_index, success ? "通过" : "失败");
        
        // 只有在定时器存在时才停止
        if (test_timeout_timer_) {
            StopTestTimeout();
        }
        
        // 如果是雷达测试完成，清理回调
        if (test_index == 0) {  // 雷达测试现在是第一个测试（索引0）
            RadarTest* radar_test = static_cast<RadarTest*>(tests_[test_index].get());
            radar_test->Cleanup();
        }
        
        // 如果是显示测试完成，立即重启
        if (test_index == 3) {  // 显示测试是第4个测试（索引3）
            ESP_LOGI(TAG, "显示测试完成，准备重启设备");
            app_->Alert("测试完成", "显示测试完成\n设备将在3秒后重启");
            
            // 确保所有定时器都已清理
            if (test_timeout_timer_) {
                StopTestTimeout();
            }
            
            // 使用任务而不是直接调用，避免栈溢出
            xTaskCreate(
                [](void* arg) {
                    vTaskDelay(pdMS_TO_TICKS(3000));
                    ESP_LOGI(TAG, "重启设备");
                    esp_restart();
                },
                "RestartTask",
                2048,
                nullptr,
                5,
                nullptr
            );
            return;
        }
        
        // 增加NextTestTask的栈大小，特别是为显示测试准备
        uint32_t stack_size = 4096;
        if (test_index + 1 == 3) {  // 下一个是显示测试
            stack_size = 12288;  // 进一步增加栈空间到12KB
        }
        
        // 使用任务来处理下一个测试的启动，避免栈溢出
        xTaskCreate(
            [](void* arg) {
                auto* params = static_cast<std::pair<FactoryTestManager*, int>*>(arg);
                vTaskDelay(pdMS_TO_TICKS(1000));  // 增加延时，给系统更多时间清理
                params->first->RunTest(params->second);
                delete params;
                vTaskDelete(nullptr);
            },
            "NextTestTask",
            stack_size,  // 使用动态栈大小
            new std::pair<FactoryTestManager*, int>(this, test_index + 1),
            5,
            nullptr
        );
    }
}

void FactoryTestManager::StartTestTimeout() {
    if (current_test_index_ >= 0 && current_test_index_ < tests_.size()) {
        uint32_t timeout_ms = tests_[current_test_index_]->GetTimeoutDuration();
        
        // 创建超时定时器
        test_timeout_timer_ = xTimerCreate(
            "TestTimeout",
            pdMS_TO_TICKS(timeout_ms),
            pdFALSE,  // 一次性定时器
            this,     // 定时器ID
            TestTimeoutCallback
        );
        
        if (test_timeout_timer_) {
            xTimerStart(test_timeout_timer_, 0);
            ESP_LOGI(TAG, "Started timeout timer for test %d (%s) - %d ms", 
                     current_test_index_, 
                     tests_[current_test_index_]->GetTestName(), 
                     (int)timeout_ms);  // 将 %d 改为 %u
        }
    }
}

void FactoryTestManager::StopTestTimeout() {
    if (test_timeout_timer_) {
        // 先停止定时器
        if (xTimerIsTimerActive(test_timeout_timer_)) {
            xTimerStop(test_timeout_timer_, portMAX_DELAY);
        }
        // 再删除定时器
        if (xTimerDelete(test_timeout_timer_, portMAX_DELAY) == pdPASS) {
            test_timeout_timer_ = nullptr;
            ESP_LOGI(TAG, "Timeout timer stopped and deleted successfully");
        } else {
            ESP_LOGE(TAG, "Failed to delete timeout timer");
        }
    }
}

void FactoryTestManager::TestTimeoutCallback(TimerHandle_t timer) {
    FactoryTestManager* manager = static_cast<FactoryTestManager*>(pvTimerGetTimerID(timer));
    if (manager) {
        // 只设置标志位，不执行复杂操作
        manager->timeout_triggered_ = true;
        ESP_LOGI(TAG, "Timeout triggered, will be processed in main task");
    }
}

void FactoryTestManager::ProcessTimeout() {
    if (!timeout_triggered_) {
        return;
    }
    
    timeout_triggered_ = false;
    
    ESP_LOGI(TAG, "Processing timeout for test %d (%s)", 
             current_test_index_, 
             current_test_index_ < tests_.size() ? tests_[current_test_index_]->GetTestName() : "Unknown");
    
    if (current_test_index_ >= 0 && current_test_index_ < tests_.size()) {
        // 显示测试不处理超时，直接忽略
        if (current_test_index_ == 3) {
            ESP_LOGI(TAG, "Display test timeout ignored");
            return;
        }
        
        // 检查测试是否已经完成，避免重复处理
        if (tests_[current_test_index_]->IsCompleted()) {
            ESP_LOGI(TAG, "Test %d already completed, ignoring timeout", current_test_index_);
            return;
        }
        
        // 调用测试的超时处理
        tests_[current_test_index_]->OnTimeout();
    }
}

void FactoryTestManager::ExitFactoryTestingMode() {
    ESP_LOGI(TAG, "退出工厂测试模式");
    
    // 如果当前正在进行雷达测试，清理雷达回调
    if (current_test_index_ == 2 && current_test_index_ < tests_.size()) {
        RadarModule::GetInstance().SetRadarStatusCallback(nullptr);
        ESP_LOGI(TAG, "强制退出时清理雷达回调");
    }
    
    app_->SetDeviceState(kDeviceStateIdle);
    current_test_index_ = 0;
    std::fill(test_results_.begin(), test_results_.end(), false);
    touch_interrupt_triggered_ = false;
}

void FactoryTestManager::ShowTestResults() {
    std::string report = GenerateTestReport();

    // 修改提示信息，提供退出选项
    report += "\n按音量键退出测试模式\n或等待10秒后自动重启";
    
    app_->Alert("测试结果", report.c_str());
    
    // 等待用户选择或超时
    TickType_t start_time = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(10000);  // 10秒超时
    
    while ((xTaskGetTickCount() - start_time) < timeout_ticks) {
        // 检查是否按下音量键
        if (exit_requested_) {
            ESP_LOGI(TAG, "用户选择退出测试模式");
            ExitFactoryTestingMode();
            app_->Alert("退出成功", "已退出工厂测试模式");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(100));  // 100ms检查间隔
    }
    
    // 超时后重启
    ESP_LOGI(TAG, "超时，重启设备");
    esp_restart();
}

std::string FactoryTestManager::GenerateTestReport() {
    std::string report = "工厂测试完成，5S后重启:\n";
    
    const char* test_names[] = {
        "触摸中断测试",
        "音频测试", 
        "雷达测试",
        "显示测试"
    };
    
    for (size_t i = 0; i < test_results_.size() && i < 4; ++i) {
        report += test_names[i];
        report += ": ";
        report += test_results_[i] ? "通过" : "失败";
        report += "\n";
    }
    
    return report;
}

void AudioTest::StopRecording() {
    if (state_ != kRecording) {
        return;
    }
    
    ESP_LOGI(TAG, "停止录音");
    state_ = kWaitingToPlay;
    app_->Alert("音频测试", "录音完成\n准备播放录音...");
    
    // 使用任务通知而不是定时器回调
    // 创建播放任务来处理音频播放
    xTaskCreate(
        PlaybackTaskWrapper,
        "AudioPlayback",
        8192,  // 增加栈大小
        this,
        5,     // 优先级
        &playback_task_handle_
    );
}

// 添加新的静态包装函数
void AudioTest::PlaybackTaskWrapper(void* arg) {
    AudioTest* audio_test = static_cast<AudioTest*>(arg);
    if (audio_test) {
        // 延迟1秒后开始播放
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (audio_test->GetState() == kWaitingToPlay) {
            ESP_LOGI(TAG, "开始播放录音");
            audio_test->PlayRecordedAudio();
        }
    }
    vTaskDelete(nullptr);
}

// 在AudioTest::PerformRecording()方法后添加
void AudioTest::RecordingTaskWrapper(void* arg) {
    AudioTest* audio_test = static_cast<AudioTest*>(arg);
    if (audio_test) {
        audio_test->PerformRecording();
    }
    vTaskDelete(nullptr);  // 删除任务
}
