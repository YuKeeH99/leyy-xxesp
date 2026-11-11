#ifndef _RADAR_2410D_H_
#define _RADAR_2410D_H_

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/uart.h>
#include <string>
#include <functional>
#include <vector>
#include "radar_common.h"

// 2410D雷达特有的状态枚举
enum class Radar2410DState {
    NO_TARGET = 0x00,     // 无目标
    MOVING_TARGET = 0x01, // 运动目标
    STATIC_TARGET = 0x02, // 静止目标
    BOTH_TARGETS = 0x03   // 运动+静止目标
};

// 2410D雷达特有的回调函数类型（保留用于向后兼容）
typedef std::function<void(Radar2410DState state, uint16_t distance, uint8_t energy)> Radar2410DCallback;
typedef std::function<void(bool motion_detected, uint16_t distance)> Radar2410DMotionCallback;

// 新增枚举：标识当前输出模式
typedef enum {
    OUTPUT_MODE_UNKNOWN = 0,
    OUTPUT_MODE_NORMAL = 1,    // 正常文本模式（0x00000064）
    OUTPUT_MODE_ENGINEERING = 2 // 工程模式（0x00000004）
} OutputMode;

class Radar2410D {
public:
    static Radar2410D& GetInstance() {
        static Radar2410D instance;
        return instance;
    }
    
    // 删除拷贝构造函数和赋值运算符
    Radar2410D(const Radar2410D&) = delete;
    Radar2410D& operator=(const Radar2410D&) = delete;

    // 初始化雷达模块
    bool Init(uart_port_t uart_num, int tx_pin, int rx_pin, int baud_rate = 115200);
    
    // 启动雷达数据监控
    void Start();
    
    // 停止雷达数据监控
    void Stop();
    
    
    // 新的统一回调函数设置方法
    void SetRadarStatusCallback(RadarStatusCallback callback) {
        radar_status_callback_ = callback;
    }
    
    void SetPersonStateCallback(PersonStateCallback callback) {
        person_state_callback_ = callback;
    }
    
    // 设置人员状态检测参数（移动到public部分）
    void SetPersonStateParams(uint32_t coming_threshold_ms, uint32_t leaving_threshold_ms, uint32_t long_sitting_threshold_ms) {
        coming_threshold_ms_ = coming_threshold_ms;
        leaving_threshold_ms_ = leaving_threshold_ms;
        long_sitting_threshold_ms_ = long_sitting_threshold_ms;
    }

    // 配置相关方法
    bool EnableConfiguration();
    bool EndConfiguration();
    bool ReadFirmwareVersion();
    bool StartAutoThresholdGeneration(uint16_t trigger_coeff, uint16_t hold_coeff, uint16_t micro_motion_coeff);
    bool QueryAutoThresholdProgress();
    bool SaveParameters();
    bool AutoGainAdjustment();
    bool ReadCurrentDeviceParameters();

    OutputMode ReadCurrentOutputMode();

private:
    Radar2410D();
    ~Radar2410D();
    
    static void RadarTask(void* pvParameters);
    void ParseRadarFrame(const uint8_t* bytes, size_t length);
    void HandleEngineeringModeData(const uint8_t* data, size_t length);
    void HandleNormalModeData(const uint8_t* data, size_t length);
    void ProcessTextLine(const std::string& line);
    std::vector<uint8_t> CreateCommand(uint16_t command, const std::vector<uint8_t>& data = {});
    bool SendCommand(const std::vector<uint8_t>& command);
    bool WaitForAck(uint16_t command, uint32_t timeout_ms = 1000);
    bool SetDataOutputMode(bool engineering_mode = true);

    uart_port_t uart_num_;
    TaskHandle_t task_handle_;
    bool is_running_;
    
    // 旧的回调函数（保留用于向后兼容）
    Radar2410DCallback radar_callback_;
    Radar2410DMotionCallback motion_callback_;
    
    // 新的统一回调函数
    RadarStatusCallback radar_status_callback_;
    PersonStateCallback person_state_callback_;
    
    // 当前状态
    Radar2410DState current_state_ = Radar2410DState::NO_TARGET;
    uint16_t latest_distance_ = 0;
    uint8_t latest_energy_ = 0;
    
    // 运动检测状态（只保留一份）
    bool motion_detected_ = false;
    uint32_t last_motion_time_ = 0;
    uint32_t motion_timeout_ms_ = 5000; // 5秒无运动则认为运动结束
    
    // 用于处理跨缓冲区的不完整数据（只保留一份）
    std::string incomplete_data_;
    
    // 人员状态管理
    PersonState current_person_state_;
    uint32_t coming_threshold_ms_;
    uint32_t leaving_threshold_ms_;
    uint32_t long_sitting_threshold_ms_;
    uint32_t first_detection_time_;
    uint32_t last_detection_time_;
    uint32_t no_target_start_time_;  // 添加这个成员变量
    bool target_detected_;
    bool is_sitting_;
    uint32_t sitting_start_time_;
    
    // 添加SHORT_HOLD状态相关变量
    uint32_t short_hold_threshold_ms_;  // SHORT_HOLD阈值，默认5秒
    uint32_t short_distance_start_time_; // 短距离开始时间
    bool is_short_distance_; // 是否处于短距离状态
    
    // 添加HandleTargetStateChange函数声明
    void HandleTargetStateChange(uint8_t target_state, uint16_t moving_distance, 
                               uint8_t moving_energy, uint16_t static_distance, 
                               uint8_t static_energy, uint16_t detection_distance);
        // 距离滤波相关
    static const size_t DISTANCE_FILTER_SIZE = 5;          // 滤波窗口大小
    uint16_t distance_filter_buffer_[DISTANCE_FILTER_SIZE]; // 距离滤波缓冲区
    size_t filter_index_ = 0;                              // 当前滤波索引
    bool filter_initialized_ = false;                      // 滤波器是否已初始化
    
    // 距离滤波函数
    uint16_t FilterDistance(uint16_t raw_distance);
    
    void SetShortHoldThreshold(uint32_t threshold_ms) {
        short_hold_threshold_ms_ = threshold_ms;
    }
public:    
    // 设置人员状态（用于系统初始化）
    void SetPersonState(PersonState state);
    
    // 获取当前人员状态
    PersonState GetPersonState() const {
        return current_person_state_;
    }
    
    // 重置状态到初始值
    void ResetState();
    
};

#endif // _RADAR_2410D_H_


