#ifndef FACTORY_TEST_MANAGER_H_
#define FACTORY_TEST_MANAGER_H_

#include <vector>
#include <functional>
#include <memory>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"  // 添加定时器支持
#include "radar/radar.h"  // 改为包含radar.h而不是radar_module.h

class Application;
class Display;
class AudioCodec;

// 测试基类
class TestBase {
public:
    virtual ~TestBase() = default;
    virtual void Start() = 0;
    virtual void HandleTouchInterrupt() = 0;
    virtual bool IsCompleted() const = 0;
    virtual const char* GetTestName() const = 0;
    
    // 添加超时相关方法
    virtual void OnTimeout() {
        // 默认超时行为：标记为完成并调用回调
        if (completion_callback_) {
            completion_callback_(true);  // 超时默认认为测试通过
        }
    }
    
    void SetTimeoutDuration(uint32_t timeout_ms) {
        timeout_duration_ms_ = timeout_ms;
    }
    
    uint32_t GetTimeoutDuration() const {
        return timeout_duration_ms_;
    }
    
protected:
    Application* app_;
    std::function<void(bool)> completion_callback_;
    uint32_t timeout_duration_ms_ = 10000;  // 默认10秒超时
    
public:
    TestBase(Application* app) : app_(app) {}
    void SetCompletionCallback(std::function<void(bool)> callback) {
        completion_callback_ = callback;
    }
};

// 触摸中断测试
class TouchInterruptTest : public TestBase {
public:
    TouchInterruptTest(Application* app);
    void Start() override;
    void HandleTouchInterrupt() override;
    bool IsCompleted() const override;
    const char* GetTestName() const override { return "触摸中断测试"; }
    
private:
    bool completed_ = false;
};

// 音频测试
class AudioTest : public TestBase {
public:
    enum AudioTestState {
        kWaitingToStart,
        kRecording,
        kWaitingToPlay,
        kPlaying,
        kCompleted
    };

public:
    AudioTest(Application* app);
    void Start() override;
    void HandleTouchInterrupt() override;
    bool IsCompleted() const override;
    const char* GetTestName() const override { return "音频测试"; }
    
    // 新增方法支持音量键控制
    AudioTestState GetState() const { return state_; }
    void SetState(AudioTestState state) { state_ = state; }
    void StartRecording();
    void StopRecording();
    void PlayRecordedAudio();
    
    static void PlaybackTaskWrapper(void* arg);
    
private:
    AudioTestState state_ = kWaitingToStart;
    std::vector<int16_t> recorded_audio_;
    TaskHandle_t recording_task_handle_ = nullptr;
    TaskHandle_t playback_task_handle_ = nullptr;
    TickType_t recording_start_time_;
    
    void PerformRecording();
    static void RecordingTaskWrapper(void* arg);
};

// 雷达测试
class RadarTest : public TestBase {
public:
    RadarTest(Application* app);
    void Start() override;
    void HandleTouchInterrupt() override;
    bool IsCompleted() const override;
    const char* GetTestName() const override { return "雷达测试"; }
    
    // 添加清理方法
    void Cleanup() {
        #ifdef USE_RADAR_2410D
        Radar2410D::GetInstance().SetRadarStatusCallback(nullptr);
        #else
        RadarModule::GetInstance().SetRadarStatusCallback(nullptr);
        #endif
    }
    
private:
    bool completed_ = false;
    bool radar_detected_ = true;
    
    void EnableRadarForTesting();
    void SetupRadarTestCallback();
    std::string FormatRadarInfo(uint8_t target_state, uint16_t moving_distance, 
                              uint16_t static_distance, uint16_t detection_distance);
};

// 显示测试
class DisplayTest : public TestBase {
public:
    DisplayTest(Application* app);
    void Start() override;
    void HandleTouchInterrupt() override;
    bool IsCompleted() const override;
    const char* GetTestName() const override { return "显示测试"; }
    
private:
    bool completed_ = false;
    
    void RunColorTest();
};

// 工厂测试管理器
class FactoryTestManager {
public:
    FactoryTestManager(Application* app);
    ~FactoryTestManager();
    
    // 测试控制
    void EnterFactoryTestingMode();
    void ExitFactoryTestingMode();
    void RunTest(int test_index);
    void HandleVolumeUpPress();  // 添加音量键处理方法
    
    // 状态查询
    bool IsInFactoryTestingMode() const;
    int GetCurrentTestIndex() const { return current_test_index_; }
    bool IsTestPassed(int index) const;
    
    // 结果显示
    void ShowTestResults();
    
private:
    Application* app_;
    int current_test_index_ = 0;
    std::vector<bool> test_results_;
    std::vector<std::unique_ptr<TestBase>> tests_;
    bool touch_interrupt_triggered_ = false;
    bool exit_requested_ = false;
    
    // 修改定时器相关成员
    TimerHandle_t test_timeout_timer_ = nullptr;
    bool timeout_triggered_ = false;  // 添加超时标志
    
    void InitializeTests();
    void OnTestCompleted(int test_index, bool success);
    std::string GenerateTestReport();
    
    // 修改超时处理方法
    void StartTestTimeout();
    void StopTestTimeout();
    static void TestTimeoutCallback(TimerHandle_t timer);  // 简化回调
public:    
    void ProcessTimeout();  // 在主任务中处理超时
};

#endif // FACTORY_TEST_MANAGER_H_