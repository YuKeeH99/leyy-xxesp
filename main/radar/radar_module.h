#ifndef _RADAR_MODULE_H_
#define _RADAR_MODULE_H_

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/uart.h>
#include <string>
#include <functional>
#include "radar_common.h"

class RadarModule {
public:
    static RadarModule& GetInstance() {
        static RadarModule instance;
        return instance;
    }
    
    // 删除拷贝构造函数和赋值运算符
    RadarModule(const RadarModule&) = delete;
    RadarModule& operator=(const RadarModule&) = delete;

    // 初始化雷达模块
    bool Init(uart_port_t uart_num, int tx_pin, int rx_pin, int baud_rate = 115200);
    
    // 启动雷达数据监控
    void Start();
    
    // 停止雷达数据监控
    void Stop();
    
    // 设置雷达开关状态
    void SetEnabled(bool enabled);
    
    // 获取雷达开关状态
    bool IsEnabled() const { return is_enabled_; }
    
    // 设置雷达状态回调函数
    void SetRadarStatusCallback(RadarStatusCallback callback) {
        radar_status_callback_ = callback;
    }
    
    // 设置人员状态回调函数
    void SetPersonStateCallback(PersonStateCallback callback) {
        person_state_callback_ = callback;
    }
    
    // 设置久坐检测回调函数
    void SetLongSittingCallback(LongSittingCallback callback) {
        long_sitting_callback_ = callback;
    }
    
    // 设置人员状态检测参数
    void SetPersonStateParams(uint32_t coming_threshold_ms, uint32_t leaving_threshold_ms,uint32_t long_sitting_threshold_ms) {
        coming_threshold_ms_ = coming_threshold_ms;
        leaving_threshold_ms_ = leaving_threshold_ms;
        long_sitting_threshold_ms_ = long_sitting_threshold_ms;
    }
    
    // 设置SHORT_HOLD检测阈值
    void SetShortHoldThreshold(uint32_t threshold_ms) {
        short_hold_threshold_ms_ = threshold_ms;
    }
    
    // 获取SHORT_HOLD检测阈值
    uint32_t GetShortHoldThreshold() const {
        return short_hold_threshold_ms_;
    }
    
    
    // 获取当前人员状态
    PersonState GetPersonState() const {
        return current_person_state_;
    }
    
    // 发送命令到雷达模块
    bool SendCommand(const std::string& command);
    
    // 获取最新的雷达数据
    std::string GetLatestData() const {
        return latest_data_;
    }
    
    // LD2410B 配置接口
    // 使能配置命令
    bool EnableConfiguration();
    
    // 结束配置命令
    bool EndConfiguration();
    
    // 设置最大距离门与无人持续时间
    // max_motion_gate: 最大运动距离门 (0-8)
    // max_static_gate: 最大静止距离门 (0-8) 
    // no_person_duration: 无人持续时间 (0-65535秒)
    bool SetMaxDistanceAndDuration(uint16_t max_motion_gate, uint16_t max_static_gate, uint16_t no_person_duration);
    
    // 设置距离门灵敏度
    // gate: 距离门 (0-8)
    // motion_sensitivity: 运动灵敏度 (0-100)
    // static_sensitivity: 静止灵敏度 (0-100)
    bool SetGateSensitivity(uint16_t gate, uint32_t motion_sensitivity, uint32_t static_sensitivity);
    
    // 设置所有距离门的灵敏度
    bool SetAllGatesSensitivity(uint32_t motion_sensitivity, uint32_t static_sensitivity);
    
    // 读取参数命令
    bool ReadParameters();

private:
    RadarModule();
    ~RadarModule();
    
    // 雷达数据处理任务
    static void RadarTask(void* pvParameters);
    
    // 解析雷达数据帧
    void ParseRadarFrame(const uint8_t* bytes, size_t length);
    
    // 处理目标状态变化
    void HandleTargetStateChange(uint8_t target_state, uint16_t moving_distance, uint8_t moving_energy, 
                                uint16_t static_distance, uint8_t static_energy, uint16_t detection_distance);
    
    uart_port_t uart_num_;
    TaskHandle_t task_handle_;
    bool is_running_;
    bool is_enabled_ = true;  // 默认开启
    RadarStatusCallback radar_status_callback_;
    PersonStateCallback person_state_callback_;
    LongSittingCallback long_sitting_callback_;
    std::string latest_data_;
    uint32_t last_callback_time_ = 0;
    
    // 人员状态相关参数
    PersonState current_person_state_ = PersonState::NOBODY;
    uint32_t coming_threshold_ms_ = 30000;  // 默认30秒，从无人到来人的阈值
    uint32_t leaving_threshold_ms_ = 40000; // 默认40秒，从有人到人走的阈值
    uint32_t first_detection_time_ = 0;     // 首次检测到目标的时间
    uint32_t last_detection_time_ = 0;      // 最后一次检测到目标的时间
    bool target_detected_ = false;          // 当前是否检测到目标
    
    // 久坐检测相关参数
    uint32_t long_sitting_threshold_ms_ = 30 * 60 * 1000;  // 默认30分钟
    uint32_t sitting_start_time_ = 0;                      // 开始久坐的时间
    bool is_sitting_ = false;                              // 是否处于久坐状态
    
    // SHORT_HOLD状态检测相关变量
    uint32_t short_hold_threshold_ms_ = 500;              // 默认5秒
    uint32_t short_distance_start_time_ = 0;               // 开始短距离检测的时间
    bool is_short_distance_ = false;                       // 是否处于短距离状态
    
    // 距离滤波相关
    static const size_t DISTANCE_FILTER_SIZE = 5;          // 滤波窗口大小
    uint16_t distance_filter_buffer_[DISTANCE_FILTER_SIZE]; // 距离滤波缓冲区
    size_t filter_index_ = 0;                              // 当前滤波索引
    bool filter_initialized_ = false;                      // 滤波器是否已初始化
    
    // 距离滤波函数
    uint16_t FilterDistance(uint16_t raw_distance);
public:    
    // 设置人员状态（用于系统初始化）
    void SetPersonState(PersonState state);
    
    // 重置状态到初始值
    void ResetState();
};

#endif // _RADAR_MODULE_H_